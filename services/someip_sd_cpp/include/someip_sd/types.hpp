#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace someip_sd {

struct ServiceDescriptor {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{0};
    std::uint8_t major_version{1};
    std::uint8_t minor_version{0};
    std::string endpoint_host{"127.0.0.1"};
    std::uint16_t endpoint_port{0};
    std::string transport{"udp"};
};

using ServiceList = std::vector<ServiceDescriptor>;

}  // namespace someip_sd
