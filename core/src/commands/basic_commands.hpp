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
	const char *isp_model_name(std::uint8_t model) const;
	std::uint8_t temperature_channel(const std::string &value) const;
	std::uint8_t pps_mode(const std::string &value) const;
	std::uint8_t pps_period(const std::string &value) const;
	void print_status(const std::vector<std::uint8_t> &payload) const;
	void print_probe(const std::vector<std::uint8_t> &data) const;
	void print_imu_samples(const std::vector<std::uint8_t> &payload) const;
    void run_bin(const std::string &path, std::size_t chunk_size);
};

}