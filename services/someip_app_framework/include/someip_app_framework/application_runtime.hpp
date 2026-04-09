#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace someip_app_framework {

struct ServiceKey {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{0};
};

struct MethodKey {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{0};
    std::uint16_t method_id{0};
};

struct EventKey {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{0};
    std::uint16_t event_id{0};
};

using MethodHandler = std::function<bool(const std::vector<std::uint8_t>&, std::vector<std::uint8_t>*)>;
using EventHandler = std::function<void(const std::vector<std::uint8_t>&)>;

class IApplicationBackend {
public:
    virtual ~IApplicationBackend() = default;

    virtual bool OfferService(const ServiceKey& key) = 0;
    virtual bool StopOfferService(const ServiceKey& key) = 0;
    virtual bool DiscoverService(const ServiceKey& key) = 0;

    virtual bool RegisterMethodHandler(const MethodKey& key, MethodHandler handler) = 0;
    virtual bool UnregisterMethodHandler(const MethodKey& key) = 0;

    virtual bool InvokeMethod(const MethodKey& key,
                              const std::vector<std::uint8_t>& request,
                              std::vector<std::uint8_t>* response) = 0;

    virtual bool PublishEvent(const EventKey& key, const std::vector<std::uint8_t>& payload) = 0;
    virtual bool SubscribeEvent(const EventKey& key, EventHandler handler) = 0;
    virtual bool UnsubscribeEvent(const EventKey& key) = 0;
};

class ApplicationRuntime {
public:
    explicit ApplicationRuntime(std::shared_ptr<IApplicationBackend> backend)
        : backend_(std::move(backend)) {}

    bool OfferService(const ServiceKey& key) const {
        return backend_ && backend_->OfferService(key);
    }

    bool StopOfferService(const ServiceKey& key) const {
        return backend_ && backend_->StopOfferService(key);
    }

    bool DiscoverService(const ServiceKey& key) const {
        return backend_ && backend_->DiscoverService(key);
    }

    bool RegisterMethodHandler(const MethodKey& key, MethodHandler handler) const {
        return backend_ && backend_->RegisterMethodHandler(key, std::move(handler));
    }

    bool UnregisterMethodHandler(const MethodKey& key) const {
        return backend_ && backend_->UnregisterMethodHandler(key);
    }

    bool InvokeMethod(const MethodKey& key,
                      const std::vector<std::uint8_t>& request,
                      std::vector<std::uint8_t>* response) const {
        return backend_ && backend_->InvokeMethod(key, request, response);
    }

    bool PublishEvent(const EventKey& key, const std::vector<std::uint8_t>& payload) const {
        return backend_ && backend_->PublishEvent(key, payload);
    }

    bool SubscribeEvent(const EventKey& key, EventHandler handler) const {
        return backend_ && backend_->SubscribeEvent(key, std::move(handler));
    }

    bool UnsubscribeEvent(const EventKey& key) const {
        return backend_ && backend_->UnsubscribeEvent(key);
    }

private:
    std::shared_ptr<IApplicationBackend> backend_;
};

}  // namespace someip_app_framework
