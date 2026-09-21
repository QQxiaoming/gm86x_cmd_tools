#pragma once
#include "protocol.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gm86x {
struct CommandContext {
    ProtocolClient &client;
    std::uint8_t sequence = 1;
    std::optional<bool> buffer_updates;
    bool end_update = false;
    std::size_t max_control_transfer_size = 8192;
    std::size_t max_response_payload_size = 8192;
};

class CommandGroup {
public:
    explicit CommandGroup(CommandContext &context) : context_(context) {}
    virtual ~CommandGroup() = default;
    virtual bool try_run(std::string_view name, const std::vector<std::string> &args) = 0;

    static constexpr int field_indent = 2;
    static constexpr int field_key_width = 11;
    static void print_section(std::string_view name, int indent = 0);
    static std::ostream &print_field(std::string_view key, int width = field_key_width, int indent = field_indent);
    static void print_hex_dump(std::span<const std::uint8_t> data);
    enum {
        GMSL_COMMAND_PING = 0x01,
        GMSL_COMMAND_GET_STATUS = 0x02,
        GMSL_COMMAND_MCU_RESET = 0x03,
        GMSL_COMMAND_SET_TIMER_OUT_RESET = 0x10,
        GMSL_COMMAND_SET_PPS_MODE = 0x11,
        GMSL_COMMAND_GET_TIMESTAMP = 0x12,
        GMSL_COMMAND_SET_TIMESTAMP = 0x13,
        GMSL_COMMAND_GET_FIRMWARE_VERSION = 0x14,
        GMSL_COMMAND_TIME_SYNC_EXCHANGE = 0x15,
        GMSL_COMMAND_ADJUST_TIMESTAMP = 0x16,
        GMSL_COMMAND_SMOOTH_ADJUST_TIMESTAMP = 0x17,
        GMSL_COMMAND_DE100_READ_REGISTER = 0x20,
        GMSL_COMMAND_DE100_WRITE_REGISTER = 0x21,
        GMSL_COMMAND_DE100_SUBSNR_READ_REGISTER = 0x22,
        GMSL_COMMAND_DE100_SUBSNR_WRITE_REGISTER = 0x23,
        GMSL_COMMAND_ISP_READ_REGISTER = 0x24,
        GMSL_COMMAND_ISP_WRITE_REGISTER = 0x25,
        GMSL_COMMAND_IMU_READ_SAMPLES = 0x26,
        GMSL_COMMAND_TEMP_READ = 0x27,
        GMSL_COMMAND_SET_AUTO_USERSET_SLOT = 0x28,
        GMSL_COMMAND_UPDATE_SYS_FIRMWARE = 0x29,
        GMSL_COMMAND_UPDATE_CFG_PARTITION = 0x2A,
        GMSL_COMMAND_UPDATE_CALIB_PARTITION = 0x2B,
        GMSL_COMMAND_UPDATE_USERSET_PARTITION = 0x2C,
        GMSL_COMMAND_UPDATE_USERDATA_PARTITION = 0x2D,
        GMSL_COMMAND_GIGE_CAM_READ_REGISTER = 0x2E,
        GMSL_COMMAND_GIGE_CAM_WRITE_REGISTER = 0x2F,
        GMSL_COMMAND_UPDATE_BOOT_FIRMWARE = 0x30,
        GMSL_COMMAND_READ_CALIB_PARTITION = 0x31,
        GMSL_COMMAND_ERASE_USERSET_PARTITION = 0x32,
        GMSL_COMMAND_ERASE_CALIB_PARTITION = 0x33,
        GMSL_COMMAND_FORMAT_USER_DATA_DEFAULT = 0x34,
        GMSL_COMMAND_READ_USERSET_PARTITION = 0x35,
        GMSL_COMMAND_GET_AUTO_USERSET_SLOT = 0x36,
        GMSL_COMMAND_GIGE_CAM_GET_ACCESS_MAP = 0x37,
        GMSL_COMMAND_APPLY_USERSET = 0x38,
        GMSL_COMMAND_LOW_POWER_MODE = 0x39,
        GMSL_COMMAND_ISP_FW_UPDATE_REQUEST = 0x3A,
        GMSL_COMMAND_ISP_FW_UPDATE_GET_PENDING = 0x3B,
        GMSL_COMMAND_ISP_FW_UPDATE_SUBMIT_PENDING = 0x3C,
        GMSL_COMMAND_ISP_GET_VERSION = 0x3D,
        GMSL_COMMAND_ISP_READ_SUBSENSOR_REGISTER = 0x3E,
        GMSL_COMMAND_ISP_WRITE_SUBSENSOR_REGISTER = 0x3F,
        GMSL_COMMAND_POST_PROCESSOR_RAW_WRITE = 0x40,
        GMSL_COMMAND_POST_PROCESSOR_RAW_READ = 0x41,

        GMSL_COMMAND_PERCIPIO_DEBUG = 0xF0,
    };
    
protected:
    static std::string hex(std::uint64_t value, int digits);
    static const char *opcode_name(std::uint8_t opcode);

    Response execute(std::uint8_t opcode, const std::vector<std::uint8_t> &payload = {},
                     bool print_timing = true, bool print_verbose = true);
    void print_response(const Response &response) const;
    void print_timing(const RequestTiming &timing) const;
    void print_command(std::uint8_t sequence, std::uint8_t opcode,
                       std::span<const std::uint8_t> payload) const;
    void print_transfer_progress(std::size_t completed, std::size_t total,
                                 std::chrono::steady_clock::time_point started) const;
    static void require_args(const std::vector<std::string> &args, std::size_t count, const std::string &usage);
    static std::vector<std::uint8_t> le16(std::uint16_t value);
    static std::vector<std::uint8_t> le32(std::uint32_t value);
    static std::vector<std::uint8_t> le64(std::uint64_t value);
    static void append_to(std::vector<std::uint8_t> &target, std::span<const std::uint8_t> source);
    static std::uint8_t userset_slot(const std::string &raw);
    static std::uint32_t read_le32(std::span<const std::uint8_t> data);
    static std::string ascii(std::span<const std::uint8_t> data);
    static std::vector<std::uint8_t> read_file(const std::string &path);

    CommandContext &context_;
};

class CommandDispatcher {
public:
    explicit CommandDispatcher(CommandContext &context);

    void register_group(std::unique_ptr<CommandGroup> group);
    int run(std::string_view name, const std::vector<std::string> &args);
    static void print_help(const std::string &exeName);

private:
    CommandContext &context_;
    std::vector<std::unique_ptr<CommandGroup>> groups_;
};

} // namespace gm86x