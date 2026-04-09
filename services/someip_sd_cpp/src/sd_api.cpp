#include "someip_sd/sd_api.hpp"

#include <arpa/inet.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <set>
#include <stdexcept>

namespace someip_sd {

SomeIpSdApi::SomeIpSdApi(std::string daemon_host, std::uint16_t daemon_port, int timeout_ms)
    : daemon_host_(std::move(daemon_host)),
      daemon_port_(daemon_port),
      discovery_multicast_group_("239.255.0.1"),
      discovery_multicast_port_(daemon_port),
      timeout_ms_(timeout_ms) {}

void SomeIpSdApi::SetControlUdp(std::string daemon_host, std::uint16_t daemon_port) {
    control_transport_ = ControlTransport::kUdp;
    daemon_host_ = std::move(daemon_host);
    daemon_port_ = daemon_port;
}

void SomeIpSdApi::SetDiscoveryMulticast(std::string multicast_group,
                                        std::uint16_t multicast_port,
                                        std::string interface_address) {
    discovery_multicast_group_ = std::move(multicast_group);
    discovery_multicast_port_ = multicast_port;
    discovery_interface_address_ = std::move(interface_address);
}

void SomeIpSdApi::SetDiscoveryMulticastTtl(int ttl) {
    discovery_multicast_ttl_ = ttl < 1 ? 1 : ttl;
}

void SomeIpSdApi::EnableUnixDomainControl(std::string socket_path) {
    control_transport_ = ControlTransport::kUnixDomainSocket;
    unix_socket_path_ = std::move(socket_path);
}

std::uint32_t SomeIpSdApi::AllocateRequestId() {
    return next_request_id_++;
}

bool SomeIpSdApi::DoControlRequest(const SdPacket& req, SdPacket* resp, std::string* error) {
    if (control_transport_ == ControlTransport::kUnixDomainSocket) {
#ifndef __linux__
        if (error) {
            *error = "unix domain socket control is not supported on this platform";
        }
        return false;
#else
        if (unix_socket_path_.empty()) {
            if (error) {
                *error = "unix socket path is empty";
            }
            return false;
        }

        int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd < 0) {
            if (error) {
                *error = "unix socket create failed";
            }
            return false;
        }

        sockaddr_un local{};
        local.sun_family = AF_UNIX;
        const std::string local_path =
            "/tmp/openveh_sd_api_" + std::to_string(getpid()) + "_" + std::to_string(req.request_id) + ".sock";
        std::snprintf(local.sun_path, sizeof(local.sun_path), "%s", local_path.c_str());
        unlink(local.sun_path);
        if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
            if (error) {
                *error = "bind unix client socket failed";
            }
            close(fd);
            return false;
        }

