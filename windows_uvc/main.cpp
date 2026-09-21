#include "bytes.hpp"
#include "commands.hpp"
#include "protocol.hpp"
#include "windows_usb_xu.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

const static std::uint8_t i2c_forward_selector = 0x05;
const static std::size_t max_chunk_size = 250;

static void print_help(const std::string &exe_name) {
    std::cout << R"(Usage:
    )" << exe_name << R"( [global options] <command> [args...]

Global options:
    -v, --vid <hex>         USB vendor id, default 0x2ac1
    -p, --pid <hex>         USB product id, default 0xfd00
    -n, --index <decimal>   matching device index, default 0
    -i, --interface <hex>   UVC VideoControl interface, default 0
    -u, --unit <hex>        UVC extension unit id, default 04

UVC I2C_FORWARD uses fixed XU Control Selector 0x05.
)";
    gm86x::CommandDispatcher::print_help(exe_name);
}

int main(int argc, char **argv) {
    try {
        std::uint16_t vendor_id = 0x2ac1;
        std::uint16_t product_id = 0xfd00;
        std::size_t device_index = 0;
        std::uint8_t control_interface = 0;
        std::uint8_t unit = 4;
        std::uint8_t sequence = 1;
        std::optional<bool> buffer_updates;
        bool end_update = false;
        std::size_t retries = 1000;
        auto interval = std::chrono::microseconds(5000);
        int index = 1;

        if (argc < 2) {
            print_help(argv[0]);
            return 1;
        }
        while (index < argc && std::string(argv[index]).starts_with('-')) {
            const std::string option = argv[index++];
            if (option == "--")
                break;
            if (option == "-h" || option == "--help") {
                print_help(argv[0]);
                return 0;
            }
            if ((option == "-v" || option == "--vid") && index < argc)
                vendor_id = gm86x::parse_u16(argv[index++]);
            else if ((option == "-p" || option == "--pid") && index < argc)
                product_id = gm86x::parse_u16(argv[index++]);
            else if ((option == "-n" || option == "--index") && index < argc)
                device_index = gm86x::parse_decimal_or_hex_u32(argv[index++]);
            else if ((option == "-i" || option == "--interface") && index < argc)
                control_interface = gm86x::parse_byte(argv[index++]);
            else if ((option == "-u" || option == "--unit") && index < argc)
                unit = gm86x::parse_byte(argv[index++]);
            else if (option == "--buffer-updates") {
                if (index >= argc || (std::string(argv[index]) != "0" && std::string(argv[index]) != "1"))
                    throw std::invalid_argument("--buffer-updates requires <0|1>");
                buffer_updates = std::string(argv[index++]) == "1";
            } else if (option == "--end-update")
                end_update = true;
            else if (option == "--seq" && index < argc)
                sequence = gm86x::parse_byte(argv[index++]);
            else if ((option == "-r" || option == "--retries") && index < argc)
                retries = gm86x::parse_decimal_or_hex_u32(argv[index++]);
            else if ((option == "-t" || option == "--interval") && index < argc) {
                const auto seconds = std::stod(argv[index++]);
                if (seconds < 0)
                    throw std::invalid_argument("interval must be >= 0");
                interval = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::duration<double>(seconds));
            } else
                throw std::invalid_argument("unknown or incomplete option: " + option);
        }
        if (index >= argc)
            throw std::invalid_argument("missing command");

        gm86x::WindowsUsbXuTransport transport(vendor_id, product_id, device_index,
                                               control_interface, unit, i2c_forward_selector);
        gm86x::ProtocolClient client(transport);
        client.set_polling(retries, interval);
        std::vector<std::string> args;
        for (int i = index + 1; i < argc; ++i)
            args.emplace_back(argv[i]);
        gm86x::CommandContext context{client, sequence, buffer_updates, end_update};
        context.max_control_transfer_size = max_chunk_size;
        context.max_response_payload_size = max_chunk_size;
        gm86x::CommandDispatcher dispatcher(context);
        return dispatcher.run(argv[index], args);
    } catch (const std::exception &error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
