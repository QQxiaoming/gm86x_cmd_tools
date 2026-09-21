#include "basic_commands.hpp"

#include "bytes.hpp"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace gm86x::detail {

void BasicCommandGroup::print_probe(const std::vector<std::uint8_t> &data) const {
    print_section("probe");
    print_field("raw_bytes", 17) << hex_bytes(data) << '\n';
    if (data.size() != 6)
        throw std::runtime_error("probe response must contain 6 bytes");
    print_field("magic", 17) << "0x" << hex(data[0], 2) << '\n';
    print_field("version", 17) << "0x" << hex(data[1], 2) << '\n';
    print_field("device_flags", 17) << "0x" << hex(data[2], 2) << '\n';
    print_field("last_error", 17) << "0x" << hex(data[3], 2) << " (" << status_name(data[3]) << ")\n";
    print_field("payload_fifo_size", 17) << read_le_u16(data, 4) << '\n';
}

std::int16_t BasicCommandGroup::read_s16(std::span<const std::uint8_t> data, std::size_t offset) const {
    return static_cast<std::int16_t>(read_le_u16(data, offset));
}

float BasicCommandGroup::read_float(std::span<const std::uint8_t> data, std::size_t offset) const {
    if (offset + 4 > data.size())
        throw std::runtime_error("short float status field");
    const auto bits = static_cast<std::uint32_t>(data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 |
                                                 data[offset + 3] << 24);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::string BasicCommandGroup::ascii_field(std::span<const std::uint8_t> data, std::size_t offset, std::size_t length) const {
    std::string value;
    for (std::size_t index = offset; index < offset + length && index < data.size() && data[index]; ++index)
        value += data[index] >= 32 && data[index] <= 126 ? static_cast<char>(data[index]) : '.';
    return value;
}

std::uint64_t BasicCommandGroup::read_le64(std::span<const std::uint8_t> data) const {
    if (data.size() < 8)
        throw std::runtime_error("short u64 payload");
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(data[index]) << (index * 8);
    return value;
}

std::uint32_t BasicCommandGroup::read_le32(std::span<const std::uint8_t> data, std::size_t offset) const {
    if (offset + 4 > data.size())
        throw std::runtime_error("short u32 payload");
    return static_cast<std::uint32_t>(data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 |
                                      data[offset + 3] << 24);
}

void BasicCommandGroup::print_imu_samples(const std::vector<std::uint8_t> &payload) const {
    if (payload.empty())
        return;
    const auto count = static_cast<std::size_t>(payload[0]);
    print_section("imu");
    print_field("sample_count") << count << '\n';
    for (std::size_t sample = 0; sample < count; ++sample) {
        const auto base = 1 + sample * 42;
        if (base + 42 > payload.size())
            break;
        const auto data = std::span<const std::uint8_t>(payload);
        print_section("sample " + std::to_string(sample), 2);
        print_field("timestamp_us", 17, 4) << read_le64(data.subspan(base)) << '\n';
        print_field("sensor_time_raw", 17, 4) << read_le32(data, base + 8) << '\n';
        print_field("sensor_temp_raw", 17, 4) << static_cast<std::int32_t>(read_le32(data, base + 12)) << '\n';
        print_field("accel_x_ms2", 17, 4) << std::setprecision(9) << read_float(data, base + 16) << '\n';
        print_field("accel_y_ms2", 17, 4) << read_float(data, base + 20) << '\n';
        print_field("accel_z_ms2", 17, 4) << read_float(data, base + 24) << '\n';
        print_field("gyro_x_rad_s", 17, 4) << read_float(data, base + 28) << '\n';
        print_field("gyro_y_rad_s", 17, 4) << read_float(data, base + 32) << '\n';
        print_field("gyro_z_rad_s", 17, 4) << read_float(data, base + 36) << '\n';
        print_field("valid_flags", 17, 4) << "0x" << hex(payload[base + 41], 2) << '\n';
    }
}

const char *BasicCommandGroup::isp_model_name(std::uint8_t model) const {
    switch (model) {
    case 0:
        return "none";
    case 1:
        return "gl3008";
    case 2:
        return "gl3009";
    case 3:
        return "xj2875";
    default:
        return "unknown";
    }
}

void BasicCommandGroup::print_status(const std::vector<std::uint8_t> &payload) const {
    if (payload.size() < 162)
        throw std::runtime_error("get-status payload must contain 162 bytes");
    const auto data = std::span<const std::uint8_t>(payload);
    const auto u32 = [&data](std::size_t offset) {
        if (offset + 4 > data.size())
            throw std::runtime_error("short u32 status field");
        return static_cast<std::uint32_t>(data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 |
                                          data[offset + 3] << 24);
    };
    const auto u64 = [&data](std::size_t offset) {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index)
            value |= static_cast<std::uint64_t>(data[offset + index]) << (index * 8);
        return value;
    };

    constexpr int width = 17;
    constexpr int indent = 4;

    print_section("status");

    print_section("init", 2);
    print_field("init_time", width, indent) << u32(158) << '\n';

    print_section("cpu", 2);
    print_field("usage", width, indent) << static_cast<unsigned>(data[1]) << "%\n";
    print_field("idle", width, indent) << static_cast<unsigned>(data[0]) << "%\n";

    print_section("gmsl", 2);
    print_field("i2c", width, indent) << "0x" << hex(data[11], 2) << '\n';
    print_field("rx", width, indent) << u32(18) << '\n';
    print_field("tx", width, indent) << u32(22) << '\n';
    print_field("out_rst", width, indent) << "0x" << hex(data[2], 2) << '\n';
    print_field("sync_in", width, indent) << u32(3) << '\n';
    print_field("sync_out", width, indent) << u32(7) << '\n';

    print_section("de100", 2);
    print_field("rst", width, indent) << "0x" << hex(data[26], 2) << '\n';
    print_field("stby", width, indent) << "0x" << hex(data[27], 2) << '\n';
    print_field("refclk", width, indent) << "0x" << hex(data[28], 2) << '\n';
    print_field("i2c", width, indent) << "0x" << hex(data[29], 2) << '\n';
    print_field("last_i2c", width, indent) << "0x" << hex(data[30], 2) << " (" << status_name(data[30]) << ")\n";
    print_field("refclk_hz", width, indent) << u32(31) << '\n';

    print_section("isp", 2);
    print_field("rst", width, indent) << "0x" << hex(data[35], 2) << '\n';
    print_field("i2c", width, indent) << "0x" << hex(data[36], 2) << '\n';
    print_field("drv_ready", width, indent) << "0x" << hex(data[37], 2) << '\n';
    print_field("last", width, indent) << "0x" << hex(data[38], 2) << " (" << status_name(data[38]) << ")\n";
    print_field("model", width, indent) << isp_model_name(data[39]) << '\n';
    print_field("name", width, indent) << ascii_field(data, 40, 16) << '\n';

    print_section("imu", 2);
    print_field("irq", width, indent) << "0x" << hex(data[64], 2) << '\n';
    print_field("cs_accel", width, indent) << "0x" << hex(data[65], 2) << '\n';
    print_field("cs_gyro", width, indent) << "0x" << hex(data[66], 2) << '\n';
    print_field("spi", width, indent) << "0x" << hex(data[67], 2) << '\n';
    print_field("sample_count", width, indent) << "0x" << hex(data[68], 2) << '\n';
    print_field("last", width, indent) << "0x" << hex(data[69], 2) << " (" << status_name(data[69]) << ")\n";
    print_field("valid_flags", width, indent) << "0x" << hex(data[70], 2) << '\n';
    print_field("timestamp_us", width, indent) << u64(71) << '\n';
    print_field("sensor_time", width, indent) << u32(79) << '\n';
    print_field("temp_raw", width, indent) << static_cast<std::int32_t>(u32(83)) << '\n';
    print_field("accel_lsb_per_ms2", width, indent) << std::setprecision(9) << read_float(data, 87) << '\n';
    print_field("gyro_lsb_per_rads", width, indent) << read_float(data, 91) << '\n';
    print_field("accel_ms2", width, indent) << read_float(data, 95) << ' ' << read_float(data, 99) << ' '
                                            << read_float(data, 103) << '\n';
    print_field("gyro_rad_s", width, indent) << read_float(data, 107) << ' ' << read_float(data, 111) << ' '
                                             << read_float(data, 115) << '\n';

    print_section("temp", 2);
    print_field("i2c", width, indent) << "0x" << hex(data[119], 2) << '\n';
    print_field("last", width, indent) << "0x" << hex(data[120], 2) << " (" << status_name(data[120]) << ")\n";
    print_field("valid_flags", width, indent) << "0x" << hex(data[121], 2) << '\n';
    print_field("left_centi_c", width, indent) << read_s16(data, 122) << '\n';
    print_field("right_centi_c", width, indent) << read_s16(data, 124) << '\n';
    print_field("laser_centi_c", width, indent) << read_s16(data, 126) << '\n';
    print_field("laser2_centi_c", width, indent) << read_s16(data, 128) << '\n';

    print_section("heap", 2);
    print_field("avail", width, indent) << u32(130) << " bytes\n";
    print_field("largest", width, indent) << u32(134) << " bytes\n";
    print_field("smallest", width, indent) << u32(138) << " bytes\n";
    print_field("free_blocks", width, indent) << u32(142) << '\n';
    print_field("min_ever_free", width, indent) << u32(146) << " bytes\n";
    print_field("allocs", width, indent) << u32(150) << '\n';
    print_field("frees", width, indent) << u32(154) << '\n';
}

