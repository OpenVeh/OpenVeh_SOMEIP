#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "someip_codec_support.hpp"


namespace openveh::someip::vehicle {

struct SetCruiseSpeedResponse : public someip_generated::SerializableObject {
    bool accepted;

    bool serializeTo(std::vector<std::uint8_t>& bytes) const override {
        someip_generated::ByteWriter writer(bytes);
        if (!writer.writeIntegralBE<std::uint8_t>(accepted ? 1u : 0u)) return false;
        return true;
    }

    bool deserializeFromReader(someip_generated::ByteReader& reader) override {
        std::uint8_t b = 0;
        if (!reader.readIntegralBE<std::uint8_t>(b)) return false;
        accepted = (b != 0);
        return true;
    }
};

}  // namespace openveh::someip::vehicle
