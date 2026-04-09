#!/usr/bin/env python3
"""
Generate SOME/IP parameter classes (C++) from a JSON-based IDL.

The generated codecs use network byte order (big-endian), matching SOME/IP payload
conventions for primitive integers. Arrays and strings are encoded as
length-prefixed sequences.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional


PRIMITIVE_CPP_TYPES = {
    "u8": "std::uint8_t",
    "u16": "std::uint16_t",
    "u32": "std::uint32_t",
    "u64": "std::uint64_t",
    "i8": "std::int8_t",
    "i16": "std::int16_t",
    "i32": "std::int32_t",
    "i64": "std::int64_t",
    "f32": "float",
    "f64": "double",
    "bool": "bool",
    "string": "std::string",
}

LENGTH_TYPES = {"u8", "u16", "u32"}


@dataclass
class FieldDef:
    name: str
    kind: str
    type_name: Optional[str] = None
    element_type: Optional[str] = None
    length_type: str = "u32"


@dataclass
class TypeDef:
    name: str
    fields: List[FieldDef]
    category: str  # "type" or "message"


def _must_be_identifier(name: str, context: str) -> None:
    if not name or not name.replace("_", "a").isalnum() or name[0].isdigit():
        raise ValueError(f"{context} '{name}' is not a valid identifier")


def parse_field(raw: dict, known_types: set[str], context: str) -> FieldDef:
    if "name" not in raw or "type" not in raw:
        raise ValueError(f"{context}: each field must contain 'name' and 'type'")

    field_name = raw["name"]
    _must_be_identifier(field_name, f"Field name in {context}")

    ftype = raw["type"]
    if ftype == "array":
        elem = raw.get("elementType")
        if not elem:
            raise ValueError(f"{context}.{field_name}: array field requires 'elementType'")
        if elem not in PRIMITIVE_CPP_TYPES and elem not in known_types:
            raise ValueError(f"{context}.{field_name}: unknown elementType '{elem}'")
        length_type = raw.get("lengthType", "u32")
        if length_type not in LENGTH_TYPES:
            raise ValueError(
                f"{context}.{field_name}: lengthType must be one of {sorted(LENGTH_TYPES)}"
            )
        return FieldDef(
            name=field_name,
            kind="array",
            element_type=elem,
            length_type=length_type,
        )

    if ftype in PRIMITIVE_CPP_TYPES:
        length_type = raw.get("lengthType", "u32")
        if ftype == "string" and length_type not in LENGTH_TYPES:
            raise ValueError(
                f"{context}.{field_name}: string lengthType must be one of {sorted(LENGTH_TYPES)}"
            )
        return FieldDef(name=field_name, kind="primitive", type_name=ftype, length_type=length_type)

    if ftype in known_types:
        return FieldDef(name=field_name, kind="custom", type_name=ftype)

    raise ValueError(f"{context}.{field_name}: unknown type '{ftype}'")


def _collect_idl_files(idl_input: Path, recursive: bool = False) -> List[Path]:
    if idl_input.is_file():
        if idl_input.suffix.lower() != ".json":
            raise ValueError(f"IDL file must be .json: {idl_input}")
        return [idl_input]

    if idl_input.is_dir():
        pattern = "**/*.json" if recursive else "*.json"
        files = sorted(p for p in idl_input.glob(pattern) if p.is_file())
        if not files:
            scope = "recursively" if recursive else ""
            raise ValueError(f"No JSON IDL files found {scope} in directory: {idl_input}")
        return files

    raise ValueError(f"IDL input path does not exist: {idl_input}")


def parse_idl_sources(idl_input: Path, recursive: bool = False) -> tuple[str, List[TypeDef]]:
    files = _collect_idl_files(idl_input, recursive=recursive)

    merged_namespace: Optional[str] = None
    pending_defs: List[tuple[str, str, List[dict]]] = []
    known_types: set[str] = set()

    for path in files:
        data = json.loads(path.read_text(encoding="utf-8"))

        ns = data.get("namespace", "someip::generated")
        if merged_namespace is None:
            merged_namespace = ns
        elif ns != merged_namespace:
            raise ValueError(
                f"Namespace mismatch: '{path}' has '{ns}', expected '{merged_namespace}'"
            )

        for category, items in (("type", data.get("types", [])), ("message", data.get("messages", []))):
            if not isinstance(items, list):
                raise ValueError(f"{path}: '{category}s' must be an array")
            for item in items:
                name = item.get("name")
                if not name:
                    raise ValueError(f"{path}: each type/message requires a name")
                _must_be_identifier(name, "Type name")
                if name in known_types:
                    raise ValueError(f"Duplicate type/message name '{name}' found in '{path}'")
                known_types.add(name)

                raw_fields = item.get("fields", [])
                if not isinstance(raw_fields, list) or not raw_fields:
                    raise ValueError(f"{path}: {name}: fields must be a non-empty array")
                pending_defs.append((category, name, raw_fields))

    assert merged_namespace is not None

    parsed: List[TypeDef] = []
    for category, name, raw_fields in pending_defs:
        fields = [parse_field(field, known_types, name) for field in raw_fields]
        parsed.append(TypeDef(name=name, fields=fields, category=category))

    return merged_namespace, parsed


def cpp_type(field: FieldDef) -> str:
    if field.kind == "primitive":
        assert field.type_name is not None
        return PRIMITIVE_CPP_TYPES[field.type_name]
    if field.kind == "custom":
        assert field.type_name is not None
        return field.type_name
    if field.kind == "array":
        assert field.element_type is not None
        elem = PRIMITIVE_CPP_TYPES.get(field.element_type, field.element_type)
        return f"std::vector<{elem}>"
    raise AssertionError(f"Unsupported field kind {field.kind}")


def write_codec_support_header(out_dir: Path) -> None:
    content = """#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace someip_generated {

