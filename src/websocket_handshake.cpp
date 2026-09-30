// =============================================================================
// Aevrix - WebSocket HTTP Upgrade Handshake Implementation (RFC 6455 §4)
// =============================================================================

#include "aevrix/websocket_handshake.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <vector>

#include "aevrix/server_config.h"

namespace aevrix {
namespace ws {

const char* const kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// =============================================================================
// SHA-1 (RFC 3174) - implemented locally so WebSocket works without OpenSSL
// =============================================================================

namespace {

class Sha1 {
public:
    Sha1() { reset(); }

    void update(const uint8_t* data, size_t length) {
        total_bytes_ += length;
        for (size_t i = 0; i < length; ++i) {
            buffer_[buffer_length_++] = data[i];
            if (buffer_length_ == 64) {
                process_block(buffer_.data());
                buffer_length_ = 0;
            }
        }
    }

    std::array<uint8_t, 20> finish() {
        const uint64_t total_bits = static_cast<uint64_t>(total_bytes_) * 8;

        // Padding: 0x80 then zeros until 56 mod 64, then the 64-bit length.
        buffer_[buffer_length_++] = 0x80;
        if (buffer_length_ > 56) {
            while (buffer_length_ < 64) {
                buffer_[buffer_length_++] = 0;
            }
            process_block(buffer_.data());
            buffer_length_ = 0;
        }
        while (buffer_length_ < 56) {
            buffer_[buffer_length_++] = 0;
        }
        for (int i = 7; i >= 0; --i) {
            buffer_[buffer_length_++] = static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF);
        }
        process_block(buffer_.data());

