# OpenVeh SOME/IP Work Report

Date: 2026-04-09

This report captures the current implementation status so follow-up work can resume quickly in the next session.

## 1. Completed Work

### 1.1 Custom Agent

- Added a workspace custom agent for SOME/IP implementation work:
  - `.github/agents/someip-implementation.agent.md`
- The agent is Linux-first and scoped for:
  - service publish / offer-withdraw
  - service discovery
  - subscription and unsubscribe
  - event / field notifications
  - request / response RPC
  - tests and sample app support

### 1.2 Parameter IDL And Code Generator

- Added a JSON-based IDL and generator for interface parameters and event payloads:
  - `tools/someip_idl/generate_someip_params.py`
  - `tools/someip_idl/README.md`
- Supports:
  - primitive types
  - strings with configurable length prefix type
  - arrays
  - nested custom sub-types
  - message types for request / response / event payloads
- Generated C++ classes implement:
  - big-endian primitive serialization / deserialization
  - `SerializableObject` abstraction
  - instance methods instead of static deserialize helpers

### 1.3 Multi-File Collaborative IDL Input

- Generator now accepts either:
  - one JSON file
  - one directory containing multiple JSON files
  - optional recursive directory scan
- This supports multiple developers editing independent interface definition files.
- Added collaborative example input split across files:
  - `tools/someip_idl/examples/collab/01_types.json`
  - `tools/someip_idl/examples/collab/02_requests.json`
  - `tools/someip_idl/examples/collab/03_events.json`

### 1.4 Generated Payload Examples

- Sample generated payload objects currently include:
  - `GetVehicleStatusRequest`
  - `GetVehicleStatusResponse`
  - `VehicleStatusEvent`
  - `VehicleStatus`
  - `WheelInfo`
- Output directory:
  - `tools/someip_idl/generated/`

### 1.5 Generated Payload Unit Tests

- Added C++ round-trip and negative-path tests:
  - `tools/someip_idl/tests/test_generated_params.cpp`
  - `tools/someip_idl/tests/run_tests.sh`
- Covered cases:
  - request / response / event round-trip
  - trailing bytes rejection
  - truncated payload rejection
  - array length overflow rejection

### 1.6 SOME/IP-SD C++ Implementation

- Added C++ SOME/IP-SD implementation under:
  - `services/someip_sd_cpp/`
- Main public headers:
  - `services/someip_sd_cpp/include/someip_sd/types.hpp`
  - `services/someip_sd_cpp/include/someip_sd/protocol.hpp`
  - `services/someip_sd_cpp/include/someip_sd/sd_api.hpp`
  - `services/someip_sd_cpp/include/someip_sd/sd_daemon.hpp`
- Main source files:
  - `services/someip_sd_cpp/src/protocol.cpp`
  - `services/someip_sd_cpp/src/sd_api.cpp`
  - `services/someip_sd_cpp/src/sd_daemon.cpp`

### 1.7 SOME/IP-SD Wire Format Status

- SD payload format uses SOME/IP-SD Entry / Option binary layout.
- Outer framing uses full 16-byte SOME/IP Common Header.
- Current SOME/IP-SD identifiers used by protocol layer:
  - Service ID: `0xFFFF`
  - Method ID: `0x8100`
- SD operations are inferred from SD payload content:
  - FindService entry -> discovery
  - OfferService with TTL > 0 -> register / refresh
  - OfferService with TTL = 0 -> unregister

### 1.8 Cross-ECU Discovery Architecture

- `someip_sd_cpp` was extended to support:
  - control plane: unicast
  - discovery plane: multicast
- Control plane options:
  - UDP unicast control
  - Unix Domain Socket control on Linux
- Discovery plane:
  - multicast FindService
  - multicast / network OfferService responses collected within timeout window

### 1.9 SOME/IP Application Demo Integration

- Example application payloads in `tools/someip_idl/generated` are now used by the demos.
- Added simple SOME/IP app-level message helper:
  - `services/someip_sd_cpp/examples/someip_app_protocol.hpp`
- Provider demo supports:
  - synchronous `GetVehicleInfo` style request / response flow
  - `VehicleInfoChanged` notification event
