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
std::int32_t parse_i32(const std::string &text);
std::int64_t parse_i64(const std::string &text);
float parse_float(const std::string &text);
std::uint8_t parse_binary_value(const std::string &text, const std::string &command);

std::vector<std::uint8_t> parse_byte_list(std::span<const std::string> values);

std::vector<std::uint8_t> le16(std::uint16_t value);
std::vector<std::uint8_t> le32(std::uint32_t value);
std::vector<std::uint8_t> le64(std::uint64_t value);
void append_to(std::vector<std::uint8_t> &target, std::span<const std::uint8_t> source);
std::string ascii(std::span<const std::uint8_t> data);
std::string ascii(std::span<const std::uint8_t> data, std::size_t offset, std::size_t length);

std::uint16_t read_le_u16(std::span<const std::uint8_t> data, std::size_t offset = 0);
std::int16_t read_le_s16(std::span<const std::uint8_t> data, std::size_t offset = 0);
std::uint32_t read_le_u32(std::span<const std::uint8_t> data, std::size_t offset = 0);
std::uint64_t read_le_u64(std::span<const std::uint8_t> data, std::size_t offset = 0);
float read_float(std::span<const std::uint8_t> data, std::size_t offset = 0);

void append_le_u16(std::vector<std::uint8_t> &data, std::uint16_t value);
void append_le_u32(std::vector<std::uint8_t> &data, std::uint32_t value);
void append_le_u64(std::vector<std::uint8_t> &data, std::uint64_t value);

std::string hex_bytes(std::span<const std::uint8_t> data);
} // namespace gm86x