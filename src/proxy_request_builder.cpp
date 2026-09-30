// =============================================================================
// Aevrix - Proxy Request Builder Implementation
// =============================================================================
// Pure request rewriting for the reverse proxy. No sockets, no I/O.
// =============================================================================

#include "aevrix/proxy_request_builder.h"

#include <array>

namespace aevrix {

namespace {

// Headers that are meaningful for a single transport hop only.
// RFC 9110 section 7.6.1 plus the proxy-specific authorization headers.
constexpr std::array<const char*, 8> kHopByHopHeaders = {
    "connection",
    "keep-alive",
    "proxy-authenticate",
    "proxy-authorization",
    "te",
    "trailer",
    "transfer-encoding",
    "upgrade"
};

/**
 * @brief Split a request target into its path and query components
 */
void split_target(const std::string& target, std::string& path, std::string& query) {
    const size_t question = target.find('?');
    if (question == std::string::npos) {
        path = target;
        query.clear();
    } else {
        path = target.substr(0, question);
        query = target.substr(question);  // keeps the leading '?'
    }
}

}  // namespace

// =============================================================================
// Routing helpers
// =============================================================================

bool proxy_target_matches(const std::string& client_target, const std::string& prefix) {
    if (prefix.empty()) {
        return false;
    }

    // A prefix of "/" proxies every absolute-path request.
    if (prefix == "/") {
        return !client_target.empty() && client_target[0] == '/';
    }

    std::string path;
    std::string query;
    split_target(client_target, path, query);

    if (path.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    // Reject "/proxyfoo" style matches: the prefix must end at a boundary.
    if (path.size() == prefix.size()) {
        return true;
    }

    if (prefix.back() == '/') {
        return true;
    }

    return path[prefix.size()] == '/';
}

std::string proxy_upstream_path(const std::string& client_target,
                                const std::string& prefix,
                                bool strip_prefix) {
    std::string path;
    std::string query;
    split_target(client_target, path, query);

    if (path.empty()) {
        path = "/";
    }

    if (strip_prefix && !prefix.empty() && prefix != "/") {
        if (path.compare(0, prefix.size(), prefix) == 0) {
            path = path.substr(prefix.size());
        }
    }

    // "/api" with prefix "/api" strips to "" which is not a valid origin-form
    // target, so fall back to "/".
    if (path.empty() || path[0] != '/') {
        path = "/" + path;
    }

    return path + query;
}

bool is_hop_by_hop_header(const std::string& normalized_name) {
    for (const char* name : kHopByHopHeaders) {
        if (normalized_name == name) {
            return true;
        }
    }
    return false;
}

// =============================================================================
// Request serialization
// =============================================================================

std::string build_upstream_request(const http::HttpRequest& request,
                                   const ProxyTarget& target,
                                   const ProxyRequestOptions& options) {
    const std::string body = request.body();
    const http::HttpHeaders& headers = request.headers();

    // -------------------------------------------------------------------------
    // Forwarding headers (computed before copying end-to-end headers so the
    // inbound versions are not duplicated).
    // -------------------------------------------------------------------------
    std::string forwarded_for = headers.get("X-Forwarded-For");
    if (!options.client_ip.empty()) {
        if (forwarded_for.empty()) {
            forwarded_for = options.client_ip;
        } else {
            forwarded_for += ", " + options.client_ip;
        }
    }

    const std::string inbound_host = headers.get("Host");

    std::string out;
    out.reserve(256 + body.size());

    // -------------------------------------------------------------------------
    // Request line
    // -------------------------------------------------------------------------
    out += http::http_method_to_string(request.method());
    out += ' ';
    out += proxy_upstream_path(request.target(), options.prefix, options.strip_prefix);
    out += " HTTP/1.1\r\n";

    // -------------------------------------------------------------------------
    // Mandatory and forwarding headers
    // -------------------------------------------------------------------------
    out += "Host: ";
    out += target.authority();
    out += "\r\n";

    if (!forwarded_for.empty()) {
        out += "X-Forwarded-For: ";
        out += forwarded_for;
        out += "\r\n";
    }

    if (!options.forwarded_proto.empty()) {
        out += "X-Forwarded-Proto: ";
        out += options.forwarded_proto;
        out += "\r\n";
    }

    if (!inbound_host.empty()) {
        out += "X-Forwarded-Host: ";
        out += inbound_host;
        out += "\r\n";
    }

    // -------------------------------------------------------------------------
    // End-to-end headers
    // -------------------------------------------------------------------------
    for (const http::HttpHeader& header : headers) {
        const std::string& normalized = header.normalized_name();

        // Replaced above, or regenerated below.
        if (normalized == "host" || normalized == "content-length" ||
            normalized == "x-forwarded-for" || normalized == "x-forwarded-proto" ||
            normalized == "x-forwarded-host") {
            continue;
        }

        if (is_hop_by_hop_header(normalized)) {
            continue;
        }

        // Defence in depth: HttpHeader validation already rejects CR/LF, but a
        // proxied request must never be able to inject a header line.
        if (!header.is_valid()) {
            continue;
        }

        out += header.name();
        out += ": ";
        out += header.value();
        out += "\r\n";
    }

    // -------------------------------------------------------------------------
    // Framing
    // -------------------------------------------------------------------------
    // The body is forwarded in a single write, so an explicit Content-Length is
    // always correct. Bodies are bounded by max_request_body in the parser.
    if (!body.empty() || headers.has("Content-Length")) {
        out += "Content-Length: ";
        out += std::to_string(body.size());
        out += "\r\n";
    }

    // The upstream connection is pooled, so it must stay open for reuse.
    out += "Connection: keep-alive\r\n";

    out += "\r\n";
    out += body;

    return out;
}

}  // namespace aevrix
