// =============================================================================
// Aevrix - Server Configuration Implementation
// =============================================================================
// This file implements the server configuration for timeouts and resource limits.
// =============================================================================

#include "aevrix/server_config.h"
#include <sstream>
#include <iomanip>

namespace aevrix {

ServerConfig::ServerConfig()
    : header_timeout_ms_(10000)        // 10 seconds
    , body_timeout_ms_(30000)          // 30 seconds
    , keep_alive_timeout_ms_(5000)    // 5 seconds
    , write_timeout_ms_(30000)         // 30 seconds
    , max_connections_(1000)          // 1000 concurrent connections
    , max_buffer_size_(65536)          // 64 KB
    , max_request_body_(10485760)      // 10 MB
{
}

ServerConfig::ServerConfig(
    uint64_t header_timeout_ms,
    uint64_t body_timeout_ms,
    uint64_t keep_alive_timeout_ms,
    uint64_t write_timeout_ms,
    uint32_t max_connections,
    uint32_t max_buffer_size,
    uint32_t max_request_body
)
    : header_timeout_ms_(header_timeout_ms)
    , body_timeout_ms_(body_timeout_ms)
    , keep_alive_timeout_ms_(keep_alive_timeout_ms)
    , write_timeout_ms_(write_timeout_ms)
    , max_connections_(max_connections)
    , max_buffer_size_(max_buffer_size)
    , max_request_body_(max_request_body)
{
}

std::string ServerConfig::summary() const {
    std::ostringstream oss;
    
    oss << "Server Configuration:\n";
    oss << "  Header timeout: " << header_timeout_ms_ << " ms\n";
    oss << "  Body timeout: " << body_timeout_ms_ << " ms\n";
    oss << "  Keep-alive timeout: " << keep_alive_timeout_ms_ << " ms\n";
    oss << "  Write timeout: " << write_timeout_ms_ << " ms\n";
    oss << "  Max connections: " << max_connections_ << "\n";
    oss << "  Max buffer size: " << max_buffer_size_ << " bytes ("
        << (max_buffer_size_ / 1024) << " KB)\n";
    oss << "  Max request body: " << max_request_body_ << " bytes ("
        << (max_request_body_ / 1024 / 1024) << " MB)\n";
    
    return oss.str();
}

} // namespace aevrix
