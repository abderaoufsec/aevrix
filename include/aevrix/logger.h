// =============================================================================
// Aevrix - Structured Logger
// =============================================================================
// This file implements a structured logger for the Aevrix HTTP server.
// In Phase 14, we add structured logging to make failures diagnosable.
//
// The logger includes:
// - Log levels (DEBUG, INFO, WARN, ERROR)
// - Timestamps
// - Request IDs
// - Connection IDs
// - Access logging with structured format
//
// Example access log format:
// INFO conn=12 req=48 GET / 200 1254B 312us
//
// Previous Phases:
// - Phase 13: Configuration system
//
// Future Phases Will Add:
// - Phase 15: Graceful shutdown
// =============================================================================

#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstdint>
#include <atomic>

namespace aevrix {

/**
 * @brief Log level enumeration
 * 
 * Defines the severity levels for log messages.
 */
enum class LogLevel {
    DEBUG,  // Detailed debugging information
    INFO,   // General informational messages
    WARN,   // Warning messages for potentially harmful situations
    ERR     // Error messages for error events (renamed from ERROR to avoid Windows macro conflict)
};

/**
 * @brief Structured logger for the Aevrix HTTP server
 * 
 * Provides structured logging with timestamps, log levels, request IDs,
// connection IDs, and formatted access logging.
 */
class Logger {
public:
    /**
     * @brief Construct a logger
     * 
     * @param level Minimum log level to output (default: INFO)
     */
    explicit Logger(LogLevel level = LogLevel::INFO);

    /**
     * @brief Destructor
     */
    ~Logger() = default;

    /**
     * @brief Set the minimum log level
     * 
     * @param level Minimum log level to output
     */
    void set_level(LogLevel level);

    /**
     * @brief Parse a log level name (case-insensitive)
     *
     * Accepted names: debug, info, warn, error (also "err"). Used by the
     * Phase 24 configuration reload path, where the level arrives as text.
     *
     * @param name Level name to parse
     * @param out Receives the parsed level when true is returned
     * @return true when the name maps to a known level
     */
    static bool level_from_string(const std::string& name, LogLevel& out);

    /**
     * @brief Render a log level as its configuration name
     *
     * @param level Level to render
     * @return std::string Lowercase name ("debug", "info", "warn", "error")
     */
    static std::string level_to_string(LogLevel level);

    /**
     * @brief Set the log level from its configuration name
     *
     * @param name Level name (case-insensitive)
     * @return true when applied, false when the name is unknown (level kept)
     */
    bool set_level_from_string(const std::string& name);

    /**
     * @brief Get the current log level
     *
     * @return LogLevel Current log level
     */
    LogLevel level() const { return level_.load(std::memory_order_relaxed); }

    /**
     * @brief Log a DEBUG message
     * 
     * @param message The message to log
     */
    void debug(const std::string& message);

    /**
     * @brief Log an INFO message
     * 
     * @param message The message to log
     */
    void info(const std::string& message);

    /**
     * @brief Log a WARN message
     * 
     * @param message The message to log
     */
    void warn(const std::string& message);

    /**
     * @brief Log an ERROR message
     * 
     * @param message The message to log
     */
    void error(const std::string& message);

    /**
     * @brief Log an access log entry
     * 
     * Format: INFO conn=X req=Y METHOD PATH STATUS BODY_SIZE DURATION
     * Example: INFO conn=12 req=48 GET / 200 1254B 312us
     * 
     * @param connection_id Connection ID
     * @param request_id Request ID
     * @param method HTTP method
     * @param path Request path
     * @param status HTTP status code
     * @param body_size Response body size in bytes
     * @param duration_us Request duration in microseconds
     */
    void access(uint64_t connection_id, uint64_t request_id,
                const std::string& method, const std::string& path,
                int status, uint64_t body_size, uint64_t duration_us);

    /**
     * @brief Log a message with connection ID
     * 
     * @param level Log level
     * @param connection_id Connection ID
     * @param message The message to log
     */
    void log_with_connection(LogLevel level, uint64_t connection_id, const std::string& message);

    /**
     * @brief Log a message with request ID
     * 
     * @param level Log level
     * @param connection_id Connection ID
     * @param request_id Request ID
     * @param message The message to log
     */
    void log_with_request(LogLevel level, uint64_t connection_id, uint64_t request_id,
                         const std::string& message);

private:
    /**
     * @brief Get the current timestamp as a string
     * 
     * @return std::string Timestamp in ISO 8601 format
     */
    std::string get_timestamp() const;

    /**
     * @brief Get the log level name as a string
     * 
     * @param level Log level
     * @return std::string Log level name
     */
    std::string level_name(LogLevel level) const;

    /**
     * @brief Check if a log level should be output
     * 
     * @param level Log level to check
     * @return true if level should be output, false otherwise
     */
    bool should_log(LogLevel level) const;

    /**
     * @brief Log a message at a specific level
     * 
     * @param level Log level
     * @param message The message to log
     */
    void log(LogLevel level, const std::string& message);

    // Atomic: the level is readable from every thread (worker threads log
    // too) while Phase 24 config reloads may change it from the event-loop
    // thread, so a plain field would be a data race.
    std::atomic<LogLevel> level_;
};

/**
 * @brief Global logger instance
 * 
 * Provides access to a global logger for use throughout the codebase.
 */
extern Logger g_logger;

} // namespace aevrix
