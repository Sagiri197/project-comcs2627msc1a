#pragma once

#include <charconv>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>

/** @brief Reads sectioned INI files with string, integer, and boolean values. */
class IniReader {
public:

    /** @brief Opens and parses an INI file. */
    explicit IniReader(const std::string& path) {
        std::ifstream input(path);
        if (!input) {
            throw std::invalid_argument("cannot open config file " + path);
        }
        parse(input);
    }

    explicit IniReader(std::istream& input) {
        parse(input);
    }

    std::string get(const std::string& section, const std::string& key) const {
        const auto section_it = m_values.find(section);
        if (section_it == m_values.end()) {
            throw std::invalid_argument("missing config section [" + section + "]");
        }
        const auto value_it = section_it->second.find(key);
        if (value_it == section_it->second.end()) {
            throw std::invalid_argument("missing config key [" + section + "] " + key);
        }
        return value_it->second;
    }

    int get_int(const std::string& section, const std::string& key) const {
        const std::string value = get(section, key);
        int result = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        if (error != std::errc{} || end != value.data() + value.size()) {
            throw std::invalid_argument("config value [" + section + "] " + key + " must be an integer");
        }
        return result;
    }

    bool get_bool(const std::string& section, const std::string& key) const {
        const std::string value = get(section, key);
        if (value == "true") return true;
        if (value == "false") return false;
        throw std::invalid_argument("config value [" + section + "] " + key + " must be true or false");
    }

private:
    using Values = std::map<std::string, std::map<std::string, std::string>>;

    static std::string trim(const std::string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    void parse(std::istream& input) {
        std::string section;
        std::string line;
        std::size_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            line = trim(line);
            if (line.empty() || line.front() == '#' || line.front() == ';') continue;

            if (line.front() == '[') {
                if (line.back() != ']' || line.find(']', 1) != line.size() - 1) {
                    throw std::invalid_argument("invalid config section on line " + std::to_string(line_number));
                }
                section = trim(line.substr(1, line.size() - 2));
                if (section.empty()) {
                    throw std::invalid_argument("empty config section on line " + std::to_string(line_number));
                }
                continue;
            }

            const auto equals = line.find('=');
            if (section.empty() || equals == std::string::npos) {
                throw std::invalid_argument("expected key=value in a section on line " + std::to_string(line_number));
            }
            const std::string key = trim(line.substr(0, equals));
            const std::string value = trim(line.substr(equals + 1));
            if (key.empty()) {
                throw std::invalid_argument("empty config key on line " + std::to_string(line_number));
            }
            if (!m_values[section].emplace(key, value).second) {
                throw std::invalid_argument("duplicate config key [" + section + "] " + key);
            }
        }
        if (input.bad()) {
            throw std::invalid_argument("failed while reading config file");
        }
    }

    Values m_values;
};
