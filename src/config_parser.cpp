// =============================================================================
// Aevrix - Configuration Parser Implementation
// =============================================================================
// This file implements the configuration parser for the Aevrix HTTP server.
// =============================================================================

#include "aevrix/config_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <vector>

namespace aevrix {

ConfigParser::ConfigParser() {
    std::cout << "Configuration parser initialized\n";
}

void ConfigParser::parse_file(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open configuration file: " + config_file);
    }

    std::cout << "Parsing configuration file: " << config_file << "\n";

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        line_number++;
        parse_line(line, line_number);
    }

    std::cout << "Configuration parsed successfully (" << values_.size() << " values)\n";
}

void ConfigParser::parse_line(const std::string& line, int line_number) {
    // Trim whitespace
    std::string trimmed = trim(line);

    // Skip empty lines and comments
    if (trimmed.empty() || trimmed[0] == '#') {
        return;
    }

    // Parse key = value
    size_t equals_pos = trimmed.find('=');
    if (equals_pos == std::string::npos) {
        std::cerr << "Warning: Invalid configuration line (no '=') at line " << line_number << ": " << line << "\n";
        return;
    }

    std::string key = trim(trimmed.substr(0, equals_pos));
    std::string value = trim(trimmed.substr(equals_pos + 1));

    if (key.empty()) {
        std::cerr << "Warning: Empty key in configuration line " << line_number << ": " << line << "\n";
        return;
    }

    values_[key] = value;
    std::cout << "  " << key << " = " << value << "\n";
}

std::string ConfigParser::get_string(const std::string& key, const std::string& default_value) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        return it->second;
    }
    return default_value;
}

int64_t ConfigParser::get_int(const std::string& key, int64_t default_value) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        try {
            return std::stoll(it->second);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Invalid integer value for " << key << ": " << it->second << "\n";
            return default_value;
        }
    }
    return default_value;
}

bool ConfigParser::get_bool(const std::string& key, bool default_value) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (value == "true" || value == "yes" || value == "1") {
            return true;
        } else if (value == "false" || value == "no" || value == "0") {
            return false;
        }
        std::cerr << "Warning: Invalid boolean value for " << key << ": " << it->second << "\n";
    }
    return default_value;
}

bool ConfigParser::has_key(const std::string& key) const {
    return values_.find(key) != values_.end();
}

std::vector<std::string> ConfigParser::get_keys() const {
    std::vector<std::string> keys;
    for (const auto& pair : values_) {
        keys.push_back(pair.first);
    }
    return keys;
}

std::string ConfigParser::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // namespace aevrix
