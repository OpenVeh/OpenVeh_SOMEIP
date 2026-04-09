# OpenVeh_SOMEIP
SOMEIP communication implementation, supporting publishing services, subscription services, and event sending

## Parameter IDL Generator

For interface parameters and event payload modeling, use the JSON-based IDL and generator in [tools/someip_idl/README.md](tools/someip_idl/README.md).

It supports:
- Basic data types
- Arrays
- Nested sub-data types
- Auto-generated C++ serialize/deserialize classes using big-endian byte order compatible with SOME/IP payload conventions

## SOME/IP-SD Service And Standard API

Linux phase SOME/IP-SD style service registration and discovery implementation is available in [services/someip_sd/README.md](services/someip_sd/README.md).

C++ implementation (recommended) is available in [services/someip_sd_cpp/README.md](services/someip_sd_cpp/README.md).

It provides:
- SD daemon for service registration and discovery
- Standard app-facing API for register/discover/unregister
- Provider/consumer runnable demos
- Unit tests for register/discover and TTL expiry behavior
