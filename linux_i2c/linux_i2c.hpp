#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "i2c_transport.hpp"

namespace gm86x {

class LinuxI2cTransport final : public I2cTransport {
  public:
    LinuxI2cTransport(const std::string &bus, std::uint8_t address);
    ~LinuxI2cTransport() override;
    void write(std::uint8_t reg, const std::vector<std::uint8_t> &data) override;
    std::vector<std::uint8_t> read(std::uint8_t reg, std::size_t length) override;

  private:
    static std::string device_path(const std::string &bus);
    static void transfer(int fd, std::uint8_t address, std::vector<std::uint8_t> &write_data,
              std::vector<std::uint8_t> *read_data);
    int fd_;
    std::uint8_t address_;
};

} // namespace gm86x