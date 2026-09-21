#pragma once

#include <cstdint>
#include <vector>

#include "i2c_transport.hpp"

struct libusb_context;
struct libusb_device_handle;

namespace gm86x {

class WindowsUsbXuTransport final : public I2cTransport {
public:
    WindowsUsbXuTransport(std::uint16_t vendor_id, std::uint16_t product_id,
                          std::size_t device_index, std::uint8_t control_interface,
                          std::uint8_t unit, std::uint8_t selector,
                          unsigned timeout_ms = 2000);
    ~WindowsUsbXuTransport() override;

    WindowsUsbXuTransport(const WindowsUsbXuTransport &) = delete;
    WindowsUsbXuTransport &operator=(const WindowsUsbXuTransport &) = delete;

    void write(std::uint8_t reg, const std::vector<std::uint8_t> &data) override;
    std::vector<std::uint8_t> read(std::uint8_t reg, std::size_t length) override;

private:
    void set_cur(const std::vector<std::uint8_t> &data);
    void get_cur(std::vector<std::uint8_t> &data);

    libusb_context *context_;
    libusb_device_handle *handle_;
    std::uint8_t interface_;
    std::uint8_t unit_;
    std::uint8_t selector_;
    unsigned timeout_ms_;
};

} // namespace gm86x
