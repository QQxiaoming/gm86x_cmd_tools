#pragma once
#include <cstdint>
#include <string>

namespace gm86x {

struct UsbXuTarget {
	std::uint16_t vendor_id;
	std::uint16_t product_id;
	std::uint8_t bus_number;
	std::uint8_t device_address;
	std::uint8_t control_interface;
};

// Resolves the physical USB device and the UVC VideoControl interface number
// that owns a given V4L2 video node (e.g. /dev/video6) by walking sysfs.
UsbXuTarget discover_usb_xu_target(const std::string &video_device);

} // namespace gm86x
