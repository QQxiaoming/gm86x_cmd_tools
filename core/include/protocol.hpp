#pragma once
#include "i2c_transport.hpp"
#include <chrono>
#include <cstdint>
#include <vector>

namespace gm86x {
struct RequestTiming {
    std::chrono::milliseconds send{};
    std::chrono::milliseconds ready_wait{};
    std::chrono::milliseconds response{};
    std::chrono::milliseconds total{};
};

struct Response {
    std::uint8_t sequence;
    std::uint8_t opcode;
    std::uint8_t status;
    std::vector<std::uint8_t> payload;
    RequestTiming timing{};
};

class ProtocolClient {
  public:
    explicit ProtocolClient(I2cTransport &transport);
    void set_polling(std::size_t retries, std::chrono::microseconds interval);
    void send(std::uint8_t sequence, std::uint8_t opcode, const std::vector<std::uint8_t> &payload);
    Response receive();
    Response command(std::uint8_t sequence, std::uint8_t opcode, const std::vector<std::uint8_t> &payload = {});
    std::vector<std::uint8_t> raw_read(std::uint8_t reg, std::size_t length);
    void raw_write(std::uint8_t reg, const std::vector<std::uint8_t> &data);

  private:
    const std::uint8_t request_reg = 0x10, response_reg = 0x60, ready_reg = 0x66, ready = 0x5a;
    I2cTransport &transport_;
    std::chrono::steady_clock::time_point request_started_;
    std::chrono::steady_clock::time_point request_sent_;
    std::chrono::steady_clock::time_point response_ready_;
    std::size_t poll_retries_ = 1000;
    std::chrono::microseconds poll_interval_ = std::chrono::milliseconds(5);
};

const char *status_name(std::uint8_t status);

} // namespace gm86x