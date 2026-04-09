#pragma once

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
