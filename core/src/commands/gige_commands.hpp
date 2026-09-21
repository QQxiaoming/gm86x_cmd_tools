#pragma once

#include "commands.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gm86x::detail {

class GigeCommandGroup final : public CommandGroup {
public:
	using CommandGroup::CommandGroup;
	bool try_run(std::string_view name, const std::vector<std::string> &args) override;

private:
	static float parse_float(const std::string &value);
	static float read_float(std::span<const std::uint8_t> data);
	static std::vector<std::uint8_t> gige_header(const std::string &address, std::uint16_t size);
	static std::uint16_t parse_gige_size(const std::string &value);
	static void print_access_map_count(const Response &response);
	static void print_access_map_page(std::size_t start_index, const Response &response);
};

}