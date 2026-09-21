#pragma once
#include <cstdint>
#include <string>

namespace gm86x {
struct DeviceAddress {
    std::string bus;
    std::uint8_t address;
    std::string media_device;
};

DeviceAddress discover_device();

} // namespace gm86x