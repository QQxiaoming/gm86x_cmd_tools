#include "gige_commands.hpp"

#include "bytes.hpp"
#include "regaddr_lookup.hpp"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <utility>

namespace gm86x::detail {

float GigeCommandGroup::parse_float(const std::string &value) {
    return std::stof(value);
}

float GigeCommandGroup::read_float(std::span<const std::uint8_t> data) {
    if (data.size() < 4)
        throw std::runtime_error("short float payload");
    const auto bits = read_le32(data);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::vector<std::uint8_t> GigeCommandGroup::gige_header(const std::string &address, std::uint16_t size) {
    auto payload = le32(parse_regaddr(address));
    append_to(payload, le16(size));
    return payload;
}

std::uint16_t GigeCommandGroup::parse_gige_size(const std::string &value) {
    const auto size = parse_decimal_or_hex_u32(value);
    if (size == 0 || size > 640)
        throw std::invalid_argument("register size out of range (1..640): " + value);
    return static_cast<std::uint16_t>(size);
}

void GigeCommandGroup::print_access_map_count(const Response &response) {
    if (response.status == 0 && response.payload.size() == 2) {
        const auto count = static_cast<unsigned>(response.payload[0] | response.payload[1] << 8);
        print_section("access-map");
        print_field("entry_count") << count << " (0x" << hex(count, 4) << ")\n";
    }
}

std::size_t GigeCommandGroup::access_map_address_column_width(const Response &response) {
    if (response.status != 0)
        return 0;
    if (response.payload.size() % 5 != 0)
        throw std::runtime_error("access-map payload length is not a multiple of 5");
    const auto entries = response.payload.size() / 5;
    std::size_t width = 0;
    for (std::size_t index = 0; index < entries; ++index) {
        const auto offset = index * 5;
        const auto address = read_le32(std::span<const std::uint8_t>(response.payload).subspan(offset));
        width = std::max(width, std::string("addr=" + regaddr_label(address)).size());
    }
    return width + 2;
}

void GigeCommandGroup::print_access_map_page(std::size_t start_index, const Response &response,
                                             bool skipPrintHeader, std::size_t addressColumnWidth) {
    if (response.status != 0)
        return;
    if (response.payload.size() % 5 != 0)
        throw std::runtime_error("access-map payload length is not a multiple of 5");
    const auto entries = response.payload.size() / 5;
    if (!skipPrintHeader) {
        print_section("access-map-page");
        print_field("start_index") << start_index << '\n';
        print_field("entry_count") << entries << '\n';
    }
    if (addressColumnWidth == 0)
        addressColumnWidth = access_map_address_column_width(response);
    for (std::size_t index = 0; index < entries; ++index) {
        const auto offset = index * 5;
        const auto address = read_le32(std::span<const std::uint8_t>(response.payload).subspan(offset));
        const auto attr = response.payload[offset + 4];
        std::string access;
        if (attr & 1)
            access = "read";
        if (attr & 2)
            access += access.empty() ? "write" : ",write";
        if (access.empty())
            access = "none";
        auto &output = print_field("[" + std::to_string(start_index + index) + "]", 11, 4);
        output << std::left << std::setw(static_cast<int>(addressColumnWidth))
               << ("addr=" + regaddr_label(address))
               << "attr=0x" << hex(attr, 2)
               << " (" << std::left << std::setw(10) << access << std::right << ")";
        output << '\n';
    }
}

bool GigeCommandGroup::try_run(std::string_view name_view, const std::vector<std::string> &args) {
    const std::string name(name_view);
    if (name == "gige-read") {
        if (args.size() != 2)
            throw std::invalid_argument(name + " arguments invalid");
        const auto size = parse_gige_size(args[1]);
        auto payload = gige_header(args[0], size);
        const auto response = execute(GMSL_COMMAND_GIGE_CAM_READ_REGISTER, payload);
        if (response.status == 0) {
            print_section("gige-read");
            print_field("data") << hex_bytes(response.payload) << '\n';
        }
        return true;
    }
    if (name == "gige-read-u32" || name == "gige-read-i32" || name == "gige-read-float") {
        require_args(args, 1, name + " requires <addr32|name>");
        const auto response = execute(GMSL_COMMAND_GIGE_CAM_READ_REGISTER, gige_header(args[0], 4));
        if (response.status != 0)
            throw std::runtime_error(std::string("GigE read failed: ") + status_name(response.status));
        print_section(name);
        if (name == "gige-read-u32")
            print_field("value") << read_le32(response.payload) << '\n';
        else if (name == "gige-read-i32")
            print_field("value") << static_cast<std::int32_t>(read_le32(response.payload)) << '\n';
        else if (name == "gige-read-float")
            print_field("value") << read_float(response.payload) << '\n';
        return true;
    }
    if (name == "gige-write") {
        if (args.size() < 3)
            throw std::invalid_argument(name + " arguments invalid");
        const auto size = parse_gige_size(args[1]);
        if (args.size() - 2 != size)
            throw std::invalid_argument("gige-write data byte count does not match size");
        auto payload = gige_header(args[0], size);
        append_to(payload, parse_byte_list(std::span<const std::string>(args).subspan(2)));
        execute(GMSL_COMMAND_GIGE_CAM_WRITE_REGISTER, payload);
        return true;
    }
    if (name == "gige-write-u32" || name == "gige-write-i32" || name == "gige-write-float") {
        require_args(args, 2, name + " requires <addr32|name> <value>");
        auto payload = gige_header(args[0], 4);
        std::vector<std::uint8_t> value;
        if (name == "gige-write-i32")
            value = le32(static_cast<std::uint32_t>(parse_decimal_or_hex_i32(args[1])));
        else if (name == "gige-write-u32")
            value = le32(parse_decimal_or_hex_u32(args[1]));
        else if (name == "gige-write-float") {
            const auto number = parse_float(args[1]);
            std::uint32_t bits;
            std::memcpy(&bits, &number, sizeof bits);
            value = le32(bits);
        }
        append_to(payload, value);
        execute(GMSL_COMMAND_GIGE_CAM_WRITE_REGISTER, payload);
        return true;
    }
    if (name == "gige-get-access-map") {
        if (args.empty()) {
            auto response = execute(GMSL_COMMAND_GIGE_CAM_GET_ACCESS_MAP, {0}, false, false);
            if (response.status == 0 && response.payload.size() == 2) {
                const auto count = static_cast<unsigned>(response.payload[0] | response.payload[1] << 8);
                if(context_.max_response_payload_size <= (count*5+50)) {
                    // 分页获取整个访问映射表
                    std::uint16_t start_index = 0;
                    print_section("access-map-page");
                    print_field("start_index") << start_index << '\n';
                    print_field("entry_count") << count << '\n';
                    std::vector<std::pair<std::size_t, Response>> pages;
                    for (std::uint16_t i = 0; i < count; i += 45) {
                        const auto page_count = std::min(static_cast<unsigned>(count - i), 45u);
                        auto page_payload = std::vector<std::uint8_t>{1};
                        append_to(page_payload, le16(start_index + i));
                        page_payload.push_back(static_cast<std::uint8_t>(page_count));
                        pages.emplace_back(
                            start_index + i,
                            execute(GMSL_COMMAND_GIGE_CAM_GET_ACCESS_MAP, page_payload, false, false));
                    }
                    std::size_t address_column_width = 0;
                    for (const auto &[page_start, page] : pages) {
                        (void)page_start;
                        address_column_width =
                            std::max(address_column_width, access_map_address_column_width(page));
                    }
                    for (const auto &[page_start, page] : pages) {
                        print_access_map_page(page_start, page, true, address_column_width);
                    }
                } else {
                    print_access_map_page(0, execute(GMSL_COMMAND_GIGE_CAM_GET_ACCESS_MAP));
                }
            } else {
                throw std::runtime_error("Failed to get access map count");
            }
            return true;
        }
        require_args(args, 2, "gige-get-access-map requires <start> <count>");
        const auto start_index = parse_u16(args[0]);
        const auto count = parse_decimal_or_hex_u32(args[1]);
        if (count == 0 || count > 45)
            throw std::invalid_argument("access-map count out of range (1..45): " + args[1]);
        auto payload = std::vector<std::uint8_t>{1};
        append_to(payload, le16(start_index));
        payload.push_back(static_cast<std::uint8_t>(count));
        print_access_map_page(start_index, execute(GMSL_COMMAND_GIGE_CAM_GET_ACCESS_MAP, payload));
        return true;
    }
    if (name == "gige-get-access-map-count") {
        require_args(args, 0, name + " takes no arguments");
        print_access_map_count(execute(GMSL_COMMAND_GIGE_CAM_GET_ACCESS_MAP, {0}));
        return true;
    }
    return false;
}

} // namespace gm86x::detail