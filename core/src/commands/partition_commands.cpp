#include "partition_commands.hpp"

#include "bytes.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <thread>

namespace gm86x::detail {
const char *PartitionCommandGroup::isp_update_status_name(std::uint8_t status) {
    switch (status) {
    case 0:
        return "IDLE";
    case 1:
        return "PENDING";
    case 2:
        return "RUNNING";
    case 3:
        return "SUCCESS";
    case 4:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

void PartitionCommandGroup::print_isp_pending(const Response &response) const {
    if (response.status != 0 || response.payload.size() != 10)
        return;
    const auto status = response.payload[0];
    print_section("isp-fw-update");
    print_field("status") << static_cast<unsigned>(status) << " (" << isp_update_status_name(status) << ")\n";
    print_field("pending") << static_cast<unsigned>(response.payload[1]) << '\n';
    print_field("addr") << "0x" << hex(read_le32(std::span<const std::uint8_t>(response.payload).subspan(2)), 8)
                        << '\n';
    print_field("size") << read_le32(std::span<const std::uint8_t>(response.payload).subspan(6)) << '\n';
}

std::uint32_t PartitionCommandGroup::encode_update_offset(std::uint32_t offset) const {
    if (offset >= 0x40000000U) {
        throw std::invalid_argument("update offset must be below 0x40000000");
    }
        return offset | (context_.buffer_updates.value_or(false) ? 0x80000000U : 0U) |
            (context_.end_update ? 0x40000000U : 0U);
}

std::size_t PartitionCommandGroup::parse_chunk_size(const std::string &value) {
    const auto parsed =
        value.find_first_not_of("0123456789") == std::string::npos ? std::stoull(value, nullptr, 10) : parse_u16(value);
    if (parsed == 0 || parsed > 8188)
        throw std::invalid_argument("chunk_size out of range: " + value);
    return static_cast<std::size_t>(parsed);
}

void PartitionCommandGroup::update_chunk(std::uint8_t opcode, std::uint32_t offset,
                                        const std::vector<std::uint8_t> &data, std::optional<std::uint8_t> slot) {
    std::vector<std::uint8_t> payload;
    if (slot)
        payload.push_back(*slot);
    append_to(payload, le32(encode_update_offset(offset)));
    append_to(payload, data);
    execute(opcode, payload);
}

void PartitionCommandGroup::update_file(std::uint8_t opcode, const std::string &path, std::size_t chunk_size,
                               std::uint32_t offset, std::vector<std::uint8_t> prefix,
                               std::optional<std::uint8_t> slot, const std::string &label, bool use_buffer_mode) {
    const auto frame_overhead = 1U + 4U + 4U + prefix.size() + (slot ? 1U : 0U);
    if (frame_overhead >= context_.max_control_transfer_size)
        throw std::invalid_argument("update frame overhead exceeds control transfer limit");
    chunk_size = std::min(chunk_size, context_.max_control_transfer_size - frame_overhead);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open file: " + path);
    const auto total_size = static_cast<std::size_t>(std::filesystem::file_size(path));
    const auto started = std::chrono::steady_clock::now();
    print_section(label);
    print_field("buffer_mode") << (use_buffer_mode ? "enabled" : "disabled") << '\n';
    print_field("buffer_size") << 4096 << '\n';
    print_field("chunk_size") << chunk_size << " bytes\n";
    std::vector<std::uint8_t> data(chunk_size);
    std::size_t total = 0;
    while (input.read(reinterpret_cast<char *>(data.data()), data.size()) || input.gcount() > 0) {
        const auto count = static_cast<std::size_t>(input.gcount());
        data.resize(count);
        auto payload = prefix;
        if (slot)
            payload.push_back(*slot);
        const auto encoded_offset = offset | (use_buffer_mode ? 0x80000000U : 0U) |
                                    (((input.peek() == std::char_traits<char>::eof()) && use_buffer_mode) ? 0x40000000U : 0U);
        append_to(payload, le32(encoded_offset));
        append_to(payload, data);
        const auto response = execute(opcode, payload, false, false);
        if (response.status != 0)
            throw std::runtime_error(std::string("update failed: ") + status_name(response.status));
        offset += static_cast<std::uint32_t>(count);
        total += count;
        context_.sequence = static_cast<std::uint8_t>(context_.sequence + 1);
        print_transfer_progress(total, total_size, started);
        data.resize(chunk_size);
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto average_rate = elapsed > 0 ? total / elapsed / 1000.0 : 0.0;
    print_section("summary", 2);
    print_field("bytes", 9, 4) << total << '\n';
    print_field("elapsed", 9, 4) << std::fixed << std::setprecision(3) << elapsed << "s\n";
    print_field("avg_rate", 9, 4) << std::setprecision(2) << average_rate << "kB/s\n";
}

std::uint8_t PartitionCommandGroup::userset_read_slot(const std::string &raw) {
    const auto value = parse_decimal_or_hex_u32(raw);
    if (value > 128 && value != 255)
        throw std::invalid_argument("userset read slot out of range: " + raw);
    return static_cast<std::uint8_t>(value);
}

std::vector<PartitionCommandGroup::Entry> PartitionCommandGroup::parse_entries(const std::vector<std::uint8_t> &blob) {
    std::vector<Entry> entries;
    std::size_t offset = 0;
    while (offset + 8 <= blob.size()) {
        const auto address = read_le32(std::span<const std::uint8_t>(blob).subspan(offset));
        const auto length = read_le32(std::span<const std::uint8_t>(blob).subspan(offset + 4));
        if (address == 0xffffffffU && length == 0xffffffffU)
            break;
        offset += 8;
        if (length == 0 || length > blob.size() - offset)
            throw std::runtime_error("invalid metadata entry");
        entries.push_back({address,
                           {blob.begin() + static_cast<std::ptrdiff_t>(offset),
                            blob.begin() + static_cast<std::ptrdiff_t>(offset + length)}});
        offset += length;
    }
    return entries;
}

void PartitionCommandGroup::write_entries(const std::string &path, const std::vector<Entry> &entries) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot write file: " + path);
    for (const auto &entry : entries) {
        auto header = le32(entry.address);
        append_to(header, le32(static_cast<std::uint32_t>(entry.data.size())));
        output.write(reinterpret_cast<const char *>(header.data()), 8);
        output.write(reinterpret_cast<const char *>(entry.data.data()),
                     static_cast<std::streamsize>(entry.data.size()));
    }
    const auto marker = le32(0xffffffffU);
    output.write(reinterpret_cast<const char *>(marker.data()), 4);
    output.write(reinterpret_cast<const char *>(marker.data()), 4);
}

void PartitionCommandGroup::read_partition(std::uint8_t opcode, const std::string &path, std::size_t partition_size,
                                           std::size_t chunk_size, std::optional<std::uint8_t> slot) {
    chunk_size = std::min(chunk_size, context_.max_response_payload_size);
    if (chunk_size == 0)
        throw std::invalid_argument("partition read chunk_size cannot be zero");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot open output file: " + path);
    const auto label = opcode == 0x31 ? "read-calib" : "read-userset-slot";
    print_section(label);
    print_field("path") << path << '\n';
    print_field("partition_size") << partition_size << '\n';
    print_field("chunk_size") << chunk_size << '\n';
    if (slot)
        print_field("slot") << static_cast<unsigned>(*slot) << '\n';
    const auto started = std::chrono::steady_clock::now();
    std::size_t completed = 0;
    for (std::size_t offset = 0; offset < partition_size; offset += chunk_size) {
        const auto size = std::min(chunk_size, partition_size - offset);
        auto payload = slot ? std::vector<std::uint8_t>{*slot} : std::vector<std::uint8_t>{};
        append_to(payload, le32(static_cast<std::uint32_t>(offset)));
        append_to(payload, le16(static_cast<std::uint16_t>(size)));
        const auto response = execute(opcode, payload, false, false);
        if (response.status != 0 || response.payload.size() != size)
            throw std::runtime_error("partition read failed or returned an unexpected size");
        output.write(reinterpret_cast<const char *>(response.payload.data()),
                     static_cast<std::streamsize>(response.payload.size()));
        completed += size;
        print_transfer_progress(completed, partition_size, started);
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto average_rate = elapsed > 0 ? completed / elapsed / 1000.0 : 0.0;
    print_section("summary", 2);
    print_field("bytes", 9, 4) << completed << '\n';
    print_field("elapsed", 9, 4) << std::fixed << std::setprecision(3) << elapsed << "s\n";
    print_field("avg_rate", 9, 4) << std::setprecision(2) << average_rate << "kB/s\n";
}

bool PartitionCommandGroup::try_run(std::string_view name_view, const std::vector<std::string> &args) {
    const std::string name(name_view);
    if (name == "read-calib") {
        require_args(args, 1, "read-calib requires <out_file>");
        read_partition(GMSL_COMMAND_READ_CALIB_PARTITION, args[0], 8192, 2048);
        return true;
    }
    if (name == "erase-userset") {
        require_args(args, 1, "erase-userset requires <slot|all>");
        if (args[0] == "all")
            for (int value : {0, 1, 2, 3, 8, 9, 10, 11}) {
                auto response = execute(GMSL_COMMAND_ERASE_USERSET_PARTITION, {static_cast<std::uint8_t>(value)}, false, false);
                print_section("erase-userset-all");
                if (response.status == 0)
                    print_field("slot") << value << " erased\n";
                else
                    print_field("slot") << value << " erase failed\n";
            }
        else
            execute(GMSL_COMMAND_ERASE_USERSET_PARTITION, {userset_slot(args[0])});
        return true;
    }
    if (name == "erase-calib" || name == "format-userdata") {
        require_args(args, 0, name + " takes no arguments");
        execute(name == "erase-calib" ? GMSL_COMMAND_ERASE_CALIB_PARTITION : GMSL_COMMAND_FORMAT_USER_DATA_DEFAULT);
        return true;
    }
    if (name == "read-userset") {
        require_args(args, 3, "read-userset requires <slot> <offset> <size>");
        constexpr std::uint32_t userset_slot_size = 2048;
        const auto offset = parse_u32(args[1]);
        const auto size = parse_decimal_or_hex_u32(args[2]);
        if (offset >= userset_slot_size || size == 0 || size > 8192 || size > userset_slot_size - offset)
            throw std::invalid_argument("userset read range exceeds slot boundary");
        auto payload = std::vector<std::uint8_t>{userset_read_slot(args[0])};
        append_to(payload, le32(offset));
        append_to(payload, le16(static_cast<std::uint16_t>(size)));
        const auto response = execute(GMSL_COMMAND_READ_USERSET_PARTITION, payload);
        if (response.status == 0) {
            print_section("read-userset");
            print_field("data") << hex_bytes(response.payload) << '\n';
        }
        return true;
    }    
    if (name == "read-userset-slot") {
        require_args(args, 2, "read-userset-slot requires <slot> <out_file>");
        read_partition(GMSL_COMMAND_READ_USERSET_PARTITION, args[1], 2048, 1024, userset_read_slot(args[0]));
        return true;
    }
    if (name == "read-calib-addr" || name == "list-calib-addr" || name == "read-userset-addr" ||
        name == "list-userset-addr") {
        const bool userset = name.find("userset") != std::string::npos;
        const bool list = name.find("list") != std::string::npos;
        const auto path =
            (std::filesystem::temp_directory_path() / (userset ? "gm86x-userset.bin" : "gm86x-calib.bin")).string();
        if (userset) {
            require_args(args, list ? 1 : 2, name + " arguments invalid");
            read_partition(GMSL_COMMAND_READ_USERSET_PARTITION, path, 2048, 1024, userset_read_slot(args[0]));
        } else {
            require_args(args, list ? 0 : 1, name + " arguments invalid");
            read_partition(GMSL_COMMAND_READ_CALIB_PARTITION, path, 8192, 2048);
        }
        const auto entries = parse_entries(read_file(path));
        std::filesystem::remove(path);
        if (list) {
            print_section("metadata");
            print_field("entry_count") << entries.size() << '\n';
            for (std::size_t index = 0; index < entries.size(); ++index)
                print_field("[" + std::to_string(index) + "]", 11, 4)
                    << "addr=0x" << hex(entries[index].address, 8) << " size=" << entries[index].data.size() << '\n';
        } else {
            const auto address = parse_u32(args.back());
            const auto found = std::find_if(entries.begin(), entries.end(),
                                            [address](const Entry &entry) { return entry.address == address; });
            if (found == entries.end())
                throw std::runtime_error("metadata address not found");
            print_section("metadata");
            print_field("address") << "0x" << hex(address, 8) << '\n';
            print_field("length") << found->data.size() << " bytes\n";
            print_field("hex") << hex_bytes(found->data) << '\n';
            print_field("ascii") << ascii(found->data) << '\n';
        }
        return true;
    }
    if (name == "read-sn") {
        require_args(args, 0, "read-sn takes no arguments");
        auto payload = le32(0xd8);
        append_to(payload, le16(128));
        const auto response = execute(GMSL_COMMAND_GIGE_CAM_READ_REGISTER, payload);
        if (response.status != 0)
            throw std::runtime_error("SN read failed");
        print_section("read-sn");
        print_field("sn") << ascii(response.payload) << '\n';
        return true;
    }
    if (name == "write-sn") {
        require_args(args, 1, "write-sn requires <sn>");
        if (args[0].empty())
            throw std::invalid_argument("sn cannot be empty");
        std::vector<std::uint8_t> data(args[0].begin(), args[0].end());
        if (data.size() > 16)
            throw std::invalid_argument("sn too long");
        if (std::any_of(data.begin(), data.end(), [](std::uint8_t value) { return value < 32 || value > 126; }))
            throw std::invalid_argument("sn must contain printable ASCII only");
        data.resize(16, 0);
        const auto path = (std::filesystem::temp_directory_path() / "gm86x-sn.bin").string();
        write_entries(path, {{0xd8, data}});
        execute(GMSL_COMMAND_ERASE_CALIB_PARTITION, {}, false, false);
        update_file(GMSL_COMMAND_UPDATE_CALIB_PARTITION, path, 8188);
        std::filesystem::remove(path);
        return true;
    }
    bool isUpdateCmd = name.starts_with("update-");
    if(isUpdateCmd) {
        //update-<partition>-[chunk|file]
        std::size_t dash1 = name.find('-');
        std::size_t dash2 = name.find('-', dash1 + 1);
        std::string partition = name.substr(dash1 + 1, dash2 - dash1 - 1);
        std::string type = name.substr(dash2 + 1);
        if (partition.empty() || type.empty()) {
            throw std::invalid_argument("invalid update command format: " + name);
        }
        std::map<std::string, std::uint8_t> opcode_map = {
            {"sys", GMSL_COMMAND_UPDATE_SYS_FIRMWARE},
            {"cfg", GMSL_COMMAND_UPDATE_CFG_PARTITION},
            {"calib", GMSL_COMMAND_UPDATE_CALIB_PARTITION},
            {"userset", GMSL_COMMAND_UPDATE_USERSET_PARTITION},
            {"userdata", GMSL_COMMAND_UPDATE_USERDATA_PARTITION},
            {"boot", GMSL_COMMAND_UPDATE_BOOT_FIRMWARE},
        };
        if (type == "chunk") {
            std::size_t reqArgSize = 2;
            if(partition == "userset" || partition == "boot")
                reqArgSize = 3;
            if (args.size() < reqArgSize){
                if(partition == "userset")
                    throw std::invalid_argument(name + "requires <slot> <offset> <byte...>");
                else if(partition == "boot")
                    throw std::invalid_argument(name + " requires <password32> <offset> <byte...>");
                else
                    throw std::invalid_argument(name + " requires <offset> <byte...>");
            }
            
            if(partition == "userset")
                update_chunk(opcode_map[partition], parse_u32(args[1]), parse_byte_list(std::span<const std::string>(args).subspan(2)), userset_slot(args[0]));
            else if(partition == "boot") {
                auto payload = le32(parse_u32(args[0]));
                append_to(payload, le32(encode_update_offset(parse_u32(args[1]))));
                append_to(payload, parse_byte_list(std::span<const std::string>(args).subspan(2)));
                execute(opcode_map[partition], payload);
            } else
                update_chunk(opcode_map[partition], parse_u32(args[0]), parse_byte_list(std::span<const std::string>(args).subspan(1)));
            return true;
        } else if (type == "file") {
            const bool boot = partition == "boot";
            const bool user = partition == "userset";
            const auto minimum_args = boot || user ? 2U : 1U;
            const auto maximum_args = boot || user ? 4U : 3U;
            if (args.size() < minimum_args || args.size() > maximum_args)
                throw std::invalid_argument(name + " has invalid argument count");
            const auto opcode = opcode_map[partition];
            const auto file_index = boot || user ? 1U : 0U;
            const auto offset_index = file_index + 1;
            const auto chunk_index = file_index + 2;
            const auto offset = args.size() > offset_index ? parse_u32(args[offset_index]) : 0;
            if (offset >= 0x40000000U)
                throw std::invalid_argument("update offset must be below 0x40000000");
            auto maximum_chunk_size = 8188U;
            if (boot)
                maximum_chunk_size = 8184U;
            else if (user)
                maximum_chunk_size = 8187U;
            const auto chunk_size = args.size() > chunk_index ? parse_chunk_size(args[chunk_index]) : (maximum_chunk_size);
            if (chunk_size > maximum_chunk_size)
                throw std::invalid_argument("chunk_size exceeds command maximum");
            std::vector<std::uint8_t> prefix;
            if (boot)
                append_to(prefix, le32(parse_u32(args[0])));
            std::optional<std::uint8_t> slot;
            if (user)
                slot = userset_slot(args[0]);
            if (partition == "calib")
                execute(GMSL_COMMAND_ERASE_CALIB_PARTITION, {}, false, false); //erase-calib
            if (user)
                execute(GMSL_COMMAND_ERASE_USERSET_PARTITION, {*slot}, false, false); //erase-userset
            update_file(opcode, args[file_index], chunk_size, offset, prefix, slot, name, context_.buffer_updates.value_or(true));
            return true;
        }
    }
    if (name == "isp-fw-update-request") {
        require_args(args, 0, name + " takes no arguments");
        execute(GMSL_COMMAND_ISP_FW_UPDATE_REQUEST);
        return true;
    }
    if (name == "isp-fw-update-get-pending") {
        require_args(args, 0, name + " takes no arguments");
        const auto response = execute(GMSL_COMMAND_ISP_FW_UPDATE_GET_PENDING);
        print_isp_pending(response);
        return true;
    }
    if (name == "isp-fw-update-submit-force-terminate") {
        require_args(args, 0, name + " takes no arguments");
        execute(GMSL_COMMAND_ISP_FW_UPDATE_SUBMIT_PENDING, {0, 0, 0, 0, 0, 0, 0, 0});
        return true;
    }
    if (name == "isp-fw-update-submit-pending") {
        if (args.size() < 2)
            throw std::invalid_argument("isp-fw-update-submit-pending requires <addr32> <size> <byte...>");
        const auto size = parse_decimal_or_hex_u32(args[1]);
        if (args.size() - 2 != size)
            throw std::invalid_argument("pending byte count does not match size");
        auto payload = le32(parse_u32(args[0]));
        append_to(payload, le32(size));
        append_to(payload, parse_byte_list(std::span<const std::string>(args).subspan(2)));
        execute(GMSL_COMMAND_ISP_FW_UPDATE_SUBMIT_PENDING, payload);
        return true;
    }
    if (name == "isp-fw-update-file") {
        require_args(args, 1, "isp-fw-update-file requires <bin_file>");
        constexpr std::size_t request_overhead = 1U + 4U + 4U + 4U;
        if (context_.max_control_transfer_size <= request_overhead)
            throw std::invalid_argument("ISP firmware update request overhead exceeds control transfer limit");
        const auto max_pending_size = context_.max_control_transfer_size - request_overhead;
        const auto file = read_file(args[0]);
        print_section("isp-fw-update-file");
        print_field("step") << "ensure sleep mode disabled\n";
        execute(GMSL_COMMAND_LOW_POWER_MODE, {0}, false, false);
        print_field("step") << "start request\n";
        execute(GMSL_COMMAND_ISP_FW_UPDATE_REQUEST, {}, false, false);
        for (unsigned poll = 1; poll <= 120000; ++poll) {
            const auto state = execute(GMSL_COMMAND_ISP_FW_UPDATE_GET_PENDING, {}, false, false);
            if (state.status != 0 || state.payload.size() != 10)
                throw std::runtime_error("invalid ISP pending response");
            const auto status = state.payload[0];
            const auto pending = state.payload[1];
            const auto offset = read_le32(std::span<const std::uint8_t>(state.payload).subspan(2));
            const auto size = read_le32(std::span<const std::uint8_t>(state.payload).subspan(6));
            print_section("poll " + std::to_string(poll), 2);
            print_field("status", 11, 4) << static_cast<unsigned>(status) << " (" << isp_update_status_name(status)
                                         << ")\n";
            print_field("pending", 11, 4) << static_cast<unsigned>(pending) << '\n';
            print_field("addr", 11, 4) << "0x" << hex(offset, 8) << '\n';
            print_field("size", 11, 4) << size << '\n';
            if (status == 3) {
                print_field("result", 11, 4) << "update success\n";
                return true;
            }
            if (status == 4)
                throw std::runtime_error("ISP firmware update failed");
            if (!pending) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            if (!size || size > max_pending_size || offset > file.size() || size > file.size() - offset)
                throw std::runtime_error("ISP pending request exceeds input file or control transfer limit");
            auto payload = le32(offset);
            append_to(payload, le32(size));
            payload.insert(payload.end(), file.begin() + offset, file.begin() + offset + size);
            print_field("submit", 11, 4) << "addr=0x" << hex(offset, 8) << " size=" << size << '\n';
            execute(GMSL_COMMAND_ISP_FW_UPDATE_SUBMIT_PENDING, payload, false, false);
        }
        execute(GMSL_COMMAND_LOW_POWER_MODE, {1}, false, false);
        throw std::runtime_error("ISP firmware update timed out");
    }
    return false;
}

} // namespace gm86x::detail