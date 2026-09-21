#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gm86x {
std::uint8_t parse_byte(const std::string &text);
std::uint16_t parse_u16(const std::string &text);
std::uint32_t parse_u32(const std::string &text);
std::uint64_t parse_u64(const std::string &text);
std::uint32_t parse_decimal_or_hex_u32(const std::string &text);
std::int32_t parse_decimal_or_hex_i32(const std::string &text);
std::int64_t parse_i64(const std::string &text);
std::vector<std::uint8_t> parse_byte_list(std::span<const std::string> values);
std::uint16_t read_le_u16(std::span<const std::uint8_t> data, std::size_t offset = 0);
void append_le_u16(std::vector<std::uint8_t> &data, std::uint16_t value);
void append_le_u32(std::vector<std::uint8_t> &data, std::uint32_t value);
std::string hex_bytes(std::span<const std::uint8_t> data);
} // namespace gm86x