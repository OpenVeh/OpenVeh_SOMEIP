#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "someip_sd/protocol.hpp"
#include "someip_sd/types.hpp"

namespace someip_sd {

enum class ControlTransport {
    kUdp,
    kUnixDomainSocket,
};

class SomeIpSdApi {
public:
    SomeIpSdApi(std::string daemon_host = "127.0.0.1", std::uint16_t daemon_port = 30490, int timeout_ms = 1000);

    void SetControlUdp(std::string daemon_host, std::uint16_t daemon_port);
    void SetDiscoveryMulticast(std::string multicast_group,
                               std::uint16_t multicast_port,
                               std::string interface_address = "0.0.0.0");
    void SetDiscoveryMulticastTtl(int ttl);
    void EnableUnixDomainControl(std::string socket_path);

    bool RegisterService(const ServiceDescriptor& descriptor, int ttl_sec = 5, std::string* error = nullptr);
    bool UnregisterService(std::uint16_t service_id, std::uint16_t instance_id, std::string* error = nullptr);
    ServiceList DiscoverServices(
        std::uint16_t service_id,
        std::optional<std::uint16_t> instance_id = std::nullopt,
        int timeout_ms = 1000,
        std::string* error = nullptr);

private:
    std::uint32_t AllocateRequestId();
    bool DoControlRequest(const SdPacket& req, SdPacket* resp, std::string* error);

    ControlTransport control_transport_{ControlTransport::kUdp};
    std::string daemon_host_;
    std::uint16_t daemon_port_;
    std::string unix_socket_path_;
    std::string discovery_multicast_group_;
    std::uint16_t discovery_multicast_port_{0};
    std::string discovery_interface_address_{"0.0.0.0"};
    int discovery_multicast_ttl_{1};
    int timeout_ms_;
    std::uint32_t next_request_id_{1};
};

}  // namespace someip_sd
