// =============================================================================
// Aevrix - Server Configuration Implementation
// =============================================================================
// This file implements the server configuration for timeouts and resource limits.
// =============================================================================

#include "aevrix/server_config.h"
#include "aevrix/config_parser.h"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace aevrix {

ServerConfig::ServerConfig()
    : header_timeout_ms_(10000)        // 10 seconds
    , body_timeout_ms_(30000)          // 30 seconds
    , keep_alive_timeout_ms_(5000)    // 5 seconds
    , write_timeout_ms_(30000)         // 30 seconds
    , max_connections_(1000)          // 1000 concurrent connections
    , max_buffer_size_(65536)          // 64 KB
    , max_request_body_(10485760)      // 10 MB
    , host_("127.0.0.1")              // Default host
    , port_(8080)                      // Default port
    , workers_(4)                      // Default workers
    , document_root_("./public")        // Default document root
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
    , host_("127.0.0.1")
    , port_(8080)
    , workers_(4)
    , document_root_("./public")
{
}

std::string ServerConfig::summary() const {
    std::ostringstream oss;
    
    oss << "Server Configuration:\n";
    oss << "  Host: " << host_ << "\n";
    oss << "  Port: " << port_ << "\n";
    oss << "  Workers: " << workers_ << "\n";
    oss << "  Document root: " << document_root_ << "\n";
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

void ServerConfig::load_from_parser(const ConfigParser& parser) {
    // Load host
    host_ = parser.get_string("host", host_);
    
    // Load port
    port_ = static_cast<uint16_t>(parser.get_int("port", port_));
    
    // Load workers
    workers_ = static_cast<uint32_t>(parser.get_int("workers", workers_));
    
    // Load document root
    document_root_ = parser.get_string("document_root", document_root_);
    
    // Load timeouts
    header_timeout_ms_ = static_cast<uint64_t>(parser.get_int("header_timeout_ms", header_timeout_ms_));
    body_timeout_ms_ = static_cast<uint64_t>(parser.get_int("body_timeout_ms", body_timeout_ms_));
    keep_alive_timeout_ms_ = static_cast<uint64_t>(parser.get_int("keep_alive_timeout_ms", keep_alive_timeout_ms_));
    write_timeout_ms_ = static_cast<uint64_t>(parser.get_int("write_timeout_ms", write_timeout_ms_));
    
    // Load resource limits
    max_connections_ = static_cast<uint32_t>(parser.get_int("max_connections", max_connections_));
    max_buffer_size_ = static_cast<uint32_t>(parser.get_int("max_buffer_size", max_buffer_size_));
    max_request_body_ = static_cast<uint32_t>(parser.get_int("max_request_body", max_request_body_));
    
    // Validate configuration
    if (port_ == 0) {
        throw std::runtime_error("Invalid port: " + std::to_string(port_));
    }
    
    if (workers_ == 0) {
        throw std::runtime_error("Invalid workers: must be at least 1");
    }
    
    if (header_timeout_ms_ == 0) {
        throw std::runtime_error("Invalid header_timeout_ms: must be positive");
    }
    
    if (body_timeout_ms_ == 0) {
        throw std::runtime_error("Invalid body_timeout_ms: must be positive");
    }
    
    if (keep_alive_timeout_ms_ == 0) {
        throw std::runtime_error("Invalid keep_alive_timeout_ms: must be positive");
    }
    
    if (write_timeout_ms_ == 0) {
        throw std::runtime_error("Invalid write_timeout_ms: must be positive");
    }
    
    if (max_connections_ == 0) {
        throw std::runtime_error("Invalid max_connections: must be at least 1");
    }
    
    if (max_buffer_size_ == 0) {
        throw std::runtime_error("Invalid max_buffer_size: must be positive");
    }
    
    if (max_request_body_ == 0) {
        throw std::runtime_error("Invalid max_request_body: must be positive");
    }
    
    std::cout << "Configuration loaded and validated successfully\n";
}

} // namespace aevrix
