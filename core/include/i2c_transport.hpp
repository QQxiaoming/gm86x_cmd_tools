#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gm86x {
class I2cTransport {
  public:
    virtual ~I2cTransport() = default;
    virtual void write(std::uint8_t reg, const std::vector<std::uint8_t> &data) = 0;
    virtual std::vector<std::uint8_t> read(std::uint8_t reg, std::size_t length) = 0;
};

} // namespace gm86x