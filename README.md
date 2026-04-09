# OpenVeh_SOMEIP
SOMEIP communication implementation, supporting publishing services, subscription services, and event sending

## Parameter IDL Generator

For interface parameters and event payload modeling, use the JSON-based IDL and generator in [tools/someip_idl/README.md](tools/someip_idl/README.md).

It supports:
- Basic data types
- Arrays
- Nested sub-data types
- Auto-generated C++ serialize/deserialize classes using big-endian byte order compatible with SOME/IP payload conventions
