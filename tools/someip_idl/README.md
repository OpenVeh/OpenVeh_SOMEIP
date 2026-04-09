# SOME/IP Parameter IDL and Generator

This folder contains:
- A JSON-based parameter definition language (IDL)
- A code generator that produces C++ parameter classes
- A code generator that produces C++ SOME/IP application framework skeletons
- Auto-generated serialize/deserialize logic aligned with SOME/IP payload byte-order conventions

## Why JSON as the language
JSON is chosen for phase 1 because it is easy to parse, diff, and integrate in CI.

The generated codec rules are:
- Integer fields: network byte order (big-endian)
- `bool`: one byte (`0` or non-zero)
- `string`: length-prefixed bytes
- `array`: length-prefixed sequence of elements
- Nested sub-types: recursively encoded by field order

Note: SOME/IP payload content is interface-defined. This generator enforces stable field order and big-endian primitive encoding. If your project needs AUTOSAR-specific string/array length tags or versioning headers, extend the IDL and templates accordingly.

## IDL format
Top-level keys:
- `namespace`: C++ namespace for generated classes
- `types`: reusable sub-data types
- `messages`: request/response/event parameter types

Each type/message has:
- `name`: class name
- `fields`: ordered field list

Field forms:
- Primitive: `{ "name": "speed", "type": "u16" }`
- String: `{ "name": "vin", "type": "string", "lengthType": "u16" }`
- Sub-type: `{ "name": "status", "type": "VehicleStatus" }`
- Array: `{ "name": "values", "type": "array", "elementType": "i32", "lengthType": "u16" }`

Supported primitive types:
- `u8` `u16` `u32` `u64`
- `i8` `i16` `i32` `i64`
- `f32` `f64`
- `bool`
- `string`

Supported length prefixes (`lengthType`):
- `u8` `u16` `u32`

## Generate code
From repository root:

```bash
python3 tools/someip_idl/generate_someip_params.py tools/someip_idl/examples/vehicle_node --out tools/someip_idl/generated
```

For team collaboration, split interfaces into multiple JSON files in one directory:

```bash
python3 tools/someip_idl/generate_someip_params.py tools/someip_idl/examples/vehicle_node --out tools/someip_idl/generated
```

If your JSON files are in nested folders:

```bash
python3 tools/someip_idl/generate_someip_params.py tools/someip_idl/examples/vehicle_node --recursive --out tools/someip_idl/generated
```

Rules for multi-file mode:
- All files must use the same `namespace`
- Type/message names must be globally unique across all files
- Cross-file type references are supported (for example one file references a type from another file)

Generated outputs include:
- `someip_codec_support.hpp` (byte reader/writer)
- One header per type/message
- `generated_someip_params.hpp` (aggregate include)

## Application framework IDL

The same JSON IDL can also define an application-facing framework layer.

Additional top-level keys:
- `application`: defines the generated application class, service instances, and client instances
- `services`: defines service contracts (service definitions)
- `clients`: defines client contracts (client definitions)

`application` format:

```json
{
	"application": {
		"name": "VehicleNodeApplication",
		"serviceInstances": [
			{
				"name": "VehicleInfoService",
				"service": "VehicleInfoService",
				"instanceId": "0x0001"
			}
		],
		"clientInstances": [
			{
				"name": "VehicleInfoClientMain",
				"client": "VehicleInfoClient",
				"serviceInstance": "VehicleInfoService"
			}
		]
	}
}
```

`application.serviceInstances` format:
- `name`: generated application attachment name for this instance
- `service`: referenced service definition name from `services`
- `instanceId`: SOME/IP instance ID for this runtime instance

`application.clientInstances` format:
- `name`: generated application factory name for this client instance
- `client`: referenced client definition name from `clients`
- `serviceInstance`: referenced service instance name from `application.serviceInstances`

`services` format:
- `name`: generated service interface class name
- `serviceId`: SOME/IP service ID
- `methods`: list of method definitions
- `events`: list of event definitions

Method definition:

```json
{
	"name": "GetVehicleInfo",
	"methodId": "0x0001",
	"request": "GetVehicleInfoRequest",
	"response": "GetVehicleInfoResponse"
}
```

Event definition:

```json
{
	"name": "VehicleInfoChanged",
	"eventId": "0x8001",
	"payload": "VehicleInfoChangedEvent"
}
```

`clients` format:
- `name`: generated client interface/proxy class name
- `service`: referenced service name from `services`
- `methods`: method names to invoke
- `events`: event names to subscribe to

Behavior rules:
- Client construction is fully instance-based: generated application factories come from `application.clientInstances`, not from implicit/default instance inference.
- If a client subscribes to one or more events, the generator emits an abstract client class with `On<Event>` callbacks that the application implements.
- If a client only invokes methods, the generator emits a concrete client proxy class.
- Services are generated as abstract interfaces with `Handle<Method>` callbacks and `Publish<Event>` helpers.

Example application IDL:

```bash
tools/someip_idl/examples/vehicle_node/
```

The example directory uses one JSON file per role:
- `application.json`
- `server.json`
- `client.json`
- `message.json`

## Generate application framework code

From repository root:

```bash
python3 tools/someip_idl/generate_someip_app.py tools/someip_idl/examples/vehicle_node --out tools/someip_idl/generated_app
```

Directory input is also supported:

```bash
python3 tools/someip_idl/generate_someip_app.py tools/someip_idl/examples/vehicle_node --out tools/someip_idl/generated_app
```

If your IDL is split into nested folders:

```bash
python3 tools/someip_idl/generate_someip_app.py tools/someip_idl/examples/vehicle_node --recursive --out tools/someip_idl/generated_app
```

Generated outputs include:
- `<ApplicationName>.hpp` containing generated service classes, client classes, and the top-level application class
- `generated_someip_application.hpp` aggregate include

The generated code depends on the runtime abstraction in:
- `services/someip_app_framework/include/someip_app_framework/application_runtime.hpp`

That runtime deliberately hides socket details behind a backend interface so Linux can use the current transport stack now and later ports can provide QNX, Android, or RTOS backends without changing generated business classes.

## Unit test sample

Run sample tests for generated parameter classes:

```bash
tools/someip_idl/tests/run_tests.sh
```

The sample covers:
- Request/response/event round-trip serialization and deserialization
- Rejection of payloads with trailing bytes
- Rejection of truncated payloads
- Length-prefix overflow protection for array fields

Run the application framework generator test:

```bash
tools/someip_idl/tests/run_app_framework_tests.sh
```

This test covers:
- payload generation from the same app IDL
- application framework generation
- compile validation of generated classes
- service attach, method invocation, event publish, and event callback flow through a mock backend

## Integration example

```cpp
#include "generated_someip_params.hpp"

openveh::someip::params::VehicleStatusEvent evt;
evt.eventSequence = 1;

std::vector<std::uint8_t> payload = evt.serialize();

openveh::someip::params::VehicleStatusEvent parsed;
bool ok = parsed.deserialize(payload);
```

## Linux first, portable later
To support QNX/Android/RTOS later:
- Keep generated model and codec logic platform-agnostic
- Isolate transport (socket/epoll/select/RT APIs) outside generated classes
- Reuse generated payload classes across all platform adapters
- Keep application runtime backends small so ports only replace backend wiring, not generated service/client logic
