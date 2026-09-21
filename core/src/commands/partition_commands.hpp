#pragma once

#include "commands.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gm86x::detail {

class PartitionCommandGroup final : public CommandGroup {
public:
	using CommandGroup::CommandGroup;
	bool try_run(std::string_view name, const std::vector<std::string> &args) override;

private:
	struct Entry {
		std::uint32_t address;
		std::vector<std::uint8_t> data;
	};
    static std::uint8_t userset_read_slot(const std::string &raw);
	static std::vector<Entry> parse_entries(const std::vector<std::uint8_t> &blob);
	static void write_entries(const std::string &path, const std::vector<Entry> &entries);
	static const char *isp_update_status_name(std::uint8_t status);
	static std::size_t parse_chunk_size(const std::string &value);
	void read_partition(std::uint8_t opcode, const std::string &path, std::size_t partition_size,
	                    std::size_t chunk_size, std::optional<std::uint8_t> slot = std::nullopt);
	void print_isp_pending(const Response &response) const;
	std::uint32_t encode_update_offset(std::uint32_t offset) const;
	void update_chunk(std::uint8_t opcode, std::uint32_t offset, const std::vector<std::uint8_t> &data,
	                  std::optional<std::uint8_t> slot = std::nullopt);
	void update_file(std::uint8_t opcode, const std::string &path, std::size_t chunk_size,
                     std::uint32_t offset = 0, std::vector<std::uint8_t> prefix = {},
                     std::optional<std::uint8_t> slot = std::nullopt, const std::string &label = "update-file",
                     bool use_buffer_mode = true);
};

}