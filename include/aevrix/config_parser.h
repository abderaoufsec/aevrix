// =============================================================================
// Aevrix - Configuration Parser
// =============================================================================
// This file implements a configuration parser for the Aevrix HTTP server.
// In Phase 13, we move runtime policy out of hard-coded constants with a
// validated configuration system.
//
// The configuration system covers:
// - host: Server binding address
// - port: Server binding port
// - workers: Number of worker threads
// - document_root: Static file serving directory
// - limits: Resource limits (max connections, buffer size, request body)
// - timeouts: Timeouts (header, body, keep-alive, write)
// - logging: Logging configuration
//
// Requirements:
// - Typed configuration object
// - Defaults for all values
// - Validation of configuration values
// - Clear startup errors
//
// Bad configuration fails before the server begins accepting connections.
//
// Configuration file format (key-value pairs):
// host = 127.0.0.1
// port = 8080
// workers = 4
// document_root = ./public
// max_connections = 1000
// max_buffer_size = 65536
// max_request_body = 10485760
// header_timeout_ms = 10000
// body_timeout_ms = 30000
// keep_alive_timeout_ms = 5000
// write_timeout_ms = 30000
//
// Previous Phases:
// - Phase 12: Router for application-level routing
//
// Future Phases Will Add:
// - Phase 14: Structured logging
// =============================================================================

#pragma once

#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cstdint>
#include <vector>

namespace aevrix {

/**
 * @brief Configuration parser for server settings
 * 
 * Parses configuration files with key-value pairs and provides typed
// access to configuration values with validation.
 * 
 * Configuration file format:
// key = value
// # Comments start with #
 */
class ConfigParser {
public:
    /**
     * @brief Construct a configuration parser
     */
    ConfigParser();

    /**
     * @brief Destructor
     */
    ~ConfigParser() = default;

    /**
     * @brief Parse a configuration file
     * 
     * @param config_file Path to the configuration file
     * @throws std::runtime_error if file cannot be read or parsed
     */
    void parse_file(const std::string& config_file);

    /**
     * @brief Get a string value
     * 
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return std::string The configuration value
     */
    std::string get_string(const std::string& key, const std::string& default_value = "") const;

    /**
     * @brief Get an integer value
     * 
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return int64_t The configuration value
     */
    int64_t get_int(const std::string& key, int64_t default_value = 0) const;

    /**
     * @brief Get a boolean value
     * 
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return bool The configuration value
     */
    bool get_bool(const std::string& key, bool default_value = false) const;

    /**
     * @brief Check if a key exists
     * 
     * @param key Configuration key
     * @return true if key exists, false otherwise
     */
    bool has_key(const std::string& key) const;

    /**
     * @brief Get all configuration keys
     * 
     * @return std::vector<std::string> All keys
     */
    std::vector<std::string> get_keys() const;

private:
    /**
     * @brief Parse a single line
     * 
     * @param line The line to parse
     * @param line_number The line number for error reporting
     */
    void parse_line(const std::string& line, int line_number);

    /**
     * @brief Trim whitespace from a string
     * 
     * @param str The string to trim
     * @return std::string The trimmed string
     */
    std::string trim(const std::string& str) const;

    std::unordered_map<std::string, std::string> values_;
};

} // namespace aevrix
