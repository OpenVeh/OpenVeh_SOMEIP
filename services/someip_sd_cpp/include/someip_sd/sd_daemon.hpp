#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <sys/socket.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "someip_sd/protocol.hpp"
#include "someip_sd/types.hpp"

namespace someip_sd {

class SomeIpSdDaemon {
public:
    SomeIpSdDaemon(std::string host = "127.0.0.1", std::uint16_t port = 30490);
    ~SomeIpSdDaemon();

    void SetDiscoveryMulticast(std::string multicast_group,
                               std::uint16_t multicast_port,
                               std::string interface_address = "0.0.0.0");
    void EnableUnixDomainControl(std::string socket_path);

    void Start();
    void Stop();

private:
    struct RegistryEntry {
        ServiceDescriptor descriptor;
        std::uint64_t expires_at_ms{0};
    };

    struct RequestContext {
        int fd{-1};
        std::vector<std::uint8_t> peer_addr;
        socklen_t peer_len{0};
    };

    void ServeLoop();
    bool SetupUdpSocket(int* udp_fd);
    bool SetupUnixSocket(int* unix_fd);
    bool HandlePacket(const SdPacket& req, SdPacket* resp);
    void CleanupExpired();

    std::string host_;
    std::uint16_t port_;
    std::string discovery_multicast_group_;
    std::uint16_t discovery_multicast_port_{0};
    std::string discovery_interface_address_{"0.0.0.0"};
    bool unix_control_enabled_{false};
    std::string unix_socket_path_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    std::mutex mutex_;
    std::unordered_map<std::uint32_t, RegistryEntry> registry_;
};

}  // namespace someip_sd