- Consumer demo supports:
  - service discovery through SD
  - event subscription request
  - synchronous method invocation
  - receiving and decoding notifications

Relevant files:
- `services/someip_sd_cpp/examples/provider_demo.cpp`
- `services/someip_sd_cpp/examples/consumer_demo.cpp`
- `services/someip_sd_cpp/examples/run_sd_daemon.cpp`

## 2. Validation Already Performed

### 2.1 Payload Generator Validation

Executed successfully:

```bash
python3 tools/someip_idl/generate_someip_params.py tools/someip_idl/examples/collab --out tools/someip_idl/generated
tools/someip_idl/tests/run_tests.sh
```

### 2.2 SOME/IP-SD C++ Validation

Executed successfully:

```bash
services/someip_sd_cpp/run_tests.sh
```

Current C++ test coverage includes:
- register and discover
- unregister then not discoverable
- TTL expiry
- Linux UDS control path scenario

### 2.3 Example End-to-End Demo Validation

Verified successfully:
- run daemon
- run provider
- run consumer
- consumer discovered provider
- consumer subscribed event
- consumer invoked synchronous method
- consumer received multiple event notifications

Observed successful flow included:
- provider found at `127.0.0.1:50001`
- `GetVehicleInfo` response decoded successfully
- multiple `VehicleInfoChanged` notifications received and decoded successfully

## 3. Current Key Constraints / Limitations

### 3.1 Payload Type Naming

- Demo semantics are `GetVehicleInfo` and `VehicleInfoChanged`, but generated payload class names are still:
  - `GetVehicleStatusRequest`
  - `GetVehicleStatusResponse`
  - `VehicleStatusEvent`
- Functional flow is correct, but naming is not yet fully aligned with demo terminology.

### 3.2 SOME/IP-SD Behavior Scope

- Current implementation supports the core register / unregister / find / offer path.
- Full production-grade AUTOSAR SOME/IP-SD behavior is not yet complete.
- Not yet implemented:
  - periodic unsolicited OfferService broadcast without a Find trigger
  - SubscribeEventgroup / StopSubscribeEventgroup SD entries
  - eventgroup state tracking inside SD daemon
  - richer option sets beyond IPv4 endpoint
  - multi-network-interface policy and route selection

### 3.3 Example App Scope

- Example request / response / event traffic is implemented in demos, not yet integrated into a reusable generic application runtime library.
- Subscription in example app is currently a demo-level request, not yet mapped to standard SD eventgroup subscription entries.

## 4. Important Runtime Defaults

### 4.1 SOME/IP-SD Defaults

- Default local SD port: `30490`
- Test alternate port used in tests: `30491`, `30492`
- Default multicast group used in current implementation/examples:
  - `239.255.0.1`

### 4.2 Example App Defaults

- Example service ID: `0x1234`
- Example instance ID: `0x0001`
- Example provider app UDP port: `50001`
- Example method IDs / event ID in demos:
  - `GetVehicleInfo` method: `0x0001`
  - subscribe request: `0x0002`
  - `VehicleInfoChanged` event: `0x8001`

## 5. Recommended Next Steps

Priority order recommended for the next session:

1. Rename and regenerate payload classes to align names with current demo API
   - target names:
     - `GetVehicleInfoRequest`
     - `GetVehicleInfoResponse`
     - `VehicleInfoChangedEvent`

2. Add unsolicited periodic OfferService multicast
   - so services become discoverable even before a Find request is sent

3. Implement standard SD eventgroup subscription flow
   - `SubscribeEventgroup`
   - `StopSubscribeEventgroup`
   - eventgroup registry and subscriber lifecycle

4. Split demo application transport into reusable library code
   - reusable SOME/IP application server/client helper
   - isolate demo logic from transport plumbing

5. Expand test coverage
   - multi-daemon / multi-provider discovery
   - duplicate offer suppression
   - multicast across multiple processes
   - malformed SOME/IP / SD frames

## 6. Suggested Resume Prompt

Good prompt to continue next time:

"Read `WORK_REPORT_2026-04-09.md` and continue from the current OpenVeh SOME/IP status. Start with payload renaming and then add standard SubscribeEventgroup support to someip_sd_cpp."
