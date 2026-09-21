#include "linux_usb_xu.hpp"

#include <libusb-1.0/libusb.h>

#include <cstring>
#include <stdexcept>

namespace gm86x {

namespace {

libusb_device_handle *open_target_device(libusb_context *context, const UsbXuTarget &target) {
	libusb_device **devices = nullptr;
	const auto count = libusb_get_device_list(context, &devices);
	if (count < 0)
		throw std::runtime_error("libusb_get_device_list failed");

	libusb_device *match = nullptr;
	for (ssize_t i = 0; i < count; ++i) {
		libusb_device *device = devices[i];
		if (libusb_get_bus_number(device) == target.bus_number &&
			libusb_get_device_address(device) == target.device_address) {
			match = device;
			break;
		}
	}

	libusb_device_handle *handle = nullptr;
	if (match) {
		const auto result = libusb_open(match, &handle);
		if (result != LIBUSB_SUCCESS) {
			libusb_free_device_list(devices, 1);
			throw std::runtime_error(std::string("libusb_open failed: ") + libusb_error_name(result));
		}
	}
	libusb_free_device_list(devices, 1);

	if (!handle)
		throw std::runtime_error("USB device not found (bus/address may have changed, retry)");
	return handle;
}

} // namespace

LinuxUsbXuTransport::LinuxUsbXuTransport(const UsbXuTarget &target, std::uint8_t unit, std::uint8_t selector,
										  unsigned timeout_ms)
	: context_(nullptr), handle_(nullptr), interface_(target.control_interface), unit_(unit), selector_(selector),
	  timeout_ms_(timeout_ms), detached_kernel_driver_(false) {
	if (libusb_init(&context_) != LIBUSB_SUCCESS)
		throw std::runtime_error("libusb_init failed");

	try {
		handle_ = open_target_device(context_, target);

		if (libusb_kernel_driver_active(handle_, interface_) == 1) {
			if (libusb_detach_kernel_driver(handle_, interface_) == LIBUSB_SUCCESS)
				detached_kernel_driver_ = true;
		}

		const auto claim_result = libusb_claim_interface(handle_, interface_);
		if (claim_result != LIBUSB_SUCCESS)
			throw std::runtime_error(std::string("libusb_claim_interface failed: ") +
									  libusb_error_name(claim_result));
	} catch (...) {
		if (handle_)
			libusb_close(handle_);
		libusb_exit(context_);
		throw;
	}
}

LinuxUsbXuTransport::~LinuxUsbXuTransport() {
	if (handle_) {
		libusb_release_interface(handle_, interface_);
		if (detached_kernel_driver_)
			libusb_attach_kernel_driver(handle_, interface_);
		libusb_close(handle_);
	}
	if (context_)
		libusb_exit(context_);
}

void LinuxUsbXuTransport::set_cur(const std::vector<std::uint8_t> &data) {
	std::vector<std::uint8_t> buffer = data;
	const std::uint16_t value = static_cast<std::uint16_t>(selector_) << 8;
	const std::uint16_t index = static_cast<std::uint16_t>((static_cast<std::uint16_t>(unit_) << 8) | interface_);
	const auto result = libusb_control_transfer(handle_, request_type_set, uvc_set_cur, value, index, buffer.data(),
												 static_cast<std::uint16_t>(buffer.size()), timeout_ms_);
	if (result < 0)
		throw std::runtime_error(std::string("USB XU SET_CUR failed: ") + libusb_error_name(result));
	if (static_cast<std::size_t>(result) != buffer.size())
		throw std::runtime_error("USB XU SET_CUR short transfer");
}

void LinuxUsbXuTransport::get_cur(std::vector<std::uint8_t> &data) {
	const std::uint16_t value = static_cast<std::uint16_t>(selector_) << 8;
	const std::uint16_t index = static_cast<std::uint16_t>((static_cast<std::uint16_t>(unit_) << 8) | interface_);
	const auto result = libusb_control_transfer(handle_, request_type_get, uvc_get_cur, value, index, data.data(),
												 static_cast<std::uint16_t>(data.size()), timeout_ms_);
	if (result < 0)
		throw std::runtime_error(std::string("USB XU GET_CUR failed: ") + libusb_error_name(result));
	if (static_cast<std::size_t>(result) != data.size())
		throw std::runtime_error("USB XU GET_CUR short transfer");
}

void LinuxUsbXuTransport::write(std::uint8_t reg, const std::vector<std::uint8_t> &data) {
	std::vector<std::uint8_t> request{reg};
	request.insert(request.end(), data.begin(), data.end());
	set_cur(request);
}

std::vector<std::uint8_t> LinuxUsbXuTransport::read(std::uint8_t reg, std::size_t length) {
	set_cur({reg});
	std::vector<std::uint8_t> result(length);
	get_cur(result);
	return result;
}

} // namespace gm86x
