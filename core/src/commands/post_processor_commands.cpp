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
    if (next_sequence_ == 0)
        next_sequence_ = 1;
    write_command(command, sequence, std::move(payload));

    for (int timeout = 1000; timeout > 0; --timeout) {
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
    print_field("status") << static_cast<unsigned>(response.status) << '\n';
    if (!response.payload.empty()) {
        print_field("payload") << response.payload.size() << " bytes\n";
        print_hex_dump(response.payload);
    }

}

const char *PostProcessorCommandGroup::usb_speed_name(std::uint8_t speed) {
    switch (speed) {
    case 0:
        return "LS";
    case 1:
        return "FS";
    case 2:
        return "HS";
    case 3:
        return "SS";
    case 4:
        return "SSP";
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
        return "STREAMING";
    case 3:
        return "STOPPING";
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

std::string PostProcessorCommandGroup::stream_flags_name(std::uint8_t flags) {
    std::string names;
    const auto append = [&names](const char *name) {
        if (!names.empty())
            names += "|";
        names += name;
    };
    if (flags & 0x20)
        append("EP_READY");
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
    const auto size = read_le_u16(data, 4);
    const auto interval = read_le_u16(data, 6);
    const auto control_gen = read_le32(data.subspan(8));
    const auto last_sequence = data[12];
    const auto last_action = data[13];
    const auto mipi_error_count = read_le32(data.subspan(14));
    const auto dropped_frame_count = read_le32(data.subspan(18));

    print_field("state", width, indent) << static_cast<unsigned>(state) << " (" << stream_state_name(state) << ")\n";
    const auto flags_name = stream_flags_name(flags);
    print_field("flags", width, indent) << "0x" << hex(flags, 2)
                                        << (flags_name.empty() ? "" : " (" + flags_name + ")") << '\n';
    print_field("last_error", width, indent) << "0x" << hex(last_error, 2) << " (" << status_name(last_error)
                                             << ")\n";
    print_field("format", width, indent) << static_cast<unsigned>(format) << '\n';
    print_field("size", width, indent) << "0x" << hex(size, 4) << '\n';
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

void PostProcessorCommandGroup::print_get_status(std::span<const std::uint8_t> payload) const {    constexpr int width = 17;

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
    constexpr Entry entries[] = {{0x01, "FS"}, {0x02, "HS"}, {0x04, "SS"}, {0x08, "SSP"}};
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
        {0x01, "PING"},         {0x02, "GET_STATUS"},        {0x03, "GET_CAPS"},
        {0x10, "SET_PIPELINE_CONFIG"}, {0x11, "STREAM_CONTROL"},  {0x12, "GET_STREAM_STATE"},
        {0x13, "RESET_PIPELINE"},      {0x14, "GET_ACTION_STATUS"}, {0x15, "SET_TEST_PATTERN"},
        {0x16, "GET_STREAM_CAPS"},     {0x17, "GET_STATISTICS"},    {0x18, "CLEAR_STATISTICS"},
        {0x19, "RESET_USB"},           {0x1a, "OTA_CONTROL"},
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
            print_field("data") << ascii(response.payload) << '\n';
        }
        return true;
    }
    if (name == "post-processor-cmd") {
        if (args.empty())
            throw std::invalid_argument("post-processor-cmd requires cmd <arg>");
        if (args[0] == "ping") {
            const auto response = execute_command(0x01, parse_byte_list(std::span<const std::string>(args).subspan(1)));
            print_command_response("ping", response);
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
        if (args[0] == "set-pipeline-config") {
            require_args(args, 5, "post-processor-cmd set-pipeline-config requires <control_gen> <pipeline_mask> <config_id> <config_flags>");
            auto payload = le32(parse_u32(args[1]));
            payload.push_back(parse_byte(args[2]));
            append_to(payload, le16(parse_u16(args[3])));
            payload.push_back(parse_byte(args[4]));
            const auto response = execute_command(0x10, std::move(payload));
            print_command_response("set-pipeline-config", response);
            return true;
        }
        if (args[0] == "stream-control") {
            require_args(args, 4, "post-processor-cmd stream-control requires <control_gen> <stream_id> <enable>");
            auto payload = le32(parse_u32(args[1]));
            payload.push_back(parse_byte(args[2]));
            payload.push_back(parse_byte(args[3]));
            const auto response = execute_command(0x11, std::move(payload));
            print_command_response("stream-control", response);
            return true;
        }
        if (args[0] == "get-stream-state") {
            require_args(args, 2, "post-processor-cmd get-stream-state requires <stream_id>");
            const auto response = execute_command(0x12, {parse_byte(args[1])});
            print_command_response("get-stream-state", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 8, "get-stream-state");
                const auto data = std::span<const std::uint8_t>(response.payload);
                print_field("stream_id") << static_cast<unsigned>(data[0]) << '\n';
                print_field("command_enable") << static_cast<unsigned>(data[1]) << '\n';
                print_field("actual_enable") << static_cast<unsigned>(data[2]) << '\n';
                print_field("action_state") << static_cast<unsigned>(data[3]) << '\n';
                print_field("action_id") << read_le32(data.subspan(4)) << '\n';
            }
            return true;
        }
        if (args[0] == "reset-pipeline") {
            require_args(args, 3, "post-processor-cmd reset-pipeline requires <control_gen> <pipeline_mask>");
            auto payload = le32(parse_u32(args[1]));
            payload.push_back(parse_byte(args[2]));
            const auto response = execute_command(0x13, std::move(payload));
            print_command_response("reset-pipeline", response);
            return true;
        }
        if (args[0] == "get-action-status") {
            require_args(args, 2, "post-processor-cmd get-action-status requires <action_id>");
            const auto response = execute_command(0x14, le32(parse_u32(args[1])));
            print_command_response("get-action-status", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 6, "get-action-status");
                const auto data = std::span<const std::uint8_t>(response.payload);
                print_field("action_id") << read_le32(data) << '\n';
                print_field("action_state") << static_cast<unsigned>(data[4]) << '\n';
                print_field("result_status") << static_cast<unsigned>(data[5]) << '\n';
            }
            return true;
        }
        if (args[0] == "set-test-pattern") {
            require_args(args, 5, "post-processor-cmd set-test-pattern requires <control_gen> <stream_id> <enable> <pattern_id>");
            auto payload = le32(parse_u32(args[1]));
            payload.push_back(parse_byte(args[2]));
            payload.push_back(parse_byte(args[3]));
            payload.push_back(parse_byte(args[4]));
            const auto response = execute_command(0x15, std::move(payload));
            print_command_response("set-test-pattern", response);
            return true;
        }
        if (args[0] == "get-statistics") {
            require_args(args, 2, "post-processor-cmd get-statistics requires <source_mask>");
            const auto response = execute_command(0x17, {parse_byte(args[1])});
            print_command_response("get-statistics", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 17, "get-statistics");
                const auto data = std::span<const std::uint8_t>(response.payload);
                print_field("source_mask") << static_cast<unsigned>(data[0]) << '\n';
                print_field("mipi_count") << read_le32(data.subspan(1)) << '\n';
                print_field("iebm_count") << read_le32(data.subspan(5)) << '\n';
                print_field("usb_count") << read_le32(data.subspan(9)) << '\n';
                print_field("i2c_count") << read_le32(data.subspan(13)) << '\n';
            }
            return true;
        }
        if (args[0] == "clear-statistics") {
            require_args(args, 2, "post-processor-cmd clear-statistics requires <source_mask>");
            const auto response = execute_command(0x18, {parse_byte(args[1])});
            print_command_response("clear-statistics", response);
            return true;
        }
        if (args[0] == "reset-usb") {
            require_args(args, 3, "post-processor-cmd reset-usb requires <control_gen> <confirm>");
            auto payload = le32(parse_u32(args[1]));
            payload.push_back(parse_byte(args[2]));
            const auto response = execute_command(0x19, std::move(payload));
            print_command_response("reset-usb", response);
            return true;
        }
        if (args[0] == "debug-reg-access") {
            require_args(args, 4, "post-processor-cmd debug-reg-access requires <operation> <address> <value>");
            auto payload = std::vector<std::uint8_t>{parse_byte(args[1])};
            append_to(payload, le32(parse_u32(args[2])));
            append_to(payload, le32(parse_u32(args[3])));
            const auto response = execute_command(0x7f, std::move(payload));
            print_command_response("debug-reg-access", response);
            if (response.status == 0) {
                require_payload_size(response.payload, 4, "debug-reg-access");
                print_field("value") << read_le32(response.payload) << '\n';
            }
            return true;
        }
        return false;
    }
    return false;
}

} // namespace gm86x::detail