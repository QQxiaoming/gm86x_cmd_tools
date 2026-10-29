#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace gm86x::detail {

bool genicam_xml_extraction_available();
std::vector<std::uint8_t> extract_genicam_xml(std::span<const std::uint8_t> archive);

}
