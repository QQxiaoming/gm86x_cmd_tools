#include "usb_discovery.hpp"

#include <libusb-1.0/libusb.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace gm86x {

namespace {

constexpr std::uint8_t usb_class_video = 0x0e;
constexpr std::uint8_t uvc_subclass_control = 0x01;
constexpr std::uint8_t uvc_cs_interface = 0x24;
constexpr std::uint8_t uvc_vc_extension_unit = 0x06;

std::string basename_of(const fs::path &path) { return path.filename().string(); }

unsigned read_attr(const fs::path &file, int base) {
	std::ifstream stream(file);
	if (!stream.is_open())
		throw std::runtime_error("cannot read sysfs attribute: " + file.string());
	std::string value;
	std::getline(stream, value);
	return static_cast<unsigned>(std::stoul(value, nullptr, base));
}

// Resolves the sysfs USB device directory backing a V4L2 video node. The
// "device" symlink may point at any interface of the device (streaming or
// control), so walk up until the directory with idVendor/idProduct is found
// instead of assuming a fixed number of parent levels.
fs::path resolve_usb_device_dir(const std::string &video_device) {
	const auto node_name = basename_of(fs::path(video_device));
	const fs::path class_link = fs::path("/sys/class/video4linux") / node_name / "device";
	if (!fs::exists(class_link))
		throw std::runtime_error("no sysfs entry for video device: " + video_device);

	fs::path device_dir = fs::canonical(class_link);
	while (!fs::exists(device_dir / "idVendor") || !fs::exists(device_dir / "idProduct")) {
		const fs::path parent = device_dir.parent_path();
		if (parent == device_dir)
			throw std::runtime_error("resolved sysfs path is not a USB device: " + video_device);
		device_dir = parent;
	}
	return device_dir;
}

// Locates the UVC VideoControl interface and its extension unit by parsing
// the class-specific descriptors embedded in the active USB configuration
// descriptor, the same way the reference host implementation does, rather
// than guessing the interface from sysfs directory names.
bool find_extension_unit(libusb_device *device, UsbXuTarget &target) {
	libusb_config_descriptor *config = nullptr;
	if (libusb_get_active_config_descriptor(device, &config) != LIBUSB_SUCCESS || !config)
		return false;

	bool found = false;
	for (int i = 0; i < config->bNumInterfaces && !found; ++i) {
		const libusb_interface &iface = config->interface[i];
		for (int alt = 0; alt < iface.num_altsetting && !found; ++alt) {
			const libusb_interface_descriptor &desc = iface.altsetting[alt];
			if (desc.bInterfaceClass != usb_class_video || desc.bInterfaceSubClass != uvc_subclass_control)
				continue;

			const std::uint8_t *data = desc.extra;
			int remaining = desc.extra_length;
			while (data && remaining >= 3) {
				const std::uint8_t length = data[0];
				if (length < 3 || length > remaining)
					break;
				if (data[1] == uvc_cs_interface && data[2] == uvc_vc_extension_unit && length >= 4) {
					target.control_interface = desc.bInterfaceNumber;
					target.extension_unit_id = data[3];
					found = true;
					break;
				}
				data += length;
				remaining -= length;
			}
		}
	}
	libusb_free_config_descriptor(config);
	return found;
}

} // namespace

UsbXuTarget discover_usb_xu_target(const std::string &video_device) {
	const fs::path usb_device_dir = resolve_usb_device_dir(video_device);

	UsbXuTarget target{};
	target.vendor_id = static_cast<std::uint16_t>(read_attr(usb_device_dir / "idVendor", 16));
	target.product_id = static_cast<std::uint16_t>(read_attr(usb_device_dir / "idProduct", 16));
	target.bus_number = static_cast<std::uint8_t>(read_attr(usb_device_dir / "busnum", 10));
	target.device_address = static_cast<std::uint8_t>(read_attr(usb_device_dir / "devnum", 10));

	libusb_context *context = nullptr;
	if (libusb_init(&context) != LIBUSB_SUCCESS)
		throw std::runtime_error("libusb_init failed");

	libusb_device **devices = nullptr;
	const auto count = libusb_get_device_list(context, &devices);
	if (count < 0) {
		libusb_exit(context);
		throw std::runtime_error("libusb_get_device_list failed");
	}

	bool found = false;
	for (ssize_t i = 0; i < count && !found; ++i) {
		libusb_device *device = devices[i];
		if (libusb_get_bus_number(device) != target.bus_number ||
			libusb_get_device_address(device) != target.device_address)
			continue;
		found = find_extension_unit(device, target);
	}
	libusb_free_device_list(devices, 1);
	libusb_exit(context);

	if (!found)
		throw std::runtime_error("no UVC extension unit found for device: " + video_device);

	return target;
}

} // namespace gm86x
