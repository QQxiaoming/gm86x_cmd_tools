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
	std::uint8_t extension_unit_id;
};

// Resolves the physical USB device backing a V4L2 video node (e.g. /dev/video6)
// via sysfs, then parses its USB configuration descriptor through libusb to
// locate the UVC VideoControl interface and extension unit, mirroring how the
// reference host implementation discovers XU targets.
UsbXuTarget discover_usb_xu_target(const std::string &video_device);

} // namespace gm86x
