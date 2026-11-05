#include "register_commands.hpp"

#include "bytes.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <thread>
#include <chrono>

#if defined(__linux__)
#  include <byteswap.h>
#elif defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  define bswap_32(x) OSSwapInt32(x)
#elif defined(_MSC_VER)
#  include <stdlib.h>
#  define bswap_32(x) _byteswap_ulong(x)
#else
#  define bswap_32(x) __builtin_bswap32(x)
#endif

namespace gm86x::detail {
namespace {

struct DocumentedRegister {
    std::string_view module;
    std::string_view name;
    std::uint32_t address;
    std::uint16_t offset;
    std::uint8_t width_bits;
    std::string_view access;
    std::string_view endian;
};

struct De100MatchField {
    std::uint8_t register_address;
    std::string_view register_name;
    std::string_view name;
    std::uint8_t offset;
    std::uint8_t width;
};

constexpr DocumentedRegister documented_registers[] = {
#include "de100_registers.inc"
};

constexpr De100MatchField de100_match_fields[] = {
#include "de100_match_fields.inc"
};

struct De100DumpOptions {
    std::optional<std::filesystem::path> output;
    std::optional<std::regex> module;
    std::optional<std::uint32_t> start;
    std::optional<std::uint32_t> end;
    bool include_write_only = false;
    bool list_only = false;
    bool fail_fast = false;
};

De100DumpOptions parse_de100_dump_options(const std::vector<std::string> &args) {
    De100DumpOptions options;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const auto &argument = args[index];
        const auto require_value = [&](const std::string &option) -> const std::string & {
            if (index + 1 >= args.size())
                throw std::invalid_argument(option + " requires a value");
            return args[++index];
        };
        if (argument == "-o" || argument == "--output") {
            options.output = require_value(argument);
        } else if (argument == "--module") {
            options.module.emplace(require_value(argument), std::regex::icase);
        } else if (argument == "--start") {
            options.start = parse_u32(require_value(argument));
        } else if (argument == "--end") {
            options.end = parse_u32(require_value(argument));
        } else if (argument == "--include-wo") {
            options.include_write_only = true;
        } else if (argument == "--list-only") {
            options.list_only = true;
        } else if (argument == "--fail-fast") {
            options.fail_fast = true;
        } else {
            throw std::invalid_argument("unknown de100-dump-registers option: " + argument);
        }
    }
    return options;
}

std::vector<const DocumentedRegister *> select_de100_registers(const De100DumpOptions &options) {
    std::vector<const DocumentedRegister *> selected;
    for (const auto &register_info : documented_registers) {
        if (!options.include_write_only && register_info.access == "WO")
            continue;
        if (options.module && !std::regex_search(register_info.module.begin(), register_info.module.end(),
                                                  *options.module))
            continue;
        if (options.start && register_info.address < *options.start)
            continue;
        if (options.end && register_info.address > *options.end)
            continue;
        selected.push_back(&register_info);
    }
    if (selected.empty())
        throw std::invalid_argument("no registers matched the requested filters");
    return selected;
}

std::string format_dump_hex(std::uint64_t value, int digits) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return output.str();
}

std::string_view decode_match_enum(std::string_view field_name, std::uint32_t value) {
    if (field_name == "LRC_fsm") {
        constexpr std::string_view names[] = {"ROW_IN", "ROW_IN_DELAY", "ROW_OUT", "INIT_BUFF"};
        return names[value];
    }
    if (field_name == "fsm_status") {
        constexpr std::string_view names[] = {"IDLE", "BEFORE_START", "DATA_CALC"};
        return value < std::size(names) ? names[value] : "UNKNOWN";
    }
    return {};
}

void print_de100_register_list(const std::vector<const DocumentedRegister *> &selected) {
    for (const auto *register_info : selected)
        std::cout << format_dump_hex(register_info->address, 8) << ' ' << register_info->module << '.'
                  << register_info->name << ' ' << static_cast<unsigned>(register_info->width_bits) << "bit "
                  << register_info->access << '\n';
    std::cerr << "listed " << selected.size() << " of " << std::size(documented_registers)
              << " documented registers\n";
}

void write_csv_row(std::ostream &output, const std::vector<std::string> &fields) {
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index)
            output << ',';
        const auto &field = fields[index];
        if (field.find_first_of(",\"\r\n") == std::string::npos) {
            output << field;
            continue;
        }
        output << '"';
        for (const auto character : field) {
            if (character == '"')
                output << '"';
            output << character;
        }
        output << '"';
    }
    output << '\n';
}

} // namespace

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
    const auto response = execute(GMSL_COMMAND_DE100_READ_REGISTER, le16(address), false, false);
    if (response.payload.empty())
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

