#pragma once

#include <cstdint>
#include <string>

namespace gm86x {

// Returns all exact register names for a full GigE register address.
// Multiple names are separated by " / ". An unknown address returns an empty string.
std::string regaddr_name(std::uint32_t address);

} // namespace gm86x
