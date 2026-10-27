#pragma once

#include "commands.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gm86x::detail {

class RegisterCommandGroup final : public CommandGroup {
public:
	using CommandGroup::CommandGroup;
	bool try_run(std::string_view name, const std::vector<std::string> &args) override;
	static bool try_run_offline(std::string_view name, const std::vector<std::string> &args);

private:
	static std::uint8_t subsnr_channel(const std::string &value, bool allow_all);
	void set_de100_page(std::uint32_t address, bool verbose = true);
	std::uint8_t read_de100_byte(std::uint16_t address);
	std::uint32_t read_de100_reg32(std::uint32_t address);
	std::uint32_t read_de100_match_probe(std::uint8_t register_address, std::uint8_t offset);
	void dump_de100_reg32(std::uint32_t address, std::uint32_t count);
};

}