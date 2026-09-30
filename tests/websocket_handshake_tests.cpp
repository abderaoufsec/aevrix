// =============================================================================
// Aevrix - WebSocket Upgrade Handshake Tests (Phase 23)
// =============================================================================
// Covers RFC 6455 Section 4.2 validation: Sec-WebSocket-Accept computation
// (RFC test vector), header/token requirements, version negotiation,
// path and Origin allowlists, and rejection responses.
// =============================================================================

#include <cassert>
#include <iostream>
#include <string>

#include "aevrix/http_request.h"
#include "aevrix/http_response.h"
#include "aevrix/http_response_serializer.h"
#include "aevrix/server_config.h"
#include "aevrix/websocket_handshake.h"

using namespace aevrix;
using namespace aevrix::ws;

namespace {

// =============================================================================
// Helpers
// =============================================================================

/// A fully valid upgrade request for /ws.
http::HttpRequest make_valid_request() {
    http::HttpRequest request(http::HttpMethod::GET, "/ws");
    request.set_header("Host", "localhost:18081");
    request.set_header("Upgrade", "websocket");
    request.set_header("Connection", "Upgrade");
    request.set_header("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    request.set_header("Sec-WebSocket-Version", "13");
    return request;
}

ServerConfig make_enabled_config() {
    ServerConfig config;
    config.set_websocket_enabled(true);
    return config;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// =============================================================================
// Accept Computation
// =============================================================================

void test_rfc_6455_accept_vector() {
    std::cout << "Testing RFC 6455 Section 1.3 test vector..." << std::endl;
    // The exact example from the RFC: key "dGhlIHNhbXBsZSBub25jZQ=="
    // must produce accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=".
    assert(compute_accept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

void test_invalid_keys_rejected() {
    std::cout << "Testing invalid Sec-WebSocket-Key values..." << std::endl;
    assert(compute_accept("").empty());                       // Missing
    assert(compute_accept("not base64!!").empty());           // Bad alphabet
    assert(compute_accept("aGVsbG8=").empty());               // Decodes to 5 bytes
    assert(compute_accept("AAAAAAAAAAAAAAAAAAAA").empty());   // 15 bytes (16 A's = 12? -> 20 chars = 15 bytes)
    // A correctly padded 16-byte value must succeed.
    assert(!compute_accept("AAAAAAAAAAAAAAAAAAAAAA==").empty());  // 16 bytes
}

// =============================================================================
// Upgrade Evaluation
// =============================================================================

void test_valid_upgrade_accepted() {
    std::cout << "Testing valid upgrade acceptance..." << std::endl;
    const UpgradeResult result = evaluate_upgrade(make_valid_request(), make_enabled_config());

    assert(result.verdict == UpgradeVerdict::Accepted);
    assert(result.accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    // Raw 101 response: status line, required headers, empty line terminator.
    assert(contains(result.response, "HTTP/1.1 101 Switching Protocols\r\n"));
    assert(contains(result.response, "Upgrade: websocket\r\n"));
    assert(contains(result.response, "Connection: Upgrade\r\n"));
    assert(contains(result.response,
                    "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"));
    assert(result.response.substr(result.response.size() - 2) == "\r\n");
    assert(contains(result.response, "\r\n\r\n"));
}

void test_intent_detection_is_case_insensitive() {
    std::cout << "Testing case-insensitive upgrade tokens..." << std::endl;
    http::HttpRequest request = make_valid_request();
    request.set_header("Upgrade", "WebSocket");
    request.set_header("Connection", "keep-alive, Upgrade");

    assert(is_websocket_intent(request));
    const UpgradeResult result = evaluate_upgrade(request, make_enabled_config());
    assert(result.verdict == UpgradeVerdict::Accepted);
}

void test_missing_connection_upgrade_rejected() {
    std::cout << "Testing missing Connection: Upgrade rejection..." << std::endl;
    http::HttpRequest request = make_valid_request();
    request.set_header("Connection", "keep-alive");

    assert(is_websocket_intent(request));  // Intent present...
    const UpgradeResult result = evaluate_upgrade(request, make_enabled_config());
    assert(result.verdict == UpgradeVerdict::BadRequest);  // ...but malformed
}

void test_missing_key_rejected() {
    std::cout << "Testing missing Sec-WebSocket-Key rejection..." << std::endl;
    http::HttpRequest request = make_valid_request();
    request.set_header("Sec-WebSocket-Key", "");

    const UpgradeResult result = evaluate_upgrade(request, make_enabled_config());
    assert(result.verdict == UpgradeVerdict::BadRequest);
}

void test_short_key_rejected() {
    std::cout << "Testing short Sec-WebSocket-Key rejection..." << std::endl;
    http::HttpRequest request = make_valid_request();
    request.set_header("Sec-WebSocket-Key", "aGVsbG8=");  // 5 bytes, not 16

    const UpgradeResult result = evaluate_upgrade(request, make_enabled_config());
    assert(result.verdict == UpgradeVerdict::BadRequest);
}

void test_version_negotiation() {
    std::cout << "Testing Sec-WebSocket-Version negotiation..." << std::endl;

    // Unsupported version -> 426 Upgrade Required (RFC 6455 Section 4.2.2).
    http::HttpRequest old_version = make_valid_request();
    old_version.set_header("Sec-WebSocket-Version", "8");
    const UpgradeResult rejected = evaluate_upgrade(old_version, make_enabled_config());
    assert(rejected.verdict == UpgradeVerdict::UnsupportedVersion);

    // Missing version -> malformed upgrade.
    http::HttpRequest no_version = make_valid_request();
    no_version.set_header("Sec-WebSocket-Version", "");
    const UpgradeResult missing = evaluate_upgrade(no_version, make_enabled_config());
    assert(missing.verdict == UpgradeVerdict::BadRequest);
}

void test_non_get_rejected() {
    std::cout << "Testing non-GET upgrade rejection..." << std::endl;
    http::HttpRequest request(http::HttpMethod::POST, "/ws");
    request.set_header("Upgrade", "websocket");
    request.set_header("Connection", "Upgrade");
    request.set_header("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    request.set_header("Sec-WebSocket-Version", "13");

    const UpgradeResult result = evaluate_upgrade(request, make_enabled_config());
    assert(result.verdict == UpgradeVerdict::BadRequest);
}

void test_wrong_path_falls_through_to_http() {
    std::cout << "Testing non-allowlisted path falls through..." << std::endl;
    http::HttpRequest request = make_valid_request();
    request.set_target("/other");

    const UpgradeResult result = evaluate_upgrade(request, make_enabled_config());
    assert(result.verdict == UpgradeVerdict::NotAnUpgrade);
}

void test_query_string_path_matches() {
    std::cout << "Testing query strings are stripped for path matching..." << std::endl;
    http::HttpRequest request = make_valid_request();
    request.set_target("/ws?token=abc");

    const UpgradeResult result = evaluate_upgrade(request, make_enabled_config());
    assert(result.verdict == UpgradeVerdict::Accepted);
}

void test_plain_get_is_not_an_upgrade() {
    std::cout << "Testing plain GET on /ws is not an upgrade attempt..." << std::endl;
    http::HttpRequest request(http::HttpMethod::GET, "/ws");
    request.set_header("Host", "localhost:18081");

    assert(!is_websocket_intent(request));
    const UpgradeResult result = evaluate_upgrade(request, make_enabled_config());
    assert(result.verdict == UpgradeVerdict::NotAnUpgrade);
}

void test_disabled_feature_rejects() {
    std::cout << "Testing disabled WebSocket feature..." << std::endl;
    ServerConfig config;  // websocket_enabled defaults to false
    const UpgradeResult result = evaluate_upgrade(make_valid_request(), config);
    assert(result.verdict == UpgradeVerdict::NotAnUpgrade);
}

void test_origin_allowlist() {
    std::cout << "Testing Origin allowlist..." << std::endl;
    ServerConfig config = make_enabled_config();
    config.set_websocket_allowed_origins({"http://allowed.example"});

    // Matching origin -> accepted.
    http::HttpRequest good = make_valid_request();
    good.set_header("Origin", "http://allowed.example");
    assert(evaluate_upgrade(good, config).verdict == UpgradeVerdict::Accepted);

    // Case-insensitive match.
    http::HttpRequest cased = make_valid_request();
    cased.set_header("Origin", "HTTP://Allowed.Example");
    assert(evaluate_upgrade(cased, config).verdict == UpgradeVerdict::Accepted);

    // Wrong origin -> 403.
    http::HttpRequest evil = make_valid_request();
    evil.set_header("Origin", "http://evil.example");
    assert(evaluate_upgrade(evil, config).verdict == UpgradeVerdict::ForbiddenOrigin);

    // Missing origin while restricted -> 403.
    http::HttpRequest none = make_valid_request();
    assert(evaluate_upgrade(none, config).verdict == UpgradeVerdict::ForbiddenOrigin);

    // Empty allowlist -> any origin allowed.
    ServerConfig open = make_enabled_config();
    http::HttpRequest anywhere = make_valid_request();
    anywhere.set_header("Origin", "http://anywhere.example");
    assert(evaluate_upgrade(anywhere, open).verdict == UpgradeVerdict::Accepted);
}

// =============================================================================
// Rejection Responses
// =============================================================================

void test_rejection_responses() {
    std::cout << "Testing rejection responses (400/403/426)..." << std::endl;

    // 400 Bad Request: malformed upgrade.
    const std::string bad =
        http::HttpResponseSerializer::serialize(build_rejection_response(UpgradeVerdict::BadRequest));
    assert(contains(bad, "HTTP/1.1 400 Bad Request"));
    assert(contains(bad, "Connection: close"));
    assert(contains(bad, "Content-Length: "));

    // 403 Forbidden: origin not allowed.
    const std::string forbidden = http::HttpResponseSerializer::serialize(
        build_rejection_response(UpgradeVerdict::ForbiddenOrigin));
    assert(contains(forbidden, "HTTP/1.1 403 Forbidden"));

    // 426 Upgrade Required: unsupported version, advertises version 13.
    const std::string version = http::HttpResponseSerializer::serialize(
        build_rejection_response(UpgradeVerdict::UnsupportedVersion));
    assert(contains(version, "HTTP/1.1 426 Upgrade Required"));
    assert(contains(version, "Sec-WebSocket-Version: 13"));

    // All rejection responses terminate with an empty line.
    assert(bad.substr(bad.size() - 4) == "\r\n\r\n" || contains(bad, "\r\n\r\n"));
}

void test_switching_protocols_format() {
    std::cout << "Testing raw 101 response format..." << std::endl;
    const std::string response = build_switching_protocols("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    // Exactly the four required lines and no body framing headers.
    assert(response ==
           "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
           "\r\n");
    assert(!contains(response, "Content-Length"));
    assert(!contains(response, "Transfer-Encoding"));
}

} // namespace

// =============================================================================
// Test Runner
// =============================================================================

int main() {
    std::cout << "=== WebSocket Handshake Tests (Phase 23) ===" << std::endl;

    test_rfc_6455_accept_vector();
    test_invalid_keys_rejected();

    test_valid_upgrade_accepted();
    test_intent_detection_is_case_insensitive();
    test_missing_connection_upgrade_rejected();
    test_missing_key_rejected();
    test_short_key_rejected();
    test_version_negotiation();
    test_non_get_rejected();
    test_wrong_path_falls_through_to_http();
    test_query_string_path_matches();
    test_plain_get_is_not_an_upgrade();
    test_disabled_feature_rejects();
    test_origin_allowlist();

    test_rejection_responses();
    test_switching_protocols_format();

    std::cout << "All WebSocket handshake tests passed!" << std::endl;
    return 0;
}
