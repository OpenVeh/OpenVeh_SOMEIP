# SOME/IP-SD C++ Implementation (Linux Phase)

This module provides a C++ implementation of SOME/IP-SD style service registry and discovery.

## Features

- Service registration
- Service unregistration
- Service discovery via multicast Find/Offer
- TTL-based service expiry
- Standard application API
- Unicast control channel over UDP, optional Unix Domain Socket control (Linux)

## Standard Application API

Header: `include/someip_sd/sd_api.hpp`

- `bool RegisterService(const ServiceDescriptor&, int ttl_sec, std::string* error)`
- `bool UnregisterService(uint16_t service_id, uint16_t instance_id, std::string* error)`
- `ServiceList DiscoverServices(uint16_t service_id, std::optional<uint16_t> instance_id, int timeout_ms, std::string* error)`
- `SetDiscoveryMulticast(group, port, interface_address)`
- `EnableUnixDomainControl(socket_path)`

## Build And Test

```bash
chmod +x services/someip_sd_cpp/run_tests.sh
services/someip_sd_cpp/run_tests.sh
```

## Run Demos

Build first:

```bash
cmake -S services/someip_sd_cpp -B services/someip_sd_cpp/build
cmake --build services/someip_sd_cpp/build -j
```

Then run in separate terminals:

1. Daemon

```bash
services/someip_sd_cpp/build/run_sd_daemon
```

2. Provider

```bash
services/someip_sd_cpp/build/provider_demo
```

3. Consumer

```bash
services/someip_sd_cpp/build/consumer_demo
```

Demo behavior:
- Provider offers synchronous `GetVehicleInfo` method (request/response)
- Provider publishes `VehicleInfoChanged` notification event
- Consumer discovers provider by SD, subscribes event, invokes `GetVehicleInfo`, then receives event notifications
- Method/event payloads reuse generated parameter objects from `tools/someip_idl/generated`

## Cross-ECU Mode

- Discovery path: multicast SOME/IP-SD FindService -> OfferService responses
- Control path: unicast UDP by default
- On Linux, control path can use Unix Domain Socket for local app <-> local daemon control traffic

Example settings in code:

- Daemon: `SetDiscoveryMulticast("239.255.0.1", 30490, "127.0.0.1")`
- API: `SetDiscoveryMulticast("239.255.0.1", 30490, "127.0.0.1")`
- Linux optional: `EnableUnixDomainControl("/tmp/openveh_someip_sd.sock")`

## Notes

- This is Linux-first (POSIX socket based).
- Wire payload now uses SOME/IP-SD style Entry/Option binary layout (FindService / OfferService + IPv4 Endpoint option).
- Outer framing now uses the full 16-byte SOME/IP Common Header (Message ID, Length, Request ID, Protocol Version, Interface Version, Message Type, Return Code).
- Current mapping uses SOME/IP-SD service/method IDs (`0xFFFF/0x8100`) with SD payload blocks for operation semantics.
