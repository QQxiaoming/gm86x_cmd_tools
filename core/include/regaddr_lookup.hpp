#pragma once

#include <cstdint>
#include <string>

namespace gm86x {

// Parses a hexadecimal address or resolves a name using regaddr_convert's
// built-in GMSL database. Unknown or ambiguous names throw invalid_argument.
std::uint32_t parse_regaddr(const std::string &text);

// Returns all exact register names for a full GigE register address.
// Multiple names are separated by " / ". An unknown address returns an empty string.
std::string regaddr_name(std::uint32_t address);

// Formats a full GigE register address as "0xXXXXXXXX [ name ]".
// The name suffix is omitted when the address is unknown.
std::string regaddr_label(std::uint32_t address);

// Value type declared for a register in the TYGenICamReg CSV files.
enum class RegaddrKind {
    Unknown, // address is unknown, non-scalar, or matched by disagreeing registers
    Integer,
    Float,
    String,
    ByteArray,
    Struct,
};

// Returns the declared value type of a full GigE register address.
RegaddrKind regaddr_kind(std::uint32_t address);

} // namespace gm86x
