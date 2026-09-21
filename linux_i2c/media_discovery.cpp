#include "media_discovery.hpp"
#include <cstdio>
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <fstream>

namespace gm86x {

DeviceAddress discover_device() {
    const std::regex id_pattern(R"(([0-9]+)-([0-9a-fA-F]+))");
    
    // 1. 换个思路：先通过必然存在的 video4linux 类别寻找子设备名称
    const std::filesystem::path v4l_class_path("/sys/class/video4linux");
    if (!std::filesystem::exists(v4l_class_path)) {
        throw std::runtime_error("sysfs video4linux class path not found");
    }

    for (const auto &entry : std::filesystem::directory_iterator(v4l_class_path)) {
        const auto subdev_name = entry.path().filename().string();
        if (subdev_name.rfind("v4l-subdev", 0) != 0)
            continue;

        // 2. 读取子设备名字
        std::ifstream name_file(entry.path() / "name");
        if (!name_file.is_open())
            continue;

        std::string line;
        if (std::getline(name_file, line)) {
            // 3. 匹配目标子设备字符串
            if (line.find("GM800 IR_L") == std::string::npos)
                continue;

            // 4. 正则匹配 I2C ID 信息
            std::smatch match;
            if (!std::regex_search(line, match, id_pattern))
                continue;

            const auto prefix = match[1].str(); // 总线号
            const auto suffix = match[2].str(); // 芯片地址

            if (suffix == "0066" || suffix == "006a") {
                
                // 5. 【核心】：寻找这个子设备属于哪个 media 节点
                // 在内核设备模型中，该子设备的父级目录或同级 device 目录下会有 mediaX 的引用
                // 常见的 Jetson 拓扑路径为: /sys/class/video4linux/v4l-subdevX/device/mediaX
                std::string dev_path = "/dev/media0"; // 安全的默认 fallback
                
                std::filesystem::path parent_device_path = entry.path() / "device";
                if (std::filesystem::exists(parent_device_path)) {
                    for (const auto &p_entry : std::filesystem::directory_iterator(parent_device_path)) {
                        const auto p_name = p_entry.path().filename().string();
                        // 寻找名字叫 media0, media1 ... 的目录或软链接
                        if (p_name.rfind("media", 0) == 0) {
                            dev_path = "/dev/" + p_name;
                            break;
                        }
                    }
                }

                return {
                    prefix,
                    static_cast<std::uint8_t>(suffix == "0066" ? 0x1a : 0x1b),
                    dev_path // 这里动态返回了它所属的 /dev/mediaX
                };
            }
        }
    }

    throw std::runtime_error("no valid GM800 IR_L device found in any v4l-subdev topology");
}

} // namespace gm86x
