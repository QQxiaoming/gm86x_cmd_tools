#include "windows_usb_xu.hpp"

#include "libusb.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace gm86x {

namespace {

std::string usb_error(int code) {
    return libusb_error_name(code) ? libusb_error_name(code) : "unknown libusb error";
}

libusb_device_handle *open_device(libusb_context *context, std::uint16_t vendor_id,
                                  std::uint16_t product_id, std::size_t device_index) {
    libusb_device **devices = nullptr;
    const auto count = libusb_get_device_list(context, &devices);
    if (count < 0)
        throw std::runtime_error("libusb_get_device_list failed: " + usb_error(static_cast<int>(count)));

    std::size_t match_index = 0;
    libusb_device_handle *handle = nullptr;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(devices[i], &descriptor) != LIBUSB_SUCCESS)
            continue;
        if (descriptor.idVendor != vendor_id || descriptor.idProduct != product_id)
            continue;
        if (match_index++ != device_index)
            continue;

        const auto result = libusb_open(devices[i], &handle);
        if (result != LIBUSB_SUCCESS) {
            libusb_free_device_list(devices, 1);
            throw std::runtime_error("libusb_open failed: " + usb_error(result));
        }
        break;
    }
    libusb_free_device_list(devices, 1);

    if (!handle)
        throw std::runtime_error("USB device not found for VID/PID/index");
    return handle;
}

void validate_transfer_size(const std::vector<std::uint8_t> &data) {
    // Parenthesize to avoid clashing with windows.h's max() macro.
    if (data.empty() || data.size() > (std::numeric_limits<std::uint16_t>::max)())
        throw std::invalid_argument("USB XU transfer length must be 1..65535 bytes");
}

} // namespace

WindowsUsbXuTransport::WindowsUsbXuTransport(std::uint16_t vendor_id, std::uint16_t product_id,
                                             std::size_t device_index, std::uint8_t control_interface,
                                             std::uint8_t unit, std::uint8_t selector, unsigned timeout_ms)
    : context_(nullptr), handle_(nullptr), interface_(control_interface), unit_(unit), selector_(selector),
      timeout_ms_(timeout_ms) {
    if (libusb_init(&context_) != LIBUSB_SUCCESS)
        throw std::runtime_error("libusb_init failed");

    try {
        handle_ = open_device(context_, vendor_id, product_id, device_index);
        const auto result = libusb_claim_interface(handle_, interface_);
        if (result != LIBUSB_SUCCESS)
            throw std::runtime_error("libusb_claim_interface failed: " + usb_error(result));
    } catch (...) {
        if (handle_)
            libusb_close(handle_);
        libusb_exit(context_);
        throw;
    }
}

WindowsUsbXuTransport::~WindowsUsbXuTransport() {
    if (handle_) {
        libusb_release_interface(handle_, interface_);
        libusb_close(handle_);
    }
    if (context_)
        libusb_exit(context_);
}

void WindowsUsbXuTransport::set_cur(const std::vector<std::uint8_t> &data) {
    validate_transfer_size(data);
    std::vector<std::uint8_t> buffer = data;
    const auto value = static_cast<std::uint16_t>(selector_) << 8;
    const auto index = static_cast<std::uint16_t>((static_cast<std::uint16_t>(unit_) << 8) | interface_);
    // interface_ is a Vendor Specific interface (not UVC Video Control), so the request type is VENDOR.
    const auto result = libusb_control_transfer(handle_, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
                                                        LIBUSB_RECIPIENT_INTERFACE,
                                                0x01, value, index, buffer.data(),
                                                static_cast<std::uint16_t>(buffer.size()), timeout_ms_);
    if (result < 0)
        throw std::runtime_error("USB XU SET_CUR failed: " + usb_error(result));
    if (static_cast<std::size_t>(result) != buffer.size())
        throw std::runtime_error("USB XU SET_CUR short transfer");
}

void WindowsUsbXuTransport::get_cur(std::vector<std::uint8_t> &data) {
    validate_transfer_size(data);
    const auto value = static_cast<std::uint16_t>(selector_) << 8;
    const auto index = static_cast<std::uint16_t>((static_cast<std::uint16_t>(unit_) << 8) | interface_);
    // interface_ is a Vendor Specific interface (not UVC Video Control), so the request type is VENDOR.
    const auto result = libusb_control_transfer(handle_, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
                                                        LIBUSB_RECIPIENT_INTERFACE,
                                                0x81, value, index, data.data(),
                                                static_cast<std::uint16_t>(data.size()), timeout_ms_);
    if (result < 0)
        throw std::runtime_error("USB XU GET_CUR failed: " + usb_error(result));
    if (static_cast<std::size_t>(result) != data.size())
        throw std::runtime_error("USB XU GET_CUR short transfer");
}

void WindowsUsbXuTransport::write(std::uint8_t reg, const std::vector<std::uint8_t> &data) {
    std::vector<std::uint8_t> request{reg};
    request.insert(request.end(), data.begin(), data.end());
    set_cur(request);
}

std::vector<std::uint8_t> WindowsUsbXuTransport::read(std::uint8_t reg, std::size_t length) {
    set_cur({reg});
    std::vector<std::uint8_t> result(length);
    get_cur(result);
    return result;
}

} // namespace gm86x
