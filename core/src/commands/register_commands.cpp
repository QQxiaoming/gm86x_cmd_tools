#include "register_commands.hpp"

#include "bytes.hpp"

#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>

namespace gm86x::detail {
std::uint8_t RegisterCommandGroup::binary_value(const std::string &value, const std::string &command) {
    if (value == "0")
        return 0;
    if (value == "1")
        return 1;
    throw std::invalid_argument(command + " requires <0|1>");
}

std::uint8_t RegisterCommandGroup::subsnr_channel(const std::string &value, bool allow_all) {
    if (value == "1" || value == "left")
        return 1;
    if (value == "2" || value == "right")
        return 2;
    if (allow_all && (value == "3" || value == "all"))
        return 3;
    throw std::invalid_argument("invalid subsnr channel: " + value);
}

void RegisterCommandGroup::set_de100_page(std::uint32_t address, bool verbose) {
    auto payload = le16(0xfffd);
    payload.push_back(static_cast<std::uint8_t>(address >> 24));
    execute(GMSL_COMMAND_DE100_WRITE_REGISTER, payload, verbose, verbose);
    payload = le16(0xfffe);
    payload.push_back(static_cast<std::uint8_t>(address >> 16));
    execute(GMSL_COMMAND_DE100_WRITE_REGISTER, payload, verbose, verbose);
}

std::uint8_t RegisterCommandGroup::read_de100_byte(std::uint16_t address) {
    const auto response = context_.client.command(context_.sequence, GMSL_COMMAND_DE100_READ_REGISTER, le16(address));
    if (response.status != 0 || response.payload.empty())
        throw std::runtime_error("DE100 register read failed");
    return response.payload.back();
}

std::uint32_t RegisterCommandGroup::read_de100_reg32(std::uint32_t address) {
    std::uint32_t value = 0;
    for (unsigned offset = 0; offset < 4; ++offset)
        value |= static_cast<std::uint32_t>(read_de100_byte(static_cast<std::uint16_t>(address + offset)))
                 << (offset * 8);
    return value;
}

void RegisterCommandGroup::dump_de100_reg32(std::uint32_t address, std::uint32_t count) {
    print_section("de100-dump-reg32");
    std::optional<std::uint16_t> current_page;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto reg = address + index * 4;
        const auto page = static_cast<std::uint16_t>(reg >> 16);
        // The page registers only need rewriting when the upper 16 address bits change.
        if (!current_page || *current_page != page) {
            set_de100_page(reg, false);
            current_page = page;
        }
        print_field("0x" + hex(reg, 8), 10) << "0x" << hex(read_de100_reg32(reg), 8) << '\n';
    }
}

