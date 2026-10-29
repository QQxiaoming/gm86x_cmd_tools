#include "genicam_zip.hpp"

#include "bytes.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <stdexcept>
#if GM86X_HAS_ZLIB
#include <zlib.h>
#endif

namespace gm86x::detail {

bool genicam_xml_extraction_available() {
    return GM86X_HAS_ZLIB != 0;
}

#if GM86X_HAS_ZLIB
namespace {

std::span<const std::uint8_t> slice(std::span<const std::uint8_t> data, std::size_t offset,
                                   std::size_t length) {
    if (offset > data.size() || length > data.size() - offset)
        throw std::runtime_error("truncated GenICam ZIP");
    return data.subspan(offset, length);
}

}

std::vector<std::uint8_t> extract_genicam_xml(std::span<const std::uint8_t> archive) {
    if (archive.size() < 22)
        throw std::runtime_error("invalid GenICam ZIP");
    std::size_t end = archive.size() - 22;
    const auto earliest = archive.size() > 65557 ? archive.size() - 65557 : 0;
    for (;;) {
        if (read_le_u32(archive, end) == 0x06054b50 &&
            read_le_u16(archive, end + 20) == archive.size() - end - 22)
            break;
        if (end == earliest)
            throw std::runtime_error("GenICam ZIP end record not found");
        --end;
    }
    const auto entries = read_le_u16(archive, end + 10);
    if (read_le_u16(archive, end + 4) != 0 || read_le_u16(archive, end + 6) != 0 ||
        read_le_u16(archive, end + 8) != entries || entries == 0xffff)
        throw std::runtime_error("multi-disk and ZIP64 GenICam archives are not supported");
    const auto directory_offset = read_le_u32(archive, end + 16);
    const auto directory_size = read_le_u32(archive, end + 12);
    const auto directory = slice(archive.first(end), directory_offset, directory_size);
    std::span<const std::uint8_t> xml_entry;
    std::size_t offset = 0;
    for (unsigned index = 0; index < entries; ++index) {
        const auto header = slice(directory, offset, 46);
        if (read_le_u32(header) != 0x02014b50)
            throw std::runtime_error("invalid GenICam ZIP directory");
        const auto name_length = read_le_u16(header, 28);
        const auto entry_size = 46U + name_length + read_le_u16(header, 30) + read_le_u16(header, 32);
        const auto entry = slice(directory, offset, entry_size);
        const auto name_bytes = entry.subspan(46, name_length);
        std::string name(name_bytes.begin(), name_bytes.end());
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (name.ends_with(".xml")) {
            if (!xml_entry.empty())
                throw std::runtime_error("GenICam ZIP contains multiple XML files");
            xml_entry = entry;
        }
        offset += entry_size;
    }
    if (offset != directory.size() || xml_entry.empty())
        throw std::runtime_error("invalid GenICam ZIP directory or missing XML file");
    const auto flags = read_le_u16(xml_entry, 8);
    const auto method = read_le_u16(xml_entry, 10);
    const auto compressed_size = read_le_u32(xml_entry, 20);
    const auto xml_size = read_le_u32(xml_entry, 24);
    if ((flags & (1U | 0x40U)) != 0 || read_le_u16(xml_entry, 34) != 0 ||
        (method != 0 && method != 8))
        throw std::runtime_error("unsupported GenICam ZIP encryption or compression");
    if (xml_size == 0 || xml_size > 16U * 1024U * 1024U)
        throw std::runtime_error("GenICam XML size out of range (1..16777216)");
    const auto local_offset = read_le_u32(xml_entry, 42);
    const auto local_area = archive.first(directory_offset);
    const auto local = slice(local_area, local_offset, 30);
    if (read_le_u32(local) != 0x04034b50 || read_le_u16(local, 6) != flags ||
        read_le_u16(local, 8) != method)
        throw std::runtime_error("invalid GenICam ZIP local header");
    const auto name_length = read_le_u16(local, 26);
    const auto local_name = slice(local_area, static_cast<std::size_t>(local_offset) + 30, name_length);
    if (!std::equal(local_name.begin(), local_name.end(),
                    xml_entry.begin() + 46, xml_entry.begin() + 46 + read_le_u16(xml_entry, 28)))
        throw std::runtime_error("GenICam ZIP filename mismatch");
    const auto data_offset = static_cast<std::size_t>(local_offset) + 30 + name_length + read_le_u16(local, 28);
    const auto compressed = slice(local_area, data_offset, compressed_size);
    std::vector<std::uint8_t> xml(xml_size);
    if (method == 0) {
        if (compressed_size != xml_size)
            throw std::runtime_error("invalid stored GenICam XML size");
        std::copy(compressed.begin(), compressed.end(), xml.begin());
    } else {
        z_stream stream{};
        stream.next_in = const_cast<Bytef *>(compressed.data());
        stream.avail_in = static_cast<uInt>(compressed.size());
        stream.next_out = xml.data();
        stream.avail_out = static_cast<uInt>(xml.size());
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            throw std::runtime_error("cannot initialize GenICam ZIP decompression");
        const auto result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result != Z_STREAM_END || stream.total_in != compressed.size() || stream.total_out != xml.size())
            throw std::runtime_error("GenICam ZIP decompression failed");
    }
    if (crc32(0, xml.data(), static_cast<uInt>(xml.size())) != read_le_u32(xml_entry, 16))
        throw std::runtime_error("GenICam XML CRC mismatch");
    return xml;
}

#else
std::vector<std::uint8_t> extract_genicam_xml(std::span<const std::uint8_t>) {
    throw std::runtime_error("XML extraction unavailable: built without compatible zlib; use a .zip output filename");
}
#endif

}
