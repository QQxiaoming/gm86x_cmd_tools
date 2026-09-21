#include "bytes.hpp"
#include "commands.hpp"
#include "media_discovery.hpp"
#include "protocol.hpp"
#include "linux_i2c.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

static void print_help(const std::string &exe_name) {
    std::cout << R"(Usage:
    )" << exe_name << R"( [global options] <command> [args...]

Global options:
    -b, --bus <index>        I2C bus index, default 9
    -a, --addr <7bit_hex>    I2C 7-bit address, default 0x1a
    -s, --scan <0|1>         resolve I2C bus/address from /dev/media*)";
	gm86x::CommandDispatcher::print_help(exe_name);
}

int main(int argc, char **argv) {
    try {
        std::string bus = "9";
        std::uint8_t address = 0x1a;
        std::uint8_t sequence = 1;
        bool scan = true;
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
                gm86x::CommandDispatcher::print_help(argv[0]);
                return 0;
            }
            if (option == "-s" || option == "--scan") {
                if (index >= argc || (std::string(argv[index]) != "0" && std::string(argv[index]) != "1"))
                    throw std::invalid_argument(option + " requires <0|1>");
                scan = std::string(argv[index++]) == "1";
            } else if (option == "--buffer-updates") {
                if (index >= argc || (std::string(argv[index]) != "0" && std::string(argv[index]) != "1"))
                    throw std::invalid_argument("--buffer-updates requires <0|1>");
                buffer_updates = std::string(argv[index++]) == "1";
            }
            else if (option == "--end-update")
                end_update = true;
            else if (option == "--seq" && index < argc)
                sequence = gm86x::parse_byte(argv[index++]);
            else if ((option == "-b" || option == "--bus") && index < argc)
                bus = argv[index++];
            else if ((option == "-a" || option == "--addr") && index < argc)
                address = gm86x::parse_byte(argv[index++]);
            else if ((option == "-r" || option == "--retries") && index < argc)
                retries = gm86x::parse_decimal_or_hex_u32(argv[index++]);
            else if ((option == "-i" || option == "--interval") && index < argc) {
                const auto seconds = std::stod(argv[index++]);
                if (seconds < 0)
                    throw std::invalid_argument("interval must be >= 0");
                interval =
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<double>(seconds));
            } else
                throw std::invalid_argument("unknown or incomplete option: " + option);
        }
        if (index >= argc)
            throw std::invalid_argument("missing command");
        if (scan) {
            const auto found = gm86x::discover_device();
            bus = found.bus;
            address = found.address;
            gm86x::CommandGroup::print_section("scan");
            gm86x::CommandGroup::print_field("bus") << bus << '\n';
            gm86x::CommandGroup::print_field("addr") << "0x" << std::hex << static_cast<int>(address) << std::dec << '\n';
        }
        gm86x::LinuxI2cTransport transport(bus, address);
        gm86x::ProtocolClient client(transport);
        client.set_polling(retries, interval);
        std::vector<std::string> args;
        for (int i = index + 1; i < argc; ++i)
            args.emplace_back(argv[i]);
        gm86x::CommandContext context{client, sequence, buffer_updates, end_update};
        gm86x::CommandDispatcher dispatcher(context);
        return dispatcher.run(argv[index], args);
    } catch (const std::exception &error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
    return 0;
}