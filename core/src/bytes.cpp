#include "bytes.hpp"
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <type_traits>

namespace gm86x {

static int integer_base(std::string &value) {
    if (value.size() < 2 || value[0] != '0')
        return 10;
    switch (value[1]) {
    case 'x':
    case 'X':
        value.erase(0, 2);
        return 16;
    case 'b':
    case 'B':
        value.erase(0, 2);
        return 2;
    case 'o':
    case 'O':
        value.erase(0, 2);
        return 8;
    case 'd':
    case 'D':
        value.erase(0, 2);
        return 10;
    default:
        return 10;
    }
}

static std::uint64_t parse_integer(const std::string &text, std::uint64_t max, const char *name) {
    std::string value = text;
    const auto base = integer_base(value);
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result, base);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size() || result > max)
        throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
    return result;
}

std::uint8_t parse_byte(const std::string &text) {
    return static_cast<std::uint8_t>(parse_integer(text, 0xff, "byte"));
}

std::uint16_t parse_u16(const std::string &text) {
    return static_cast<std::uint16_t>(parse_integer(text, 0xffff, "u16"));
}

std::uint32_t parse_u32(const std::string &text) {
    return static_cast<std::uint32_t>(parse_integer(text, 0xffffffff, "u32"));
}

std::uint64_t parse_u64(const std::string &text) {
    return parse_integer(text, 0xffffffffffffffffULL, "u64");
}

template <typename Signed>
static Signed parse_signed_integer(const std::string &text, const char *name) {
    using Unsigned = std::make_unsigned_t<Signed>;
    const bool negative = !text.empty() && text[0] == '-';
    const auto prefix_offset = !text.empty() && (text[0] == '-' || text[0] == '+') ? 1u : 0u;
    const auto digits = text.substr(prefix_offset);
    const auto max_magnitude = static_cast<std::uint64_t>(std::numeric_limits<Signed>::max()) +
                               static_cast<std::uint64_t>(negative);
    const auto magnitude = parse_integer(digits, max_magnitude, name);
    if (!negative)
        return static_cast<Signed>(static_cast<Unsigned>(magnitude));
    if (magnitude == max_magnitude)
        return std::numeric_limits<Signed>::min();
    return static_cast<Signed>(-static_cast<Signed>(magnitude));
}

std::int32_t parse_i32(const std::string &text) {
    return parse_signed_integer<std::int32_t>(text, "i32");
}

std::int64_t parse_i64(const std::string &text) {
    return parse_signed_integer<std::int64_t>(text, "i64");
}

float parse_float(const std::string &text) {
    return std::stof(text);
}

std::uint8_t parse_binary_value(const std::string &text, const std::string &command) {
    const auto value = parse_byte(text);
    if (value <= 1)
        return value;
    throw std::invalid_argument(command + " requires <0|1>");
}

std::vector<std::uint8_t> parse_byte_list(std::span<const std::string> values) {
    std::vector<std::uint8_t> result;
    for (const auto &value : values)
        result.push_back(parse_byte(value));
    return result;
}

std::vector<std::uint8_t> le16(std::uint16_t value) {
    std::vector<std::uint8_t> result;
    append_le_u16(result, value);
    return result;
}

std::vector<std::uint8_t> le32(std::uint32_t value) {
    std::vector<std::uint8_t> result;
    append_le_u32(result, value);
    return result;
}

std::vector<std::uint8_t> le64(std::uint64_t value) {
    std::vector<std::uint8_t> result;
    append_le_u64(result, value);
    return result;
}

void append_to(std::vector<std::uint8_t> &target, std::span<const std::uint8_t> source) {
    target.insert(target.end(), source.begin(), source.end());
}

std::string ascii(std::span<const std::uint8_t> data) {
    return ascii(data, 0, data.size());
}

std::string ascii(std::span<const std::uint8_t> data, std::size_t offset, std::size_t length) {
    std::string result;
    if (offset >= data.size())
        return result;
    const auto end = offset + std::min(length, data.size() - offset);
    for (std::size_t index = offset; index < end && data[index]; ++index) {
        const auto value = data[index];
        result += value >= 32 && value <= 126 ? static_cast<char>(value) : '.';
    }
    return result;
}

std::uint16_t read_le_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size())
        throw std::runtime_error("short little-endian u16");
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::int16_t read_le_s16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::int16_t>(read_le_u16(data, offset));
}

std::uint32_t read_le_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size())
        throw std::runtime_error("short u32 payload");
    return static_cast<std::uint32_t>(data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 |
                                      data[offset + 3] << 24);
}

std::uint64_t read_le_u64(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 8 > data.size())
        throw std::runtime_error("short u64 payload");
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(data[offset + index]) << (index * 8);
    return value;
}

float read_float(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size())
        throw std::runtime_error("short float status field");
    const auto bits = static_cast<std::uint32_t>(data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 |
                                                 data[offset + 3] << 24);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void append_le_u16(std::vector<std::uint8_t> &data, std::uint16_t value) {
    data.push_back(static_cast<std::uint8_t>(value));
    data.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append_le_u32(std::vector<std::uint8_t> &data, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        data.push_back(static_cast<std::uint8_t>(value >> shift));
}

void append_le_u64(std::vector<std::uint8_t> &data, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8)
        data.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::string hex_bytes(std::span<const std::uint8_t> data) {
    std::ostringstream out;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i)
            out << ' ';
        out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return out.str();
}

} // namespace gm86x