#include "commands.hpp"
#include "bytes.hpp"

#include "commands/basic_commands.hpp"
#include "commands/gige_commands.hpp"
#include "commands/partition_commands.hpp"
#include "commands/register_commands.hpp"

#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gm86x {

void CommandGroup::print_section(std::string_view name, int indent) {
    std::cout << std::string(static_cast<std::size_t>(indent), ' ') << '[' << name << "]\n";
}

std::ostream &CommandGroup::print_field(std::string_view key, int width, int indent) {
    std::cout << std::string(static_cast<std::size_t>(indent), ' ') << std::left
              << std::setw(width) << key << std::right << " : ";
    return std::cout;
}

std::string CommandGroup::hex(std::uint64_t value, int digits) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return output.str();
}

const char *CommandGroup::opcode_name(std::uint8_t opcode) {
    switch (opcode) {
    case GMSL_COMMAND_PING:
        return "PING";
    case GMSL_COMMAND_GET_STATUS:
        return "GET_STATUS";
    case GMSL_COMMAND_MCU_RESET:
        return "MCU_RESET";
    case GMSL_COMMAND_SET_TIMER_OUT_RESET:
        return "SET_TIMER_OUT_RESET";
    case GMSL_COMMAND_SET_PPS_MODE:
        return "SET_PPS_MODE";
    case GMSL_COMMAND_GET_TIMESTAMP:
        return "GET_TIMESTAMP";
    case GMSL_COMMAND_SET_TIMESTAMP:
        return "SET_TIMESTAMP";
    case GMSL_COMMAND_GET_FIRMWARE_VERSION:
        return "GET_FIRMWARE_VERSION";
    case GMSL_COMMAND_TIME_SYNC_EXCHANGE:
        return "TIME_SYNC_EXCHANGE";
    case GMSL_COMMAND_ADJUST_TIMESTAMP:
        return "ADJUST_TIMESTAMP";
    case GMSL_COMMAND_SMOOTH_ADJUST_TIMESTAMP:
        return "SMOOTH_ADJUST_TIMESTAMP";
    case GMSL_COMMAND_DE100_READ_REGISTER:
        return "DE100_READ_REGISTER";
    case GMSL_COMMAND_DE100_WRITE_REGISTER:
        return "DE100_WRITE_REGISTER";
    case GMSL_COMMAND_DE100_SUBSNR_READ_REGISTER:
        return "DE100_SUBSNR_READ_REGISTER";
    case GMSL_COMMAND_DE100_SUBSNR_WRITE_REGISTER:
        return "DE100_SUBSNR_WRITE_REGISTER";
    case GMSL_COMMAND_ISP_READ_REGISTER:
        return "ISP_READ_REGISTER";
    case GMSL_COMMAND_ISP_WRITE_REGISTER:
        return "ISP_WRITE_REGISTER";
    case GMSL_COMMAND_IMU_READ_SAMPLES:
        return "IMU_READ_SAMPLES";
    case GMSL_COMMAND_TEMP_READ:
        return "TEMP_READ";
    case GMSL_COMMAND_SET_AUTO_USERSET_SLOT:
        return "SET_AUTO_USERSET_SLOT";
    case GMSL_COMMAND_UPDATE_SYS_FIRMWARE:
        return "UPDATE_SYS_FIRMWARE";
    case GMSL_COMMAND_UPDATE_CFG_PARTITION:
        return "UPDATE_CFG_PARTITION";
    case GMSL_COMMAND_UPDATE_CALIB_PARTITION:
        return "UPDATE_CALIB_PARTITION";
    case GMSL_COMMAND_UPDATE_USERSET_PARTITION:
        return "UPDATE_USERSET_PARTITION";
    case GMSL_COMMAND_UPDATE_USERDATA_PARTITION:
        return "UPDATE_USERDATA_PARTITION";
    case GMSL_COMMAND_GIGE_CAM_READ_REGISTER:
        return "GIGE_CAM_READ_REGISTER";
    case GMSL_COMMAND_GIGE_CAM_WRITE_REGISTER:
        return "GIGE_CAM_WRITE_REGISTER";
    case GMSL_COMMAND_UPDATE_BOOT_FIRMWARE:
        return "UPDATE_BOOT_FIRMWARE";
    case GMSL_COMMAND_READ_CALIB_PARTITION:
        return "READ_CALIB_PARTITION";
    case GMSL_COMMAND_ERASE_USERSET_PARTITION:
        return "ERASE_USERSET_PARTITION";
    case GMSL_COMMAND_ERASE_CALIB_PARTITION:
        return "ERASE_CALIB_PARTITION";
    case GMSL_COMMAND_FORMAT_USER_DATA_DEFAULT:
        return "FORMAT_USER_DATA_DEFAULT";
    case GMSL_COMMAND_READ_USERSET_PARTITION:
        return "READ_USERSET_PARTITION";
    case GMSL_COMMAND_GET_AUTO_USERSET_SLOT:
        return "GET_AUTO_USERSET_SLOT";
    case GMSL_COMMAND_GIGE_CAM_GET_ACCESS_MAP:
        return "GIGE_CAM_GET_ACCESS_MAP";
    case GMSL_COMMAND_APPLY_USERSET:
        return "APPLY_USERSET";
    case GMSL_COMMAND_LOW_POWER_MODE:
        return "LOW_POWER_MODE";
    case GMSL_COMMAND_ISP_FW_UPDATE_REQUEST:
        return "ISP_FW_UPDATE_REQUEST";
    case GMSL_COMMAND_ISP_FW_UPDATE_GET_PENDING:
        return "ISP_FW_UPDATE_GET_PENDING";
    case GMSL_COMMAND_ISP_FW_UPDATE_SUBMIT_PENDING:
        return "ISP_FW_UPDATE_SUBMIT_PENDING";
    case GMSL_COMMAND_ISP_GET_VERSION:
        return "ISP_GET_VERSION";
    case GMSL_COMMAND_ISP_READ_SUBSENSOR_REGISTER:
        return "ISP_READ_SUBSENSOR_REGISTER";
    case GMSL_COMMAND_ISP_WRITE_SUBSENSOR_REGISTER:
        return "ISP_WRITE_SUBSENSOR_REGISTER";
    case GMSL_COMMAND_POST_PROCESSOR_RAW_WRITE:
        return "POST_PROCESSOR_RAW_WRITE";
    case GMSL_COMMAND_POST_PROCESSOR_RAW_READ:
        return "POST_PROCESSOR_RAW_READ";
    case GMSL_COMMAND_PERCIPIO_DEBUG:
        return "PERCIPIO_DEBUG";
    default:
        return "UNKNOWN";
    }
}

