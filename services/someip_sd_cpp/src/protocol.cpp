#include "someip_sd/protocol.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <stdexcept>

#include <algorithm>

namespace someip_sd {
namespace {

struct SomeIpHeader {
    std::uint32_t message_id_be;
    std::uint32_t length_be;
    std::uint32_t request_id_be;
    std::uint8_t protocol_version;
    std::uint8_t interface_version;
    std::uint8_t message_type;
    std::uint8_t return_code;
};

constexpr std::size_t kSomeIpHeaderSize = sizeof(SomeIpHeader);

std::uint32_t BuildMessageId(std::uint16_t service_id, std::uint16_t method_id) {
    return (static_cast<std::uint32_t>(service_id) << 16) | static_cast<std::uint32_t>(method_id);
}

std::uint32_t BuildRequestId(std::uint16_t client_id, std::uint16_t session_id) {
    return (static_cast<std::uint32_t>(client_id) << 16) | static_cast<std::uint32_t>(session_id);
}

void WriteU16(std::vector<std::uint8_t>* out, std::uint16_t v) {
    out->push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out->push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void WriteU24(std::vector<std::uint8_t>* out, std::uint32_t v) {
    out->push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out->push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out->push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void WriteU32(std::vector<std::uint8_t>* out, std::uint32_t v) {
    out->push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out->push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out->push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out->push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

bool ReadU16(const std::vector<std::uint8_t>& in, std::size_t* pos, std::uint16_t* v) {
    if (*pos + 2 > in.size()) {
        return false;
    }
    *v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(in[*pos]) << 8) |
                                    static_cast<std::uint16_t>(in[*pos + 1]));
    *pos += 2;
    return true;
}

bool ReadU32(const std::vector<std::uint8_t>& in, std::size_t* pos, std::uint32_t* v) {
    if (*pos + 4 > in.size()) {
        return false;
    }
    *v = (static_cast<std::uint32_t>(in[*pos]) << 24) |
         (static_cast<std::uint32_t>(in[*pos + 1]) << 16) |
         (static_cast<std::uint32_t>(in[*pos + 2]) << 8) |
         static_cast<std::uint32_t>(in[*pos + 3]);
    *pos += 4;
    return true;
}

std::vector<std::uint8_t> BuildIpv4EndpointOption(const ServiceDescriptor& svc) {
    std::vector<std::uint8_t> out;
    // SOME/IP-SD option header: length(2), type(1), reserved(1)
    // IPv4 endpoint payload: reserved(1), IPv4(4), reserved(1), L4(1), port(2)
    WriteU16(&out, 9);
    out.push_back(kSdOptionIpv4Endpoint);
    out.push_back(0x00);

    out.push_back(0x00);
    in_addr addr{};
    if (inet_pton(AF_INET, svc.endpoint_host.c_str(), &addr) != 1) {
        addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    const auto* ip_bytes = reinterpret_cast<const std::uint8_t*>(&addr.s_addr);
    out.push_back(ip_bytes[0]);
    out.push_back(ip_bytes[1]);
    out.push_back(ip_bytes[2]);
    out.push_back(ip_bytes[3]);
    out.push_back(0x00);
    out.push_back(kL4ProtocolUdp);
    WriteU16(&out, svc.endpoint_port);
    return out;
}

std::vector<std::uint8_t> BuildServiceEntry(std::uint8_t entry_type,
                                            std::uint8_t idx1,
                                            std::uint8_t idx2,
                                            std::uint8_t num_opt1,
                                            std::uint8_t num_opt2,
                                            const ServiceDescriptor& svc,
                                            std::uint32_t ttl_sec,
                                            std::uint32_t minor_version = 0) {
    std::vector<std::uint8_t> out;
    out.reserve(16);
    out.push_back(entry_type);
    out.push_back(idx1);
    out.push_back(idx2);
    out.push_back(static_cast<std::uint8_t>(((num_opt1 & 0x0Fu) << 4) | (num_opt2 & 0x0Fu)));
    WriteU16(&out, svc.service_id);
    WriteU16(&out, svc.instance_id);
    out.push_back(svc.major_version);
    WriteU24(&out, std::min<std::uint32_t>(ttl_sec, 0x00FFFFFFu));
    WriteU32(&out, minor_version);
    return out;
}

bool ParseSdBlocks(const std::vector<std::uint8_t>& payload,
                   std::vector<std::vector<std::uint8_t>>* entries,
                   std::vector<std::vector<std::uint8_t>>* options) {
    if (payload.size() < 12) {
        return false;
    }

    std::size_t pos = 0;
    pos += 4;  // flags + reserved

    std::uint32_t entries_len = 0;
    if (!ReadU32(payload, &pos, &entries_len)) {
        return false;
    }
    if (pos + entries_len > payload.size()) {
        return false;
    }

    const std::size_t entries_end = pos + entries_len;
    while (pos < entries_end) {
        if (pos + 16 > entries_end) {
            return false;
        }
        entries->emplace_back(payload.begin() + static_cast<std::ptrdiff_t>(pos),
                              payload.begin() + static_cast<std::ptrdiff_t>(pos + 16));
        pos += 16;
    }

    std::uint32_t options_len = 0;
    if (!ReadU32(payload, &pos, &options_len)) {
        return false;
    }
    if (pos + options_len != payload.size()) {
        return false;
    }

    const std::size_t options_end = pos + options_len;
    while (pos < options_end) {
        std::uint16_t opt_payload_len = 0;
        if (!ReadU16(payload, &pos, &opt_payload_len)) {
            return false;
        }
        if (pos + 2 + opt_payload_len > options_end) {
            return false;
        }
        const std::size_t opt_full_len = 2 + 2 + opt_payload_len;
        options->emplace_back(payload.begin() + static_cast<std::ptrdiff_t>(pos - 2),
                              payload.begin() + static_cast<std::ptrdiff_t>(pos - 2 + opt_full_len));
        pos += 2 + opt_payload_len;
    }

    return true;
}

bool ParseEndpointOption(const std::vector<std::uint8_t>& option,
                         std::string* host,
                         std::uint16_t* port,
                         std::string* transport) {
    if (option.size() != 13) {
        return false;
    }
    if (option[2] != kSdOptionIpv4Endpoint) {
        return false;
    }
    const std::uint8_t l4 = option[10];
    if (l4 != kL4ProtocolUdp) {
        return false;
    }

    char ipbuf[INET_ADDRSTRLEN] = {};
    in_addr addr{};
    auto* addr_bytes = reinterpret_cast<std::uint8_t*>(&addr.s_addr);
    addr_bytes[0] = option[5];
    addr_bytes[1] = option[6];
    addr_bytes[2] = option[7];
    addr_bytes[3] = option[8];
    if (!inet_ntop(AF_INET, &addr, ipbuf, sizeof(ipbuf))) {
        return false;
    }

    *host = ipbuf;
    *transport = "udp";
    *port = static_cast<std::uint16_t>((static_cast<std::uint16_t>(option[11]) << 8) |
                                       static_cast<std::uint16_t>(option[12]));
    return true;
}

}  // namespace

std::vector<std::uint8_t> EncodePacket(const SdPacket& packet) {
    SomeIpHeader hdr{};
    hdr.message_id_be = htonl(BuildMessageId(kSomeIpSdServiceId, kSomeIpSdMethodId));
    hdr.length_be = htonl(static_cast<std::uint32_t>(8 + packet.payload.size()));
    hdr.request_id_be = htonl(BuildRequestId(kSomeIpClientId, static_cast<std::uint16_t>(packet.request_id & 0xFFFFu)));
    hdr.protocol_version = static_cast<std::uint8_t>(kProtocolVersion);
    hdr.interface_version = kSomeIpInterfaceVersion;
    hdr.message_type = static_cast<std::uint8_t>(packet.message_type);
    hdr.return_code = (packet.message_type == MessageType::kError) ? kSomeIpReturnCodeMalformed : kSomeIpReturnCodeOk;

    std::vector<std::uint8_t> bytes(kSomeIpHeaderSize + packet.payload.size());
    std::memcpy(bytes.data(), &hdr, kSomeIpHeaderSize);
    if (!packet.payload.empty()) {
        std::memcpy(bytes.data() + kSomeIpHeaderSize, packet.payload.data(), packet.payload.size());
    }
    return bytes;
}

SdPacket DecodePacket(const std::uint8_t* data, std::size_t size) {
    if (size < kSomeIpHeaderSize) {
        throw std::runtime_error("packet too short");
    }

    SomeIpHeader hdr{};
    std::memcpy(&hdr, data, kSomeIpHeaderSize);

    const auto message_id = ntohl(hdr.message_id_be);
    const auto expected_message_id = BuildMessageId(kSomeIpSdServiceId, kSomeIpSdMethodId);
    if (message_id != expected_message_id) {
        throw std::runtime_error("unexpected service/method id");
    }
    if (hdr.protocol_version != static_cast<std::uint8_t>(kProtocolVersion)) {
        throw std::runtime_error("unsupported protocol version");
    }
    if (hdr.interface_version != kSomeIpInterfaceVersion) {
        throw std::runtime_error("unsupported interface version");
    }

    const auto length = ntohl(hdr.length_be);
    if (length < 8) {
        throw std::runtime_error("invalid SOME/IP length");
    }
    const auto payload_len = static_cast<std::size_t>(length - 8);
    if (kSomeIpHeaderSize + payload_len != size) {
        throw std::runtime_error("SOME/IP length mismatch");
    }

    SdPacket packet;
    packet.message_type = static_cast<MessageType>(hdr.message_type);
    const auto request_id = ntohl(hdr.request_id_be);
    packet.request_id = static_cast<std::uint16_t>(request_id & 0xFFFFu);
    packet.payload.assign(data + kSomeIpHeaderSize, data + kSomeIpHeaderSize + payload_len);
    return packet;
}

std::vector<std::uint8_t> BuildSdFindPayload(std::uint16_t service_id,
                                             std::optional<std::uint16_t> instance_id) {
    ServiceDescriptor svc;
    svc.service_id = service_id;
    svc.instance_id = instance_id.has_value() ? *instance_id : 0xFFFF;
    svc.major_version = 0xFF;

    auto entry = BuildServiceEntry(kSdEntryFindService, 0, 0, 0, 0, svc, 0, 0xFFFFFFFFu);
    std::vector<std::uint8_t> payload;
    payload.push_back(0x00);  // flags
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    WriteU32(&payload, static_cast<std::uint32_t>(entry.size()));
    payload.insert(payload.end(), entry.begin(), entry.end());
    WriteU32(&payload, 0);
    return payload;
}

std::vector<std::uint8_t> BuildSdOfferPayload(const ServiceDescriptor& descriptor,
                                              std::uint32_t ttl_sec,
                                              bool stop_offer) {
    const auto option = BuildIpv4EndpointOption(descriptor);
    const auto entry = BuildServiceEntry(kSdEntryOfferService,
                                         0,
                                         0,
                                         1,
                                         0,
                                         descriptor,
                                         stop_offer ? 0 : ttl_sec,
                                         static_cast<std::uint32_t>(descriptor.minor_version));

    std::vector<std::uint8_t> payload;
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    WriteU32(&payload, static_cast<std::uint32_t>(entry.size()));
    payload.insert(payload.end(), entry.begin(), entry.end());
    WriteU32(&payload, static_cast<std::uint32_t>(option.size()));
    payload.insert(payload.end(), option.begin(), option.end());
    return payload;
}

std::vector<std::uint8_t> BuildSdOfferPayloadList(const ServiceList& services,
                                                  std::uint32_t ttl_sec) {
    std::vector<std::vector<std::uint8_t>> options;
    options.reserve(services.size());
    for (const auto& svc : services) {
        options.push_back(BuildIpv4EndpointOption(svc));
    }

    std::vector<std::uint8_t> entries_blob;
    entries_blob.reserve(services.size() * 16);
    for (std::size_t i = 0; i < services.size(); ++i) {
        const auto entry = BuildServiceEntry(kSdEntryOfferService,
                                             static_cast<std::uint8_t>(i),
                                             0,
                                             1,
                                             0,
                                             services[i],
                                             ttl_sec,
                                             static_cast<std::uint32_t>(services[i].minor_version));
        entries_blob.insert(entries_blob.end(), entry.begin(), entry.end());
    }

    std::vector<std::uint8_t> payload;
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    WriteU32(&payload, static_cast<std::uint32_t>(entries_blob.size()));
    payload.insert(payload.end(), entries_blob.begin(), entries_blob.end());

    std::vector<std::uint8_t> options_blob;
    for (const auto& o : options) {
        options_blob.insert(options_blob.end(), o.begin(), o.end());
    }
    WriteU32(&payload, static_cast<std::uint32_t>(options_blob.size()));
    payload.insert(payload.end(), options_blob.begin(), options_blob.end());
    return payload;
}

bool ParseSdFindPayload(const std::vector<std::uint8_t>& payload,
                        std::uint16_t* service_id,
                        std::optional<std::uint16_t>* instance_id) {
    std::vector<std::vector<std::uint8_t>> entries;
    std::vector<std::vector<std::uint8_t>> options;
    if (!ParseSdBlocks(payload, &entries, &options) || entries.empty()) {
        return false;
    }
    const auto& e = entries.front();
    if (e[0] != kSdEntryFindService) {
        return false;
    }
    *service_id = static_cast<std::uint16_t>((static_cast<std::uint16_t>(e[4]) << 8) |
                                             static_cast<std::uint16_t>(e[5]));
    const auto inst = static_cast<std::uint16_t>((static_cast<std::uint16_t>(e[6]) << 8) |
                                                 static_cast<std::uint16_t>(e[7]));
    if (inst == 0xFFFF) {
        *instance_id = std::nullopt;
    } else {
        *instance_id = inst;
    }
    return true;
}

bool ParseSdOffers(const std::vector<std::uint8_t>& payload, ServiceList* services) {
    services->clear();
    std::vector<std::vector<std::uint8_t>> entries;
    std::vector<std::vector<std::uint8_t>> options;
    if (!ParseSdBlocks(payload, &entries, &options)) {
        return false;
    }

    for (const auto& e : entries) {
        if (e[0] != kSdEntryOfferService) {
            continue;
        }
        ServiceDescriptor svc;
        svc.service_id = static_cast<std::uint16_t>((static_cast<std::uint16_t>(e[4]) << 8) |
                                                    static_cast<std::uint16_t>(e[5]));
        svc.instance_id = static_cast<std::uint16_t>((static_cast<std::uint16_t>(e[6]) << 8) |
                                                     static_cast<std::uint16_t>(e[7]));
        svc.major_version = e[8];
        svc.minor_version = static_cast<std::uint8_t>(e[15]);

        const std::uint8_t option_index = e[1];
        const std::uint8_t option_count = static_cast<std::uint8_t>((e[3] >> 4) & 0x0Fu);
        if (option_count > 0 && option_index < options.size()) {
            std::uint16_t port = 0;
            std::string host;
            std::string transport;
            if (ParseEndpointOption(options[option_index], &host, &port, &transport)) {
                svc.endpoint_host = host;
                svc.endpoint_port = port;
                svc.transport = transport;
            }
        }
        services->push_back(svc);
    }

    return true;
}

bool ParseSdOfferFirst(const std::vector<std::uint8_t>& payload,
                       ServiceDescriptor* descriptor,
                       std::uint32_t* ttl_sec) {
    std::vector<std::vector<std::uint8_t>> entries;
    std::vector<std::vector<std::uint8_t>> options;
    if (!ParseSdBlocks(payload, &entries, &options) || entries.empty()) {
        return false;
    }
    const auto& e = entries.front();
    if (e[0] != kSdEntryOfferService) {
        return false;
    }

    descriptor->service_id = static_cast<std::uint16_t>((static_cast<std::uint16_t>(e[4]) << 8) |
                                                        static_cast<std::uint16_t>(e[5]));
    descriptor->instance_id = static_cast<std::uint16_t>((static_cast<std::uint16_t>(e[6]) << 8) |
                                                         static_cast<std::uint16_t>(e[7]));
    descriptor->major_version = e[8];
    descriptor->minor_version = static_cast<std::uint8_t>(e[15]);

    *ttl_sec = (static_cast<std::uint32_t>(e[9]) << 16) |
               (static_cast<std::uint32_t>(e[10]) << 8) |
               static_cast<std::uint32_t>(e[11]);

    descriptor->endpoint_host = "127.0.0.1";
    descriptor->endpoint_port = 0;
    descriptor->transport = "udp";

    const std::uint8_t option_index = e[1];
    const std::uint8_t option_count = static_cast<std::uint8_t>((e[3] >> 4) & 0x0Fu);
    if (option_count > 0 && option_index < options.size()) {
        std::uint16_t port = 0;
        std::string host;
        std::string transport;
        if (ParseEndpointOption(options[option_index], &host, &port, &transport)) {
            descriptor->endpoint_host = host;
            descriptor->endpoint_port = port;
            descriptor->transport = transport;
        }
    }

    return true;
}

}  // namespace someip_sd