class ByteReader;

class SerializableObject {
public:
    virtual ~SerializableObject() = default;

    virtual bool serializeTo(std::vector<std::uint8_t>& bytes) const = 0;
    virtual bool deserializeFromReader(ByteReader& reader) = 0;

    std::vector<std::uint8_t> serialize() const {
        std::vector<std::uint8_t> bytes;
        if (!serializeTo(bytes)) {
            return {};
        }
        return bytes;
    }

    bool deserialize(const std::vector<std::uint8_t>& bytes);
};

class ByteWriter {
public:
    explicit ByteWriter(std::vector<std::uint8_t>& out) : out_(out) {}

    template <typename T>
    bool writeIntegralBE(T value) {
        static_assert(std::is_integral<T>::value && !std::is_same<T, bool>::value,
                      "Non-bool integral type required");
        using UnsignedT = typename std::make_unsigned<T>::type;
        UnsignedT u = static_cast<UnsignedT>(value);
        for (int i = sizeof(T) - 1; i >= 0; --i) {
            out_.push_back(static_cast<std::uint8_t>((u >> (i * 8)) & 0xFFu));
        }
        return true;
    }

    bool writeFloat32BE(float value) {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        std::uint32_t u = 0;
        std::memcpy(&u, &value, sizeof(float));
        return writeIntegralBE<std::uint32_t>(u);
    }

    bool writeFloat64BE(double value) {
        static_assert(sizeof(double) == 8, "double must be 64-bit");
        std::uint64_t u = 0;
        std::memcpy(&u, &value, sizeof(double));
        return writeIntegralBE<std::uint64_t>(u);
    }

private:
    std::vector<std::uint8_t>& out_;
};

class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& in) : in_(in), pos_(0) {}

    template <typename T>
    bool readIntegralBE(T& value) {
        static_assert(std::is_integral<T>::value && !std::is_same<T, bool>::value,
                      "Non-bool integral type required");
        if (remaining() < sizeof(T)) {
            return false;
        }
        using UnsignedT = typename std::make_unsigned<T>::type;
        UnsignedT u = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            u = static_cast<UnsignedT>((u << 8) | in_[pos_++]);
        }
        value = static_cast<T>(u);
        return true;
    }

    bool readFloat32BE(float& value) {
        std::uint32_t u = 0;
        if (!readIntegralBE<std::uint32_t>(u)) {
            return false;
        }
        std::memcpy(&value, &u, sizeof(float));
        return true;
    }

    bool readFloat64BE(double& value) {
        std::uint64_t u = 0;
        if (!readIntegralBE<std::uint64_t>(u)) {
            return false;
        }
        std::memcpy(&value, &u, sizeof(double));
        return true;
    }

    bool readBytes(std::size_t n, std::string& value) {
        if (remaining() < n) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(&in_[pos_]), n);
        pos_ += n;
        return true;
    }

    bool fullyConsumed() const { return pos_ == in_.size(); }

private:
    std::size_t remaining() const { return in_.size() - pos_; }

    const std::vector<std::uint8_t>& in_;
    std::size_t pos_;
};

