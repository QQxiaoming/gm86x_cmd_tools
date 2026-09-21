#include "usb_discovery.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace gm86x {

namespace {

std::string basename_of(const fs::path &path) { return path.filename().string(); }

unsigned read_attr(const fs::path &file, int base) {
	std::ifstream stream(file);
	if (!stream.is_open())
		throw std::runtime_error("cannot read sysfs attribute: " + file.string());
	std::string value;
	std::getline(stream, value);
	return static_cast<unsigned>(std::stoul(value, nullptr, base));
}

// USB interface directories are named "<bus>-<port>[.<port>...]:<config>.<iface>".
bool looks_like_interface_dir(const std::string &name) { return name.find(':') != std::string::npos; }

} // namespace

UsbXuTarget discover_usb_xu_target(const std::string &video_device) {
	const auto node_name = basename_of(fs::path(video_device));
	const fs::path class_link = fs::path("/sys/class/video4linux") / node_name / "device";
	if (!fs::exists(class_link))
		throw std::runtime_error("no sysfs entry for video device: " + video_device);

	const fs::path streaming_iface_dir = fs::canonical(class_link);
	const fs::path usb_device_dir = streaming_iface_dir.parent_path();

	if (!fs::exists(usb_device_dir / "idVendor") || !fs::exists(usb_device_dir / "idProduct"))
		throw std::runtime_error("resolved sysfs path is not a USB device: " + usb_device_dir.string());

	UsbXuTarget target{};
	target.vendor_id = static_cast<std::uint16_t>(read_attr(usb_device_dir / "idVendor", 16));
	target.product_id = static_cast<std::uint16_t>(read_attr(usb_device_dir / "idProduct", 16));
	target.bus_number = static_cast<std::uint8_t>(read_attr(usb_device_dir / "busnum", 10));
	target.device_address = static_cast<std::uint8_t>(read_attr(usb_device_dir / "devnum", 10));

	constexpr unsigned uvc_class = 0x0e;
	constexpr unsigned uvc_subclass_control = 0x01;
	bool found_control_interface = false;
	for (const auto &entry : fs::directory_iterator(usb_device_dir)) {
		if (!entry.is_directory())
			continue;
		const auto name = basename_of(entry.path());
		if (!looks_like_interface_dir(name))
			continue;
		const fs::path class_file = entry.path() / "bInterfaceClass";
		const fs::path subclass_file = entry.path() / "bInterfaceSubClass";
		const fs::path number_file = entry.path() / "bInterfaceNumber";
		if (!fs::exists(class_file) || !fs::exists(subclass_file) || !fs::exists(number_file))
			continue;
		if (read_attr(class_file, 16) != uvc_class || read_attr(subclass_file, 16) != uvc_subclass_control)
			continue;
		target.control_interface = static_cast<std::uint8_t>(read_attr(number_file, 16));
		found_control_interface = true;
		break;
	}
	if (!found_control_interface)
		throw std::runtime_error("no UVC VideoControl interface found for device: " + video_device);

	return target;
}

} // namespace gm86x
