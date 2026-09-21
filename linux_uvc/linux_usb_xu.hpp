#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "i2c_transport.hpp"
#include "usb_discovery.hpp"

struct libusb_context;
struct libusb_device_handle;

namespace gm86x {

// Emulates the UVC extension-unit SET_CUR/GET_CUR class-specific control
// requests directly over libusb, bypassing the uvcvideo driver's control
// transfer size limit (ENOBUFS beyond ~a few hundred bytes on many kernels).
class LinuxUsbXuTransport final : public I2cTransport {
	public:
		LinuxUsbXuTransport(const UsbXuTarget &target, std::uint8_t unit, std::uint8_t selector,
							 unsigned timeout_ms = 2000);
		~LinuxUsbXuTransport() override;
		LinuxUsbXuTransport(const LinuxUsbXuTransport &) = delete;
		LinuxUsbXuTransport &operator=(const LinuxUsbXuTransport &) = delete;

		void write(std::uint8_t reg, const std::vector<std::uint8_t> &data) override;
		std::vector<std::uint8_t> read(std::uint8_t reg, std::size_t length) override;

	private:
		static constexpr std::uint8_t uvc_set_cur = 0x01;
		static constexpr std::uint8_t uvc_get_cur = 0x81;
		static constexpr std::uint8_t request_type_set = 0x21; // host->device, class, interface
		static constexpr std::uint8_t request_type_get = 0xA1; // device->host, class, interface

		void set_cur(const std::vector<std::uint8_t> &data);
		void get_cur(std::vector<std::uint8_t> &data);

		libusb_context *context_;
		libusb_device_handle *handle_;
		std::uint8_t interface_;
		std::uint8_t unit_;
		std::uint8_t selector_;
		unsigned timeout_ms_;
		bool detached_kernel_driver_;
};

} // namespace gm86x
