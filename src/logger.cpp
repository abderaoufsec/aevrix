// =============================================================================
// Aevrix - Structured Logger Implementation
// =============================================================================
// This file implements the structured logger for the Aevrix HTTP server.
// =============================================================================

#include "aevrix/logger.h"
#include <ctime>
#include <cctype>
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
    level_.store(level, std::memory_order_relaxed);
}

bool Logger::level_from_string(const std::string& name, LogLevel& out) {
    std::string lowered;
    lowered.reserve(name.size());
    for (char c : name) {
        lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // Trim surrounding whitespace so "info " from a config file still parses.
    const size_t first = lowered.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return false;
    }
    const size_t last = lowered.find_last_not_of(" \t");
    lowered = lowered.substr(first, last - first + 1);

    if (lowered == "debug") { out = LogLevel::DEBUG; return true; }
    if (lowered == "info")  { out = LogLevel::INFO;  return true; }
    if (lowered == "warn")  { out = LogLevel::WARN;  return true; }
    if (lowered == "error" || lowered == "err") { out = LogLevel::ERR; return true; }
    return false;
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "debug";
        case LogLevel::INFO:  return "info";
        case LogLevel::WARN:  return "warn";
        case LogLevel::ERR:   return "error";
        default: return "info";
    }
}

bool Logger::set_level_from_string(const std::string& name) {
    LogLevel parsed = LogLevel::INFO;
    if (!level_from_string(name, parsed)) {
        return false;
    }
    set_level(parsed);
    return true;
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
    return static_cast<int>(level) >=
           static_cast<int>(level_.load(std::memory_order_relaxed));
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
