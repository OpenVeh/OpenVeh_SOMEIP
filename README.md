# OpenVeh_SOMEIP
SOMEIP communication implementation, supporting publishing services, subscription services, and event sending

## Parameter IDL Generator

For interface parameters and event payload modeling, use the JSON-based IDL and generator in [tools/someip_idl/README.md](tools/someip_idl/README.md).

It supports:
- Basic data types
- Arrays
- Nested sub-data types
- Auto-generated C++ serialize/deserialize classes using big-endian byte order compatible with SOME/IP payload conventions

## Generated Application Framework

The same JSON-based IDL can also define application-level SOME/IP roles and generate framework code.

It supports:
- Generated abstract service interfaces for providers
- Generated client proxies for consumers
- Generated event callback hooks for subscribing clients
- A top-level generated application composition class
- A backend-based runtime abstraction that hides socket details from business code

See [tools/someip_idl/README.md](tools/someip_idl/README.md) for the IDL schema, generation commands, and validation workflow.

The runtime abstraction is in [services/someip_app_framework/include/someip_app_framework/application_runtime.hpp](services/someip_app_framework/include/someip_app_framework/application_runtime.hpp).

## SOME/IP-SD Service And Standard API

Linux phase SOME/IP-SD style service registration and discovery implementation is available in [services/someip_sd/README.md](services/someip_sd/README.md).

C++ implementation (recommended) is available in [services/someip_sd_cpp/README.md](services/someip_sd_cpp/README.md).

It provides:
- SD daemon for service registration and discovery
- Standard app-facing API for register/discover/unregister
- Provider/consumer runnable demos
- Unit tests for register/discover and TTL expiry behavior
