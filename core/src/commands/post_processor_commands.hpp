#pragma once

#include "commands.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gm86x::detail {

class PostProcessorCommandGroup final : public CommandGroup {
public:
    using CommandGroup::CommandGroup;
    bool try_run(std::string_view name, const std::vector<std::string> &args) override;

private:
    struct CommandResponse {
        std::uint8_t status;
        std::vector<std::uint8_t> payload;
    };

    void write_register(std::uint8_t addr, std::vector<std::uint8_t> data);
    std::vector<std::uint8_t> read_register(std::uint8_t addr, std::uint8_t size);
    void write_command(std::uint8_t command, std::uint8_t sequence, std::vector<std::uint8_t> data);
    bool command_is_ready();
    std::vector<std::uint8_t> read_response(std::uint8_t command, std::uint8_t sequence,
                                            std::uint8_t &size, std::uint8_t &status);
    CommandResponse execute_command(std::uint8_t command, std::vector<std::uint8_t> payload);
    static void require_payload_size(std::span<const std::uint8_t> payload, std::size_t size,
                                     const std::string &command);
    void print_command_response(std::string_view command, const CommandResponse &response) const;
    void print_get_status(std::span<const std::uint8_t> payload) const;
    void print_stream_status(std::string_view name, std::span<const std::uint8_t> data) const;
    void print_get_caps(std::span<const std::uint8_t> payload) const;
    static const char *usb_speed_name(std::uint8_t speed);
    static const char *stream_state_name(std::uint8_t state);
    static const char *stream_action_name(std::uint8_t action);
    static std::string stream_flags_name(std::uint8_t flags);
    static std::string usb_speed_caps_name(std::uint8_t caps);
    static std::string command_mask_name(std::uint32_t mask);

    std::uint8_t next_sequence_ = 1;
};

} // namespace gm86x::detail