        std::array<uint8_t, 20> digest{};
        for (size_t i = 0; i < 5; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>((state_[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((state_[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((state_[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(state_[i] & 0xFF);
        }
        return digest;
    }

private:
    void reset() {
        state_[0] = 0x67452301;
        state_[1] = 0xEFCDAB89;
        state_[2] = 0x98BADCFE;
        state_[3] = 0x10325476;
        state_[4] = 0xC3D2E1F0;
        buffer_length_ = 0;
        total_bytes_ = 0;
    }

    static uint32_t rotl(uint32_t value, uint32_t bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    void process_block(const uint8_t* block) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];

        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            const uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    uint32_t state_[5];
    std::array<uint8_t, 64> buffer_{};
    size_t buffer_length_ = 0;
    uint64_t total_bytes_ = 0;
};

std::array<uint8_t, 20> sha1(const std::string& input) {
    Sha1 hasher;
    hasher.update(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    return hasher.finish();
}

// =============================================================================
// Base64 (RFC 4648) - encode and strict decode
// =============================================================================

const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t length) {
    std::string out;
    out.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= length) {
        const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                                (static_cast<uint32_t>(data[i + 1]) << 8) |
                                static_cast<uint32_t>(data[i + 2]);
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
        out.push_back(kBase64Alphabet[triple & 0x3F]);
        i += 3;
    }

    const size_t remaining = length - i;
    if (remaining == 1) {
        const uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                                (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/// Strict base64 decode. Returns false on bad alphabet/alignment/padding.
bool base64_decode(const std::string& input, std::string& out) {
    if (input.size() % 4 != 0) {
        return false;
    }

    out.clear();
    out.reserve((input.size() / 4) * 3);

    for (size_t i = 0; i < input.size(); i += 4) {
        int values[4];
        int padding = 0;

        for (size_t j = 0; j < 4; ++j) {
            const char c = input[i + j];
            if (c == '=') {
                // Padding is only allowed in the last quantum, trailing.
                if (i + 4 != input.size() || j < 2) {
                    return false;
                }
                if (j == 2) {
                    padding = 2;
                    values[2] = 0;
                    values[3] = 0;
                    // Third character '=' implies fourth must be '=' too.
                    if (input[i + 3] != '=') {
                        return false;
                    }
                    break;
                }
                padding = 1;
                values[3] = 0;
                break;
            }
            values[j] = base64_value(c);
            if (values[j] < 0) {
                return false;
            }
        }

        const uint32_t triple = (static_cast<uint32_t>(values[0]) << 18) |
                                (static_cast<uint32_t>(values[1]) << 12) |
                                (static_cast<uint32_t>(values[2]) << 6) |
                                static_cast<uint32_t>(values[3]);
        out.push_back(static_cast<char>((triple >> 16) & 0xFF));
        if (padding != 2) {
            out.push_back(static_cast<char>((triple >> 8) & 0xFF));
        }
        if (padding != 1 && padding != 2) {
            out.push_back(static_cast<char>(triple & 0xFF));
        }
    }

    return true;
}

// =============================================================================
// Header Helpers
// =============================================================================

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

/// True when a comma-separated header value contains the given token
/// (case-insensitive), per RFC 9110 Section 7.6.1.
bool header_has_token(const std::string& value, const std::string& token) {
    const std::string target = to_lower(token);
    size_t start = 0;
    while (start <= value.size()) {
        const size_t comma = value.find(',', start);
        const std::string part = to_lower(trim(value.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start)));
        if (part == target) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

/// Path component of a request target ("/ws?x=1" -> "/ws").
std::string target_path(const std::string& target) {
    const size_t cut = target.find_first_of("?#");
    return cut == std::string::npos ? target : target.substr(0, cut);
}

} // namespace

// =============================================================================
// Public API
// =============================================================================

bool is_websocket_intent(const http::HttpRequest& request) {
    return header_has_token(request.headers().get("Upgrade"), "websocket");
}

std::string compute_accept(const std::string& client_key) {
    // RFC 6455: the key must decode from base64 to exactly 16 bytes.
    std::string decoded;
    if (!base64_decode(client_key, decoded) || decoded.size() != 16) {
        return "";
    }

    const std::string digest_input = client_key + kWebSocketGuid;
    const std::array<uint8_t, 20> digest = sha1(digest_input);
    return base64_encode(digest.data(), digest.size());
}

std::string build_switching_protocols(const std::string& accept) {
    std::string response;
    response += "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + accept + "\r\n";
    response += "\r\n";
    return response;
}

UpgradeResult evaluate_upgrade(const http::HttpRequest& request,
                               const ServerConfig& config) {
    UpgradeResult result;

    // The feature must be explicitly enabled.
    if (!config.websocket_enabled()) {
        return result;
    }

    // Only allowlisted paths are WebSocket endpoints; anything else continues
    // through the normal HTTP pipeline (static files, router, proxy, 404...).
    const std::string path = target_path(request.target());
    const auto& allowed_paths = config.websocket_allowed_paths();
    if (std::find(allowed_paths.begin(), allowed_paths.end(), path) == allowed_paths.end()) {
        return result;
    }

    // No upgrade intent on an allowed path: a plain GET, answer as HTTP.
    if (!is_websocket_intent(request)) {
        return result;
    }

    // RFC 6455 Section 4.2.1: GET over HTTP/1.1 or better.
    if (request.method() != http::HttpMethod::GET ||
        request.version() != "HTTP/1.1") {
        result.verdict = UpgradeVerdict::BadRequest;
        return result;
    }

    // Connection header must include the "upgrade" token.
    if (!header_has_token(request.headers().get("Connection"), "upgrade")) {
        result.verdict = UpgradeVerdict::BadRequest;
        return result;
    }

    // Sec-WebSocket-Version must be present and equal to 13.
    const std::string version = trim(request.headers().get("Sec-WebSocket-Version"));
    if (version.empty()) {
        result.verdict = UpgradeVerdict::BadRequest;
        return result;
    }
    if (version != "13") {
        result.verdict = UpgradeVerdict::UnsupportedVersion;
        return result;
    }

    // Sec-WebSocket-Key must be present and decode to exactly 16 bytes.
    const std::string key = trim(request.headers().get("Sec-WebSocket-Key"));
    if (key.empty()) {
        result.verdict = UpgradeVerdict::BadRequest;
        return result;
    }
    const std::string accept = compute_accept(key);
    if (accept.empty()) {
        result.verdict = UpgradeVerdict::BadRequest;
        return result;
    }

    // Origin allowlist (when configured): exact match, case-insensitive.
    const auto& allowed_origins = config.websocket_allowed_origins();
    if (!allowed_origins.empty()) {
        const std::string origin = to_lower(trim(request.headers().get("Origin")));
        bool allowed = false;
        for (const std::string& candidate : allowed_origins) {
            if (to_lower(candidate) == origin && !origin.empty()) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            result.verdict = UpgradeVerdict::ForbiddenOrigin;
            return result;
        }
    }

    result.verdict = UpgradeVerdict::Accepted;
    result.accept = accept;
    result.response = build_switching_protocols(accept);
    return result;
}

http::HttpResponse build_rejection_response(UpgradeVerdict verdict) {
    http::HttpResponse response(http::StatusCode::BadRequest,
                                "Bad WebSocket upgrade request");

    switch (verdict) {
        case UpgradeVerdict::ForbiddenOrigin:
            response = http::HttpResponse(http::StatusCode::Forbidden,
                                          "Origin not allowed for WebSocket upgrade");
            break;
        case UpgradeVerdict::UnsupportedVersion:
            response = http::HttpResponse(
                http::StatusCode::UpgradeRequired,
                "Unsupported WebSocket version; this server requires version 13");
            response.set_header("Sec-WebSocket-Version", "13");
            break;
        default:
            break;  // 400 Bad Request
    }

    response.set_header("Content-Type", "text/plain");
    response.set_header("Server", "Aevrix/0.1.0");
    response.set_connection_policy(http::ConnectionPolicy::Close);
    return response;
}

} // namespace ws
} // namespace aevrix

