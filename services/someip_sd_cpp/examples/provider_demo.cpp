#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

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
constexpr std::uint16_t kProviderPort = 50001;

volatile std::sig_atomic_t g_stop = 0;

void OnSignal(int) {
    g_stop = 1;
}

openveh::someip::params::VehicleStatus BuildCurrentVehicleStatus(std::uint32_t tick) {
    openveh::someip::params::VehicleStatus s;
    s.vin = "LINUX-SOMEIP-0001";
    s.speedKph = static_cast<std::uint16_t>(80 + (tick % 20));
    s.engineRpm = 2000 + (tick % 1000);
    s.doorOpen = (tick % 2 == 0);

    openveh::someip::params::WheelInfo w0;
    w0.wheelId = 0;
    w0.pressureKpa = 235;
    w0.temperatureC = 35;
    openveh::someip::params::WheelInfo w1;
    w1.wheelId = 1;
    w1.pressureKpa = 236;
    w1.temperatureC = 34;
    openveh::someip::params::WheelInfo w2;
    w2.wheelId = 2;
    w2.pressureKpa = 234;
    w2.temperatureC = 36;
    openveh::someip::params::WheelInfo w3;
    w3.wheelId = 3;
    w3.pressureKpa = 235;
    w3.temperatureC = 35;
    s.wheels = {w0, w1, w2, w3};

    s.accelHistory = {0.1f + static_cast<float>(tick % 3), 0.2f, -0.1f};
    return s;
}

std::string PeerKey(const sockaddr_in& peer) {
    return std::to_string(peer.sin_addr.s_addr) + ":" + std::to_string(peer.sin_port);
}

}  // namespace

int main() {
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    someip_sd::SomeIpSdApi api("127.0.0.1", 30490, 1000);
    api.SetDiscoveryMulticast("239.255.0.1", 30490, "127.0.0.1");

    someip_sd::ServiceDescriptor desc;
    desc.service_id = kServiceId;
    desc.instance_id = kInstanceId;
    desc.major_version = 1;
    desc.minor_version = 0;
    desc.endpoint_host = "127.0.0.1";
    desc.endpoint_port = kProviderPort;
    desc.transport = "udp";

    std::string error;
    if (!api.RegisterService(desc, 5, &error)) {
        std::cerr << "Register failed: " << error << "\n";
        return 1;
    }

    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::cerr << "Failed to create UDP socket\n";
        api.UnregisterService(desc.service_id, desc.instance_id, &error);
        return 1;
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(kProviderPort);
    bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        std::cerr << "Failed to bind provider UDP socket\n";
        close(fd);
        api.UnregisterService(desc.service_id, desc.instance_id, &error);
        return 1;
    }

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::map<std::string, sockaddr_in> subscribers;
    std::uint32_t tick = 0;
    auto last_refresh = std::chrono::steady_clock::now();
    auto last_event = std::chrono::steady_clock::now();

    std::cout << "Provider ready: GetVehicleInfo(sync) + VehicleInfoChanged(event)\n";

    while (!g_stop) {
        std::uint8_t buffer[4096] = {};
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        const auto n = recvfrom(fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (n > 0) {
            try {
                const auto msg = someip_demo::DecodeSomeIp(buffer, static_cast<std::size_t>(n));
                if (msg.service_id != kServiceId || msg.message_type != SomeIpMessageType::kRequest) {
                    continue;
                }

                if (msg.method_id == kGetVehicleInfoMethodId) {
                    GetVehicleStatusRequest req;
                    if (!req.deserialize(msg.payload)) {
                        continue;
                    }

                    GetVehicleStatusResponse resp;
                    resp.requestId = req.requestId;
                    resp.status = BuildCurrentVehicleStatus(tick);

                    SomeIpMessage out;
                    out.service_id = kServiceId;
                    out.method_id = kGetVehicleInfoMethodId;
                    out.client_id = msg.client_id;
                    out.session_id = msg.session_id;
                    out.interface_version = 1;
                    out.message_type = SomeIpMessageType::kResponse;
                    out.return_code = 0;
                    out.payload = resp.serialize();

                    const auto bytes = someip_demo::EncodeSomeIp(out);
                    sendto(fd, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&peer), peer_len);
                } else if (msg.method_id == kSubscribeVehicleInfoChangedMethodId) {
                    subscribers[PeerKey(peer)] = peer;

                    SomeIpMessage ack;
                    ack.service_id = kServiceId;
                    ack.method_id = kSubscribeVehicleInfoChangedMethodId;
                    ack.client_id = msg.client_id;
                    ack.session_id = msg.session_id;
                    ack.interface_version = 1;
                    ack.message_type = SomeIpMessageType::kResponse;
                    ack.return_code = 0;
                    ack.payload = {};

                    const auto bytes = someip_demo::EncodeSomeIp(ack);
                    sendto(fd, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&peer), peer_len);
                    std::cout << "Subscriber added, total=" << subscribers.size() << "\n";
                }
            } catch (...) {
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_refresh >= std::chrono::seconds(2)) {
            if (!api.RegisterService(desc, 5, &error)) {
                std::cerr << "Refresh failed: " << error << "\n";
            }
            last_refresh = now;
        }

        if (now - last_event >= std::chrono::seconds(1)) {
            ++tick;
            if (!subscribers.empty()) {
                VehicleStatusEvent evt;
                evt.eventSequence = tick;
                evt.status = BuildCurrentVehicleStatus(tick);

                SomeIpMessage notify;
                notify.service_id = kServiceId;
                notify.method_id = kVehicleInfoChangedEventId;
                notify.client_id = 0;
                notify.session_id = 0;
                notify.interface_version = 1;
                notify.message_type = SomeIpMessageType::kNotification;
                notify.return_code = 0;
                notify.payload = evt.serialize();

                const auto bytes = someip_demo::EncodeSomeIp(notify);
                for (const auto& kv : subscribers) {
                    const auto& sub = kv.second;
                    sendto(fd,
                           bytes.data(),
                           bytes.size(),
                           0,
                           reinterpret_cast<const sockaddr*>(&sub),
                           sizeof(sub));
                }
                std::cout << "VehicleInfoChanged sent seq=" << tick
                          << " subscribers=" << subscribers.size() << "\n";
            }
            last_event = now;
        }
    }

    close(fd);
    api.UnregisterService(desc.service_id, desc.instance_id, &error);
    return 0;
}
