#include "linux_i2c.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

namespace gm86x {

void LinuxI2cTransport::transfer(int fd, std::uint8_t address, std::vector<std::uint8_t> &write_data,
              std::vector<std::uint8_t> *read_data) {
    i2c_msg messages[2]{};
    messages[0] = {address, 0, static_cast<__u16>(write_data.size()), write_data.data()};
    if (read_data)
        messages[1] = {address, I2C_M_RD, static_cast<__u16>(read_data->size()), read_data->data()};
    i2c_rdwr_ioctl_data request{messages, static_cast<__u32>(read_data ? 2 : 1)};
    if (ioctl(fd, I2C_RDWR, &request) < 0)
        throw std::runtime_error(std::string("I2C transfer failed: ") + std::strerror(errno));
}

std::string LinuxI2cTransport::device_path(const std::string &bus) {
    if (bus.rfind("/dev/", 0) == 0)
        return bus;
    if (bus.rfind("i2c-", 0) == 0)
        return "/dev/" + bus;
    return "/dev/i2c-" + bus;
}

LinuxI2cTransport::LinuxI2cTransport(const std::string &bus, std::uint8_t address) : fd_(-1), address_(address) {
    fd_ = open(device_path(bus).c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0)
        throw std::runtime_error(std::string("cannot open I2C bus: ") + std::strerror(errno));
}

LinuxI2cTransport::~LinuxI2cTransport() {
    if (fd_ >= 0)
        close(fd_);
}

void LinuxI2cTransport::write(std::uint8_t reg, const std::vector<std::uint8_t> &data) {
    std::vector<std::uint8_t> request{reg};
    request.insert(request.end(), data.begin(), data.end());
    transfer(fd_, address_, request, nullptr);
}

std::vector<std::uint8_t> LinuxI2cTransport::read(std::uint8_t reg, std::size_t length) {
    std::vector<std::uint8_t> request{reg};
    std::vector<std::uint8_t> result(length);
    transfer(fd_, address_, request, &result);
    return result;
}

} // namespace gm86x