inline bool SerializableObject::deserialize(const std::vector<std::uint8_t>& bytes) {
    ByteReader reader(bytes);
    if (!deserializeFromReader(reader)) {
        return false;
    }
    return reader.fullyConsumed();
}

}  // namespace someip_generated
"""
    (out_dir / "someip_codec_support.hpp").write_text(content, encoding="utf-8")


def write_field_serialize(field: FieldDef, out: List[str], indent: str = "        ") -> None:
    name = field.name
    if field.kind == "primitive":
        t = field.type_name
        if t == "string":
            lt = PRIMITIVE_CPP_TYPES[field.length_type]
            out.append(f"{indent}if ({name}.size() > static_cast<std::size_t>(std::numeric_limits<{lt}>::max())) return false;")
            out.append(f"{indent}if (!writer.writeIntegralBE<{lt}>(static_cast<{lt}>({name}.size()))) return false;")
            out.append(f"{indent}bytes.insert(bytes.end(), {name}.begin(), {name}.end());")
        elif t == "f32":
            out.append(f"{indent}if (!writer.writeFloat32BE({name})) return false;")
        elif t == "f64":
            out.append(f"{indent}if (!writer.writeFloat64BE({name})) return false;")
        elif t == "bool":
            out.append(f"{indent}if (!writer.writeIntegralBE<std::uint8_t>({name} ? 1u : 0u)) return false;")
        else:
            ctype = PRIMITIVE_CPP_TYPES[t]
            out.append(f"{indent}if (!writer.writeIntegralBE<{ctype}>({name})) return false;")
        return

    if field.kind == "custom":
        out.append(f"{indent}if (!{name}.serializeTo(bytes)) return false;")
        return

    if field.kind == "array":
        lt = PRIMITIVE_CPP_TYPES[field.length_type]
        elem = field.element_type
        out.append(f"{indent}if ({name}.size() > static_cast<std::size_t>(std::numeric_limits<{lt}>::max())) return false;")
        out.append(f"{indent}if (!writer.writeIntegralBE<{lt}>(static_cast<{lt}>({name}.size()))) return false;")
        out.append(f"{indent}for (const auto& elem : {name}) {{")
        if elem == "string":
            out.append(f"{indent}    if (elem.size() > static_cast<std::size_t>(std::numeric_limits<{lt}>::max())) return false;")
            out.append(f"{indent}    if (!writer.writeIntegralBE<{lt}>(static_cast<{lt}>(elem.size()))) return false;")
            out.append(f"{indent}    bytes.insert(bytes.end(), elem.begin(), elem.end());")
        elif elem == "f32":
            out.append(f"{indent}    if (!writer.writeFloat32BE(elem)) return false;")
        elif elem == "f64":
            out.append(f"{indent}    if (!writer.writeFloat64BE(elem)) return false;")
        elif elem == "bool":
            out.append(f"{indent}    if (!writer.writeIntegralBE<std::uint8_t>(elem ? 1u : 0u)) return false;")
        elif elem in PRIMITIVE_CPP_TYPES:
            etype = PRIMITIVE_CPP_TYPES[elem]
            out.append(f"{indent}    if (!writer.writeIntegralBE<{etype}>(elem)) return false;")
        else:
            out.append(f"{indent}    if (!elem.serializeTo(bytes)) return false;")
        out.append(f"{indent}}}")


def write_field_deserialize(field: FieldDef, out: List[str], indent: str = "        ") -> None:
    name = field.name
    if field.kind == "primitive":
        t = field.type_name
        if t == "string":
            lt = PRIMITIVE_CPP_TYPES[field.length_type]
            str_len_var = f"{name}StrLen"
            out.append(f"{indent}{lt} {str_len_var} = 0;")
            out.append(f"{indent}if (!reader.readIntegralBE<{lt}>({str_len_var})) return false;")
            out.append(f"{indent}if (!reader.readBytes(static_cast<std::size_t>({str_len_var}), {name})) return false;")
        elif t == "f32":
            out.append(f"{indent}if (!reader.readFloat32BE({name})) return false;")
        elif t == "f64":
            out.append(f"{indent}if (!reader.readFloat64BE({name})) return false;")
        elif t == "bool":
            out.append(f"{indent}std::uint8_t b = 0;")
            out.append(f"{indent}if (!reader.readIntegralBE<std::uint8_t>(b)) return false;")
            out.append(f"{indent}{name} = (b != 0);")
        else:
            ctype = PRIMITIVE_CPP_TYPES[t]
            out.append(f"{indent}if (!reader.readIntegralBE<{ctype}>({name})) return false;")
        return

    if field.kind == "custom":
        out.append(f"{indent}if (!{name}.deserializeFromReader(reader)) return false;")
        return

    if field.kind == "array":
        lt = PRIMITIVE_CPP_TYPES[field.length_type]
        elem = field.element_type
        len_var = f"{name}Len"
        out.append(f"{indent}{lt} {len_var} = 0;")
        out.append(f"{indent}if (!reader.readIntegralBE<{lt}>({len_var})) return false;")
        out.append(f"{indent}{name}.clear();")
        out.append(f"{indent}{name}.reserve(static_cast<std::size_t>({len_var}));")
        out.append(f"{indent}for ({lt} i = 0; i < {len_var}; ++i) {{")
        if elem == "string":
            elem_str_len_var = f"{name}ElemStrLen"
            out.append(f"{indent}    {lt} {elem_str_len_var} = 0;")
            out.append(f"{indent}    if (!reader.readIntegralBE<{lt}>({elem_str_len_var})) return false;")
            out.append(f"{indent}    std::string temp;")
            out.append(f"{indent}    if (!reader.readBytes(static_cast<std::size_t>({elem_str_len_var}), temp)) return false;")
            out.append(f"{indent}    {name}.push_back(std::move(temp));")
        elif elem == "f32":
            out.append(f"{indent}    float temp = 0.0f;")
            out.append(f"{indent}    if (!reader.readFloat32BE(temp)) return false;")
            out.append(f"{indent}    {name}.push_back(temp);")
        elif elem == "f64":
            out.append(f"{indent}    double temp = 0.0;")
            out.append(f"{indent}    if (!reader.readFloat64BE(temp)) return false;")
            out.append(f"{indent}    {name}.push_back(temp);")
        elif elem == "bool":
            out.append(f"{indent}    std::uint8_t b = 0;")
            out.append(f"{indent}    if (!reader.readIntegralBE<std::uint8_t>(b)) return false;")
            out.append(f"{indent}    {name}.push_back(b != 0);")
        elif elem in PRIMITIVE_CPP_TYPES:
            etype = PRIMITIVE_CPP_TYPES[elem]
            out.append(f"{indent}    {etype} temp = 0;")
            out.append(f"{indent}    if (!reader.readIntegralBE<{etype}>(temp)) return false;")
            out.append(f"{indent}    {name}.push_back(temp);")
        else:
            out.append(f"{indent}    {elem} temp;")
            out.append(f"{indent}    if (!temp.deserializeFromReader(reader)) return false;")
            out.append(f"{indent}    {name}.push_back(std::move(temp));")
        out.append(f"{indent}}}")


def write_type_header(out_dir: Path, namespace: str, t: TypeDef) -> None:
    includes = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <limits>",
        "#include <string>",
        "#include <utility>",
        "#include <vector>",
        "",
        "#include \"someip_codec_support.hpp\"",
        "",
    ]

    custom_deps = sorted(
        {
            f.type_name
            for f in t.fields
            if f.kind == "custom"
        }
        | {
            f.element_type
            for f in t.fields
            if f.kind == "array" and f.element_type not in PRIMITIVE_CPP_TYPES
        }
    )
    for dep in custom_deps:
        if dep != t.name:
            includes.append(f"#include \"{dep}.hpp\"")
    includes.append("")

    lines: List[str] = []
    lines.extend(includes)
    lines.append(f"namespace {namespace} {{")
    lines.append("")
    lines.append(f"struct {t.name} : public someip_generated::SerializableObject {{")

    for field in t.fields:
        lines.append(f"    {cpp_type(field)} {field.name};")

    lines.append("")
    lines.append("    bool serializeTo(std::vector<std::uint8_t>& bytes) const override {")
    lines.append("        someip_generated::ByteWriter writer(bytes);")
    for field in t.fields:
        write_field_serialize(field, lines)
    lines.append("        return true;")
    lines.append("    }")
    lines.append("")
    lines.append("    bool deserializeFromReader(someip_generated::ByteReader& reader) override {")
    for field in t.fields:
        write_field_deserialize(field, lines, indent="        ")
    lines.append("        return true;")
    lines.append("    }")
    lines.append("};")
    lines.append("")
    lines.append(f"}}  // namespace {namespace}")

    (out_dir / f"{t.name}.hpp").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_aggregate_header(out_dir: Path, namespace: str, types: List[TypeDef]) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "#include \"someip_codec_support.hpp\"",
    ]
    for t in types:
        lines.append(f"#include \"{t.name}.hpp\"")
    lines.extend(["", f"// Namespace: {namespace}"])
    (out_dir / "generated_someip_params.hpp").write_text("\n".join(lines) + "\n", encoding="utf-8")


def generate(idl_input: Path, out_dir: Path, recursive: bool = False) -> None:
    namespace, types = parse_idl_sources(idl_input, recursive=recursive)
    out_dir.mkdir(parents=True, exist_ok=True)

    write_codec_support_header(out_dir)
    for t in types:
        write_type_header(out_dir, namespace, t)
    write_aggregate_header(out_dir, namespace, types)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate SOME/IP parameter classes from JSON IDL")
    parser.add_argument(
        "idl",
        type=Path,
        help="Path to JSON IDL file or directory containing JSON IDL files",
    )
    parser.add_argument("--out", type=Path, default=Path("generated"), help="Output directory")
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="When 'idl' is a directory, include nested JSON files recursively",
    )
    args = parser.parse_args()

    generate(args.idl, args.out, recursive=args.recursive)
    print(f"Generated files in: {args.out}")


if __name__ == "__main__":
    main()
