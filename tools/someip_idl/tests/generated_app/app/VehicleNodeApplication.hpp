#pragma once

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "someip_app_framework/application_runtime.hpp"
#include "GetVehicleInfoRequest.hpp"
#include "GetVehicleInfoResponse.hpp"
#include "SetCruiseSpeedRequest.hpp"
#include "SetCruiseSpeedResponse.hpp"
#include "VehicleInfoChangedEvent.hpp"

namespace openveh::someip::vehicle {

class VehicleInfoService {
public:
    virtual ~VehicleInfoService() = default;

    virtual GetVehicleInfoResponse HandleGetVehicleInfo(const GetVehicleInfoRequest& request) = 0;
    virtual SetCruiseSpeedResponse HandleSetCruiseSpeed(const SetCruiseSpeedRequest& request) = 0;

    bool PublishVehicleInfoChanged(const VehicleInfoChangedEvent& event) const {
        return runtime_ && runtime_->PublishEvent(
            someip_app_framework::EventKey{0x1234, instance_id_, 0x8001},
            event.serialize());
    }

private:
    friend class VehicleNodeApplication;
    void AttachRuntime(const std::shared_ptr<someip_app_framework::ApplicationRuntime>& runtime, std::uint16_t instance_id) {
        runtime_ = runtime;
        instance_id_ = instance_id;
    }
    bool RegisterWithRuntime() {
        if (!runtime_) { return false; }
        bool ok = runtime_->OfferService(someip_app_framework::ServiceKey{0x1234, instance_id_});
        ok = ok && runtime_->RegisterMethodHandler(
            someip_app_framework::MethodKey{0x1234, instance_id_, 0x0001},
            [this](const std::vector<std::uint8_t>& request_bytes, std::vector<std::uint8_t>* response_bytes) -> bool {
                GetVehicleInfoRequest request;
                if (!request.deserialize(request_bytes)) { return false; }
                const auto response = HandleGetVehicleInfo(request);
                *response_bytes = response.serialize();
                return true;
            });
        ok = ok && runtime_->RegisterMethodHandler(
            someip_app_framework::MethodKey{0x1234, instance_id_, 0x0003},
            [this](const std::vector<std::uint8_t>& request_bytes, std::vector<std::uint8_t>* response_bytes) -> bool {
                SetCruiseSpeedRequest request;
                if (!request.deserialize(request_bytes)) { return false; }
                const auto response = HandleSetCruiseSpeed(request);
                *response_bytes = response.serialize();
                return true;
            });
        return ok;
    }
    bool UnregisterFromRuntime() {
        if (!runtime_) { return false; }
        bool ok = true;
        ok = runtime_->UnregisterMethodHandler(someip_app_framework::MethodKey{0x1234, instance_id_, 0x0001}) && ok;
        ok = runtime_->UnregisterMethodHandler(someip_app_framework::MethodKey{0x1234, instance_id_, 0x0003}) && ok;
        ok = runtime_->StopOfferService(someip_app_framework::ServiceKey{0x1234, instance_id_}) && ok;
        return ok;
    }
    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime_;
    std::uint16_t instance_id_{0};
};

class VehicleInfoClient {
public:
    virtual ~VehicleInfoClient() = default;
    VehicleInfoClient(std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime, std::uint16_t instance_id) : runtime_(std::move(runtime)), instance_id_(instance_id) {}

    bool Discover() const {
        return runtime_ && runtime_->DiscoverService(someip_app_framework::ServiceKey{0x1234, instance_id_});
    }

    GetVehicleInfoResponse GetVehicleInfo(const GetVehicleInfoRequest& request) const {
        GetVehicleInfoResponse response{};
        if (!runtime_) { return response; }
        std::vector<std::uint8_t> response_bytes;
        if (!runtime_->InvokeMethod(someip_app_framework::MethodKey{0x1234, instance_id_, 0x0001}, request.serialize(), &response_bytes)) {
            return response;
        }
        response.deserialize(response_bytes);
        return response;
    }

    bool SubscribeVehicleInfoChanged() {
        if (!runtime_) { return false; }
        return runtime_->SubscribeEvent(
            someip_app_framework::EventKey{0x1234, instance_id_, 0x8001},
            [this](const std::vector<std::uint8_t>& payload) {
                VehicleInfoChangedEvent event{};
                if (event.deserialize(payload)) {
                    this->OnVehicleInfoChanged(event);
                }
            });
    }

    bool UnsubscribeVehicleInfoChanged() {
        return runtime_ && runtime_->UnsubscribeEvent(
            someip_app_framework::EventKey{0x1234, instance_id_, 0x8001});
    }

protected:
    virtual void OnVehicleInfoChanged(const VehicleInfoChangedEvent& event) = 0;

private:
    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime_;
    const std::uint16_t instance_id_{0};
};

class CruiseControlClient {
public:
    ~CruiseControlClient() = default;
    CruiseControlClient(std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime, std::uint16_t instance_id) : runtime_(std::move(runtime)), instance_id_(instance_id) {}

    bool Discover() const {
        return runtime_ && runtime_->DiscoverService(someip_app_framework::ServiceKey{0x1234, instance_id_});
    }

    SetCruiseSpeedResponse SetCruiseSpeed(const SetCruiseSpeedRequest& request) const {
        SetCruiseSpeedResponse response{};
        if (!runtime_) { return response; }
        std::vector<std::uint8_t> response_bytes;
        if (!runtime_->InvokeMethod(someip_app_framework::MethodKey{0x1234, instance_id_, 0x0003}, request.serialize(), &response_bytes)) {
            return response;
        }
        response.deserialize(response_bytes);
        return response;
    }

private:
    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime_;
    const std::uint16_t instance_id_{0};
};

class VehicleNodeApplication {
public:
    explicit VehicleNodeApplication(std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime) : runtime_(std::move(runtime)) {}

    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime() const { return runtime_; }

    bool AttachVehicleInfoService(const std::shared_ptr<VehicleInfoService>& service) {
        if (!service || !runtime_) { return false; }
        service->AttachRuntime(runtime_, 0x0001);
        if (!service->RegisterWithRuntime()) { return false; }
        vehicle_info_service_instance_ = service;
        return true;
    }

    bool DetachVehicleInfoService() {
        if (!vehicle_info_service_instance_) { return false; }
        const bool ok = vehicle_info_service_instance_->UnregisterFromRuntime();
        vehicle_info_service_instance_.reset();
        return ok;
    }

    template <typename TClient, typename... Args> std::shared_ptr<TClient> CreateVehicleInfoClientMain(Args&&... args) const {
        static_assert(std::is_base_of<VehicleInfoClient, TClient>::value, "TClient must derive from VehicleInfoClient");
        return std::make_shared<TClient>(runtime_, 0x0001, std::forward<Args>(args)...);
    }

    std::shared_ptr<CruiseControlClient> CreateCruiseControlClientMain() const {
        return std::make_shared<CruiseControlClient>(runtime_, 0x0001);
    }

private:
    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime_;
    std::shared_ptr<VehicleInfoService> vehicle_info_service_instance_;
};

}  // namespace openveh::someip::vehicle