        timeval tv{};
        tv.tv_sec = timeout_ms_ / 1000;
        tv.tv_usec = (timeout_ms_ % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sockaddr_un server{};
        server.sun_family = AF_UNIX;
        std::snprintf(server.sun_path, sizeof(server.sun_path), "%s", unix_socket_path_.c_str());

        const auto out = EncodePacket(req);
        if (sendto(fd,
                   out.data(),
                   out.size(),
                   0,
                   reinterpret_cast<sockaddr*>(&server),
                   sizeof(server)) < 0) {
            if (error) {
                *error = "send unix control request failed";
            }
            close(fd);
            unlink(local_path.c_str());
            return false;
        }

        std::uint8_t buffer[4096] = {};
        const auto n = recvfrom(fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        close(fd);
        unlink(local_path.c_str());
        if (n <= 0) {
            if (error) {
                *error = "unix control request timeout";
            }
            return false;
        }
        try {
            *resp = DecodePacket(buffer, static_cast<std::size_t>(n));
            return true;
        } catch (const std::exception& ex) {
            if (error) {
                *error = ex.what();
            }
            return false;
        }
#endif
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        if (error) {
            *error = "socket create failed";
        }
        return false;
    }

    timeval tv{};
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(daemon_port_);
    addr.sin_addr.s_addr = inet_addr(daemon_host_.c_str());

    const auto out = EncodePacket(req);
    if (sendto(fd, out.data(), out.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (error) {
            *error = "send control request failed";
        }
        close(fd);
        return false;
    }

    std::uint8_t buffer[4096] = {};
    const auto n = recvfrom(fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
    close(fd);

    if (n <= 0) {
        if (error) {
            *error = "control request timeout";
        }
        return false;
    }

    try {
        *resp = DecodePacket(buffer, static_cast<std::size_t>(n));
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }
}

bool SomeIpSdApi::RegisterService(const ServiceDescriptor& descriptor, int ttl_sec, std::string* error) {
    const auto request_id = AllocateRequestId();
    SdPacket req{MessageType::kRequest, request_id,
                 BuildSdOfferPayload(descriptor, static_cast<std::uint32_t>(ttl_sec), false)};

    SdPacket resp;
    if (!DoControlRequest(req, &resp, error)) {
        return false;
    }

    try {
        if (resp.request_id != request_id) {
            if (error) {
                *error = "register request_id mismatch";
            }
            return false;
        }
        if (resp.message_type != MessageType::kResponse) {
            if (error) {
                *error = "register unexpected response type";
            }
            return false;
        }

        ServiceDescriptor parsed;
        std::uint32_t parsed_ttl = 0;
        if (!ParseSdOfferFirst(resp.payload, &parsed, &parsed_ttl)) {
            if (error) {
                *error = "register response payload parse failed";
            }
            return false;
        }
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }

    return true;
}

bool SomeIpSdApi::UnregisterService(std::uint16_t service_id, std::uint16_t instance_id, std::string* error) {
    const auto request_id = AllocateRequestId();
    ServiceDescriptor desc;
    desc.service_id = service_id;
    desc.instance_id = instance_id;
    SdPacket req{MessageType::kRequest, request_id, BuildSdOfferPayload(desc, 0, true)};

    SdPacket resp;
    if (!DoControlRequest(req, &resp, error)) {
        return false;
    }

    try {
        if (resp.request_id != request_id) {
            if (error) {
                *error = "unregister request_id mismatch";
            }
            return false;
        }
        if (resp.message_type != MessageType::kResponse) {
            if (error) {
                *error = "unregister unexpected response type";
            }
            return false;
        }

        ServiceDescriptor parsed;
        std::uint32_t parsed_ttl = 0;
        if (!ParseSdOfferFirst(resp.payload, &parsed, &parsed_ttl)) {
            if (error) {
                *error = "unregister response payload parse failed";
            }
            return false;
        }
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }

    return true;
}

ServiceList SomeIpSdApi::DiscoverServices(std::uint16_t service_id,
                                          std::optional<std::uint16_t> instance_id,
                                          int timeout_ms,
                                          std::string* error) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        if (error) {
            *error = "socket create failed";
        }
        return {};
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        if (error) {
            *error = "bind discover socket failed";
        }
        close(fd);
        return {};
    }

    in_addr ifaddr{};
    ifaddr.s_addr = inet_addr(discovery_interface_address_.c_str());
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof(ifaddr));
    const unsigned char ttl = static_cast<unsigned char>(discovery_multicast_ttl_);
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(discovery_multicast_port_);
    addr.sin_addr.s_addr = inet_addr(discovery_multicast_group_.c_str());

    const auto request_id = AllocateRequestId();
    const auto out = EncodePacket(
        SdPacket{MessageType::kRequest, request_id, BuildSdFindPayload(service_id, instance_id)});
    if (sendto(fd, out.data(), out.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (error) {
            *error = "send find failed";
        }
        close(fd);
        return {};
    }

    ServiceList list;
    std::set<std::uint64_t> dedup;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;
        }

        const auto remaining_ms =
            static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int pr = poll(&pfd, 1, remaining_ms);
        if (pr <= 0) {
            break;
        }

        std::uint8_t buffer[4096] = {};
        const auto n = recvfrom(fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (n <= 0) {
            continue;
        }

        try {
            const auto resp = DecodePacket(buffer, static_cast<std::size_t>(n));
            if (resp.request_id != request_id) {
                continue;
            }
            if (resp.message_type != MessageType::kResponse) {
                continue;
            }
            ServiceList partial;
            if (!ParseSdOffers(resp.payload, &partial)) {
                continue;
            }
            for (const auto& svc : partial) {
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(svc.service_id) << 48) |
                    (static_cast<std::uint64_t>(svc.instance_id) << 32) |
                    static_cast<std::uint64_t>(svc.endpoint_port);
                if (dedup.insert(key).second) {
                    list.push_back(svc);
                }
            }
        } catch (const std::exception& ex) {
            if (error) {
                *error = ex.what();
            }
        }
    }

    close(fd);
    return list;
}

}  // namespace someip_sd
