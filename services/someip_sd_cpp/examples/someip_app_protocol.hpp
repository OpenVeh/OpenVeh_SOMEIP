#pragma once

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace someip_demo {

enum class SomeIpMessageType : std::uint8_t {
    kRequest = 0x00,
    kRequestNoReturn = 0x01,
    kNotification = 0x02,
    kResponse = 0x80,
    kError = 0x81,
};

struct SomeIpMessage {
    std::uint16_t service_id{0};
    std::uint16_t method_id{0};
    std::uint16_t client_id{0};
    std::uint16_t session_id{0};
    std::uint8_t interface_version{1};
    SomeIpMessageType message_type{SomeIpMessageType::kRequest};
    std::uint8_t return_code{0};
    std::vector<std::uint8_t> payload;
};

inline std::vector<std::uint8_t> EncodeSomeIp(const SomeIpMessage& msg) {
    if (msg.payload.size() > 0xFFFFFFFFu - 8u) {
        throw std::runtime_error("payload too large");
    }

    const std::uint32_t message_id =
        (static_cast<std::uint32_t>(msg.service_id) << 16) | static_cast<std::uint32_t>(msg.method_id);
    const std::uint32_t request_id =
        (static_cast<std::uint32_t>(msg.client_id) << 16) | static_cast<std::uint32_t>(msg.session_id);
    const std::uint32_t length = static_cast<std::uint32_t>(msg.payload.size() + 8u);

    std::vector<std::uint8_t> bytes(16u + msg.payload.size());

    const std::uint32_t message_id_be = htonl(message_id);
    const std::uint32_t length_be = htonl(length);
    const std::uint32_t request_id_be = htonl(request_id);

    std::memcpy(bytes.data() + 0, &message_id_be, sizeof(message_id_be));
    std::memcpy(bytes.data() + 4, &length_be, sizeof(length_be));
    std::memcpy(bytes.data() + 8, &request_id_be, sizeof(request_id_be));

    bytes[12] = 0x01;  // protocol version
    bytes[13] = msg.interface_version;
    bytes[14] = static_cast<std::uint8_t>(msg.message_type);
    bytes[15] = msg.return_code;

    if (!msg.payload.empty()) {
        std::memcpy(bytes.data() + 16, msg.payload.data(), msg.payload.size());
    }

    return bytes;
}

inline SomeIpMessage DecodeSomeIp(const std::uint8_t* data, std::size_t size) {
    if (size < 16) {
        throw std::runtime_error("SOME/IP frame too short");
    }

    std::uint32_t message_id_be = 0;
    std::uint32_t length_be = 0;
    std::uint32_t request_id_be = 0;

    std::memcpy(&message_id_be, data + 0, sizeof(message_id_be));
    std::memcpy(&length_be, data + 4, sizeof(length_be));
    std::memcpy(&request_id_be, data + 8, sizeof(request_id_be));

    const std::uint32_t message_id = ntohl(message_id_be);
    const std::uint32_t length = ntohl(length_be);
    const std::uint32_t request_id = ntohl(request_id_be);

    if (length < 8) {
        throw std::runtime_error("invalid SOME/IP length");
    }

    const std::size_t payload_len = static_cast<std::size_t>(length - 8);
    if (size != 16 + payload_len) {
        throw std::runtime_error("SOME/IP payload length mismatch");
    }

    SomeIpMessage msg;
    msg.service_id = static_cast<std::uint16_t>((message_id >> 16) & 0xFFFFu);
    msg.method_id = static_cast<std::uint16_t>(message_id & 0xFFFFu);
    msg.client_id = static_cast<std::uint16_t>((request_id >> 16) & 0xFFFFu);
    msg.session_id = static_cast<std::uint16_t>(request_id & 0xFFFFu);
    if (data[12] != 0x01) {
        throw std::runtime_error("unsupported SOME/IP protocol version");
    }
    msg.interface_version = data[13];
    msg.message_type = static_cast<SomeIpMessageType>(data[14]);
    msg.return_code = data[15];
    msg.payload.assign(data + 16, data + 16 + payload_len);
    return msg;
}

}  // namespace someip_demo