bool RegisterCommandGroup::try_run(std::string_view name_view, const std::vector<std::string> &args) {
    const std::string name(name_view);
    if (name == "de100-read" || name == "isp-read") {
        require_args(args, 1, name + " requires <reg16>");
        const auto response = execute(name == "de100-read" ? GMSL_COMMAND_DE100_READ_REGISTER : GMSL_COMMAND_ISP_READ_REGISTER, le16(parse_u16(args[0])));
        if (response.status == 0 && (response.payload.size() == 1)) {
            print_section(name);
            print_field("value") << "0x" << hex(response.payload[0], 2) << '\n';
        }
        return true;
    }
    if (name == "de100-write" || name == "isp-write") {
        require_args(args, 2, name + " requires <reg16> <value8>");
        auto payload = le16(parse_u16(args[0]));
        payload.push_back(parse_byte(args[1]));
        execute(name == "de100-write" ? GMSL_COMMAND_DE100_WRITE_REGISTER : GMSL_COMMAND_ISP_WRITE_REGISTER, payload);
        return true;
    }
    if (name == "de100-read-reg32-val8" || 
        name == "de100-write-reg32-val8" || 
        name == "de100-read-reg32-val32" ||
        name == "de100-write-reg32-val32") {
        const bool write = name.find("write") != std::string::npos;
        const bool word = name.ends_with("val32");
        require_args(args, write ? 2 : 1, name + " arguments invalid");
        const auto address = parse_u32(args[0]);
        set_de100_page(address, false);
        if (write) {
            const auto value = parse_u32(args[1]);
            for (unsigned offset = 0; offset < (word ? 4U : 1U); ++offset) {
                auto payload = le16(static_cast<std::uint16_t>(address + offset));
                payload.push_back(static_cast<std::uint8_t>(value >> (offset * 8)));
                execute(GMSL_COMMAND_DE100_WRITE_REGISTER, payload, false, false);
            }
            print_section(name);
            print_field("addr") << "0x" << hex(address, 8) << '\n';
            if (word)
                print_field("le_bytes") << hex_bytes(le32(value)) << '\n';
            else
                print_field("le_bytes") << hex_bytes(std::vector<std::uint8_t>{static_cast<std::uint8_t>(value)}) << '\n';
            print_field("value") << "0x" << hex(value, word ? 8 : 2) << '\n';
        } else {
            std::vector<std::uint8_t> values;
            for (unsigned offset = 0; offset < (word ? 4U : 1U); ++offset)
                values.push_back(read_de100_byte(static_cast<std::uint16_t>(address + offset)));
            print_section(name);
            print_field("addr") << "0x" << hex(address, 8) << '\n';
            print_field("le_bytes") << hex_bytes(values) << '\n';
            if (word)
                print_field("value") << "0x" << hex(values[3], 2) << hex(values[2], 2) << hex(values[1], 2)
                                     << hex(values[0], 2) << '\n';
            else
                print_field("value") << "0x" << hex(values[0], 2) << '\n';
        }
        return true;
    }
    if (name == "de100-dump-reg32") {
        require_args(args, 2, "de100-dump-reg32 requires <addr32> <count>");
        const auto address = parse_u32(args[0]);
        const auto count = parse_decimal_or_hex_u32(args[1]);
        if (count == 0 || count > 4096)
            throw std::invalid_argument("register count out of range (1..4096)");
        if (address % 4 != 0)
            throw std::invalid_argument("addr32 must be 4-byte aligned");
        if (count - 1 > (0xfffffffcU - address) / 4)
            throw std::invalid_argument("dump range exceeds the 32-bit address space");
        dump_de100_reg32(address, count);
        return true;
    }
    if (name == "subsnr-read") {
        require_args(args, 2, "subsnr-read requires <channel> <reg16>");
        auto payload = std::vector<std::uint8_t>{subsnr_channel(args[0], false)};
        append_to(payload, le16(parse_u16(args[1])));
        const auto response = execute(GMSL_COMMAND_DE100_SUBSNR_READ_REGISTER, payload);
        if (response.status == 0 && response.payload.size() == 2) {
            print_section("subsnr-read");
            print_field("value") << "0x" << hex(response.payload[1], 2) << hex(response.payload[0], 2) << '\n';
        }
        return true;
    }
    if (name == "subsnr-write") {
        require_args(args, 3, name + " arguments invalid");
        auto payload = std::vector<std::uint8_t>{subsnr_channel(args[0], true)};
        append_to(payload, le16(parse_u16(args[1])));
        append_to(payload, le16(parse_u16(args[2])));
        execute(GMSL_COMMAND_DE100_SUBSNR_WRITE_REGISTER, payload);
        return true;
    }
    if (name == "isp-subsnr-read") {
        require_args(args, 1, name + " requires <reg16>");
        const auto response = execute(GMSL_COMMAND_ISP_READ_SUBSENSOR_REGISTER, le16(parse_u16(args[0])));
        if (response.status == 0 && (response.payload.size() == 2)) {
            print_section(name);
            print_field("value") << "0x" << hex(response.payload[1], 2) << hex(response.payload[0], 2) << '\n';
        }
        return true;
    }
    if (name == "isp-subsnr-write") {
        require_args(args, 2, name + " arguments invalid");
        auto payload = std::vector<std::uint8_t>{};
        append_to(payload, le16(parse_u16(args[0])));
        append_to(payload, le16(parse_u16(args[1])));
        execute(GMSL_COMMAND_ISP_WRITE_SUBSENSOR_REGISTER, payload);
        return true;
    }
    return false;
}
} // namespace gm86x::detail