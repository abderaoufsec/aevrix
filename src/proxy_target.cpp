// =============================================================================
// Aevrix - Reverse Proxy Upstream Target Implementation
// =============================================================================
// Pure parsing of the proxy_pass configuration value. No sockets, no I/O.
// =============================================================================

#include "aevrix/proxy_target.h"

#include <cctype>

namespace aevrix {

namespace {

// =============================================================================
// Local Helpers
// =============================================================================

/**
 * @brief Trim leading/trailing ASCII whitespace
 */
std::string trim_ascii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' ||
                                    value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }

    return value.substr(start, end - start);
}

/**
 * @brief Lowercase an ASCII string
 */
std::string to_lower_ascii(const std::string& value) {
    std::string result = value;
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

/**
 * @brief Validate a host component
 *
 * Rejects empty hosts, whitespace/control characters and characters that would
 * corrupt the authority string (which is also used as a Host header value).
 */
bool is_valid_host(const std::string& host) {
    if (host.empty()) {
        return false;
    }

    for (char c : host) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc <= 0x20 || uc == 0x7f) {
            return false;  // whitespace or control character
        }
        if (c == '/' || c == '\\' || c == '?' || c == '#' || c == '@') {
            return false;  // would change the meaning of the authority
        }
    }

    return true;
}

/**
 * @brief Parse a decimal TCP port
 *
 * @param text The digits to parse
 * @param error Receives the reason on failure
 * @return std::optional<uint16_t> The port, or nullopt on failure
 */
std::optional<uint16_t> parse_port(const std::string& text, std::string& error) {
    if (text.empty() || text.size() > 5) {
        error = "invalid port '" + text + "' in proxy_pass";
        return std::nullopt;
    }

    uint32_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            error = "invalid port '" + text + "' in proxy_pass";
            return std::nullopt;
        }
        value = value * 10U + static_cast<uint32_t>(c - '0');
    }

    if (value == 0 || value > 65535U) {
        error = "port '" + text + "' out of range in proxy_pass";
        return std::nullopt;
    }

    return static_cast<uint16_t>(value);
}

}  // namespace

// =============================================================================
// ProxyTarget
// =============================================================================

std::string ProxyTarget::authority() const {
    return host + ":" + std::to_string(port);
}

// =============================================================================
// Parsing
// =============================================================================

std::optional<ProxyTarget> parse_proxy_target(const std::string& value, std::string& error) {
    error.clear();

    std::string text = trim_ascii(value);
    if (text.empty()) {
        error = "proxy_pass is empty";
        return std::nullopt;
    }

    // -------------------------------------------------------------------------
    // Scheme
    // -------------------------------------------------------------------------
    const size_t scheme_separator = text.find("://");
    if (scheme_separator != std::string::npos) {
        const std::string scheme = to_lower_ascii(text.substr(0, scheme_separator));
        text = text.substr(scheme_separator + 3);

        if (scheme != "http") {
            error = "unsupported proxy_pass scheme '" + scheme +
                    "' (only http:// upstreams are supported)";
            return std::nullopt;
        }
    }

    // Drop any path/query/fragment: only the authority is meaningful.
    const size_t authority_end = text.find_first_of("/?#");
    if (authority_end != std::string::npos) {
        text = text.substr(0, authority_end);
    }

    if (text.empty()) {
        error = "proxy_pass has no host component";
        return std::nullopt;
    }

    // -------------------------------------------------------------------------
    // Host / port split (supports [IPv6]:port)
    // -------------------------------------------------------------------------
    std::string host;
    std::string port_text;

    if (text[0] == '[') {
        const size_t close = text.find(']');
        if (close == std::string::npos) {
            error = "unterminated IPv6 literal in proxy_pass";
            return std::nullopt;
        }

        host = text.substr(1, close - 1);
        const std::string remainder = text.substr(close + 1);
        if (!remainder.empty()) {
            if (remainder[0] != ':') {
                error = "malformed proxy_pass authority '" + text + "'";
                return std::nullopt;
            }
            port_text = remainder.substr(1);
        }
    } else {
        const size_t first_colon = text.find(':');
        const size_t last_colon = text.rfind(':');

        if (first_colon == std::string::npos) {
            host = text;
        } else if (first_colon == last_colon) {
            host = text.substr(0, first_colon);
            port_text = text.substr(first_colon + 1);
        } else {
            // Several colons without brackets: a bare IPv6 address.
            error = "IPv6 proxy_pass targets must use the [address]:port form";
            return std::nullopt;
        }
    }

    if (!is_valid_host(host)) {
        error = "invalid host '" + host + "' in proxy_pass";
        return std::nullopt;
    }

    ProxyTarget target;
    target.host = host;

    if (port_text.empty()) {
        // Default port for the (only supported) http scheme.
        target.port = 80;
    } else {
        const std::optional<uint16_t> port = parse_port(port_text, error);
        if (!port.has_value()) {
            return std::nullopt;
        }
        target.port = port.value();
    }

    return target;
}

}  // namespace aevrix
