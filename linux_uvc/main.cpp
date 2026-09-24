
#include "bytes.hpp"
#include "commands.hpp"
#include "linux_usb_xu.hpp"
#include "protocol.hpp"
#include "usb_discovery.hpp"

#include <chrono>
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
    -d, --device <path>      UVC video node, default /dev/video6

Talks to the UVC extension unit directly via libusb control transfers
(bypassing the uvcvideo driver's XU control size limit). UVC I2C_FORWARD
uses fixed XU Control Selector 0x05.
)";
	gm86x::CommandDispatcher::print_help(exe_name);
}

int main(int argc, char **argv) {
	try {
		std::string device = "/dev/video6";
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
			if ((option == "-d" || option == "--device") && index < argc)
				device = argv[index++];
			else if (option == "--buffer-updates") {
				if (index >= argc || (std::string(argv[index]) != "0" && std::string(argv[index]) != "1"))
					throw std::invalid_argument("--buffer-updates requires <0|1>");
				buffer_updates = std::string(argv[index++]) == "1";
			}
			else if (option == "--end-update")
				end_update = true;
			else if (option == "--seq" && index < argc)
				sequence = gm86x::parse_byte(argv[index++]);
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
		const auto target = gm86x::discover_usb_xu_target(device);
		const std::uint8_t unit = target.extension_unit_id;
		printf("Discovered USB XU target: vendor_id=0x%04x, product_id=0x%04x, bus_number=%u, device_address=%u, control_interface=%u, extension_unit_id=%u\n",
			target.vendor_id, target.product_id, target.bus_number, target.device_address, target.control_interface, target.extension_unit_id);
		gm86x::LinuxUsbXuTransport transport(target, unit, i2c_forward_selector);
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
	return 0;
}