std::uint8_t BasicCommandGroup::temperature_channel(const std::string &value) const {
    if (value == "0" || value == "left")
        return 1;
    if (value == "1" || value == "right")
        return 2;
    if (value == "2" || value == "laser")
        return 4;
    if (value == "3" || value == "laser2")
        return 7;
    throw std::invalid_argument("invalid temperature channel: " + value);
}

std::uint8_t BasicCommandGroup::binary_value(const std::string &value, const std::string &command) const {
    if (value == "0")
        return 0;
    if (value == "1")
        return 1;
    throw std::invalid_argument(command + " requires <0|1>");
}

std::uint8_t BasicCommandGroup::pps_mode(const std::string &value) const {
    if (value == "0" || value == "input")
        return 0;
    if (value == "1" || value == "output")
        return 1;
    throw std::invalid_argument("invalid PPS mode: " + value);
}

std::uint8_t BasicCommandGroup::pps_period(const std::string &value) const {
    if (value == "0" || value == "1s")
        return 0;
    if (value == "1" || value == "1ms")
        return 1;
    if (value == "2" || value == "1us")
        return 2;
    throw std::invalid_argument("invalid PPS period: " + value);
}

bool BasicCommandGroup::try_run(std::string_view name_view, const std::vector<std::string> &args) {
    const std::string name(name_view);
    if (name == "probe") {
        require_args(args, 0, "probe takes no arguments");
        const auto data = context_.client.raw_read(0, 6);
        print_probe(data);
        return true;
    }
    if (name == "raw-read") {
        require_args(args, 2, "raw-read requires <reg> <len>");
        const auto length = parse_decimal_or_hex_u32(args[1]);
        if (length == 0 || length > 0xffff)
            throw std::invalid_argument("raw-read length out of range");
        print_section("raw-read");
        print_field("data") << hex_bytes(context_.client.raw_read(parse_byte(args[0]), length)) << '\n';
        return true;
    }
    if (name == "raw-write") {
        if (args.size() < 2)
            throw std::invalid_argument("raw-write requires <reg> <byte...>");
        context_.client.raw_write(parse_byte(args[0]), parse_byte_list(std::span<const std::string>(args).subspan(1)));
        return true;
    }
    if (name == "send") {
        if (args.size() < 1)
            throw std::invalid_argument("send requires <opcode> [payload]");
        execute(parse_byte(args[0]), parse_byte_list(std::span<const std::string>(args).subspan(1)));
        return true;
    }
    if (name == "ping") {
        execute(GMSL_COMMAND_PING, parse_byte_list(std::span<const std::string>(args).subspan(0)));
        return true;
    }
    if (name == "ping-random") {
        require_args(args, 1, name + " requires <size>");
        const auto size = parse_decimal_or_hex_u32(args[0]);
        auto payload = std::vector<std::uint8_t>(size);
        for (auto& byte : payload) {
            byte = static_cast<std::uint8_t>(rand() % 256);
        }
        execute(GMSL_COMMAND_PING, payload);
        return true;
    }
    if (name == "get-status") {
        require_args(args, 0, name + " takes no arguments");
        const auto response = execute(GMSL_COMMAND_GET_STATUS);
        if (response.status == 0)
            print_status(response.payload);
        return true;
    }
    if (name == "mcu-reset") {
        require_args(args, 0, name + " takes no arguments");
        execute(GMSL_COMMAND_MCU_RESET);
        return true;
    }
    if (name == "set-reset") {
        std::uint8_t value = 1;
        require_args(args, 1, name + " requires <0|1>");
        value = binary_value(args[0], name);
        execute(GMSL_COMMAND_SET_TIMER_OUT_RESET, {value});
        return true;
    }
    if (name == "set-pps-mode") {
        require_args(args, 1, "set-pps-mode requires <enable>");
        auto payload = std::vector<std::uint8_t>{static_cast<std::uint8_t>(binary_value(args[0], name))};
        execute(GMSL_COMMAND_SET_PPS_MODE, payload);
        return true;
    }
    if (name == "get-timestamp") {
        require_args(args, 0, name + " takes no arguments");
        const auto response = execute(GMSL_COMMAND_GET_TIMESTAMP);
        if (response.status != 0)
            return true;
        if (response.payload.size() == 8) {
            print_section("get-timestamp");
            print_field("timestamp_us", 12) << read_le64(response.payload) << '\n';
        }
        return true;
    }
    if (name == "set-timestamp") {
        require_args(args, 1, "set-timestamp requires <u64_dec>");
        execute(GMSL_COMMAND_SET_TIMESTAMP, le64(std::stoull(args[0])));
        return true;
    }
    if (name == "time-sync-exchange") {
        require_args(args, 0, name + " takes no arguments");
        const auto response = execute(GMSL_COMMAND_TIME_SYNC_EXCHANGE);
        if (response.status != 0)
            return true;
        if (response.payload.size() != 16)
            throw std::runtime_error("time-sync-exchange response must contain 16 bytes");
        const auto payload = std::span<const std::uint8_t>(response.payload);
        print_section("time-sync-exchange");
        print_field("device_receive_us", 19) << read_le64(payload.first(8)) << '\n';
        print_field("device_response_us", 19) << read_le64(payload.subspan(8)) << '\n';
        return true;
    }
    if (name == "adjust-timestamp") {
        require_args(args, 1, "adjust-timestamp requires <adjustment_i64_us>");
        execute(GMSL_COMMAND_ADJUST_TIMESTAMP,
                le64(static_cast<std::uint64_t>(parse_i64(args[0]))));
        return true;
    }
    if (name == "smooth-set-timestamp") {
        require_args(args, 1, "smooth-set-timestamp requires <timestamp_u64>");
        execute(GMSL_COMMAND_SMOOTH_ADJUST_TIMESTAMP, le64(std::stoull(args[0])));
        return true;
    }
    if (name == "get-fw-version") {
        require_args(args, 0, name + " takes no arguments");
        const auto response = execute(GMSL_COMMAND_GET_FIRMWARE_VERSION);
        if (response.status != 0)
            return true;
        if (!response.payload.empty()) {
            print_section("get-fw-version");
            print_field("version") << ascii_field(response.payload, 0, response.payload.size()) << '\n';
        }
        return true;
    }
    if (name == "imu-read") {
        require_args(args, 1, name + " requires an argument");
        std::uint8_t channel = 0;
        const auto sample_count_value = parse_decimal_or_hex_u32(args[0]);
        if (sample_count_value > 255)
            throw std::invalid_argument("imu sample count out of range");
        channel = static_cast<std::uint8_t>(sample_count_value);
        const auto response = execute(GMSL_COMMAND_IMU_READ_SAMPLES, {channel});
        if (response.status == 0)
            print_imu_samples(response.payload);
        return true;
    }
    if (name == "temp-read") {
        require_args(args, 1, name + " requires an argument");
        std::uint8_t channel = 0;
        channel = temperature_channel(args[0]);
        const auto response = execute(GMSL_COMMAND_TEMP_READ, {channel});
        if (response.status == 0 && response.payload.size() == 2) {
            print_section("temperature");
            print_field("centi_deg_c") << read_s16(response.payload, 0) << '\n';
        }
        return true;
    }
    if (name == "set-auto-userset-slot") {
        require_args(args, 1, name + " requires <slot>");
        execute(GMSL_COMMAND_SET_AUTO_USERSET_SLOT, {userset_slot(args[0])});
        return true;
    }
    if (name == "get-auto-userset-slot") {
        require_args(args, 0, name + " takes no arguments");
        const auto response = execute(GMSL_COMMAND_GET_AUTO_USERSET_SLOT);
        if (response.status != 0)
            return true;
        if (response.payload.size() == 1) {
            print_section("get-auto-userset-slot");
            print_field("auto_slot") << static_cast<unsigned>(response.payload[0]) << '\n';
        }
        return true;
    }
    if (name == "apply-userset") {
        require_args(args, 1, name + " requires <slot>");
        const auto response = execute(GMSL_COMMAND_APPLY_USERSET, {userset_slot(args[0])});
        if (response.status == 0 && response.payload.size() == 1) {
            print_section("apply-userset");
            print_field("slot") << static_cast<unsigned>(response.payload[0]) << '\n';
        }
        return true;
    }
    if (name == "low-power-mode") {
        std::uint8_t value = 1;
        if (!args.empty()) {
            require_args(args, 1, name + " requires <0|1>");
            value = binary_value(args[0], name);
        }
        execute(GMSL_COMMAND_LOW_POWER_MODE, {value});
        return true;
    }
    if (name == "post-processor-raw-write") {
        if (args.empty())
            throw std::invalid_argument("post-processor-raw-write requires <data>");
        execute(GMSL_COMMAND_POST_PROCESSOR_RAW_WRITE, parse_byte_list(args));
        return true;
    }
    if (name == "post-processor-raw-read") {
        require_args(args, 1, name + " requires <size>");
        const auto response = execute(GMSL_COMMAND_POST_PROCESSOR_RAW_READ, le16(parse_u16(args[0])));
        if (response.status != 0)
            return true;
        if (!response.payload.empty()) {
            print_section("post-processor-raw-read");
            print_field("data") << ascii_field(response.payload, 0, response.payload.size()) << '\n';
        }
        return true;
    }
    if (name == "isp-get-version") {
        require_args(args, 0, name + " takes no arguments");
        const auto response = execute(GMSL_COMMAND_ISP_GET_VERSION);
        if (response.status != 0)
            return true;
        if (!response.payload.empty()) {
            print_section("isp-get-version");
            print_field("version") << ascii_field(response.payload, 0, response.payload.size()) << '\n';
        }
        return true;
    }
    if (name == "percipio-debug") {
        if (args.empty())
            throw std::invalid_argument("percipio-debug requires <op>");
        execute(GMSL_COMMAND_PERCIPIO_DEBUG, parse_byte_list(args));
        return true;
    }
    if (name == "percipio-debug-read32" || name == "percipio-debug-write32") {
        const auto count = name == "percipio-debug-read32" ? 1U : 2U;
        require_args(args, count, name + " arguments invalid");
        auto payload = std::vector<std::uint8_t>{static_cast<std::uint8_t>(count)};
        append_to(payload, le32(parse_u32(args[0])));
        if (count == 2)
            append_to(payload, le32(parse_u32(args[1])));
        const auto response = execute(GMSL_COMMAND_PERCIPIO_DEBUG, payload, false, false);
        if (count == 2) {
            print_section("percipio-debug-write32");
            print_field("status") << static_cast<unsigned>(response.status) << '\n';
        } else {
            print_section("percipio-debug-read32");
            print_field("value") << read_le32(response.payload, 0) << '\n';
        }
        return true;
    }
    if (name == "percipio-debug-status-log") {
        require_args(args, 1, "percipio-debug-status-log requires <0|1>");
        execute(GMSL_COMMAND_PERCIPIO_DEBUG, {0xc0, binary_value(args[0], name)});
        return true;
    }
    return false;
}

} // namespace gm86x::detail