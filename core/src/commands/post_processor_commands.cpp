#include "post_processor_commands.hpp"

#include "bytes.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <thread>

namespace gm86x::detail {

void PostProcessorCommandGroup::write_register(std::uint8_t addr, std::vector<std::uint8_t> data) {
    std::vector<std::uint8_t> payload;
    payload.push_back(addr);
    payload.insert(payload.end(), data.begin(), data.end());
    execute(GMSL_COMMAND_POST_PROCESSOR_RAW_WRITE, payload, false, false);
}

std::vector<std::uint8_t> PostProcessorCommandGroup::read_register(std::uint8_t addr, std::uint8_t size) {
    std::vector<std::uint8_t> payload{addr};
    execute(GMSL_COMMAND_POST_PROCESSOR_RAW_WRITE, payload, false, false);

    payload = {size, 0x00};
    return execute(GMSL_COMMAND_POST_PROCESSOR_RAW_READ, payload, false, false).payload;
}

void PostProcessorCommandGroup::write_command(std::uint8_t command, std::uint8_t sequence,
                                               std::vector<std::uint8_t> data) {
    std::vector<std::uint8_t> payload{sequence, command, static_cast<std::uint8_t>(data.size())};
    payload.insert(payload.end(), data.begin(), data.end());
    write_register(0x10, payload);
}

bool PostProcessorCommandGroup::command_is_ready() {
    return read_register(0x19, 1)[0] == 0x5a;
}

std::vector<std::uint8_t> PostProcessorCommandGroup::read_response(std::uint8_t command, std::uint8_t sequence,
                                                                    std::uint8_t &size, std::uint8_t &status) {
    std::vector<std::uint8_t> response = read_register(0x14, 4);
    if (response.size() != 4)
        throw std::runtime_error("post-processor response header must contain 4 bytes");
    if (sequence != response[0])
        throw std::runtime_error("sequence mismatch");
    if (command != response[1])
        throw std::runtime_error("command mismatch");
    status = response[2];
    size = response[3];

    auto payload = read_register(0x18, size);
    if (payload.size() != size)
        throw std::runtime_error("post-processor response payload length mismatch");
    return payload;
}

PostProcessorCommandGroup::CommandResponse PostProcessorCommandGroup::execute_command(
    std::uint8_t command, std::vector<std::uint8_t> payload) {
    if (payload.size() > 0xfb)
        throw std::invalid_argument("post-processor payload exceeds 251 bytes");

    const auto sequence = next_sequence_++;
    write_command(command, sequence, std::move(payload));

    const int timeout_ms = command == 0x1a ? 30000 : command == 0x19 ? 10000 : 1000;
    for (int timeout = timeout_ms; timeout > 0; --timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!command_is_ready())
            continue;
        std::uint8_t size = 0;
        std::uint8_t status = 0;
        auto response = read_response(command, sequence, size, status);
        write_register(0x19, {0x00});
        return {status, std::move(response)};
    }
    throw std::runtime_error("post-processor command timed out");
}

void PostProcessorCommandGroup::require_payload_size(std::span<const std::uint8_t> payload, std::size_t size,
                                                      const std::string &command) {
    if (payload.size() != size)
        throw std::runtime_error(command + " response must contain " + std::to_string(size) + " bytes");
}

void PostProcessorCommandGroup::print_command_response(std::string_view command,
                                                        const CommandResponse &response) const {
    print_section("post-processor-cmd " + std::string(command));
    print_field("status") << "0x" << hex(response.status, 2) << " (" << post_processor_status_name(response.status)
                           << ")\n";
    if (!response.payload.empty()) {
        print_field("payload") << response.payload.size() << " bytes\n";
        print_hex_dump(response.payload);
    }

}

const char *PostProcessorCommandGroup::usb_speed_name(std::uint8_t speed) {
    switch (speed) {
    case 0:
        return "UNKNOWN";
    case 1:
        return "FS";
    case 2:
        return "HS";
    case 3:
        return "SS";
    default:
        return "UNKNOWN";
    }
}

