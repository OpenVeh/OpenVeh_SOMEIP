#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "someip_codec_support.hpp"


namespace openveh::someip::params {

struct WheelInfo : public someip_generated::SerializableObject {
    std::uint8_t wheelId;
    std::uint16_t pressureKpa;
    std::int16_t temperatureC;

    bool serializeTo(std::vector<std::uint8_t>& bytes) const override {
        someip_generated::ByteWriter writer(bytes);
        if (!writer.writeIntegralBE<std::uint8_t>(wheelId)) return false;
        if (!writer.writeIntegralBE<std::uint16_t>(pressureKpa)) return false;
        if (!writer.writeIntegralBE<std::int16_t>(temperatureC)) return false;
        return true;
    }

    bool deserializeFromReader(someip_generated::ByteReader& reader) override {
        if (!reader.readIntegralBE<std::uint8_t>(wheelId)) return false;
        if (!reader.readIntegralBE<std::uint16_t>(pressureKpa)) return false;
        if (!reader.readIntegralBE<std::int16_t>(temperatureC)) return false;
        return true;
    }
};

}  // namespace openveh::someip::params
