#!/usr/bin/env python3
"""Generate SOME/IP application framework skeleton code from JSON IDL.

The generator reads the same JSON-based IDL used for payload generation and additionally
consumes `application`, `services`, and `clients` sections to emit C++ framework code for:
- service-side abstract interfaces (service definitions)
- service instance attachment points defined by application.serviceInstances
- client instance factory points defined by application.clientInstances
- client-side abstract proxies with callback hooks when events are subscribed
- client-side concrete proxies when no subscribed events exist
- top-level application composition class
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Set


@dataclass
class MethodSpec:
    name: str
    method_id: int
    request: str
    response: str


@dataclass
class EventSpec:
    name: str
    event_id: int
    payload: str


@dataclass
class ServiceSpec:
    name: str
    service_id: int
    methods: List[MethodSpec]
    events: List[EventSpec]


@dataclass
class ServiceInstanceSpec:
    name: str
    service_name: str
    instance_id: int


@dataclass
class ClientSpec:
    name: str
    service_name: str
    methods: List[MethodSpec]
    events: List[EventSpec]


@dataclass
class ClientInstanceSpec:
    name: str
    client_name: str
    service_instance_name: str


@dataclass
class AppSpec:
    namespace: str
    application_name: str
    message_names: Set[str]
    services: List[ServiceSpec]
    service_instances: List[ServiceInstanceSpec]
    clients: List[ClientSpec]
    client_instances: List[ClientInstanceSpec]


def _collect_idl_files(idl_input: Path, recursive: bool = False) -> List[Path]:
    if idl_input.is_file():
        if idl_input.suffix.lower() != ".json":
            raise ValueError(f"IDL file must be .json: {idl_input}")
        return [idl_input]
    if idl_input.is_dir():
        pattern = "**/*.json" if recursive else "*.json"
        files = sorted(p for p in idl_input.glob(pattern) if p.is_file())
        if not files:
            raise ValueError(f"No JSON files found in {idl_input}")
        return files
    raise ValueError(f"IDL path does not exist: {idl_input}")


def _must_identifier(name: str, context: str) -> None:
    if not name or not name.replace("_", "a").isalnum() or name[0].isdigit():
        raise ValueError(f"{context} '{name}' is not a valid identifier")


def _parse_int(value: object, context: str) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"{context} must be int or numeric string")


def _parse_method(raw: dict, known_messages: Set[str], context: str) -> MethodSpec:
    for key in ("name", "methodId", "request", "response"):
        if key not in raw:
            raise ValueError(f"{context}: method requires '{key}'")
    name = raw["name"]
    _must_identifier(name, f"Method name in {context}")
    request = raw["request"]
    response = raw["response"]
    if request not in known_messages:
        raise ValueError(f"{context}.{name}: unknown request message '{request}'")
    if response not in known_messages:
        raise ValueError(f"{context}.{name}: unknown response message '{response}'")
    return MethodSpec(name=name, method_id=_parse_int(raw["methodId"], f"{context}.{name}.methodId"), request=request, response=response)


def _parse_event(raw: dict, known_messages: Set[str], context: str) -> EventSpec:
    for key in ("name", "eventId", "payload"):
        if key not in raw:
            raise ValueError(f"{context}: event requires '{key}'")
    name = raw["name"]
    _must_identifier(name, f"Event name in {context}")
    payload = raw["payload"]
    if payload not in known_messages:
        raise ValueError(f"{context}.{name}: unknown payload message '{payload}'")
    return EventSpec(name=name, event_id=_parse_int(raw["eventId"], f"{context}.{name}.eventId"), payload=payload)


def parse_app_idl(idl_input: Path, recursive: bool = False) -> AppSpec:
    files = _collect_idl_files(idl_input, recursive=recursive)

    merged_namespace: Optional[str] = None
    application_name: Optional[str] = None
    message_names: Set[str] = set()
    raw_services: List[dict] = []
    raw_clients: List[dict] = []
    raw_service_instances: List[dict] = []
    raw_client_instances: List[dict] = []

    for path in files:
        data = json.loads(path.read_text(encoding="utf-8"))
        namespace = data.get("namespace", "someip::generated")
        if merged_namespace is None:
            merged_namespace = namespace
        elif merged_namespace != namespace:
            raise ValueError(f"Namespace mismatch in {path}: {namespace} != {merged_namespace}")

        app = data.get("application")
        if app is not None:
            name = app.get("name")
            if not name:
                raise ValueError(f"{path}: application.name is required when application section exists")
            _must_identifier(name, "Application name")
            if application_name is None:
                application_name = name
            elif application_name != name:
                raise ValueError(f"Application name mismatch in {path}: {name} != {application_name}")
            raw_service_instances.extend(app.get("serviceInstances", []))
            raw_client_instances.extend(app.get("clientInstances", []))

        for msg in data.get("messages", []):
            name = msg.get("name")
            if not name:
                raise ValueError(f"{path}: message name is required")
            _must_identifier(name, "Message name")
            message_names.add(name)

        raw_services.extend(data.get("services", []))
        raw_clients.extend(data.get("clients", []))

    if merged_namespace is None:
        raise ValueError("No namespace found")
    if application_name is None:
        raise ValueError("No application.name found in IDL")

    services: List[ServiceSpec] = []
    service_map: Dict[str, ServiceSpec] = {}
    for raw in raw_services:
        for key in ("name", "serviceId"):
            if key not in raw:
                raise ValueError(f"Service requires '{key}'")
        name = raw["name"]
        _must_identifier(name, "Service name")
        if name in service_map:
            raise ValueError(f"Duplicate service '{name}'")
        methods = [_parse_method(item, message_names, name) for item in raw.get("methods", [])]
        events = [_parse_event(item, message_names, name) for item in raw.get("events", [])]
        spec = ServiceSpec(
            name=name,
            service_id=_parse_int(raw["serviceId"], f"{name}.serviceId"),
            methods=methods,
            events=events,
        )
        services.append(spec)
        service_map[name] = spec

    service_instances: List[ServiceInstanceSpec] = []
    instance_names: Set[str] = set()
    for raw in raw_service_instances:
        for key in ("name", "service", "instanceId"):
            if key not in raw:
                raise ValueError(f"serviceInstances item requires '{key}'")
        name = raw["name"]
        _must_identifier(name, "Service instance name")
        if name in instance_names:
            raise ValueError(f"Duplicate service instance '{name}'")
        instance_names.add(name)

        service_name = raw["service"]
        if service_name not in service_map:
            raise ValueError(f"Service instance '{name}' references unknown service '{service_name}'")

        service_instances.append(
            ServiceInstanceSpec(
                name=name,
                service_name=service_name,
                instance_id=_parse_int(raw["instanceId"], f"{name}.instanceId"),
            )
        )

    clients: List[ClientSpec] = []
    client_map: Dict[str, ClientSpec] = {}
    client_names: Set[str] = set()
    for raw in raw_clients:
        for key in ("name", "service"):
            if key not in raw:
                raise ValueError(f"Client requires '{key}'")
        name = raw["name"]
        _must_identifier(name, "Client name")
        if name in client_names:
            raise ValueError(f"Duplicate client '{name}'")
        client_names.add(name)

        service_name = raw["service"]
        if service_name not in service_map:
            raise ValueError(f"Client '{name}' references unknown service '{service_name}'")
        service = service_map[service_name]
        method_map = {m.name: m for m in service.methods}
        event_map = {e.name: e for e in service.events}

        methods: List[MethodSpec] = []
        for method_name in raw.get("methods", []):
            if method_name not in method_map:
                raise ValueError(f"Client '{name}' references unknown method '{method_name}'")
            methods.append(method_map[method_name])

        events: List[EventSpec] = []
        for event_name in raw.get("events", []):
            if event_name not in event_map:
                raise ValueError(f"Client '{name}' references unknown event '{event_name}'")
            events.append(event_map[event_name])

        client_spec = ClientSpec(name=name, service_name=service_name, methods=methods, events=events)
        clients.append(client_spec)
        client_map[name] = client_spec

    service_instance_map: Dict[str, ServiceInstanceSpec] = {s.name: s for s in service_instances}
    client_instances: List[ClientInstanceSpec] = []
    client_instance_names: Set[str] = set()
    for raw in raw_client_instances:
        for key in ("name", "client", "serviceInstance"):
            if key not in raw:
                raise ValueError(f"clientInstances item requires '{key}'")

        name = raw["name"]
        _must_identifier(name, "Client instance name")
        if name in client_instance_names:
            raise ValueError(f"Duplicate client instance '{name}'")
        client_instance_names.add(name)

        client_name = raw["client"]
        if client_name not in client_map:
            raise ValueError(f"Client instance '{name}' references unknown client '{client_name}'")

        service_instance_name = raw["serviceInstance"]
        if service_instance_name not in service_instance_map:
            raise ValueError(
                f"Client instance '{name}' references unknown service instance '{service_instance_name}'"
            )

        client_spec = client_map[client_name]
        service_instance_spec = service_instance_map[service_instance_name]
        if client_spec.service_name != service_instance_spec.service_name:
            raise ValueError(
                f"Client instance '{name}' binds client '{client_name}'(service={client_spec.service_name}) "
                f"to service instance '{service_instance_name}'(service={service_instance_spec.service_name})"
            )

        client_instances.append(
            ClientInstanceSpec(
                name=name,
                client_name=client_name,
                service_instance_name=service_instance_name,
            )
        )

    return AppSpec(
        namespace=merged_namespace,
        application_name=application_name,
        message_names=message_names,
        services=services,
        service_instances=service_instances,
        clients=clients,
        client_instances=client_instances,
    )


def _fmt_u16(value: int) -> str:
    return f"0x{value:04X}"


def _snake(name: str) -> str:
    out: List[str] = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0 and (not name[i - 1].isupper()):
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


def _instance_member_name(instance_name: str) -> str:
    return _snake(instance_name) + "_instance_"


def _client_instance_factory_name(client_instance_name: str) -> str:
    return "Create" + client_instance_name


def generate_header(spec: AppSpec, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)

    include_headers: Set[str] = set()
    for service in spec.services:
        for method in service.methods:
            include_headers.add(method.request + ".hpp")
            include_headers.add(method.response + ".hpp")
        for event in service.events:
            include_headers.add(event.payload + ".hpp")

    service_by_name: Dict[str, ServiceSpec] = {s.name: s for s in spec.services}
    client_by_name: Dict[str, ClientSpec] = {c.name: c for c in spec.clients}
    service_instance_by_name: Dict[str, ServiceInstanceSpec] = {s.name: s for s in spec.service_instances}

    lines: List[str] = []
    lines.extend([
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <memory>",
        "#include <type_traits>",
        "#include <utility>",
        "#include <vector>",
        "",
        "#include \"someip_app_framework/application_runtime.hpp\"",
    ])
    for header in sorted(include_headers):
        lines.append(f"#include \"{header}\"")
    lines.extend(["", f"namespace {spec.namespace} {{", ""])

    for service in spec.services:
        lines.append(f"class {service.name} {{")
        lines.append("public:")
        lines.append(f"    virtual ~{service.name}() = default;")
        lines.append("")
        for method in service.methods:
            lines.append(f"    virtual {method.response} Handle{method.name}(const {method.request}& request) = 0;")
        if service.events:
            lines.append("")
            for event in service.events:
                lines.append(f"    bool Publish{event.name}(const {event.payload}& event) const {{")
                lines.append("        return runtime_ && runtime_->PublishEvent(")
                lines.append(
                    f"            someip_app_framework::EventKey{{{_fmt_u16(service.service_id)}, instance_id_, {_fmt_u16(event.event_id)}}},"
                )
                lines.append("            event.serialize());")
                lines.append("    }")
        lines.append("")
        lines.append("private:")
        lines.append(f"    friend class {spec.application_name};")
        lines.append(
            "    void AttachRuntime(const std::shared_ptr<someip_app_framework::ApplicationRuntime>& runtime, std::uint16_t instance_id) {"
        )
        lines.append("        runtime_ = runtime;")
        lines.append("        instance_id_ = instance_id;")
        lines.append("    }")
        lines.append("    bool RegisterWithRuntime() {")
        lines.append("        if (!runtime_) { return false; }")
        lines.append(
            f"        bool ok = runtime_->OfferService(someip_app_framework::ServiceKey{{{_fmt_u16(service.service_id)}, instance_id_}});"
        )
        for method in service.methods:
            lines.append("        ok = ok && runtime_->RegisterMethodHandler(")
            lines.append(
                f"            someip_app_framework::MethodKey{{{_fmt_u16(service.service_id)}, instance_id_, {_fmt_u16(method.method_id)}}},"
            )
            lines.append("            [this](const std::vector<std::uint8_t>& request_bytes, std::vector<std::uint8_t>* response_bytes) -> bool {")
            lines.append(f"                {method.request} request;")
            lines.append("                if (!request.deserialize(request_bytes)) { return false; }")
            lines.append(f"                const auto response = Handle{method.name}(request);")
            lines.append("                *response_bytes = response.serialize();")
            lines.append("                return true;")
            lines.append("            });")
        lines.append("        return ok;")
        lines.append("    }")
        lines.append("    bool UnregisterFromRuntime() {")
        lines.append("        if (!runtime_) { return false; }")
        lines.append("        bool ok = true;")
        for method in service.methods:
            lines.append(
                f"        ok = runtime_->UnregisterMethodHandler(someip_app_framework::MethodKey{{{_fmt_u16(service.service_id)}, instance_id_, {_fmt_u16(method.method_id)}}}) && ok;"
            )
        lines.append(
            f"        ok = runtime_->StopOfferService(someip_app_framework::ServiceKey{{{_fmt_u16(service.service_id)}, instance_id_}}) && ok;"
        )
        lines.append("        return ok;")
        lines.append("    }")
        lines.append("    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime_;")
        lines.append("    std::uint16_t instance_id_{0};")
        lines.append("};")
        lines.append("")

    for client in spec.clients:
        service = service_by_name[client.service_name]
        abstract = bool(client.events)

        lines.append(f"class {client.name} {{")
        lines.append("public:")
        if abstract:
            lines.append(f"    virtual ~{client.name}() = default;")
        else:
            lines.append(f"    ~{client.name}() = default;")
        lines.append(
            f"    {client.name}(std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime, std::uint16_t instance_id)"
            " : runtime_(std::move(runtime)), instance_id_(instance_id) {}"
        )
        lines.append("")
        lines.append("    bool Discover() const {")
        lines.append(
            f"        return runtime_ && runtime_->DiscoverService(someip_app_framework::ServiceKey{{{_fmt_u16(service.service_id)}, instance_id_}});"
        )
        lines.append("    }")

        for method in client.methods:
            lines.append("")
            lines.append(f"    {method.response} {method.name}(const {method.request}& request) const {{")
            lines.append(f"        {method.response} response{{}};")
            lines.append("        if (!runtime_) { return response; }")
            lines.append("        std::vector<std::uint8_t> response_bytes;")
            lines.append(
                f"        if (!runtime_->InvokeMethod(someip_app_framework::MethodKey{{{_fmt_u16(service.service_id)}, instance_id_, {_fmt_u16(method.method_id)}}}, request.serialize(), &response_bytes)) {{"
            )
            lines.append("            return response;")
            lines.append("        }")
            lines.append("        response.deserialize(response_bytes);")
            lines.append("        return response;")
            lines.append("    }")

        for event in client.events:
            lines.append("")
            lines.append(f"    bool Subscribe{event.name}() {{")
            lines.append("        if (!runtime_) { return false; }")
            lines.append("        return runtime_->SubscribeEvent(")
            lines.append(
                f"            someip_app_framework::EventKey{{{_fmt_u16(service.service_id)}, instance_id_, {_fmt_u16(event.event_id)}}},"
            )
            lines.append("            [this](const std::vector<std::uint8_t>& payload) {")
            lines.append(f"                {event.payload} event{{}};")
            lines.append("                if (event.deserialize(payload)) {")
            lines.append(f"                    this->On{event.name}(event);")
            lines.append("                }")
            lines.append("            });")
            lines.append("    }")
            lines.append("")
            lines.append(f"    bool Unsubscribe{event.name}() {{")
            lines.append("        return runtime_ && runtime_->UnsubscribeEvent(")
            lines.append(
                f"            someip_app_framework::EventKey{{{_fmt_u16(service.service_id)}, instance_id_, {_fmt_u16(event.event_id)}}});"
            )
            lines.append("    }")

        if abstract:
            lines.append("")
            lines.append("protected:")
            for event in client.events:
                lines.append(f"    virtual void On{event.name}(const {event.payload}& event) = 0;")

        lines.append("")
        lines.append("private:")
        lines.append("    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime_;")
        lines.append("    const std::uint16_t instance_id_{0};")
        lines.append("};")
        lines.append("")

    lines.append(f"class {spec.application_name} {{")
    lines.append("public:")
    lines.append(
        f"    explicit {spec.application_name}(std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime) : runtime_(std::move(runtime)) {{}}"
    )
    lines.append("")
    lines.append("    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime() const { return runtime_; }")

    for instance in spec.service_instances:
        service = service_by_name[instance.service_name]
        member = _instance_member_name(instance.name)
        lines.append("")
        lines.append(f"    bool Attach{instance.name}(const std::shared_ptr<{service.name}>& service) {{")
        lines.append("        if (!service || !runtime_) { return false; }")
        lines.append(f"        service->AttachRuntime(runtime_, {_fmt_u16(instance.instance_id)});")
        lines.append("        if (!service->RegisterWithRuntime()) { return false; }")
        lines.append(f"        {member} = service;")
        lines.append("        return true;")
        lines.append("    }")
        lines.append("")
        lines.append(f"    bool Detach{instance.name}() {{")
        lines.append(f"        if (!{member}) {{ return false; }}")
        lines.append(f"        const bool ok = {member}->UnregisterFromRuntime();")
        lines.append(f"        {member}.reset();")
        lines.append("        return ok;")
        lines.append("    }")

    for client_instance in spec.client_instances:
        client = client_by_name[client_instance.client_name]
        service_instance = service_instance_by_name[client_instance.service_instance_name]
        if client.events:
            lines.append("")
            lines.append(
                f"    template <typename TClient, typename... Args> std::shared_ptr<TClient> {_client_instance_factory_name(client_instance.name)}(Args&&... args) const {{"
            )
            lines.append(
                f"        static_assert(std::is_base_of<{client.name}, TClient>::value, \"TClient must derive from {client.name}\");"
            )
            lines.append(
                f"        return std::make_shared<TClient>(runtime_, {_fmt_u16(service_instance.instance_id)}, std::forward<Args>(args)...);"
            )
            lines.append("    }")
        else:
            lines.append("")
            lines.append(
                f"    std::shared_ptr<{client.name}> {_client_instance_factory_name(client_instance.name)}() const {{"
            )
            lines.append(
                f"        return std::make_shared<{client.name}>(runtime_, {_fmt_u16(service_instance.instance_id)});"
            )
            lines.append("    }")

    lines.append("")
    lines.append("private:")
    lines.append("    std::shared_ptr<someip_app_framework::ApplicationRuntime> runtime_;")
    for instance in spec.service_instances:
        service = service_by_name[instance.service_name]
        lines.append(f"    std::shared_ptr<{service.name}> {_instance_member_name(instance.name)};")
    lines.append("};")
    lines.append("")
    lines.append(f"}}  // namespace {spec.namespace}")

    header_path = out_dir / f"{spec.application_name}.hpp"
    header_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    agg_lines = [
        "#pragma once",
        "",
        f"#include \"{spec.application_name}.hpp\"",
        "",
        f"// Namespace: {spec.namespace}",
    ]
    (out_dir / "generated_someip_application.hpp").write_text("\n".join(agg_lines) + "\n", encoding="utf-8")
    return header_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate SOME/IP application skeleton from JSON IDL")
    parser.add_argument("idl", type=Path, help="Path to JSON IDL file or directory")
    parser.add_argument("--out", type=Path, default=Path("generated_app"), help="Output directory")
    parser.add_argument("--recursive", action="store_true", help="Recursively scan directory input")
    args = parser.parse_args()

    spec = parse_app_idl(args.idl, recursive=args.recursive)
    header_path = generate_header(spec, args.out)
    print(f"Generated application framework: {header_path}")


if __name__ == "__main__":
    main()