void CommandGroup::print_hex_dump(std::span<const std::uint8_t> data) {
    constexpr std::size_t bytes_per_line = 16;
    constexpr std::size_t group_size = 8;
    const auto continuation = std::string(static_cast<std::size_t>(field_indent + field_key_width + 3), ' ');
    for (std::size_t offset = 0; offset < data.size(); offset += bytes_per_line) {
        const auto line = data.subspan(offset, std::min(bytes_per_line, data.size() - offset));
        if (offset == 0)
            print_field("data");
        else
            std::cout << continuation;
        std::cout << hex(offset, 4) << "  ";
        std::string gutter;
        for (std::size_t index = 0; index < bytes_per_line; ++index) {
            if (index == group_size)
                std::cout << ' ';
            if (index >= line.size()) {
                std::cout << "   ";
                continue;
            }
            std::cout << hex(line[index], 2) << ' ';
            gutter += line[index] >= 32 && line[index] <= 126 ? static_cast<char>(line[index]) : '.';
        }
        std::cout << " |" << gutter << "|\n";
    }
}

void CommandGroup::require_args(const std::vector<std::string> &args, std::size_t count, const std::string &usage) {
    if (args.size() != count)
        throw std::invalid_argument(usage);
}

std::vector<std::uint8_t> CommandGroup::le16(std::uint16_t value) {
    return {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8)};
}

std::vector<std::uint8_t> CommandGroup::le32(std::uint32_t value) {
    std::vector<std::uint8_t> result;
    append_le_u32(result, value);
    return result;
}

std::vector<std::uint8_t> CommandGroup::le64(std::uint64_t value) {
    std::vector<std::uint8_t> result;
    for (int shift = 0; shift < 64; shift += 8)
        result.push_back(static_cast<std::uint8_t>(value >> shift));
    return result;
}

void CommandGroup::append_to(std::vector<std::uint8_t> &target, std::span<const std::uint8_t> source) {
    target.insert(target.end(), source.begin(), source.end());
}

std::uint8_t CommandGroup::userset_slot(const std::string &raw) {
    const auto value = parse_decimal_or_hex_u32(raw);
    if (value > 128)
        throw std::invalid_argument("userset slot out of range: " + raw);
    return static_cast<std::uint8_t>(value);
}

std::uint32_t CommandGroup::read_le32(std::span<const std::uint8_t> data) {
    if (data.size() < 4)
        throw std::runtime_error("short u32 payload");
    return static_cast<std::uint32_t>(data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24);
}

