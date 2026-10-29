#include "gige_commands.hpp"
#include "genicam_zip.hpp"

#include "bytes.hpp"
#include "regaddr_lookup.hpp"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <utility>

namespace gm86x::detail {

std::vector<std::uint8_t> GigeCommandGroup::read_gige_data(std::uint32_t address, std::uint32_t size,
                                                         bool show_progress) {
    const auto chunk_size = std::min<std::size_t>(std::numeric_limits<std::uint16_t>::max(),
                                                context_.max_response_payload_size);
    if (chunk_size == 0)
        throw std::invalid_argument("GigE response payload limit cannot be zero");
    if (size == 0 || address > std::numeric_limits<std::uint32_t>::max() - (size - 1))
        throw std::invalid_argument("GigE read address range invalid");
    std::vector<std::uint8_t> data;
    data.reserve(size);
    const auto started = std::chrono::steady_clock::now();
    while (data.size() < size) {
        const auto count = static_cast<std::uint16_t>(std::min(chunk_size, size - data.size()));
        auto payload = le32(address + static_cast<std::uint32_t>(data.size()));
        append_to(payload, le16(count));
        const auto response = execute(GMSL_COMMAND_GIGE_CAM_READ_REGISTER, payload, false, false);
        if (response.payload.size() != count)
            throw std::runtime_error("GigE read response length does not match requested size");
        append_to(data, response.payload);
        context_.sequence = static_cast<std::uint8_t>(context_.sequence + 1);
        if (show_progress)
            print_transfer_progress(data.size(), size, started);
    }
    return data;
}

void GigeCommandGroup::get_xml(const std::string &path) {
    auto extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!path.empty() && extension != ".zip" && extension != ".xml")
        throw std::invalid_argument("gige-get-xml output filename must end in .zip or .xml");
    if (extension != ".zip" && !genicam_xml_extraction_available())
        throw std::runtime_error("XML extraction unavailable: built without compatible zlib; use a .zip output filename");
    const auto url_data = read_gige_data(0x200, 512);
    const auto terminator = std::find(url_data.begin(), url_data.end(), 0);
    if (terminator == url_data.end())
        throw std::runtime_error("GevFirstURL is not NUL-terminated");
    const std::string url(url_data.begin(), terminator);
    const auto address_separator = url.find(';');
    const auto size_separator = url.find(';', address_separator == std::string::npos ? 0 : address_separator + 1);
    if (!url.starts_with("Local:") || address_separator == std::string::npos ||
        address_separator <= 6 || size_separator == std::string::npos ||
        url.find(';', size_separator + 1) != std::string::npos)
        throw std::runtime_error("invalid GenICam Local URL: " + url);
    const auto address = parse_u32("0x" + url.substr(address_separator + 1,
                                                    size_separator - address_separator - 1));
    const auto size = parse_u32("0x" + url.substr(size_separator + 1));
    if (size == 0 || size > 1048576)
        throw std::runtime_error("GenICam file size out of range (1..1048576)");
    if (!path.empty()) {
        print_section("gige-get-xml");
        print_field("url") << url << '\n';
        print_field("path") << path << '\n';
    }
    auto data = read_gige_data(address, size, !path.empty());
    if (extension != ".zip")
        data = extract_genicam_xml(data);
    if (path.empty()) {
        std::cout.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!std::cout)
            throw std::runtime_error("cannot print GenICam XML");
        return;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot open output file: " + path);
    output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output)
        throw std::runtime_error("cannot write output file: " + path);
    print_field("bytes") << data.size() << (extension == ".zip" ? " (raw ZIP)\n" : " (extracted XML)\n");
}

std::vector<std::uint8_t> GigeCommandGroup::gige_header(const std::string &address, std::uint16_t size) {
    auto payload = le32(parse_regaddr(address));
    append_to(payload, le16(size));
    return payload;
}

std::uint16_t GigeCommandGroup::parse_gige_size(const std::string &value, std::size_t max_size) {
    const auto size = parse_u32(value);
    max_size = std::min<std::size_t>(max_size, std::numeric_limits<std::uint16_t>::max());
    if (size == 0 || size > max_size)
        throw std::invalid_argument("register size out of range (1.." + std::to_string(max_size) + "): " + value);
    return static_cast<std::uint16_t>(size);
}

