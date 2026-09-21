#include "protocol.hpp"
#include "bytes.hpp"
#include <algorithm>
#include <stdexcept>
#include <thread>

namespace gm86x {

ProtocolClient::ProtocolClient(I2cTransport &transport) : transport_(transport) {
}

void ProtocolClient::set_polling(std::size_t retries, std::chrono::microseconds interval) {
    if (retries == 0 || interval.count() < 0)
        throw std::invalid_argument("invalid polling configuration");
    poll_retries_ = retries;
    poll_interval_ = interval;
}

void ProtocolClient::send(std::uint8_t sequence, std::uint8_t opcode, const std::vector<std::uint8_t> &payload) {
    constexpr std::size_t max_payload_size = 8187;
    if (payload.size() > max_payload_size)
        throw std::invalid_argument("payload exceeds Linux I2C message limit of 8187 bytes");
    std::vector<std::uint8_t> frame{sequence, opcode};
    append_le_u16(frame, static_cast<std::uint16_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    request_started_ = std::chrono::steady_clock::now();
    transport_.write(request_reg, frame);
    request_sent_ = std::chrono::steady_clock::now();
}

Response ProtocolClient::receive() {
    for (std::size_t attempt = 0; attempt < poll_retries_; ++attempt) {
        if (transport_.read(ready_reg, 1).at(0) == ready)
            break;
        if (attempt + 1 == poll_retries_)
            throw std::runtime_error("response timeout after " + std::to_string(poll_retries_) + " polls");
        std::this_thread::sleep_for(poll_interval_);
    }
    response_ready_ = std::chrono::steady_clock::now();
    const auto header = transport_.read(response_reg, 5);
    if (header.size() != 5)
        throw std::runtime_error("short response header");
    const auto length = read_le_u16(header, 3);
    auto payload = length ? transport_.read(0x65, length) : std::vector<std::uint8_t>{};
    if (payload.size() != length)
        throw std::runtime_error("short response payload");
    const auto response_finished = std::chrono::steady_clock::now();
    transport_.write(ready_reg, {0});
    const auto elapsed = [](auto begin, auto end) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    };
    return {header[0],
            header[1],
            header[2],
            std::move(payload),
            {elapsed(request_started_, request_sent_), elapsed(request_sent_, response_ready_),
             elapsed(response_ready_, response_finished), elapsed(request_started_, response_finished)}};
}

Response ProtocolClient::command(std::uint8_t sequence, std::uint8_t opcode, const std::vector<std::uint8_t> &payload) {
    send(sequence, opcode, payload);
    auto response = receive();
    if (response.sequence != sequence || response.opcode != opcode)
        throw std::runtime_error("response sequence/opcode mismatch");
    return response;
}

std::vector<std::uint8_t> ProtocolClient::raw_read(std::uint8_t reg, std::size_t length) {
    return transport_.read(reg, length);
}

void ProtocolClient::raw_write(std::uint8_t reg, const std::vector<std::uint8_t> &data) {
    transport_.write(reg, data);
}

const char *status_name(std::uint8_t status) {
    switch (status) {
    case 0:
        return "OK";
    case 1:
        return "BAD_COMMAND";
    case 2:
        return "BAD_LENGTH";
    case 3:
        return "BAD_PARAM";
    case 4:
        return "BUSY";
    case 5:
        return "I2C_ERROR";
    case 6:
        return "NOT_IMPLEMENTED";
    default:
        return "UNKNOWN";
    }
}

} // namespace gm86x