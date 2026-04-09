#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <string>

#include "GetVehicleStatusRequest.hpp"
#include "GetVehicleStatusResponse.hpp"
#include "VehicleStatusEvent.hpp"
#include "someip_sd/sd_api.hpp"
#include "someip_app_protocol.hpp"

namespace {

using openveh::someip::params::GetVehicleStatusRequest;
using openveh::someip::params::GetVehicleStatusResponse;
using openveh::someip::params::VehicleStatusEvent;
using someip_demo::SomeIpMessage;
using someip_demo::SomeIpMessageType;

constexpr std::uint16_t kServiceId = 0x1234;
constexpr std::uint16_t kInstanceId = 0x0001;
constexpr std::uint16_t kGetVehicleInfoMethodId = 0x0001;
constexpr std::uint16_t kSubscribeVehicleInfoChangedMethodId = 0x0002;
constexpr std::uint16_t kVehicleInfoChangedEventId = 0x8001;

bool SendSomeIp(int fd, const sockaddr_in& peer, const SomeIpMessage& msg) {
    const auto bytes = someip_demo::EncodeSomeIp(msg);
    return sendto(fd,
                  bytes.data(),
                  bytes.size(),
                  0,
                  reinterpret_cast<const sockaddr*>(&peer),
                  sizeof(peer)) >= 0;
}

}  // namespace

int main() {
    someip_sd::SomeIpSdApi api("127.0.0.1", 30490, 1000);
    api.SetDiscoveryMulticast("239.255.0.1", 30490, "127.0.0.1");
    std::string error;

    const auto services = api.DiscoverServices(kServiceId, kInstanceId, 1000, &error);
    if (!error.empty()) {
        std::cerr << "Discover error: " << error << "\n";
        return 1;
    }
    if (services.empty()) {
        std::cout << "No service found\n";
        return 0;
    }

    const auto& provider = services.front();
    std::cout << "Found provider at " << provider.endpoint_host << ":" << provider.endpoint_port << "\n";

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cerr << "socket create failed\n";
        return 1;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(0);
    local.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        std::cerr << "bind failed\n";
        close(fd);
        return 1;
    }

    timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(provider.endpoint_port);
    peer.sin_addr.s_addr = inet_addr(provider.endpoint_host.c_str());

    std::uint16_t session = 1;

    SomeIpMessage subscribe;
    subscribe.service_id = kServiceId;
    subscribe.method_id = kSubscribeVehicleInfoChangedMethodId;
    subscribe.client_id = 0x1001;
    subscribe.session_id = session++;
    subscribe.interface_version = 1;
    subscribe.message_type = SomeIpMessageType::kRequest;
    subscribe.return_code = 0;
    subscribe.payload = {};

    if (!SendSomeIp(fd, peer, subscribe)) {
        std::cerr << "subscribe send failed\n";
        close(fd);
        return 1;
    }

    {
        std::uint8_t buffer[4096] = {};
        const auto n = recvfrom(fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (n <= 0) {
            std::cerr << "subscribe response timeout\n";
            close(fd);
            return 1;
        }
        const auto msg = someip_demo::DecodeSomeIp(buffer, static_cast<std::size_t>(n));
        if (msg.message_type != SomeIpMessageType::kResponse || msg.method_id != kSubscribeVehicleInfoChangedMethodId) {
            std::cerr << "invalid subscribe response\n";
            close(fd);
            return 1;
        }
        std::cout << "Subscribed VehicleInfoChanged\n";
    }

    GetVehicleStatusRequest req;
    req.requestId = 101;
    req.targetVin = "LINUX-SOMEIP-0001";

    SomeIpMessage get_info;
    get_info.service_id = kServiceId;
    get_info.method_id = kGetVehicleInfoMethodId;
    get_info.client_id = 0x1001;
    get_info.session_id = session++;
    get_info.interface_version = 1;
    get_info.message_type = SomeIpMessageType::kRequest;
    get_info.return_code = 0;
    get_info.payload = req.serialize();

    if (!SendSomeIp(fd, peer, get_info)) {
        std::cerr << "GetVehicleInfo send failed\n";
        close(fd);
        return 1;
    }

    {
        std::uint8_t buffer[4096] = {};
        const auto n = recvfrom(fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (n <= 0) {
            std::cerr << "GetVehicleInfo response timeout\n";
            close(fd);
            return 1;
        }
        const auto msg = someip_demo::DecodeSomeIp(buffer, static_cast<std::size_t>(n));
        if (msg.message_type != SomeIpMessageType::kResponse || msg.method_id != kGetVehicleInfoMethodId) {
            std::cerr << "invalid GetVehicleInfo response\n";
            close(fd);
            return 1;
        }

        GetVehicleStatusResponse resp;
        if (!resp.deserialize(msg.payload)) {
            std::cerr << "response payload decode failed\n";
            close(fd);
            return 1;
        }

        std::cout << "GetVehicleInfo response: requestId=" << resp.requestId
                  << " vin=" << resp.status.vin
                  << " speedKph=" << resp.status.speedKph
                  << " rpm=" << resp.status.engineRpm << "\n";
    }

    std::cout << "Waiting VehicleInfoChanged events...\n";
    int received_events = 0;
    while (received_events < 3) {
        std::uint8_t buffer[4096] = {};
        const auto n = recvfrom(fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (n <= 0) {
            std::cerr << "event timeout\n";
            break;
        }

        const auto msg = someip_demo::DecodeSomeIp(buffer, static_cast<std::size_t>(n));
        if (msg.message_type != SomeIpMessageType::kNotification || msg.method_id != kVehicleInfoChangedEventId) {
            continue;
        }

        VehicleStatusEvent evt;
        if (!evt.deserialize(msg.payload)) {
            std::cerr << "event payload decode failed\n";
            continue;
        }

        ++received_events;
        std::cout << "VehicleInfoChanged #" << received_events
                  << " seq=" << evt.eventSequence
                  << " speedKph=" << evt.status.speedKph
                  << " doorOpen=" << (evt.status.doorOpen ? "true" : "false") << "\n";
    }

    close(fd);
    return 0;
}
