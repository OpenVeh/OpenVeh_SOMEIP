#include "someip_sd/sd_daemon.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <optional>
#include <vector>

#include "someip_sd/protocol.hpp"

namespace someip_sd {
namespace {

std::uint32_t MakeRegistryKey(std::uint16_t service_id, std::uint16_t instance_id) {
    return (static_cast<std::uint32_t>(service_id) << 16) | instance_id;
}

std::uint64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

SomeIpSdDaemon::SomeIpSdDaemon(std::string host, std::uint16_t port)
    : host_(std::move(host)),
      port_(port),
      discovery_multicast_group_("239.255.0.1"),
      discovery_multicast_port_(port) {}

SomeIpSdDaemon::~SomeIpSdDaemon() { Stop(); }

void SomeIpSdDaemon::SetDiscoveryMulticast(std::string multicast_group,
                                           std::uint16_t multicast_port,
                                           std::string interface_address) {
    discovery_multicast_group_ = std::move(multicast_group);
    discovery_multicast_port_ = multicast_port;
    discovery_interface_address_ = std::move(interface_address);
}

void SomeIpSdDaemon::EnableUnixDomainControl(std::string socket_path) {
    unix_control_enabled_ = true;
    unix_socket_path_ = std::move(socket_path);
}

void SomeIpSdDaemon::Start() {
    if (running_.exchange(true)) {
        return;
    }
    server_thread_ = std::thread(&SomeIpSdDaemon::ServeLoop, this);
}

void SomeIpSdDaemon::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    int probe = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe >= 0) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(discovery_multicast_port_);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        const char* marker = "STOP";
        sendto(probe, marker, 4, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        close(probe);
    }