std::string CommandGroup::ascii(std::span<const std::uint8_t> data) {
    std::string result;
    for (const auto value : data) {
        if (!value)
            break;
        result += value >= 32 && value <= 126 ? static_cast<char>(value) : '.';
    }
    return result;
}

std::vector<std::uint8_t> CommandGroup::read_file(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open file: " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void CommandGroup::print_timing(const RequestTiming &timing) const {
    print_section("timing");
    print_field("send") << timing.send.count() << " ms\n";
    print_field("ready_wait") << timing.ready_wait.count() << " ms\n";
    print_field("response") << timing.response.count() << " ms\n";
    print_field("total") << timing.total.count() << " ms\n";
}

void CommandGroup::print_command(std::uint8_t sequence, std::uint8_t opcode,
                                 std::span<const std::uint8_t> payload) const {
    print_section("request");
    print_field("sequence") << "0x" << hex(sequence, 2) << '\n';
    print_field("opcode") << "0x" << hex(opcode, 2) << " (" << opcode_name(opcode) << ")\n";
    print_field("payload") << payload.size() << " bytes\n";
    print_hex_dump(payload);
}

void CommandGroup::print_response(const Response &response) const {
    print_section("response");
    print_field("sequence") << "0x" << hex(response.sequence, 2) << '\n';
    print_field("opcode") << "0x" << hex(response.opcode, 2) << " (" << opcode_name(response.opcode) << ")\n";
    print_field("status") << "0x" << hex(response.status, 2) << " (" << status_name(response.status) << ")\n";
    print_field("payload") << response.payload.size() << " bytes\n";
    print_hex_dump(response.payload);
}

Response CommandGroup::execute(std::uint8_t opcode, const std::vector<std::uint8_t> &payload,
                               bool print_timing_enabled, bool print_verbose) {
    if (print_verbose)
        print_command(context_.sequence, opcode, payload);
    const auto response = context_.client.command(context_.sequence, opcode, payload);
    if (print_verbose)
        print_response(response);
    if (print_timing_enabled)
        print_timing(response.timing);
    return response;
}

void CommandGroup::print_transfer_progress(std::size_t completed, std::size_t total,
                                           std::chrono::steady_clock::time_point started) const {
    constexpr std::size_t width = 28;
    const auto fraction = total ? static_cast<double>(completed) / total : 1.0;
    const auto filled = std::min(width, static_cast<std::size_t>(fraction * width));
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto average_rate = elapsed > 0 ? completed / elapsed / 1000.0 : 0.0;
    std::cout << "\r\033[K  progress    : [" << std::string(filled, '=') << std::string(width - filled, ' ') << "] "
              << std::fixed << std::setprecision(2) << std::setw(6) << fraction * 100.0 << "% (" << completed << '/'
              << total << " bytes) avg=" << average_rate << "kB/s elapsed=" << std::setprecision(1) << elapsed << "s"
              << std::flush;
    if (completed == total)
        std::cout << '\n';
}

CommandDispatcher::CommandDispatcher(CommandContext &context) : context_(context) {
    register_group(std::make_unique<detail::BasicCommandGroup>(context));
    register_group(std::make_unique<detail::GigeCommandGroup>(context));
    register_group(std::make_unique<detail::RegisterCommandGroup>(context));
    register_group(std::make_unique<detail::PartitionCommandGroup>(context));
}

void CommandDispatcher::register_group(std::unique_ptr<CommandGroup> group) {
    groups_.push_back(std::move(group));
}

int CommandDispatcher::run(std::string_view name, const std::vector<std::string> &args) {
    for (const auto &group : groups_) {
        if (group->try_run(name, args))
            return 0;
    }
    throw std::invalid_argument("unknown command: " + std::string(name));
}

