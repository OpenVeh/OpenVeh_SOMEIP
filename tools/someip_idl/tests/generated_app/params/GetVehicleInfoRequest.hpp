#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "someip_codec_support.hpp"


namespace openveh::someip::vehicle {

struct GetVehicleInfoRequest : public someip_generated::SerializableObject {
    std::uint32_t requestId;
    std::string targetVin;

    bool serializeTo(std::vector<std::uint8_t>& bytes) const override {
        someip_generated::ByteWriter writer(bytes);
        if (!writer.writeIntegralBE<std::uint32_t>(requestId)) return false;
        if (targetVin.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) return false;
        if (!writer.writeIntegralBE<std::uint16_t>(static_cast<std::uint16_t>(targetVin.size()))) return false;
        bytes.insert(bytes.end(), targetVin.begin(), targetVin.end());
        return true;
    }

    bool deserializeFromReader(someip_generated::ByteReader& reader) override {
        if (!reader.readIntegralBE<std::uint32_t>(requestId)) return false;
        std::uint16_t targetVinStrLen = 0;
        if (!reader.readIntegralBE<std::uint16_t>(targetVinStrLen)) return false;
        if (!reader.readBytes(static_cast<std::size_t>(targetVinStrLen), targetVin)) return false;
        return true;
    }
};

}  // namespace openveh::someip::vehicle
