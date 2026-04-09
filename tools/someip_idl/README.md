# SOME/IP Parameter IDL and Generator

This folder contains:
- A JSON-based parameter definition language (IDL)
- A code generator that produces C++ parameter classes
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
python3 tools/someip_idl/generate_someip_params.py tools/someip_idl/examples/vehicle_interface.json --out tools/someip_idl/generated
```

For team collaboration, you can split interfaces into multiple JSON files in one directory:

```bash
python3 tools/someip_idl/generate_someip_params.py tools/someip_idl/examples/collab --out tools/someip_idl/generated
```

If your JSON files are in nested folders:

```bash
python3 tools/someip_idl/generate_someip_params.py tools/someip_idl/examples/collab --recursive --out tools/someip_idl/generated
```

Rules for multi-file mode:
- All files must use the same `namespace`
- Type/message names must be globally unique across all files
- Cross-file type references are supported (for example one file references a type from another file)

Generated outputs include:
- `someip_codec_support.hpp` (byte reader/writer)
- One header per type/message
- `generated_someip_params.hpp` (aggregate include)

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
