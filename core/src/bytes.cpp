#include "bytes.hpp"
#include <charconv>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace gm86x {

static std::uint64_t parse_integer(const std::string &text, std::uint64_t max, const char *name) {
    std::string value = text;
    if (value.starts_with("0x") || value.starts_with("0X"))
        value = value.substr(2);
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result, 16);
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

std::uint32_t parse_decimal_or_hex_u32(const std::string &text) {
    if (!text.empty() && text.find_first_not_of("0123456789") == std::string::npos) {
        std::uint64_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
        if (error != std::errc{} || end != text.data() + text.size() || value > 0xffffffffULL)
            throw std::invalid_argument("invalid u32: " + text);
        return static_cast<std::uint32_t>(value);
    }
    return parse_u32(text);
}

std::int32_t parse_decimal_or_hex_i32(const std::string &text) {
    if (!text.empty() && (text[0] == '-' || text.find_first_not_of("0123456789") == std::string::npos)) {
        std::int64_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
        if (error != std::errc{} || end != text.data() + text.size() || value < -2147483648LL || value > 2147483647LL)
            throw std::invalid_argument("invalid i32: " + text);
        return static_cast<std::int32_t>(value);
    }
    return static_cast<std::int32_t>(parse_u32(text));
}

std::int64_t parse_i64(const std::string &text) {
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("invalid i64: " + text);
    return value;
}

std::vector<std::uint8_t> parse_byte_list(std::span<const std::string> values) {
    std::vector<std::uint8_t> result;
    for (const auto &value : values)
        result.push_back(parse_byte(value));
    return result;
}

std::uint16_t read_le_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size())
        throw std::runtime_error("short little-endian u16");
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

void append_le_u16(std::vector<std::uint8_t> &data, std::uint16_t value) {
    data.push_back(static_cast<std::uint8_t>(value));
    data.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append_le_u32(std::vector<std::uint8_t> &data, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
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