void CommandDispatcher::print_help(const std::string &exeName) {
    std::cout << R"(
    -r, --retries <count>    poll retries, default 1000
    -i, --interval <sec>     poll interval, default 0.005
    --seq <hex>              request sequence, default 01
    --buffer-updates <0|1>   override buffer mode for update commands
    --end-update             mark the following update chunk as the final chunk
    -h, --help               show this help

Commands:
    probe
    raw-read <reg> <len>
    raw-write <reg> <byte...>
    send <opcode> [payload_byte...]
    ping [payload_byte...]
    ping-random <size>
    get-status
    mcu-reset
    set-reset <0|1>
    get-fw-version
    get-timestamp
    set-timestamp <timestamp_u64>
    time-sync-exchange
    adjust-timestamp <adjustment_i64_us>
    smooth-set-timestamp <timestamp_u64>
    set-pps-mode <0|1>
    de100-read <reg16>
    de100-write <reg16> <value8>
    de100-read-reg32-val8 <reg32>
    de100-write-reg32-val8 <reg32> <value8>
    de100-read-reg32-val32 <reg32>
    de100-write-reg32-val32 <reg32> <value32>
    de100-dump-reg32 <addr32> <count>
    subsnr-read <1|2|left|right> <reg16>
    subsnr-write <1|2|3|left|right|all> <reg16> <value16>
    isp-read <reg16>
    isp-write <reg16> <value8>
    isp-subsnr-read <reg16>
    isp-subsnr-write <reg16> <value16>
    post-processor-raw-write <byte...>
    post-processor-raw-read <size>
    imu-read <count>
    temp-read <0|1|2|3|left|right|laser|laser2>
    set-auto-userset-slot <slot>
    get-auto-userset-slot
    apply-userset <slot>
    low-power-mode [0|1]
    isp-get-version
    isp-fw-update-request
    isp-fw-update-get-pending
    isp-fw-update-submit-pending <addr32> <size> <byte...>
    isp-fw-update-submit-force-terminate
    isp-fw-update-file <bin_file>
    gige-read <addr32> <size>
    gige-write <addr32> <size> <byte...>
    gige-get-access-map [start_index count]
    gige-get-access-map-count
    gige-read-u32 <addr32>
    gige-read-i32 <addr32>
    gige-read-float <addr32>
    gige-write-u32 <addr32> <value>
    gige-write-i32 <addr32> <value>
    gige-write-float <addr32> <value>
    read-userset <slot> <offset32> <size>
    read-userset-slot <slot> <out_file>
    list-userset-addr <slot>
    read-userset-addr <slot> <addr32>
    read-calib <out_file>
    list-calib-addr
    read-calib-addr <addr32>
    read-sn
    write-sn <sn>
    erase-userset <slot|all>
    erase-calib
    format-userdata
    update-sys-chunk <offset32> <byte...>
    update-boot-chunk <password32> <offset32> <byte...>
    update-cfg-chunk <offset32> <byte...>
    update-calib-chunk <offset32> <byte...>
    update-userset-chunk <slot> <offset32> <byte...>
    update-userdata-chunk <offset32> <byte...>
    update-sys-file <bin_file> [start_offset32] [chunk_size]
    update-boot-file <password32> <bin_file> [start_offset32] [chunk_size]
    update-cfg-file <bin_file> [start_offset32] [chunk_size]
    update-calib-file <bin_file> [start_offset32] [chunk_size]
    update-userset-file <slot> <bin_file> [start_offset32] [chunk_size]
    update-userdata-file <bin_file> [start_offset32] [chunk_size]
    percipio-debug <op> [payload_byte...]
    percipio-debug-read32 <addr32>
    percipio-debug-write32 <addr32> <value32>
    percipio-debug-status-log <0|1>

Notes:
    - Byte and register arguments use hexadecimal by default.
    - This tool uses the Linux I2C_RDWR backend instead of debugfs node.
    - Per-request timing logs are enabled by default.
    - Userset slots for write/update/erase commands are 0..128.
    - update-*-chunk offset32 is hexadecimal by default.
    - --buffer-updates 1 sets offset bit31; --buffer-updates 0 clears it.
    - Without --buffer-updates, file update commands use their default buffer mode.
    - --end-update sets offset bit30.
    - update-boot-chunk requires a little-endian password32 argument.
    - Default chunk sizes are sys/cfg/calib/userdata=8188, boot=8184, userset=8187.

Examples:
    )" << exeName << R"( mcu-reset
    )" << exeName << R"( get-fw-version
    )" << exeName << R"( read-sn
    )" << exeName << R"( imu-read 1
    )" << exeName << R"( set-timestamp 1778837956645321
    )" << exeName << R"( time-sync-exchange
    )" << exeName << R"( adjust-timestamp -250
    )" << exeName << R"( smooth-set-timestamp 1778837956645321
    )" << exeName << R"( temp-read left
    )" << exeName << R"( erase-userset all
    )" << exeName << R"( erase-calib
    )" << exeName << R"( format-userdata
    )" << exeName << R"( gige-get-access-map
    )" << exeName << R"( read-userset-slot 0 userset_slot0.bin
    )" << exeName << R"( read-calib calib.bin
    )" << exeName << R"( update-boot-file 0xffffffff boot.bin
    )" << exeName << R"( update-sys-file sys.bin
    )" << exeName << R"( update-cfg-file cfg.bin
)";
}

} // namespace gm86x