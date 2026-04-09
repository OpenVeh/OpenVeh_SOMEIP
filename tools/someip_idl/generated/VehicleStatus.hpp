#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "someip_codec_support.hpp"

#include "WheelInfo.hpp"

namespace openveh::someip::params {

struct VehicleStatus : public someip_generated::SerializableObject {
    std::string vin;
    std::uint16_t speedKph;
    std::uint32_t engineRpm;
    bool doorOpen;
    std::vector<WheelInfo> wheels;
    std::vector<float> accelHistory;

    bool serializeTo(std::vector<std::uint8_t>& bytes) const override {
        someip_generated::ByteWriter writer(bytes);
        if (vin.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) return false;
        if (!writer.writeIntegralBE<std::uint16_t>(static_cast<std::uint16_t>(vin.size()))) return false;
        bytes.insert(bytes.end(), vin.begin(), vin.end());
        if (!writer.writeIntegralBE<std::uint16_t>(speedKph)) return false;
        if (!writer.writeIntegralBE<std::uint32_t>(engineRpm)) return false;
        if (!writer.writeIntegralBE<std::uint8_t>(doorOpen ? 1u : 0u)) return false;
        if (wheels.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max())) return false;
        if (!writer.writeIntegralBE<std::uint8_t>(static_cast<std::uint8_t>(wheels.size()))) return false;
        for (const auto& elem : wheels) {
            if (!elem.serializeTo(bytes)) return false;
        }
        if (accelHistory.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) return false;
        if (!writer.writeIntegralBE<std::uint16_t>(static_cast<std::uint16_t>(accelHistory.size()))) return false;
        for (const auto& elem : accelHistory) {
            if (!writer.writeFloat32BE(elem)) return false;
        }
        return true;
    }

    bool deserializeFromReader(someip_generated::ByteReader& reader) override {
        std::uint16_t vinStrLen = 0;
        if (!reader.readIntegralBE<std::uint16_t>(vinStrLen)) return false;
        if (!reader.readBytes(static_cast<std::size_t>(vinStrLen), vin)) return false;
        if (!reader.readIntegralBE<std::uint16_t>(speedKph)) return false;
        if (!reader.readIntegralBE<std::uint32_t>(engineRpm)) return false;
        std::uint8_t b = 0;
        if (!reader.readIntegralBE<std::uint8_t>(b)) return false;
        doorOpen = (b != 0);
        std::uint8_t wheelsLen = 0;
        if (!reader.readIntegralBE<std::uint8_t>(wheelsLen)) return false;
        wheels.clear();
        wheels.reserve(static_cast<std::size_t>(wheelsLen));
        for (std::uint8_t i = 0; i < wheelsLen; ++i) {
            WheelInfo temp;
            if (!temp.deserializeFromReader(reader)) return false;
            wheels.push_back(std::move(temp));
        }
        std::uint16_t accelHistoryLen = 0;
        if (!reader.readIntegralBE<std::uint16_t>(accelHistoryLen)) return false;
        accelHistory.clear();
        accelHistory.reserve(static_cast<std::size_t>(accelHistoryLen));
        for (std::uint16_t i = 0; i < accelHistoryLen; ++i) {
            float temp = 0.0f;
            if (!reader.readFloat32BE(temp)) return false;
            accelHistory.push_back(temp);
        }
        return true;
    }
};

}  // namespace openveh::someip::params