    if (unix_control_enabled_ && !unix_socket_path_.empty()) {
        int uds = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (uds >= 0) {
            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", unix_socket_path_.c_str());
            const char* marker = "STOP";
            sendto(uds, marker, 4, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            close(uds);
        }
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void SomeIpSdDaemon::CleanupExpired() {
    const auto now = NowMs();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = registry_.begin(); it != registry_.end();) {
        if (it->second.expires_at_ms <= now) {
            it = registry_.erase(it);
        } else {
            ++it;
        }
    }
}

bool SomeIpSdDaemon::SetupUdpSocket(int* udp_fd) {
    *udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (*udp_fd < 0) {
        return false;
    }

    int reuse = 1;
    setsockopt(*udp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(discovery_multicast_port_);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(*udp_fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        close(*udp_fd);
        *udp_fd = -1;
        return false;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(discovery_multicast_group_.c_str());
    mreq.imr_interface.s_addr = inet_addr(discovery_interface_address_.c_str());
    setsockopt(*udp_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    return true;
}

bool SomeIpSdDaemon::SetupUnixSocket(int* unix_fd) {
    *unix_fd = -1;
    if (!unix_control_enabled_) {
        return true;
    }

#ifndef __linux__
    return false;
#else
    if (unix_socket_path_.empty()) {
        return false;
    }
    *unix_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (*unix_fd < 0) {
        return false;
    }
    unlink(unix_socket_path_.c_str());
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", unix_socket_path_.c_str());
    if (bind(*unix_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(*unix_fd);
        *unix_fd = -1;
        return false;
    }
    return true;
#endif
}

bool SomeIpSdDaemon::HandlePacket(const SdPacket& req, SdPacket* resp) {
    if (req.message_type != MessageType::kRequest) {
        *resp = SdPacket{MessageType::kError, req.request_id, {}};
        return true;
    }

    std::uint16_t service_id = 0;
    std::optional<std::uint16_t> instance_id;
    if (ParseSdFindPayload(req.payload, &service_id, &instance_id)) {
        std::vector<RegistryEntry> matches;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& kv : registry_) {
                const auto& svc = kv.second;
                if (svc.descriptor.service_id != service_id) {
                    continue;
                }
                if (instance_id.has_value() && svc.descriptor.instance_id != *instance_id) {
                    continue;
                }
                matches.push_back(svc);
            }
        }

        ServiceList list;
        list.reserve(matches.size());
        for (const auto& e : matches) {
            list.push_back(e.descriptor);
        }

        *resp = SdPacket{MessageType::kResponse, req.request_id, BuildSdOfferPayloadList(list, 3)};
        return true;
    }

    ServiceDescriptor parsed;
    std::uint32_t ttl_sec = 0;
    if (!ParseSdOfferFirst(req.payload, &parsed, &ttl_sec)) {
        *resp = SdPacket{MessageType::kError, req.request_id, {}};
        return true;
    }

    if (ttl_sec == 0) {
        const auto key = MakeRegistryKey(parsed.service_id, parsed.instance_id);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            registry_.erase(key);
        }
        *resp = SdPacket{MessageType::kResponse, req.request_id, BuildSdOfferPayload(parsed, 0, true)};
        return true;
    }

    const auto key = MakeRegistryKey(parsed.service_id, parsed.instance_id);
    RegistryEntry entry;
    entry.descriptor = parsed;
    entry.expires_at_ms = NowMs() + static_cast<std::uint64_t>(ttl_sec) * 1000U;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        registry_[key] = entry;
    }
    *resp = SdPacket{MessageType::kResponse, req.request_id, BuildSdOfferPayload(parsed, ttl_sec, false)};
    return true;
}

void SomeIpSdDaemon::ServeLoop() {
    int udp_fd = -1;
    int unix_fd = -1;
    if (!SetupUdpSocket(&udp_fd) || !SetupUnixSocket(&unix_fd)) {
        if (udp_fd >= 0) {
            close(udp_fd);
        }
        if (unix_fd >= 0) {
            close(unix_fd);
        }
        running_ = false;
        return;
    }

    std::vector<std::uint8_t> buffer(4096);

    while (running_) {
        CleanupExpired();

        pollfd pfds[2] = {};
        nfds_t nfds = 0;
        pfds[nfds].fd = udp_fd;
        pfds[nfds].events = POLLIN;
        ++nfds;
        if (unix_fd >= 0) {
            pfds[nfds].fd = unix_fd;
            pfds[nfds].events = POLLIN;
            ++nfds;
        }

        const int pr = poll(pfds, nfds, 200);
        if (pr <= 0) {
            continue;
        }

        auto process_fd = [&](int fd) {
            std::vector<std::uint8_t> peer_addr(128);
            socklen_t peer_len = static_cast<socklen_t>(peer_addr.size());
            const auto n = recvfrom(fd,
                                    buffer.data(),
                                    buffer.size(),
                                    0,
                                    reinterpret_cast<sockaddr*>(peer_addr.data()),
                                    &peer_len);
            if (n <= 0) {
                return;
            }

            SdPacket req;
            try {
                req = DecodePacket(buffer.data(), static_cast<std::size_t>(n));
            } catch (...) {
                return;
            }

            SdPacket resp;
            try {
                if (!HandlePacket(req, &resp)) {
                    return;
                }
            } catch (...) {
                resp = SdPacket{MessageType::kError, req.request_id, {}};
            }

            const auto out = EncodePacket(resp);
            sendto(fd,
                   out.data(),
                   out.size(),
                   0,
                   reinterpret_cast<const sockaddr*>(peer_addr.data()),
                   peer_len);
        };

        if (pfds[0].revents & POLLIN) {
            process_fd(udp_fd);
        }
        if (unix_fd >= 0 && (pfds[1].revents & POLLIN)) {
            process_fd(unix_fd);
        }
    }

    close(udp_fd);
    if (unix_fd >= 0) {
        close(unix_fd);
    }
    if (unix_control_enabled_ && !unix_socket_path_.empty()) {
        unlink(unix_socket_path_.c_str());
    }
}

}  // namespace someip_sd
