#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "someip_sd/types.hpp"

namespace someip_sd {

enum class MessageType : std::uint8_t {
    kRequest = 0x00,
    kResponse = 0x80,
    kError = 0x81,
};

struct SdPacket {
    MessageType message_type{MessageType::kError};
    std::uint32_t request_id{0};
    std::vector<std::uint8_t> payload;
};

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::uint16_t kSomeIpSdServiceId = 0xFFFF;
constexpr std::uint16_t kSomeIpSdMethodId = 0x8100;
constexpr std::uint16_t kSomeIpClientId = 0x0001;
constexpr std::uint8_t kSomeIpInterfaceVersion = 0x01;
constexpr std::uint8_t kSomeIpReturnCodeOk = 0x00;
constexpr std::uint8_t kSomeIpReturnCodeMalformed = 0x09;
constexpr std::uint8_t kSdEntryFindService = 0x00;
constexpr std::uint8_t kSdEntryOfferService = 0x01;
constexpr std::uint8_t kSdOptionIpv4Endpoint = 0x04;
constexpr std::uint8_t kL4ProtocolUdp = 0x11;

std::vector<std::uint8_t> EncodePacket(const SdPacket& packet);
SdPacket DecodePacket(const std::uint8_t* data, std::size_t size);

std::vector<std::uint8_t> BuildSdFindPayload(std::uint16_t service_id,
                                             std::optional<std::uint16_t> instance_id);

std::vector<std::uint8_t> BuildSdOfferPayload(const ServiceDescriptor& descriptor,
                                              std::uint32_t ttl_sec,
                                              bool stop_offer = false);

std::vector<std::uint8_t> BuildSdOfferPayloadList(const ServiceList& services,
                                                  std::uint32_t ttl_sec);

bool ParseSdFindPayload(const std::vector<std::uint8_t>& payload,
                        std::uint16_t* service_id,
                        std::optional<std::uint16_t>* instance_id);

bool ParseSdOffers(const std::vector<std::uint8_t>& payload, ServiceList* services);

bool ParseSdOfferFirst(const std::vector<std::uint8_t>& payload,
                       ServiceDescriptor* descriptor,
                       std::uint32_t* ttl_sec);

}  // namespace someip_sd
