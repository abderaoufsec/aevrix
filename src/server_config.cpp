// =============================================================================
// Aevrix - Server Configuration Implementation
// =============================================================================
// This file implements the server configuration for timeouts and resource limits.
// =============================================================================

#include "aevrix/server_config.h"
#include "aevrix/config_parser.h"
#include "aevrix/logger.h"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace aevrix {

namespace {

// CSV list helpers for comma-separated configuration values
// (websocket_paths, websocket_allowed_origins).
std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t comma = value.find(',', start);
        const size_t end = (comma == std::string::npos) ? value.size() : comma;
        const size_t first = value.find_first_not_of(" \t", start);
        if (first != std::string::npos && first < end) {
            const size_t last = value.find_last_not_of(" \t", end - 1);
            if (last >= first) {
                result.push_back(value.substr(first, last - first + 1));
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

std::string join_csv(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += values[i];
    }
    return out;
}

} // namespace

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
    oss << "  Log level: " << log_level_ << "\n";
    oss << "  Admin API: " << (admin_api_enabled_ ? "enabled" : "disabled") << "\n";
    oss << "  Header timeout: " << header_timeout_ms_ << " ms\n";
    oss << "  Body timeout: " << body_timeout_ms_ << " ms\n";
    oss << "  Keep-alive timeout: " << keep_alive_timeout_ms_ << " ms\n";
    oss << "  Write timeout: " << write_timeout_ms_ << " ms\n";
    oss << "  Max connections: " << max_connections_ << "\n";
    oss << "  Max buffer size: " << max_buffer_size_ << " bytes ("
        << (max_buffer_size_ / 1024) << " KB)\n";
    oss << "  Max request body: " << max_request_body_ << " bytes ("
        << (max_request_body_ / 1024 / 1024) << " MB)\n";

#ifdef AEVRIX_ENABLE_TLS
    oss << "  TLS enabled: " << (tls_enabled_ ? "yes" : "no") << "\n";
    if (tls_enabled_) {
        oss << "  TLS certificate: " << tls_cert_file_ << "\n";
        oss << "  TLS private key: " << tls_key_file_ << "\n";
        if (!tls_min_version_.empty()) {
            oss << "  TLS min version: " << tls_min_version_ << "\n";
        }
        if (!tls_max_version_.empty()) {
            oss << "  TLS max version: " << tls_max_version_ << "\n";
        }
        oss << "  TLS port: " << tls_port_ << "\n";
    }
#endif

    oss << "  Proxy enabled: " << (proxy_enabled_ ? "yes" : "no") << "\n";
    if (proxy_enabled_) {
        oss << "  Proxy pass: " << proxy_pass_ << "\n";
        oss << "  Proxy prefix: " << proxy_prefix_
            << (proxy_strip_prefix_ ? " (prefix stripped)" : " (prefix preserved)") << "\n";
        oss << "  Proxy connect timeout: " << proxy_connect_timeout_ms_ << " ms\n";
        oss << "  Proxy read timeout: " << proxy_read_timeout_ms_ << " ms\n";
        oss << "  Proxy max idle connections: " << proxy_max_idle_connections_ << "\n";
        oss << "  Proxy max response: " << proxy_max_response_bytes_ << " bytes\n";
    }

    oss << "  WebSocket enabled: " << (websocket_enabled_ ? "yes" : "no") << "\n";
    if (websocket_enabled_) {
        oss << "  WebSocket paths: ";
        for (size_t i = 0; i < websocket_allowed_paths_.size(); ++i) {
            oss << (i ? ", " : "") << websocket_allowed_paths_[i];
        }
        oss << "\n";
        oss << "  WebSocket max message: " << websocket_max_message_bytes_ << " bytes\n";
        oss << "  WebSocket close timeout: " << websocket_close_timeout_ms_ << " ms\n";
        oss << "  WebSocket ping interval: " << websocket_ping_interval_ms_ << " ms\n";
        oss << "  WebSocket allowed origins: "
            << (websocket_allowed_origins_.empty() ? "(any)" : std::to_string(websocket_allowed_origins_.size()))
            << "\n";
    }

    return oss.str();
}