const char *PostProcessorCommandGroup::stream_state_name(std::uint8_t state) {
    switch (state) {
    case 0:
        return "CLOSED";
    case 1:
        return "OPENING";
    case 2:
        return "OPEN";
    case 3:
        return "CLOSING";
    case 4:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

const char *PostProcessorCommandGroup::stream_action_name(std::uint8_t action) {
    switch (action) {
    case 0:
        return "CLOSE";
    case 1:
        return "OPEN";
    default:
        return "UNKNOWN";
    }
}

const char *PostProcessorCommandGroup::post_processor_status_name(std::uint8_t status) {
    switch (status) {
    case 0x00:
        return "OK";
    case 0x01:
        return "BAD_COMMAND";
    case 0x02:
        return "BAD_LENGTH";
    case 0x03:
        return "BAD_PARAM";
    case 0x04:
        return "BUSY";
    case 0x05:
        return "I2C_ERROR";
    case 0x06:
        return "NOT_IMPLEMENTED";
    case 0x07:
        return "NOT_READY";
    case 0x08:
        return "STALE_TRANSACTION";
    case 0x09:
        return "DENIED";
    case 0x0a:
        return "STATE_CONFLICT";
    case 0x0b:
        return "TIMEOUT";
    case 0x0c:
        return "INTERNAL_ERROR";
    default:
        return "UNKNOWN";
    }
}

std::string PostProcessorCommandGroup::stream_flags_name(std::uint8_t flags) {
    std::string names;
    const auto append = [&names](const char *name) {
        if (!names.empty())
            names += "|";
        names += name;
    };
    if (flags & 0x01)
        append("CONFIGURED");
    if (flags & 0x02)
        append("COMMANDED_OPEN");
    if (flags & 0x04)
        append("PATH_ACTIVE");
    if (flags & 0x08)
        append("ACTION_BUSY");
    if (flags & 0x10)
        append("MIPI_LOCK");
    if (flags & 0x20)
        append("EP_READY");
    if (flags & 0x40)
        append("ERROR");
    if (flags & 0x80)
        append("FORCED_CLOSED");
    return names;
}

void PostProcessorCommandGroup::print_stream_status(std::string_view name, std::span<const std::uint8_t> data) const {
    constexpr int width = 17;
    constexpr int indent = 4;

    print_section(name, 2);
    const auto state = data[0];
    const auto flags = data[1];
    const auto last_error = data[2];
    const auto format = data[3];
    const auto frame_width = read_le_u16(data, 4);
    const auto height = read_le_u16(data, 6);
    const auto interval = read_le32(data.subspan(8));
    const auto control_gen = read_le_u16(data, 12);
    const auto last_sequence = data[14];
    const auto last_action = data[15];
    const auto mipi_error_count = read_le32(data.subspan(16));
    const auto dropped_frame_count = read_le32(data.subspan(20));

    print_field("state", width, indent) << static_cast<unsigned>(state) << " (" << stream_state_name(state) << ")\n";
    const auto flags_name = stream_flags_name(flags);
    print_field("flags", width, indent) << "0x" << hex(flags, 2)
                                        << (flags_name.empty() ? "" : " (" + flags_name + ")") << '\n';
    print_field("last_error", width, indent) << "0x" << hex(last_error, 2) << " ("
                                             << post_processor_status_name(last_error) << ")\n";
    print_field("format", width, indent) << static_cast<unsigned>(format) << '\n';
    print_field("width", width, indent) << frame_width << '\n';
    print_field("height", width, indent) << height << '\n';
    print_field("interval", width, indent) << interval << " (fps=" << std::fixed << std::setprecision(3)
                                           << (interval == 0 ? 0.0 : 10000000.0 / interval) << std::defaultfloat
                                           << ")\n";
    print_field("control_gen", width, indent) << control_gen << '\n';
    print_field("last_sequence", width, indent) << "0x" << hex(last_sequence, 2) << '\n';
    print_field("last_action", width, indent) << static_cast<unsigned>(last_action) << " ("
                                              << stream_action_name(last_action) << ")\n";
    print_field("mipi_errors", width, indent) << mipi_error_count << '\n';
    print_field("dropped_frames", width, indent) << dropped_frame_count << '\n';
}

void PostProcessorCommandGroup::print_get_status(std::span<const std::uint8_t> payload) const {
    constexpr int width = 17;

    print_section("get-status");
    print_field("protocol_version", width) << static_cast<unsigned>(payload[0]) << '\n';
    print_field("firmware", width) << static_cast<unsigned>(payload[1]) << '.' << static_cast<unsigned>(payload[2])
                                   << '\n';
    print_field("boot_id", width) << "0x" << hex(read_le32(payload.subspan(4)), 8) << '\n';
    print_field("uptime_ms", width) << read_le32(payload.subspan(8)) << '\n';
    print_field("usb_speed", width) << static_cast<unsigned>(payload[12]) << " (" << usb_speed_name(payload[12])
                                    << ")\n";
    print_field("usb_dev_state", width) << static_cast<unsigned>(payload[13]) << '\n';
    print_field("stream_count", width) << static_cast<unsigned>(payload[14]) << '\n';

    constexpr std::size_t stream_struct_size = 24;
    constexpr std::array<std::string_view, 2> stream_names{"rgb", "depth_ir"};
    for (std::size_t index = 0; index < stream_names.size(); ++index) {
        const auto base = 15 + index * stream_struct_size;
        if (base + stream_struct_size > payload.size())
            break;
        print_stream_status(stream_names[index], payload.subspan(base, stream_struct_size));
    }
}

std::string PostProcessorCommandGroup::usb_speed_caps_name(std::uint8_t caps) {
    struct Entry {
        std::uint8_t bit;
        const char *name;
    };
    constexpr Entry entries[] = {{0x01, "FS"}, {0x02, "HS"}, {0x04, "SS"}};
    std::string names;
    for (const auto &entry : entries) {
        if (!(caps & entry.bit))
            continue;
        if (!names.empty())
            names += "|";
        names += entry.name;
    }
    return names;
}

std::string PostProcessorCommandGroup::command_mask_name(std::uint32_t mask) {
    struct Entry {
        std::uint8_t opcode;
        const char *name;
    };
    constexpr Entry entries[] = {
        {0x01, "PING"},          {0x02, "GET_STATUS"},       {0x03, "GET_CAPS"},
        {0x11, "STREAM_CONTROL"}, {0x12, "GET_STREAM_STATE"}, {0x13, "RESET_PIPELINE"},
        {0x15, "SET_TEST_PATTERN"}, {0x16, "GET_STREAM_CAPS"}, {0x17, "GET_STATISTICS"},
        {0x18, "CLEAR_STATISTICS"}, {0x19, "RESET_USB"},       {0x1a, "OTA_CONTROL"},
    };
    std::string names;
    for (const auto &entry : entries) {
        if (!(mask & (1u << entry.opcode)))
            continue;
        if (!names.empty())
            names += ", ";
        names += entry.name;
    }
    return names;
}

void PostProcessorCommandGroup::print_get_caps(std::span<const std::uint8_t> payload) const {
    constexpr int width = 17;

    print_section("get-caps");
    print_field("protocol_version", width) << static_cast<unsigned>(payload[0]) << '\n';
    print_field("max_payload", width) << static_cast<unsigned>(payload[1]) << '\n';
    print_field("stream_count", width) << static_cast<unsigned>(payload[2]) << '\n';
    const auto speed_caps_name = usb_speed_caps_name(payload[3]);
    print_field("usb_speed_caps", width) << "0x" << hex(payload[3], 2)
                                        << (speed_caps_name.empty() ? "" : " (" + speed_caps_name + ")") << '\n';
    const auto mask = read_le32(payload.subspan(4));
    const auto mask_name = command_mask_name(mask);
    print_field("command_mask", width) << "0x" << hex(mask, 8)
                                       << (mask_name.empty() ? "" : ": " + mask_name) << '\n';
}

void PostProcessorCommandGroup::print_get_stream_caps(std::span<const std::uint8_t> payload) const {
    constexpr int width = 20;
    print_section("get-stream-caps");
    print_field("caps_version", width) << static_cast<unsigned>(payload[0]) << '\n';
    print_field("stream_count", width) << static_cast<unsigned>(payload[1]) << '\n';
    print_field("format_record_count", width) << static_cast<unsigned>(payload[2]) << '\n';
    print_field("frame_record_count", width) << static_cast<unsigned>(payload[3]) << '\n';
    if (payload[0] != 1 || payload[1] != 2 || payload[2] != 4 || payload[3] != 4)
        throw std::runtime_error("get-stream-caps contains unsupported record counts");

    constexpr std::size_t stream_offset = 4;
    constexpr std::size_t stream_record_size = 4;
    for (std::size_t index = 0; index < payload[1]; ++index) {
        const auto offset = stream_offset + index * stream_record_size;
        print_section("stream " + std::to_string(index), 2);
        print_field("stream_id", width, 4) << static_cast<unsigned>(payload[offset]) << '\n';
        print_field("format_count", width, 4) << static_cast<unsigned>(payload[offset + 1]) << '\n';
        print_field("default_format_index", width, 4) << static_cast<unsigned>(payload[offset + 2]) << '\n';
        print_field("flags", width, 4) << "0x" << hex(payload[offset + 3], 2) << '\n';
    }

    constexpr std::size_t formats_offset = 12;
    constexpr std::size_t format_record_size = 11;
    constexpr std::array<const char *, 4> source_names{"Color", "IR1", "Depth", "IR2"};
    for (std::size_t index = 0; index < payload[2]; ++index) {
        const auto offset = formats_offset + index * format_record_size;
        const auto source = payload[offset + 2];
        std::string fourcc;
        for (std::size_t byte = 0; byte < 4; ++byte)
            fourcc.push_back(static_cast<char>(payload[offset + 3 + byte]));
        print_section("format " + std::to_string(index), 2);
        print_field("stream_id", width, 4) << static_cast<unsigned>(payload[offset]) << '\n';
        print_field("format_index", width, 4) << static_cast<unsigned>(payload[offset + 1]) << '\n';
        print_field("source_type", width, 4) << static_cast<unsigned>(source)
                                              << (source < source_names.size() ? " (" + std::string(source_names[source]) + ")" : "")
                                              << '\n';
        print_field("fourcc", width, 4) << fourcc << '\n';
        print_field("bits_per_pixel", width, 4) << static_cast<unsigned>(payload[offset + 7]) << '\n';
        print_field("frame_count", width, 4) << static_cast<unsigned>(payload[offset + 8]) << '\n';
        print_field("default_frame_index", width, 4) << static_cast<unsigned>(payload[offset + 9]) << '\n';
        print_field("flags", width, 4) << "0x" << hex(payload[offset + 10], 2) << '\n';
    }

    constexpr std::size_t frames_offset = 56;
    constexpr std::size_t frame_record_size = 24;
    for (std::size_t index = 0; index < payload[3]; ++index) {
        const auto offset = frames_offset + index * frame_record_size;
        print_section("frame " + std::to_string(index), 2);
        print_field("stream_id", width, 4) << static_cast<unsigned>(payload[offset]) << '\n';
        print_field("format_index", width, 4) << static_cast<unsigned>(payload[offset + 1]) << '\n';
        print_field("frame_index", width, 4) << static_cast<unsigned>(payload[offset + 2]) << '\n';
        print_field("frame_interval_type", width, 4) << static_cast<unsigned>(payload[offset + 3]) << '\n';
        print_field("width", width, 4) << read_le_u16(payload, offset + 4) << '\n';
        print_field("height", width, 4) << read_le_u16(payload, offset + 6) << '\n';
        print_field("default_interval", width, 4) << read_le32(payload.subspan(offset + 8)) << '\n';
        print_field("min_interval", width, 4) << read_le32(payload.subspan(offset + 12)) << '\n';
        print_field("max_interval", width, 4) << read_le32(payload.subspan(offset + 16)) << '\n';
        print_field("interval_step", width, 4) << read_le32(payload.subspan(offset + 20)) << '\n';
    }
}

bool PostProcessorCommandGroup::try_run(std::string_view name, const std::vector<std::string> &args) {
    if (name == "post-processor-raw-write") {
        if (args.empty())
            throw std::invalid_argument("post-processor-raw-write requires <data>");
        execute(GMSL_COMMAND_POST_PROCESSOR_RAW_WRITE, parse_byte_list(args));
        return true;
    }
    if (name == "post-processor-raw-read") {
        require_args(args, 1, "post-processor-raw-read requires <size>");
        const auto response = execute(GMSL_COMMAND_POST_PROCESSOR_RAW_READ, le16(parse_u16(args[0])));
        if (response.status == 0 && !response.payload.empty()) {
            print_section("post-processor-raw-read");
            print_field("payload") << response.payload.size() << " bytes\n";
            print_hex_dump(response.payload);
        }
        return true;
    }
    if (name == "post-processor-register-write") {
        require_args(args, 2, "post-processor-register-write requires <register> <value>");
        auto reg = parse_byte(args[0]);
        auto value = parse_byte_list(std::span<const std::string>(args).subspan(1));
        write_register(reg, value);
        return true;
    }
    if (name == "post-processor-register-read") {
        require_args(args, 2, "post-processor-register-read requires <register> <size>");
        auto reg = parse_byte(args[0]);
        auto size = parse_byte(args[1]);
        auto response = read_register(reg, size);
        print_section("post-processor-register-read");
        print_field("payload") << response.size() << " bytes\n";
        print_hex_dump(response);
        return true;
    }
    if (name == "post-processor-cmd") {
        if (args.empty())
            throw std::invalid_argument("post-processor-cmd requires cmd <arg>");
        if (args[0] == "ping") {
            const auto payload = parse_byte_list(std::span<const std::string>(args).subspan(1));
            const auto response = execute_command(0x01, payload);
            print_command_response("ping", response);
            if (response.status == 0)
                require_payload_size(response.payload, payload.size(), "ping");
            return true;
        }
        if (args[0] == "get-status") {
            const auto response = execute_command(0x02, {});
            print_command_response("get-status", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 63, "get-status");
                print_get_status(response.payload);
            }
            return true;
        }
        if (args[0] == "get-caps") {
            const auto response = execute_command(0x03, {});
            print_command_response("get-caps", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 8, "get-caps");
                print_get_caps(response.payload);
            }
            return true;
        }
        if (args[0] == "stream-control") {
            require_args(args, 8, "post-processor-cmd stream-control requires <stream_id> <action> <format> <width> <height> <frame_interval> <flags>");
            auto payload = std::vector<std::uint8_t>{parse_byte(args[1]), parse_byte(args[2]), parse_byte(args[3])};
            append_to(payload, le16(parse_u16(args[4])));
            append_to(payload, le16(parse_u16(args[5])));
            append_to(payload, le32(parse_u32(args[6])));
            payload.push_back(parse_byte(args[7]));
            const auto response = execute_command(0x11, std::move(payload));
            print_command_response("stream-control", response);
            if (response.status == 0) {
                if (response.payload.size() != 4 && response.payload.size() != 6)
                    throw std::runtime_error("stream-control response must contain 4 or 6 bytes");
                const auto data = std::span<const std::uint8_t>(response.payload);
                print_field("stream_id") << static_cast<unsigned>(data[0]) << '\n';
                print_field("action") << static_cast<unsigned>(data[1]) << " (" << stream_action_name(data[1]) << ")\n";
                print_field("result_state") << static_cast<unsigned>(data[2]) << '\n';
                print_field("stream_flags") << "0x" << hex(data[3], 2) << '\n';
            }
            return true;
        }
        if (args[0] == "get-stream-state") {
            require_args(args, 2, "post-processor-cmd get-stream-state requires <stream_id>");
            const auto response = execute_command(0x12, {parse_byte(args[1])});
            print_command_response("get-stream-state", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 24, "get-stream-state");
                const auto data = std::span<const std::uint8_t>(response.payload);
                print_stream_status("stream-status", data);
            }
            return true;
        }
        if (args[0] == "reset-pipeline") {
            require_args(args, 2, "post-processor-cmd reset-pipeline requires <stream_id>");
            auto payload = std::vector<std::uint8_t>{parse_byte(args[1])};
            const auto response = execute_command(0x13, std::move(payload));
            print_command_response("reset-pipeline", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 2, "reset-pipeline");
                print_field("stream_id") << static_cast<unsigned>(response.payload[0]) << '\n';
                print_field("state") << static_cast<unsigned>(response.payload[1]) << " ("
                                      << stream_state_name(response.payload[1]) << ")\n";
            }
            return true;
        }
        if (args[0] == "set-test-pattern") {
            require_args(args, 3, "post-processor-cmd set-test-pattern requires <stream_id> <enable>");
            auto payload = std::vector<std::uint8_t>{parse_byte(args[1]), parse_byte(args[2])};
            const auto response = execute_command(0x15, std::move(payload));
            print_command_response("set-test-pattern", response);
            if (response.status == 0)
                require_payload_size(response.payload, 2, "set-test-pattern");
            return true;
        }
        if (args[0] == "get-stream-caps") {
            require_args(args, 1, "post-processor-cmd get-stream-caps takes no arguments");
            const auto response = execute_command(0x16, {});
            print_command_response("get-stream-caps", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 152, "get-stream-caps");
                print_get_stream_caps(response.payload);
            }
            return true;
        }
        if (args[0] == "get-statistics") {
            require_args(args, 1, "post-processor-cmd get-statistics takes no arguments");
            const auto response = execute_command(0x17, {});
            print_command_response("get-statistics", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 24, "get-statistics");
                const auto data = std::span<const std::uint8_t>(response.payload);
                print_field("request_count") << read_le32(data) << '\n';
                print_field("response_count") << read_le32(data.subspan(4)) << '\n';
                print_field("duplicate_count") << read_le32(data.subspan(8)) << '\n';
                print_field("bad_frame_count") << read_le32(data.subspan(12)) << '\n';
                print_field("busy_count") << read_le32(data.subspan(16)) << '\n';
                print_field("i2c_error_count") << read_le32(data.subspan(20)) << '\n';
            }
            return true;
        }
        if (args[0] == "clear-statistics") {
            require_args(args, 1, "post-processor-cmd clear-statistics takes no arguments");
            const auto response = execute_command(0x18, {});
            print_command_response("clear-statistics", response);
            if (response.status == 0)
                require_payload_size(response.payload, 0, "clear-statistics");
            return true;
        }
        if (args[0] == "reset-usb") {
            require_args(args, 1, "post-processor-cmd reset-usb takes no arguments");
            const auto response = execute_command(0x19, {});
            print_command_response("reset-usb", response);
            if (response.status == 0)
                require_payload_size(response.payload, 0, "reset-usb");
            return true;
        }
        if (args[0] == "ota-control") {
            if (args.size() < 2)
                throw std::invalid_argument("post-processor-cmd ota-control requires <ota_payload_byte...>");
            const auto payload = parse_byte_list(std::span<const std::string>(args).subspan(1));
            const auto response = execute_command(0x1a, payload);
            print_command_response("ota-control", response);
            if (response.payload.size() == 24) {
                const auto data = std::span<const std::uint8_t>(response.payload);
                print_field("ota_version") << static_cast<unsigned>(data[0]) << '\n';
                print_field("state") << static_cast<unsigned>(data[1]) << '\n';
                print_field("last_op") << "0x" << hex(data[2], 2) << '\n';
                print_field("last_result") << static_cast<unsigned>(data[3]) << '\n';
                print_field("session_id") << read_le32(data.subspan(4)) << '\n';
                print_field("next_offset") << read_le32(data.subspan(8)) << '\n';
                print_field("image_size") << read_le32(data.subspan(12)) << '\n';
                print_field("running_slot") << static_cast<unsigned>(data[16]) << '\n';
                print_field("target_slot") << static_cast<unsigned>(data[17]) << '\n';
                print_field("owner") << static_cast<unsigned>(data[18]) << '\n';
                print_field("flags") << "0x" << hex(data[19], 2) << '\n';
                print_field("bootinfo_generation") << read_le32(data.subspan(20)) << '\n';
            }
            return true;
        }
        return false;
    }
    return false;
}

} // namespace gm86x::detail