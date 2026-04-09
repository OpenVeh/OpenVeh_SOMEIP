#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "GetVehicleStatusRequest.hpp"
#include "GetVehicleStatusResponse.hpp"
#include "VehicleStatus.hpp"
#include "VehicleStatusEvent.hpp"
#include "WheelInfo.hpp"

using namespace openveh::someip::params;

namespace {

WheelInfo makeWheel(std::uint8_t id, std::uint16_t pressure, std::int16_t temp) {
    WheelInfo w;
    w.wheelId = id;
    w.pressureKpa = pressure;
    w.temperatureC = temp;
    return w;
}

VehicleStatus makeStatus() {
    VehicleStatus s;
    s.vin = "LINUX-SOMEIP-0001";
    s.speedKph = 88;
    s.engineRpm = 2450;
    s.doorOpen = true;
    s.wheels = {
        makeWheel(0, 235, 35),
        makeWheel(1, 236, 34),
        makeWheel(2, 237, 36),
        makeWheel(3, 234, 33),
    };
    s.accelHistory = {0.1f, 0.2f, -0.3f, 1.5f};
    return s;
}

void testRequestRoundTrip() {
    GetVehicleStatusRequest req;
    req.requestId = 0x01020304u;
    req.targetVin = "VIN123";

    const std::vector<std::uint8_t> bytes = req.serialize();
    assert(!bytes.empty());

    GetVehicleStatusRequest parsed;
    const bool ok = parsed.deserialize(bytes);
    assert(ok);
    assert(parsed.requestId == req.requestId);
    assert(parsed.targetVin == req.targetVin);
}

void testResponseRoundTrip() {
    GetVehicleStatusResponse resp;
    resp.requestId = 42;
    resp.status = makeStatus();

    const std::vector<std::uint8_t> bytes = resp.serialize();
    assert(!bytes.empty());

    GetVehicleStatusResponse parsed;
    const bool ok = parsed.deserialize(bytes);
    assert(ok);

    assert(parsed.requestId == resp.requestId);
    assert(parsed.status.vin == resp.status.vin);
    assert(parsed.status.speedKph == resp.status.speedKph);
    assert(parsed.status.engineRpm == resp.status.engineRpm);
    assert(parsed.status.doorOpen == resp.status.doorOpen);
    assert(parsed.status.wheels.size() == resp.status.wheels.size());
    assert(parsed.status.accelHistory.size() == resp.status.accelHistory.size());

    for (std::size_t i = 0; i < parsed.status.wheels.size(); ++i) {
        assert(parsed.status.wheels[i].wheelId == resp.status.wheels[i].wheelId);
        assert(parsed.status.wheels[i].pressureKpa == resp.status.wheels[i].pressureKpa);
        assert(parsed.status.wheels[i].temperatureC == resp.status.wheels[i].temperatureC);
    }

    for (std::size_t i = 0; i < parsed.status.accelHistory.size(); ++i) {
        // Small tolerance for float round-trip.
        const float a = parsed.status.accelHistory[i];
        const float b = resp.status.accelHistory[i];
        assert((a - b < 0.0001f) && (b - a < 0.0001f));
    }
}

void testEventRoundTripAndTrailingBytesReject() {
    VehicleStatusEvent evt;
    evt.eventSequence = 7;
    evt.status = makeStatus();

    std::vector<std::uint8_t> bytes = evt.serialize();
    assert(!bytes.empty());

    VehicleStatusEvent parsed;
    assert(parsed.deserialize(bytes));
    assert(parsed.eventSequence == evt.eventSequence);
    assert(parsed.status.vin == evt.status.vin);

    // Add trailing bytes. Top-level deserialize should reject non-fully-consumed payload.
    bytes.push_back(0xAA);
    bytes.push_back(0xBB);

    VehicleStatusEvent shouldFail;
    assert(!shouldFail.deserialize(bytes));
}

void testTruncatedPayloadReject() {
    GetVehicleStatusResponse resp;
    resp.requestId = 9;
    resp.status = makeStatus();

    std::vector<std::uint8_t> bytes = resp.serialize();
    assert(bytes.size() > 5);

    bytes.pop_back();

    GetVehicleStatusResponse parsed;
    assert(!parsed.deserialize(bytes));
}

void testArrayLengthOverflowProtection() {
    VehicleStatus s = makeStatus();
    s.wheels.resize(300);

    std::vector<std::uint8_t> bytes;
    // In IDL, wheels uses u8 lengthType, so >255 must fail.
    assert(!s.serializeTo(bytes));
}

}  // namespace

int main() {
    testRequestRoundTrip();
    testResponseRoundTrip();
    testEventRoundTripAndTrailingBytesReject();
    testTruncatedPayloadReject();
    testArrayLengthOverflowProtection();

    std::cout << "All SOME/IP generated parameter tests passed.\n";
    return 0;
}