void ServerConfig::load_from_parser(const ConfigParser& parser) {
    // Load host
    host_ = parser.get_string("host", host_);
    
    // Load port
    port_ = static_cast<uint16_t>(parser.get_int("port", static_cast<int64_t>(port_)));
    
    // Load workers
    workers_ = static_cast<uint32_t>(parser.get_int("workers", static_cast<int64_t>(workers_)));
    
    // Load document root
    document_root_ = parser.get_string("document_root", document_root_);
    
    // Load timeouts
    header_timeout_ms_ = static_cast<uint64_t>(parser.get_int("header_timeout_ms", static_cast<int64_t>(header_timeout_ms_)));
    body_timeout_ms_ = static_cast<uint64_t>(parser.get_int("body_timeout_ms", static_cast<int64_t>(body_timeout_ms_)));
    keep_alive_timeout_ms_ = static_cast<uint64_t>(parser.get_int("keep_alive_timeout_ms", static_cast<int64_t>(keep_alive_timeout_ms_)));
    write_timeout_ms_ = static_cast<uint64_t>(parser.get_int("write_timeout_ms", static_cast<int64_t>(write_timeout_ms_)));
    
    // Load resource limits
    max_connections_ = static_cast<uint32_t>(parser.get_int("max_connections", static_cast<int64_t>(max_connections_)));
    max_buffer_size_ = static_cast<uint32_t>(parser.get_int("max_buffer_size", static_cast<int64_t>(max_buffer_size_)));
    max_request_body_ = static_cast<uint32_t>(parser.get_int("max_request_body", static_cast<int64_t>(max_request_body_)));

#ifdef AEVRIX_ENABLE_TLS
    // Load TLS configuration
    tls_enabled_ = parser.get_bool("tls_enabled", tls_enabled_);
    tls_cert_file_ = parser.get_string("tls_cert_file", tls_cert_file_);
    tls_key_file_ = parser.get_string("tls_key_file", tls_key_file_);
    tls_min_version_ = parser.get_string("tls_min_version", tls_min_version_);
    tls_max_version_ = parser.get_string("tls_max_version", tls_max_version_);
    tls_port_ = static_cast<uint16_t>(parser.get_int("tls_port", static_cast<int64_t>(tls_port_)));

    // Validate TLS configuration if enabled
    if (tls_enabled_) {
        if (tls_cert_file_.empty()) {
            throw std::runtime_error("TLS enabled but tls_cert_file not specified");
        }
        if (tls_key_file_.empty()) {
            throw std::runtime_error("TLS enabled but tls_key_file not specified");
        }
        if (tls_port_ == 0) {
            throw std::runtime_error("Invalid tls_port: must be positive");
        }
    }
#endif

    // Load reverse proxy configuration (Phase 22)
    proxy_enabled_ = parser.get_bool("proxy_enabled", proxy_enabled_);
    proxy_pass_ = parser.get_string("proxy_pass", proxy_pass_);
    proxy_prefix_ = parser.get_string("proxy_prefix", proxy_prefix_);
    proxy_strip_prefix_ = parser.get_bool("proxy_strip_prefix", proxy_strip_prefix_);
    proxy_connect_timeout_ms_ = static_cast<uint64_t>(parser.get_int("proxy_connect_timeout_ms", static_cast<int64_t>(proxy_connect_timeout_ms_)));
    proxy_read_timeout_ms_ = static_cast<uint64_t>(parser.get_int("proxy_read_timeout_ms", static_cast<int64_t>(proxy_read_timeout_ms_)));
    proxy_max_idle_connections_ = static_cast<uint32_t>(parser.get_int("proxy_max_idle_connections", static_cast<int64_t>(proxy_max_idle_connections_)));
    proxy_max_response_bytes_ = static_cast<uint32_t>(parser.get_int("proxy_max_response_bytes", static_cast<int64_t>(proxy_max_response_bytes_)));
    proxy_idle_timeout_ms_ = static_cast<uint64_t>(parser.get_int("proxy_idle_timeout_ms", static_cast<int64_t>(proxy_idle_timeout_ms_)));

    // Load WebSocket configuration (Phase 23)
    websocket_enabled_ = parser.get_bool("websocket_enabled", websocket_enabled_);
    websocket_allowed_paths_ = split_csv(
        parser.get_string("websocket_paths", join_csv(websocket_allowed_paths_)));
    websocket_max_message_bytes_ = static_cast<uint32_t>(
        parser.get_int("websocket_max_message_bytes", static_cast<int64_t>(websocket_max_message_bytes_)));
    websocket_close_timeout_ms_ = static_cast<uint64_t>(
        parser.get_int("websocket_close_timeout_ms", static_cast<int64_t>(websocket_close_timeout_ms_)));
    websocket_ping_interval_ms_ = static_cast<uint64_t>(
        parser.get_int("websocket_ping_interval_ms", static_cast<int64_t>(websocket_ping_interval_ms_)));
    websocket_allowed_origins_ = split_csv(
        parser.get_string("websocket_allowed_origins", join_csv(websocket_allowed_origins_)));

    // Phase 24: runtime/observability keys. log_level is validated below so a
    // bad value rejects the whole reload instead of silently keeping the old
    // verbosity.
    log_level_ = parser.get_string("log_level", log_level_);
    admin_api_enabled_ = parser.get_bool("admin_api_enabled", admin_api_enabled_);
    admin_token_ = parser.get_string("admin_token", admin_token_);

    // Validate the log level name (Phase 24)
    {
        LogLevel parsed = LogLevel::INFO;
        if (!Logger::level_from_string(log_level_, parsed)) {
            throw std::runtime_error("Invalid log_level: '" + log_level_
                                     + "' (expected debug, info, warn or error)");
        }
        // Store the canonical spelling so reload diffs ignore case/space.
        log_level_ = Logger::level_to_string(parsed);
    }

    // A proxied prefix must be an absolute path, otherwise it can never match a
    // request target.
    if (proxy_enabled_) {
        if (proxy_pass_.empty()) {
            throw std::runtime_error("Proxy enabled but proxy_pass not specified");
        }
        if (proxy_prefix_.empty() || proxy_prefix_[0] != '/') {
            throw std::runtime_error("proxy_prefix must be an absolute path (e.g. /proxy)");
        }
    }

    // Validate WebSocket configuration (Phase 23)
    if (websocket_enabled_) {
        if (websocket_allowed_paths_.empty()) {
            throw std::runtime_error("WebSocket enabled but websocket_paths is empty");
        }
        for (const std::string& path : websocket_allowed_paths_) {
            if (path.empty() || path[0] != '/') {
                throw std::runtime_error("websocket_paths entries must be absolute paths (e.g. /ws)");
            }
        }
        if (websocket_max_message_bytes_ == 0) {
            throw std::runtime_error("Invalid websocket_max_message_bytes: must be positive");
        }
        if (websocket_close_timeout_ms_ == 0) {
            throw std::runtime_error("Invalid websocket_close_timeout_ms: must be positive");
        }
    }

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
    aevrix::g_logger.info("Configuration loaded and validated successfully");
}

} // namespace aevrix
