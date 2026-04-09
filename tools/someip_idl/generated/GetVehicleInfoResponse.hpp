#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "someip_codec_support.hpp"

#include "VehicleStatus.hpp"

namespace openveh::someip::vehicle {

struct GetVehicleInfoResponse : public someip_generated::SerializableObject {
    std::uint32_t requestId;
    VehicleStatus status;

    bool serializeTo(std::vector<std::uint8_t>& bytes) const override {
        someip_generated::ByteWriter writer(bytes);
        if (!writer.writeIntegralBE<std::uint32_t>(requestId)) return false;
        if (!status.serializeTo(bytes)) return false;
        return true;
    }

    bool deserializeFromReader(someip_generated::ByteReader& reader) override {
        if (!reader.readIntegralBE<std::uint32_t>(requestId)) return false;
        if (!status.deserializeFromReader(reader)) return false;
        return true;
    }
};

}  // namespace openveh::someip::vehicle
