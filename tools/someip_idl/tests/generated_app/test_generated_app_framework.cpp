#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "VehicleNodeApplication.hpp"
#include "someip_app_framework/application_runtime.hpp"

namespace {

using someip_app_framework::ApplicationRuntime;
using someip_app_framework::EventHandler;
using someip_app_framework::EventKey;
using someip_app_framework::IApplicationBackend;
using someip_app_framework::MethodHandler;
using someip_app_framework::MethodKey;
using someip_app_framework::ServiceKey;

std::uint64_t PackService(const ServiceKey& key) {
    return (static_cast<std::uint64_t>(key.service_id) << 16) | key.instance_id;
}

std::uint64_t PackMethod(const MethodKey& key) {
    return (static_cast<std::uint64_t>(key.service_id) << 32) |
           (static_cast<std::uint64_t>(key.instance_id) << 16) |
           key.method_id;
}

std::uint64_t PackEvent(const EventKey& key) {
    return (static_cast<std::uint64_t>(key.service_id) << 32) |
           (static_cast<std::uint64_t>(key.instance_id) << 16) |
           key.event_id;
}

class MockBackend : public IApplicationBackend {
public:
    bool OfferService(const ServiceKey& key) override {
        offered_[PackService(key)] = true;
        return true;
    }

    bool StopOfferService(const ServiceKey& key) override {
        offered_.erase(PackService(key));
        return true;
    }

    bool DiscoverService(const ServiceKey& key) override {
        return offered_.count(PackService(key)) != 0;
    }

    bool RegisterMethodHandler(const MethodKey& key, MethodHandler handler) override {
        methods_[PackMethod(key)] = std::move(handler);
        return true;
    }

    bool UnregisterMethodHandler(const MethodKey& key) override {
        methods_.erase(PackMethod(key));
        return true;
    }

    bool InvokeMethod(const MethodKey& key,
                      const std::vector<std::uint8_t>& request,
                      std::vector<std::uint8_t>* response) override {
        const auto it = methods_.find(PackMethod(key));
        if (it == methods_.end()) {
            return false;
        }
        return it->second(request, response);
    }

    bool PublishEvent(const EventKey& key, const std::vector<std::uint8_t>& payload) override {
        const auto it = events_.find(PackEvent(key));
        if (it == events_.end()) {
            return false;
        }
        it->second(payload);
        return true;
    }

    bool SubscribeEvent(const EventKey& key, EventHandler handler) override {
        events_[PackEvent(key)] = std::move(handler);
        return true;
    }

    bool UnsubscribeEvent(const EventKey& key) override {
        events_.erase(PackEvent(key));
        return true;
    }

private:
    std::map<std::uint64_t, bool> offered_;
    std::map<std::uint64_t, MethodHandler> methods_;
    std::map<std::uint64_t, EventHandler> events_;
};

class VehicleInfoServiceImpl : public openveh::someip::vehicle::VehicleInfoService {
public:
    openveh::someip::vehicle::GetVehicleInfoResponse HandleGetVehicleInfo(
        const openveh::someip::vehicle::GetVehicleInfoRequest& request) override {
        openveh::someip::vehicle::GetVehicleInfoResponse response;
        response.requestId = request.requestId;
        response.status.vin = request.targetVin;
        response.status.speedKph = 88;
        response.status.engineRpm = 2300;
        response.status.doorOpen = false;
        return response;
    }

    openveh::someip::vehicle::SetCruiseSpeedResponse HandleSetCruiseSpeed(
        const openveh::someip::vehicle::SetCruiseSpeedRequest& request) override {
        openveh::someip::vehicle::SetCruiseSpeedResponse response;
        response.accepted = request.targetSpeedKph > 0;
        return response;
    }
};

class VehicleInfoClientImpl : public openveh::someip::vehicle::VehicleInfoClient {
public:
    using openveh::someip::vehicle::VehicleInfoClient::VehicleInfoClient;

    void OnVehicleInfoChanged(const openveh::someip::vehicle::VehicleInfoChangedEvent& event) override {
        last_sequence = event.eventSequence;
        last_speed = event.status.speedKph;
    }

    std::uint32_t last_sequence{0};
    std::uint16_t last_speed{0};
};

}  // namespace

int main() {
    auto backend = std::make_shared<MockBackend>();
    auto runtime = std::make_shared<ApplicationRuntime>(backend);

    openveh::someip::vehicle::VehicleNodeApplication app(runtime);
    auto service = std::make_shared<VehicleInfoServiceImpl>();
    assert(app.AttachVehicleInfoService(service));

    auto client = app.CreateVehicleInfoClientMain<VehicleInfoClientImpl>();
    assert(client->Discover());

    openveh::someip::vehicle::GetVehicleInfoRequest request;
    request.requestId = 7;
    request.targetVin = "VIN-123";
    const auto response = client->GetVehicleInfo(request);
    assert(response.requestId == 7);
    assert(response.status.vin == "VIN-123");
    assert(response.status.speedKph == 88);

    auto cruise_client = app.CreateCruiseControlClientMain();
    openveh::someip::vehicle::SetCruiseSpeedRequest set_request;
    set_request.targetSpeedKph = 100;
    const auto set_response = cruise_client->SetCruiseSpeed(set_request);
    assert(set_response.accepted);

    assert(client->SubscribeVehicleInfoChanged());
    openveh::someip::vehicle::VehicleInfoChangedEvent event;
    event.eventSequence = 5;
    event.status.speedKph = 99;
    assert(service->PublishVehicleInfoChanged(event));
    assert(client->last_sequence == 5);
    assert(client->last_speed == 99);

    assert(client->UnsubscribeVehicleInfoChanged());
    assert(app.DetachVehicleInfoService());

    std::cout << "Generated SOME/IP application framework test passed.\n";
    return 0;
}