void GigeCommandGroup::print_access_map_count(const Response &response) {
    if (response.status == 0 && response.payload.size() == 2) {
        const auto count = static_cast<unsigned>(read_le_u16(response.payload));
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
        const auto address = read_le_u32(std::span<const std::uint8_t>(response.payload).subspan(offset));
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
        const auto address = read_le_u32(std::span<const std::uint8_t>(response.payload).subspan(offset));
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

void GigeCommandGroup::print_long_ack_time_map_count(const Response &response) {
    if (response.status != 0)
        return;
    if (response.payload.size() != 2)
        throw std::runtime_error("long-ack-time-map count payload length is not 2");
    const auto count = static_cast<unsigned>(read_le_u16(response.payload));
    print_section("long-ack-time-map");
    print_field("entry_count") << count << " (0x" << hex(count, 4) << ")\n";
}

std::size_t GigeCommandGroup::long_ack_time_map_address_column_width(const Response &response) {
    if (response.status != 0)
        return 0;
    if (response.payload.size() % 8 != 0)
        throw std::runtime_error("long-ack-time-map payload length is not a multiple of 8");
    std::size_t width = 0;
    for (std::size_t offset = 0; offset < response.payload.size(); offset += 8) {
        const auto address = read_le_u32(std::span<const std::uint8_t>(response.payload).subspan(offset));
        width = std::max(width, std::string("addr=" + regaddr_label(address)).size());
    }
    return width + 2;
}

void GigeCommandGroup::print_long_ack_time_map_page(std::size_t start_index, const Response &response,
                                                   bool skipPrintHeader, std::size_t addressColumnWidth) {
    if (response.status != 0)
        return;
    if (response.payload.size() % 8 != 0)
        throw std::runtime_error("long-ack-time-map payload length is not a multiple of 8");
    const auto entries = response.payload.size() / 8;
    if (!skipPrintHeader) {
        print_section("long-ack-time-map-page");
        print_field("start_index") << start_index << '\n';
        print_field("entry_count") << entries << '\n';
    }
    if (addressColumnWidth == 0)
        addressColumnWidth = long_ack_time_map_address_column_width(response);
    for (std::size_t index = 0; index < entries; ++index) {
        const auto offset = index * 8;
        const auto address = read_le_u32(std::span<const std::uint8_t>(response.payload).subspan(offset));
        const auto read_timeout = static_cast<unsigned>(read_le_u16(response.payload, offset + 4));
        const auto write_timeout = static_cast<unsigned>(read_le_u16(response.payload, offset + 6));
        auto &output = print_field("[" + std::to_string(start_index + index) + "]", 11, 4);
        output << std::left << std::setw(static_cast<int>(addressColumnWidth))
               << ("addr=" + regaddr_label(address)) << std::right
               << "read_timeout_ms=" << read_timeout
               << " write_timeout_ms=" << write_timeout << '\n';
    }
}

bool GigeCommandGroup::try_run(std::string_view name_view, const std::vector<std::string> &args) {
    const std::string name(name_view);
    if (name == "gige-get-xml") {
        if (args.size() > 1)
            throw std::invalid_argument("gige-get-xml accepts [out_file.zip|out_file.xml]");
        get_xml(args.empty() ? "" : args[0]);
        return true;
    }
    if (name == "gige-read") {
        if (args.size() != 2)
            throw std::invalid_argument(name + " arguments invalid");
        const auto size = parse_gige_size(args[1], context_.max_response_payload_size);
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
            print_field("value") << read_le_u32(response.payload) << '\n';
        else if (name == "gige-read-i32")
            print_field("value") << static_cast<std::int32_t>(read_le_u32(response.payload)) << '\n';
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
            value = le32(static_cast<std::uint32_t>(parse_i32(args[1])));
        else if (name == "gige-write-u32")
            value = le32(parse_u32(args[1]));
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
                const auto count = static_cast<unsigned>(read_le_u16(response.payload));
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
        const auto count = parse_u32(args[1]);
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
    if (name == "gige-get-long-ack-time-map") {
        if (args.empty()) {
            const auto response = execute(GMSL_COMMAND_GIGE_CAM_GET_LONG_ACK_TIME_MAP, {0}, false, false);
            if (response.status != 0 || response.payload.size() != 2)
                throw std::runtime_error("Failed to get long ack time map count");
            const auto count = static_cast<unsigned>(read_le_u16(response.payload));
            if (context_.max_response_payload_size <= count * 8 + 50) {
                print_section("long-ack-time-map-page");
                print_field("start_index") << 0 << '\n';
                print_field("entry_count") << count << '\n';
                std::vector<std::pair<std::size_t, Response>> pages;
                for (unsigned start_index = 0; start_index < count; start_index += 28) {
                    const auto page_count = std::min(count - start_index, 28u);
                    auto payload = std::vector<std::uint8_t>{1};
                    append_to(payload, le16(static_cast<std::uint16_t>(start_index)));
                    payload.push_back(static_cast<std::uint8_t>(page_count));
                    auto page = execute(GMSL_COMMAND_GIGE_CAM_GET_LONG_ACK_TIME_MAP, payload, false, false);
                    if (page.status != 0)
                        throw std::runtime_error("Failed to get long ack time map page");
                    pages.emplace_back(start_index, std::move(page));
                }
                std::size_t address_column_width = 0;
                for (const auto &[page_start, page] : pages) {
                    (void)page_start;
                    address_column_width =
                        std::max(address_column_width, long_ack_time_map_address_column_width(page));
                }
                for (const auto &[page_start, page] : pages)
                    print_long_ack_time_map_page(page_start, page, true, address_column_width);
            } else {
                print_long_ack_time_map_page(0, execute(GMSL_COMMAND_GIGE_CAM_GET_LONG_ACK_TIME_MAP));
            }
            return true;
        }
        require_args(args, 2, "gige-get-long-ack-time-map requires <start> <count>");
        const auto start_index = parse_u16(args[0]);
        const auto count = parse_u32(args[1]);
        if (count == 0 || count > 28)
            throw std::invalid_argument("long-ack-time-map count out of range (1..28): " + args[1]);
        auto payload = std::vector<std::uint8_t>{1};
        append_to(payload, le16(start_index));
        payload.push_back(static_cast<std::uint8_t>(count));
        print_long_ack_time_map_page(start_index, execute(GMSL_COMMAND_GIGE_CAM_GET_LONG_ACK_TIME_MAP, payload));
        return true;
    }
    if (name == "gige-get-long-ack-time-map-count") {
        require_args(args, 0, name + " takes no arguments");
        print_long_ack_time_map_count(execute(GMSL_COMMAND_GIGE_CAM_GET_LONG_ACK_TIME_MAP, {0}));
        return true;
    }
    return false;
}

} // namespace gm86x::detail