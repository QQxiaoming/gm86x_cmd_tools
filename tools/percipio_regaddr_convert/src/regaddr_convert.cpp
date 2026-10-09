#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "regaddr_convert.hpp"
#include "regaddr_lookup.hpp"
#include "regaddr_csv_data.h"

namespace fs = std::filesystem;

namespace {

struct CsvRegister {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 4;
    std::vector<uint32_t> bases;
    std::string base_name;
    std::string kind;
    std::string access;
    std::string description;
};

struct Entry {
    std::string name;
    uint32_t offset;
    uint32_t size;
    uint32_t base;
    std::string source;
    std::string kind;
    std::string access;
    std::string description;

    uint32_t address() const { return base + offset; }
};

std::string trim(const std::string &text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool ends_with(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size()
        && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_register_name(const std::string &text)
{
    if (text.starts_with("Current") && ends_with(text, "_BaseRegAddr")) {
        return false;
    }
    return ends_with(text, "_RegAddr") || ends_with(text, "_BaseRegAddr") || ends_with(text, "_Reg");
}

std::string normalize(const std::string &text)
{
    std::string result;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            result.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return result;
}

std::vector<std::string> parse_csv_line(const std::string &line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    fields.push_back(trim(field));
    return fields;
}

uint32_t parse_u32(const std::string &text, int base = 0)
{
    size_t consumed = 0;
    const unsigned long value = std::stoul(trim(text), &consumed, base);
    if (consumed != trim(text).size() || value > UINT32_MAX) {
        throw std::invalid_argument("invalid 32-bit number: " + text);
    }
    return static_cast<uint32_t>(value);
}

std::vector<CsvRegister> parse_register_csv(
    std::istream &input, const std::string &origin, bool absolute)
{
    static const std::regex base_re(
        R"(^//[-\s]*(?:[A-Za-z][A-Za-z0-9_-]*\s*)?BASE ADDR:\s*(0x[0-9a-fA-F]+))");

    std::vector<CsvRegister> registers;
    std::vector<uint32_t> bases = absolute ? std::vector<uint32_t>{}
                                            : std::vector<uint32_t>{0};
    bool collecting_bases = false;
    bool in_block = false;
    uint32_t current_address = 0;
    std::string line;
    size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        const std::string stripped = trim(line);
        if (stripped.rfind("%xmlcode", 0) == 0
            || stripped.rfind("%event_xmlcode", 0) == 0
            || stripped.rfind("%headercode", 0) == 0) {
            in_block = true;
            continue;
        }
        if (stripped.rfind("%enddef", 0) == 0) {
            in_block = false;
            continue;
        }
        if (in_block || stripped.empty()) {
            continue;
        }
        if (stripped.rfind("//", 0) == 0) {
            std::smatch match;
            if (!absolute && std::regex_search(stripped, match, base_re)) {
                if (!collecting_bases) {
                    bases.clear();
                    collecting_bases = true;
                }
                bases.push_back(parse_u32(match[1].str()));
            }
            continue;
        }

        const auto row = parse_csv_line(line);
        if (row.empty()) {
            continue;
        }
        collecting_bases = false;

        const auto column = [&row](size_t index) -> std::string {
            return index < row.size() ? row[index] : std::string{};
        };
        const std::string kind = column(1);
        if (kind != "integer" && kind != "float" && kind != "struct"
            && kind != "byte array" && kind != "string" && kind != "EventId") {
            continue;
        }

        try {
            uint32_t offset = current_address;
            uint32_t size = 4;
            if (kind == "EventId") {
                offset = parse_u32(column(2));
            } else {
                size = parse_u32(column(2), 10);
                if (!column(3).empty()) {
                    current_address = parse_u32(column(3));
                }
                offset = current_address;
                current_address += size;
                if (current_address % 4U != 0) {
                    // Match generate_genicam_hearderfile.py exactly. Its expression
                    // `(m_address >> 2 + 1) * 4` shifts by three due to precedence.
                    current_address = (current_address >> 3U) * 4U;
                }
            }

            if (is_register_name(row[0])) {
                CsvRegister reg;
                reg.name = row[0];
                reg.offset = offset;
                reg.size = size;
                reg.kind = kind;
                reg.base_name = column(4);
                reg.access = column(5);
                reg.description = column(6);
                reg.bases = absolute ? std::vector<uint32_t>{0} : bases;
                registers.push_back(std::move(reg));
            }
        } catch (const std::exception &error) {
            throw std::runtime_error(origin + ":" + std::to_string(line_number)
                                     + ": " + error.what());
        }
    }
    return registers;
}

std::vector<CsvRegister> load_register_csv(
    const fs::path &csv_dir, const fs::path &relative_path, bool absolute)
{
    const std::string relative = relative_path.generic_string();

    if (csv_dir.empty()) {
        for (const auto &file : regaddr_csv_data::kFiles) {
            if (file.path == relative) {
                std::istringstream input{std::string(file.content)};
                return parse_register_csv(input, "<builtin>/" + relative, absolute);
            }
        }
        throw std::runtime_error("no built-in CSV for: " + relative);
    }

    const fs::path path = csv_dir / relative_path;
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open CSV: " + path.string());
    }
    return parse_register_csv(input, path.string(), absolute);
}

std::optional<uint32_t> parse_hex(const std::string &text)
{
    const std::string value = trim(text);
    static const std::regex prefixed(R"(^0[xX][0-9a-fA-F]+$)");
    static const std::regex bare(R"(^(?=.*[0-9])[0-9a-fA-F]+$)");
    if (!std::regex_match(value, prefixed) && !std::regex_match(value, bare)) {
        return std::nullopt;
    }
    try {
        return parse_u32(value, 16);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::string hex32(uint32_t value)
{
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return output.str();
}

std::string hex_offset(uint32_t value)
{
    std::ostringstream output;
    output << "0x" << std::hex << value;
    return output.str();
}

class RegisterDatabase {
public:
    RegisterDatabase(const fs::path &csv_dir, const std::string &interface_name)
    {
        static const std::map<std::string, fs::path> bootstrap_dirs = {
            {"gige", "GigE"},
            {"gmsl", "gmsl"},
            {"usb3vision", "usb3vison"},
        };
        const auto interface_it = bootstrap_dirs.find(interface_name);
        if (interface_it == bootstrap_dirs.end()) {
            throw std::runtime_error("invalid interface: " + interface_name);
        }

        struct Source {
            fs::path relative_path;
            bool absolute;
            std::vector<CsvRegister> registers;
        };
        std::vector<Source> sources;
        const std::vector<std::pair<fs::path, bool>> files = {
            {interface_it->second / "BootstrapReg.csv", true},
            {"BaseReg.csv", true},
            {"Device.csv", false},
            {"Stream.csv", false},
            {"LightSource.csv", false},
            {"Additional.csv", false},
            {"Event.csv", false},
        };

        for (const auto &[relative_path, absolute] : files) {
            sources.push_back({relative_path,
                               absolute,
                               load_register_csv(csv_dir, relative_path, absolute)});
            if (relative_path == "BaseReg.csv") {
                for (const auto &reg : sources.back().registers) {
                    if (ends_with(reg.name, "Category_RegAddr")) {
                        categories_[reg.offset] = reg.name;
                    }
                }
            }
        }

        std::unordered_map<std::string, uint32_t> category_by_name;
        for (const auto &[base, name] : categories_) {
            category_by_name[name] = base;
        }

        for (const auto &source : sources) {
            for (const auto &reg : source.registers) {
                std::vector<uint32_t> bases = reg.bases;
                const auto category = category_by_name.find(reg.base_name);
                if (!source.absolute && category != category_by_name.end()) {
                    bases = {category->second};
                }
                if (bases.empty()) {
                    bases = {0};
                }
                for (uint32_t base : bases) {
                    entries_.push_back({reg.name,
                                        reg.offset,
                                        reg.size,
                                        source.absolute ? 0U : base,
                                        source.relative_path.generic_string(),
                                        reg.kind,
                                        reg.access,
                                        reg.description});
                }
            }
        }

        for (const auto &[base, name] : categories_) {
            const std::string short_name =
                name.substr(0, name.size() - std::string("Category_RegAddr").size());
            category_aliases_[normalize(name)] = base;
            category_aliases_[normalize(
                name.substr(0, name.size() - std::string("_RegAddr").size()))] = base;
            category_aliases_[normalize(short_name)] = base;
        }
        add_alias("depth", "DepthCategory_RegAddr", category_by_name);
        add_alias("leftir", "LeftCameraCategory_RegAddr", category_by_name);
        add_alias("rightir", "RightCameraCategory_RegAddr", category_by_name);
        add_alias("color", "ColorCameraCategory_RegAddr", category_by_name);
        add_alias("rgb", "ColorCameraCategory_RegAddr", category_by_name);
        add_alias("laser", "LightSourceCategory_RegAddr", category_by_name);
    }

    size_t size() const { return entries_.size(); }

    std::string full_name(const Entry &entry) const
    {
        std::ostringstream output;
        const auto category = categories_.find(entry.base);
        if (entry.base != 0 && category != categories_.end()) {
            output << category->second << " + ";
        }
        output << entry.name;
        return output.str();
    }

    std::vector<std::string> exact_names(uint32_t address) const
    {
        std::vector<std::string> names;
        for (const auto &entry : entries_) {
            if (entry.address() == address) {
                const auto name = full_name(entry);
                if (std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
        }
        return names;
    }

    std::string describe(const Entry &entry) const
    {
        std::ostringstream output;
        output << hex32(entry.address()) << "  " << full_name(entry)
               << "  [" << entry.source << ", "
               << (entry.kind.empty() ? "?" : entry.kind) << ", " << entry.size << "B";
        if (!entry.access.empty()) {
            output << ", " << entry.access;
        }
        output << "]";
        if (!entry.description.empty()) {
            output << "  " << entry.description;
        }
        return output.str();
    }

    std::vector<std::string> lookup_address(uint32_t value) const
    {
        std::vector<std::string> output;
        for (const auto &entry : entries_) {
            if (entry.address() == value) {
                output.push_back(describe(entry));
            }
        }
        if (output.empty()) {
            for (const auto &entry : entries_) {
                if (entry.size > 4 && value > entry.address()
                    && value < entry.address() + entry.size) {
                    output.push_back(describe(entry) + "  (+"
                                     + hex_offset(value - entry.address()) + ")");
                }
            }
        }
        if (output.empty() && value <= 0x00ffffffU) {
            for (const auto &entry : entries_) {
                if (entry.base != 0 && entry.offset == value) {
                    output.push_back(describe(entry) + "  (as offset)");
                }
            }
        }
        if (output.empty()) {
            std::string message = "not found";
            const uint32_t category_base = value & 0x0f000000U;
            const auto category = categories_.find(category_base);
            if (category_base != 0 && category != categories_.end()) {
                message += " (in " + category->second + ", offset "
                    + hex_offset(value & 0x00ffffffU) + ")";
            }
            output.push_back(std::move(message));
        }
        return output;
    }

    std::vector<std::string> lookup_name(const std::string &text) const
    {
        std::optional<uint32_t> category_base;
        std::string name = trim(text);
        static const std::regex qualified(
            R"(^\s*([A-Za-z0-9_-]+)\s*(?:\+|:|\.)\s*([A-Za-z0-9_-]+)\s*$)");
        std::smatch match;
        if (std::regex_match(text, match, qualified)) {
            const std::string category_key = normalize(match[1].str());
            const auto category = category_aliases_.find(category_key);
            if (category == category_aliases_.end()) {
                return {"unknown category: " + match[1].str()};
            }
            category_base = category->second;
            name = match[2].str();
        }

        const std::string key = normalize(name);
        const std::string full_key =
            ends_with(key, "regaddr") ? key : key + "regaddr";
        const auto matches = [&](bool exact) {
            std::vector<const Entry *> found;
            for (const auto &entry : entries_) {
                if (category_base && entry.base != *category_base) {
                    continue;
                }
                const std::string entry_name = normalize(entry.name);
                const bool name_matches =
                    exact ? (entry_name == full_key || entry_name == key)
                          : entry_name.find(key) != std::string::npos;
                if (name_matches) {
                    found.push_back(&entry);
                }
            }
            return found;
        };

        auto found = matches(true);
        if (found.empty()) {
            found = matches(false);
            std::set<std::string> names;
            for (const auto *entry : found) {
                names.insert(entry->name);
            }
            if (names.size() > 1) {
                std::vector<std::string> output = {
                    "no exact match, " + std::to_string(names.size()) + " candidates:"};
                size_t count = 0;
                for (const auto &candidate : names) {
                    if (count++ == 50) {
                        output.push_back("  ...");
                        break;
                    }
                    output.push_back("  " + candidate);
                }
                return output;
            }
        }
        if (found.empty()) {
            return {"not found"};
        }

        std::vector<std::string> output;
        for (const auto *entry : found) {
            output.push_back(describe(*entry));
        }
        return output;
    }

    void dump() const
    {
        std::vector<const Entry *> sorted;
        for (const auto &entry : entries_) {
            sorted.push_back(&entry);
        }
        std::sort(sorted.begin(), sorted.end(), [](const Entry *left, const Entry *right) {
            return std::make_pair(left->address(), left->name)
                < std::make_pair(right->address(), right->name);
        });
        for (const auto *entry : sorted) {
            std::cout << describe(*entry) << '\n';
        }
    }

private:
    void add_alias(
        const std::string &alias,
        const std::string &category,
        const std::unordered_map<std::string, uint32_t> &category_by_name)
    {
        const auto found = category_by_name.find(category);
        if (found != category_by_name.end()) {
            category_aliases_[alias] = found->second;
        }
    }

    std::vector<Entry> entries_;
    std::map<uint32_t, std::string> categories_;
    std::unordered_map<std::string, uint32_t> category_aliases_;
};

void print_help(const char *program)
{
    std::cout
        << "Usage: " << program << " [options] [hex-address|register-name ...]\n"
        << "\n"
        << "Convert between GenICam register hex addresses and *_RegAddr names.\n"
        << "\n"
        << "Options:\n"
        << "  -i, --interface NAME  gmsl (default), gige, or usb3vision\n"
        << "  --csv-dir DIR         Load CSVs from DIR instead of the built-in copy\n"
        << "  --dump                List all registers sorted by address\n"
        << "  -h, --help            Show this help\n"
        << "\n"
        << "The TYGenICamReg CSV files are embedded in this executable, so no\n"
        << "external files are needed unless --csv-dir is given.\n"
        << "\n"
        << "Examples:\n"
        << "  " << program << " 0x02820008\n"
        << "  " << program << " StreamExposureTime\n"
        << "  " << program << " DepthCategory_RegAddr+StreamExposureTime\n";
}

} // namespace

std::string gm86x::regaddr_name(std::uint32_t address)
{
    static const RegisterDatabase database({}, "gmsl");
    const auto names = database.exact_names(address);

    if (names.size() > 1) {
        const auto separator = names.front().find(" + ");
        if (separator != std::string::npos) {
            const auto category = names.front().substr(0, separator);
            const auto same_category = std::all_of(
                names.begin(), names.end(), [&](const std::string &name) {
                    return name.starts_with(category + " + ");
                });
            if (same_category) {
                std::ostringstream output;
                output << category << " + (";
                for (size_t index = 0; index < names.size(); ++index) {
                    if (index != 0) {
                        output << " / ";
                    }
                    output << names[index].substr(separator + 3);
                }
                output << ')';
                return output.str();
            }
        }
    }

    std::ostringstream output;
    for (size_t index = 0; index < names.size(); ++index) {
        if (index != 0) {
            output << " / ";
        }
        output << names[index];
    }
    return output.str();
}

std::string gm86x::regaddr_label(std::uint32_t address)
{
    std::string label = hex32(address);
    const auto name = regaddr_name(address);
    if (!name.empty()) {
        label += " [ " + name + " ]";
    }
    return label;
}

int gm86x::run_regaddr_convert(int argc, char **argv)
{
    try {
        std::string interface_name = "gmsl";
        fs::path csv_dir; // empty: use the built-in CSV copy
        bool dump = false;
        std::vector<std::string> items;

        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "-h" || argument == "--help") {
                print_help(argv[0]);
                return 0;
            }
            if (argument == "--dump") {
                dump = true;
                continue;
            }
            if (argument == "-i" || argument == "--interface"
                || argument == "--csv-dir") {
                if (++i >= argc) {
                    throw std::runtime_error("missing value for " + argument);
                }
                if (argument == "--csv-dir") {
                    csv_dir = argv[i];
                    if (csv_dir.empty()) {
                        throw std::runtime_error("--csv-dir must not be empty");
                    }
                } else {
                    interface_name = argv[i];
                }
                continue;
            }
            if (!argument.empty() && argument[0] == '-') {
                throw std::runtime_error("unknown option: " + argument);
            }
            items.push_back(argument);
        }

        RegisterDatabase database(csv_dir, interface_name);
        if (dump) {
            database.dump();
            return 0;
        }

        const auto convert = [&database](const std::string &item) {
            const auto value = parse_hex(item);
            return value ? database.lookup_address(*value)
                         : database.lookup_name(item);
        };

        if (!items.empty()) {
            for (const auto &item : items) {
                std::cout << item << ":\n";
                for (const auto &line : convert(item)) {
                    std::cout << "  " << line << '\n';
                }
            }
            return 0;
        }

        std::cout << database.size()
                  << " registers loaded (" << interface_name << ", "
                  << (csv_dir.empty() ? std::string("built-in CSV")
                                      : csv_dir.string())
                  << "). Enter hex or name, empty/q to quit.\n";
        std::string input;
        while (std::cout << "> " && std::getline(std::cin, input)) {
            input = trim(input);
            if (input.empty() || input == "q" || input == "quit" || input == "exit") {
                break;
            }
            for (const auto &line : convert(input)) {
                std::cout << "  " << line << '\n';
            }
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
