#pragma once

#include "commands.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gm86x::detail {

class BasicCommandGroup final : public CommandGroup {
public:
	using CommandGroup::CommandGroup;
	bool try_run(std::string_view name, const std::vector<std::string> &args) override;

private:
	void print_probe(const std::vector<std::uint8_t> &data) const;
	std::int16_t read_s16(std::span<const std::uint8_t> data, std::size_t offset) const;
	float read_float(std::span<const std::uint8_t> data, std::size_t offset) const;
	std::string ascii_field(std::span<const std::uint8_t> data, std::size_t offset, std::size_t length) const;
	std::uint64_t read_le64(std::span<const std::uint8_t> data) const;
	std::uint32_t read_le32(std::span<const std::uint8_t> data, std::size_t offset) const;
	void print_imu_samples(const std::vector<std::uint8_t> &payload) const;
	const char *isp_model_name(std::uint8_t model) const;
	void print_status(const std::vector<std::uint8_t> &payload) const;
	std::uint8_t temperature_channel(const std::string &value) const;
	std::uint8_t binary_value(const std::string &value, const std::string &command) const;
	std::uint8_t pps_mode(const std::string &value) const;
	std::uint8_t pps_period(const std::string &value) const;

	void write_post_processor_register(uint8_t addr, std::vector<std::uint8_t> data);
	std::vector<std::uint8_t> read_post_processor_register(uint8_t addr, uint8_t size);
	void write_post_processor_cmd(uint8_t cmd, uint8_t sequence, std::vector<std::uint8_t> data);
	bool is_post_processor_cmd_ready();
	void clear_post_processor_cmd_is_ready();
	std::vector<std::uint8_t> read_post_processor_response(uint8_t cmd, uint8_t sequence, uint8_t &size, uint8_t &status);
};

}