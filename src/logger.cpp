// =============================================================================
// Aevrix - Structured Logger Implementation
// =============================================================================
// This file implements the structured logger for the Aevrix HTTP server.
// =============================================================================

#include "aevrix/logger.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace aevrix {

// Global logger instance
Logger g_logger(LogLevel::INFO);

Logger::Logger(LogLevel level)
    : level_(level)
{
}

void Logger::set_level(LogLevel level) {
    level_ = level;
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::WARN, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERR, message);
}

void Logger::access(uint64_t connection_id, uint64_t request_id,
                   const std::string& method, const std::string& path,
                   int status, uint64_t body_size, uint64_t duration_us) {
    if (!should_log(LogLevel::INFO)) {
        return;
    }

    std::ostringstream oss;
    oss << get_timestamp() << " "
        << level_name(LogLevel::INFO) << " "
        << "conn=" << connection_id << " "
        << "req=" << request_id << " "
        << method << " "
        << path << " "
        << status << " "
        << body_size << "B "
        << duration_us << "us";

    std::cout << oss.str() << std::endl;
}

void Logger::log_with_connection(LogLevel level, uint64_t connection_id, const std::string& message) {
    if (!should_log(level)) {
        return;
    }

    std::ostringstream oss;
    oss << get_timestamp() << " "
        << level_name(level) << " "
        << "conn=" << connection_id << " "
        << message;

    std::cout << oss.str() << std::endl;
}

void Logger::log_with_request(LogLevel level, uint64_t connection_id, uint64_t request_id,
                             const std::string& message) {
    if (!should_log(level)) {
        return;
    }

    std::ostringstream oss;
    oss << get_timestamp() << " "
        << level_name(level) << " "
        << "conn=" << connection_id << " "
        << "req=" << request_id << " "
        << message;

    std::cout << oss.str() << std::endl;
}

std::string Logger::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

std::string Logger::level_name(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERR:   return "ERROR";
        default: return "UNKNOWN";
    }
}

bool Logger::should_log(LogLevel level) const {
    return level >= level_;
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!should_log(level)) {
        return;
    }

    std::ostringstream oss;
    oss << get_timestamp() << " "
        << level_name(level) << " "
        << message;

    std::cout << oss.str() << std::endl;
}

} // namespace aevrix