std::uint32_t RegisterCommandGroup::read_de100_match_probe(std::uint8_t register_address, std::uint8_t offset) {
    const auto write_probe_word = [&](std::uint32_t address, std::uint32_t value) {
        for (unsigned byte_offset = 0; byte_offset < 4; ++byte_offset) {
            auto payload = le16(static_cast<std::uint16_t>(address + byte_offset));
            payload.push_back(static_cast<std::uint8_t>(value >> (byte_offset * 8)));
            execute(GMSL_COMMAND_DE100_WRITE_REGISTER, payload, false, false);
        }
    };
    write_probe_word(0x80311460, register_address);
    write_probe_word(0x80311464, offset);
    std::uint32_t value = 0;
    for (unsigned byte_offset = 0; byte_offset < 4; ++byte_offset) {
        const auto response = execute(GMSL_COMMAND_DE100_READ_REGISTER,
                                      le16(static_cast<std::uint16_t>(0x1468 + byte_offset)), false, false);
        if (response.payload.empty())
            throw std::runtime_error("DE100 match probe read failed");
        value |= static_cast<std::uint32_t>(response.payload.back()) << (byte_offset * 8);
    }
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

bool RegisterCommandGroup::try_run_offline(std::string_view name, const std::vector<std::string> &args) {
    if (name != "de100-dump-registers")
        return false;
    const auto options = parse_de100_dump_options(args);
    if (!options.list_only)
        return false;
    print_de100_register_list(select_de100_registers(options));
    return true;
}

bool RegisterCommandGroup::try_run(std::string_view name_view, const std::vector<std::string> &args) {
    const std::string name(name_view);
    if (name == "de100-read") {
        require_args(args, 1, "de100-read requires <reg16>");
        const auto response = execute(GMSL_COMMAND_DE100_READ_REGISTER, le16(parse_u16(args[0])));
        if (response.status == 0 && (response.payload.size() == 1)) {
            print_section("de100-read");
            print_field("value") << "0x" << hex(response.payload[0], 2) << '\n';
        }
        return true;
    }
    if (name == "de100-write") {
        require_args(args, 2, "de100-write requires <reg16> <value8>");
        auto payload = le16(parse_u16(args[0]));
        payload.push_back(parse_byte(args[1]));
        execute(GMSL_COMMAND_DE100_WRITE_REGISTER, payload);
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
        const auto count = parse_u32(args[1]);
        if (count == 0 || count > 4096)
            throw std::invalid_argument("register count out of range (1..4096)");
        if (address % 4 != 0)
            throw std::invalid_argument("addr32 must be 4-byte aligned");
        if (count - 1 > (0xfffffffcU - address) / 4)
            throw std::invalid_argument("dump range exceeds the 32-bit address space");
        dump_de100_reg32(address, count);
        return true;
    }
    if (name == "de100-subsnr-read") {
        require_args(args, 2, "de100-subsnr-read requires <channel> <reg16>");
        auto payload = std::vector<std::uint8_t>{subsnr_channel(args[0], false)};
        append_to(payload, le16(parse_u16(args[1])));
        const auto response = execute(GMSL_COMMAND_DE100_SUBSNR_READ_REGISTER, payload);
        if (response.status == 0 && response.payload.size() == 2) {
            print_section("de100-subsnr-read");
            print_field("value") << "0x" << hex(response.payload[1], 2) << hex(response.payload[0], 2) << '\n';
        }
        return true;
    }
    if (name == "de100-subsnr-write") {
        require_args(args, 3, "de100-subsnr-write arguments invalid");
        auto payload = std::vector<std::uint8_t>{subsnr_channel(args[0], true)};
        append_to(payload, le16(parse_u16(args[1])));
        append_to(payload, le16(parse_u16(args[2])));
        execute(GMSL_COMMAND_DE100_SUBSNR_WRITE_REGISTER, payload);
        return true;
    }
    if (name == "de100-get-statistics") {
        uint32_t sof_eof_num_base_addr = 0;
        uint32_t err_num_base_addr = 0;
        if (args.size() == 0) {
            auto payload = std::vector<std::uint8_t>{0xc1};
            auto response = execute(GMSL_COMMAND_PERCIPIO_DEBUG, payload, false, false);
            if(response.status != 0)
                throw std::runtime_error("Failed to get statistics");
            sof_eof_num_base_addr = read_le_u32(response.payload, 0);
            err_num_base_addr = read_le_u32(response.payload, 4);
        } else {
            require_args(args, 2, name + " requires <sof_eof_num_base_addr> <err_num_base_addr>");
            sof_eof_num_base_addr = parse_u32(args[0]);
            err_num_base_addr = parse_u32(args[1]);
        }
        set_de100_page(sof_eof_num_base_addr, false);
        print_section("de100-get-statistics");

        uint8_t value = read_de100_byte((sof_eof_num_base_addr+0)&0xffff);
        print_field("match_sof") << static_cast<unsigned>(value) << '\n';
        value = read_de100_byte((sof_eof_num_base_addr+1)&0xffff);
        print_field("match_eof") << static_cast<unsigned>(value) << '\n';
        value = read_de100_byte((sof_eof_num_base_addr+2)&0xffff);
        print_field("rx2_eof") << static_cast<unsigned>(value) << '\n';
        value = read_de100_byte((sof_eof_num_base_addr+3)&0xffff);
        print_field("rx2_sof") << static_cast<unsigned>(value) << '\n';
        value = read_de100_byte((sof_eof_num_base_addr+4)&0xffff);
        print_field("rx1_eof") << static_cast<unsigned>(value) << '\n';
        value = read_de100_byte((sof_eof_num_base_addr+5)&0xffff);
        print_field("rx1_sof") << static_cast<unsigned>(value) << '\n';

        uint32_t value32 = read_de100_reg32((err_num_base_addr+0));
        print_field("rx1_ecc_sig_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+4));
        print_field("rx1_ecc_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+8));
        print_field("rx1_crc_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+12));
        print_field("rx1_byte_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+16));
        print_field("rx1_sot_err_lane0") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+20));
        print_field("rx1_sot_err_lane1") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+24));
        print_field("rx1_sot_err_lane2") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+28));
        print_field("rx1_sot_err_lane3") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+32));
        print_field("rx1_sof_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+36));
        print_field("rx1_eof_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+40));
        print_field("rx1_line_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+44));
        print_field("rx2_ecc_sig_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+48));
        print_field("rx2_ecc_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+52));
        print_field("rx2_crc_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+56));
        print_field("rx2_byte_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+60));
        print_field("rx2_sot_err_lane0") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+64));
        print_field("rx2_sot_err_lane1") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+68));
        print_field("rx2_sot_err_lane2") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+72));
        print_field("rx2_sot_err_lane3") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+76));
        print_field("rx2_sof_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+80));
        print_field("rx2_eof_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+84));
        print_field("rx2_line_err") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+88));
        print_field("pre_fifo_overflow1") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+92));
        print_field("pre_fifo_overflow2") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+96));
        print_field("pre1_loss_data[0]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+100));
        print_field("pre1_loss_data[1]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+104));
        print_field("pre1_loss_data[2]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+108));
        print_field("pre2_loss_data[0]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+112));
        print_field("pre2_loss_data[1]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+116));
        print_field("pre2_loss_data[2]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+120));
        print_field("pre1_frame_err[0]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+124));
        print_field("pre1_frame_err[1]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+128));
        print_field("pre1_frame_err[2]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+132));
        print_field("pre2_frame_err[0]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+136));
        print_field("pre2_frame_err[1]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+140));
        print_field("pre2_frame_err[2]") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+144));
        print_field("rectify1_err_eol_irq") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+148));
        print_field("rectify1_err_eof_irq") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+152));
        print_field("rectify1_buf_err_irq") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+156));
        print_field("rectify2_err_eol_irq") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+160));
        print_field("rectify2_err_eof_irq") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+164));
        print_field("rectify2_buf_err_irq") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+168));
        print_field("async_fifo_full_left_int") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+172));
        print_field("async_fifo_full_right_int") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+176));
        print_field("frame_buf_ctrl1_conflict") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+180));
        print_field("frame_buf_ctrl2_conflict") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+184));
        print_field("image_stitch_u_fifo1_overflow_int") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+188));
        print_field("image_stitch_u_fifo2_overflow_int") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+192));
        print_field("image_stitch_u_fifo3_overflow_int") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+196));
        print_field("image_stitch_u_fifo4_overflow_int") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+200));
        print_field("image_stitch_u_fifo1_underflow") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+204));
        print_field("image_stitch_u_fifo2_underflow") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+208));
        print_field("image_stitch_u_fifo3_underflow") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        value32 = read_de100_reg32((err_num_base_addr+212));
        print_field("image_stitch_u_fifo4_underflow") << static_cast<unsigned>(bswap_32(value32)) << '\n';
        return true;
    }
    if (name == "de100-dump-match-debug-reg") {
        if (args.size() > 1)
            throw std::invalid_argument("de100-dump-match-debug-reg accepts at most one <probe_reg>");
        std::optional<std::uint8_t> filter;
        if (!args.empty()) {
            const auto address = parse_u32(args[0]);
            if (address > 0x24)
                throw std::invalid_argument("probe register must be in range 0x0..0x24");
            filter = static_cast<std::uint8_t>(address);
        }

        set_de100_page(0x80311460, false);
        std::size_t failures = 0;
        std::optional<std::uint8_t> current_register;
        for (const auto &field : de100_match_fields) {
            if (filter && *filter != field.register_address)
                continue;
            if (!current_register || *current_register != field.register_address) {
                std::cout << "\n=== [" << format_dump_hex(field.register_address, 1) << "] "
                          << field.register_name << " ===\n";
                current_register = field.register_address;
            }

            std::uint32_t value = 0;
            try {
                value = read_de100_match_probe(field.register_address, field.offset);
            } catch (const std::runtime_error &error) {
                ++failures;
                std::cout << "  " << std::left << std::setw(28) << field.name << std::right << " [w="
                          << std::setw(2) << static_cast<unsigned>(field.width) << " off=" << std::setw(2)
                          << static_cast<unsigned>(field.offset) << "] = <read fail>\n";
                std::cerr << "DE100 match probe " << field.register_name << '.' << field.name
                          << " read failed: " << error.what() << '\n';
                continue;
            }
            const auto mask = field.width >= 32 ? 0xffffffffU : (std::uint32_t{1} << field.width) - 1;
            const auto decoded_value = value & mask;
            const auto enumeration = decode_match_enum(field.name, decoded_value);
            std::cout << "  " << std::left << std::setw(28) << field.name << std::right << " [w="
                      << std::setw(2) << static_cast<unsigned>(field.width) << " off=" << std::setw(2)
                      << static_cast<unsigned>(field.offset) << "] = " << std::left << std::setw(10)
                      << decoded_value << std::right << " 0x" << std::uppercase << std::hex << decoded_value
                      << std::dec << std::nouppercase;
            if (!enumeration.empty())
                std::cout << " (" << enumeration << ')';
            std::cout << '\n';
        }
        if (failures)
            throw std::runtime_error(std::to_string(failures) + " DE100 match probe field(s) failed");
        return true;
    }
    if (name == "de100-dump-registers") {
        const auto options = parse_de100_dump_options(args);
        const auto selected = select_de100_registers(options);
        if (options.list_only) {
            print_de100_register_list(selected);
            return true;
        }

        std::ofstream output_file;
        std::ostream *csv_output = nullptr;
        if (options.output) {
            const auto parent = options.output->parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);
            output_file.open(*options.output, std::ios::out | std::ios::trunc);
            if (!output_file)
                throw std::runtime_error("cannot write " + options.output->string());
            csv_output = &output_file;
            write_csv_row(output_file, {"module", "register", "address", "offset", "width_bits", "access",
                                        "value", "status", "description", "source"});
        }

        std::size_t successes = 0;
        std::size_t errors = 0;
        std::string last_module;
        std::optional<std::uint16_t> current_page;
        for (const auto *register_info : selected) {
            const auto byte_count = (register_info->width_bits + 7U) / 8U;
            std::string value_text;
            std::string status = "OK";
            try {
                std::uint64_t value = 0;
                for (unsigned byte_offset = 0; byte_offset < byte_count; ++byte_offset) {
                    const auto address = static_cast<std::uint64_t>(register_info->address) + byte_offset;
                    if (address > 0xffffffffULL)
                        throw std::runtime_error("register read exceeds the 32-bit address space");
                    const auto full_address = static_cast<std::uint32_t>(address);
                    const auto page = static_cast<std::uint16_t>(full_address >> 16);
                    if (!current_page || *current_page != page) {
                        set_de100_page(full_address, false);
                        current_page = page;
                    }
                    value |= static_cast<std::uint64_t>(
                                 read_de100_byte(static_cast<std::uint16_t>(full_address)))
                             << (byte_offset * 8);
                }
                bool msb = (register_info->endian == "MSB");
                if (msb && byte_count > 1) {
                    std::uint64_t reversed_value = 0;
                    for (unsigned i = 0; i < byte_count; ++i) {
                        reversed_value |= ((value >> (i * 8)) & 0xFF) << ((byte_count - 1 - i) * 8);
                    }
                    value = reversed_value;
                }
                if (register_info->width_bits < 64)
                    value &= (std::uint64_t{1} << register_info->width_bits) - 1;
                value_text = format_dump_hex(value, byte_count * 2);
                ++successes;
            } catch (const std::runtime_error &error) {
                status = "ERROR: " + std::string(error.what());
                ++errors;
            }

            if (csv_output) {
                write_csv_row(*csv_output,
                              {std::string(register_info->module), std::string(register_info->name),
                               format_dump_hex(register_info->address, 8),
                               format_dump_hex(register_info->offset, 0),
                               std::to_string(register_info->width_bits), std::string(register_info->access),
                               value_text, status, "", "embedded-trm"});
                csv_output->flush();
                if (!*csv_output)
                    throw std::runtime_error("cannot write " + options.output->string());
            }

            if (last_module != register_info->module) {
                std::cout << '[' << register_info->module << "]\n";
                last_module = register_info->module;
            }
            std::cout << "  " << std::left << std::setw(35) << register_info->name << std::right
                      << (value_text.empty() ? status : value_text) << '\n';
            if (errors && options.fail_fast)
                break;
        }

        if (options.output)
            std::cout << "saved " << successes << " register(s), " << errors << " error(s) to "
                      << options.output->string() << '\n';
        else
            std::cout << "dumped " << successes << " register(s), " << errors << " error(s) to stdout\n";
        if (errors)
            throw std::runtime_error(std::to_string(errors) + " register read(s) failed");
        return true;
    }
    if (name == "isp-flash-status" || name == "isp-flash-abort" ||
        name == "isp-flash-update") {
        const auto query = [&](std::uint8_t op, std::vector<std::uint8_t> payload = {}) {
            payload.insert(payload.begin(), op);
            const auto response = execute(GMSL_COMMAND_ISP_FLASH_UPDATE, payload, false, false);
            if (response.payload.size() != 20U)
                throw std::runtime_error("ISP Flash protocol mismatch: update MCU and host tool together");
            context_.sequence = static_cast<std::uint8_t>(context_.sequence + 1);
            return response.payload;
        };
        const auto read_u32 = [](const auto &bytes, std::size_t offset) {
            return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
                   (std::uint32_t(bytes[offset + 2]) << 16) |
                   (std::uint32_t(bytes[offset + 3]) << 24);
        };
        const auto print_status = [&](const auto &status) {
            print_field("state") << read_u32(status, 0) << '\n';
            print_field("received") << read_u32(status, 4) << '\n';
            print_field("programmed") << read_u32(status, 8) << '\n';
            print_field("verified") << read_u32(status, 12) << '\n';
            print_field("error") << static_cast<std::int32_t>(read_u32(status, 16)) << '\n';
        };
        if (name != "isp-flash-update") {
            require_args(args, 0, name + " takes no arguments");
            print_status(query(name == "isp-flash-status" ? 2 : 4));
            return true;
        }
        require_args(args, 1, name + " requires <image.src>");
        std::ifstream input(args[0], std::ios::binary | std::ios::ate);
        if (!input || input.tellg() != 0x20000)
            throw std::runtime_error("ISP update requires an exact 128 KiB image");
        input.seekg(0);
        std::vector<std::uint8_t> image(0x20000);
        if (!input.read(reinterpret_cast<char *>(image.data()), image.size()))
            throw std::runtime_error("cannot read ISP image");
        if (image[0] != 'S' || image[1] != 'N' || image[2] != '9' || image[3] != 'C')
            throw std::runtime_error("ISP image is missing SN9C signature");
        std::uint32_t crc = 0xffffffff;
        for (const auto byte : image) {
            crc ^= byte;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
        constexpr std::size_t frame_overhead = 10;
        const auto transfer_limit = std::min<std::size_t>(8192, context_.max_control_transfer_size);
        if (transfer_limit < frame_overhead + 4)
            throw std::invalid_argument("ISP update control transfer limit must allow at least 4 data bytes");
        const auto chunk_size = std::min<std::size_t>(4096, transfer_limit - frame_overhead);
        const auto started = std::chrono::steady_clock::now();
        const auto show_progress = [&](const auto &status) {
            const auto programmed = read_u32(status, 8);
            const auto verified = read_u32(status, 12);
            if (programmed > image.size() || verified > programmed ||
                programmed > read_u32(status, 4) || read_u32(status, 4) > image.size())
                throw std::runtime_error("invalid ISP Flash progress counters");
            const auto completed = std::size_t(programmed);
            const auto total = image.size();
            print_transfer_progress(read_u32(status, 0) == 4 ? completed :
                                    std::min(completed, total - (total / 10000 + 1)), total, started);
        };
        const auto wait_ready = [&] {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
            std::uint32_t previous_progress = 0;
            while (true) {
                const auto status = query(2);
                const auto state = read_u32(status, 0);
                if (state == 5)
                    throw std::runtime_error("ISP Flash update failed, error=" +
                        std::to_string(static_cast<std::int32_t>(read_u32(status, 16))));
                show_progress(status);
                const auto progress = read_u32(status, 8);
                if (progress != previous_progress) {
                    previous_progress = progress;
                    deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
                }
                if (state == 2 || state == 4)
                    return status;
                if (state != 1 && state != 3 && state != 6 && state != 7)
                    throw std::runtime_error("unexpected ISP update state=" + std::to_string(state));
                if (std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("ISP Flash update polling timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        };
        auto start = le32(static_cast<std::uint32_t>(image.size()));
        append_to(start, le32(crc ^ 0xffffffff));
        std::cerr << "WARNING: replacing first 128 KiB of ISP Flash; interruption may require external recovery\n";
        const auto power_response = execute(GMSL_COMMAND_LOW_POWER_MODE, {0}, false, false);
        if (!power_response.payload.empty())
            throw std::runtime_error("invalid low-power-mode response size");
        std::cerr << "MCU low-power mode disabled; left disabled until reset or explicit re-enable.\n";
        query(2);
        print_section("isp-flash-update");
        print_field("chunk_size") << chunk_size << " bytes\n";
        print_field("phases") << "program only; power-cycle and verify via USB afterwards\n";
        query(0, start);
        try {
            wait_ready();
            for (std::uint32_t offset = 0; offset < image.size();) {
                const auto count = std::min<std::size_t>(chunk_size, 4096 - offset % 4096);
                auto payload = le32(offset);
                payload.insert(payload.end(), image.begin() + offset, image.begin() + offset + count);
                const auto status = query(1, payload);
                offset += static_cast<std::uint32_t>(count);
                if (read_u32(status, 4) != offset)
                    throw std::runtime_error("ISP update offset acknowledgment mismatch");
                if (offset % 4096 == 0) {
                    const auto ready = wait_ready();
                    if (read_u32(ready, 8) != offset)
                        throw std::runtime_error("ISP programming acknowledgment mismatch");
                }
            }
            query(3);
            const auto status = wait_ready();
            if (read_u32(status, 0) != 4 || read_u32(status, 8) != image.size() ||
                read_u32(status, 4) != image.size() || read_u32(status, 16) != 0)
                throw std::runtime_error("ISP update did not reach completed state");
            std::cout << "ISP programming completed; Flash contents NOT verified. Power-cycle and verify via USB.\n";
        } catch (const std::exception &error) {
            std::cout << '\n';
            try {
                query(4);
            } catch (const std::exception &abort_error) {
                throw std::runtime_error(std::string(error.what()) +
                    "; abort request failed: " + abort_error.what());
            }
            throw;
        }
        return true;
    }
    if (name == "isp-read") {
        require_args(args, 1, "isp-read requires <reg16>");
        const auto response = execute(GMSL_COMMAND_ISP_READ_REGISTER, le16(parse_u16(args[0])));
        if (response.status == 0 && (response.payload.size() == 1)) {
            print_section("isp-read");
            print_field("value") << "0x" << hex(response.payload[0], 2) << '\n';
        }
        return true;
    }
    if (name == "isp-write") {
        require_args(args, 2, "isp-write requires <reg16> <value8>");
        auto payload = le16(parse_u16(args[0]));
        payload.push_back(parse_byte(args[1]));
        execute(GMSL_COMMAND_ISP_WRITE_REGISTER, payload);
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