#pragma once
#include <cstddef>
#include <cstdint>

namespace gm86x {

struct UsbXuTarget {
	std::uint16_t vendor_id;
	std::uint16_t product_id;
	std::uint8_t bus_number;
	std::uint8_t device_address;
	std::uint8_t control_interface;
	std::uint8_t extension_unit_id;
};

// Finds a matching USB device exposed through V4L2, then parses its USB
// configuration descriptor to locate the UVC VideoControl interface and
// extension unit.
UsbXuTarget discover_usb_xu_target(std::uint16_t vendor_id, std::uint16_t product_id,
								   std::size_t device_index);

} // namespace gm86x
