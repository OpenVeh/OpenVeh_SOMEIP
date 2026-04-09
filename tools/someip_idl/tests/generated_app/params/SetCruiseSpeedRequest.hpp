#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "someip_codec_support.hpp"


namespace openveh::someip::vehicle {

struct SetCruiseSpeedRequest : public someip_generated::SerializableObject {
    std::uint16_t targetSpeedKph;

    bool serializeTo(std::vector<std::uint8_t>& bytes) const override {
        someip_generated::ByteWriter writer(bytes);
        if (!writer.writeIntegralBE<std::uint16_t>(targetSpeedKph)) return false;
        return true;
    }

    bool deserializeFromReader(someip_generated::ByteReader& reader) override {
        if (!reader.readIntegralBE<std::uint16_t>(targetSpeedKph)) return false;
        return true;
    }
};

}  // namespace openveh::someip::vehicle
