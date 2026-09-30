// =============================================================================
// Aevrix - Reverse Proxy Target Tests (Phase 22)
// =============================================================================
// Unit tests for parse_proxy_target(): the configuration boundary of the
// reverse proxy. A malformed proxy_pass must be rejected at startup rather than
// producing a half-configured proxy at runtime.
// =============================================================================

#include "aevrix/proxy_target.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace aevrix;

// =============================================================================
// Helpers
// =============================================================================

namespace {

void expect_accepted(const std::string& input,
                     const std::string& expected_host,
                     uint16_t expected_port,
                     const std::string& label) {
    std::string error;
    const auto target = parse_proxy_target(input, error);

    if (!target.has_value()) {
        std::cerr << "FAILED: '" << input << "' should be accepted (" << label
                  << ") but was rejected: " << error << std::endl;
        assert(false && "expected target to be accepted");
    }

    assert(target->host == expected_host && "host mismatch");
    assert(target->port == expected_port && "port mismatch");
    assert(error.empty() && "error must stay empty on success");
}

void expect_rejected(const std::string& input, const std::string& label) {
    std::string error;
    const auto target = parse_proxy_target(input, error);

    if (target.has_value()) {
        std::cerr << "FAILED: '" << input << "' should be rejected (" << label
                  << ") but was accepted" << std::endl;
        assert(false && "expected target to be rejected");
    }

    assert(!error.empty() && "a rejection must explain itself");
}

}  // namespace

// =============================================================================
// Tests
// =============================================================================

void test_absolute_url_with_port() {
    std::cout << "Testing http://host:port..." << std::endl;

    expect_accepted("http://127.0.0.1:9001", "127.0.0.1", 9001, "ipv4 with port");
    expect_accepted("http://localhost:3000", "localhost", 3000, "hostname with port");
    expect_accepted("HTTP://Example.COM:8080", "Example.COM", 8080, "scheme is case insensitive");

    std::string error;
    const auto target = parse_proxy_target("http://127.0.0.1:9001", error);
    assert(target.has_value());
    assert(target->authority() == "127.0.0.1:9001" && "authority formatting");

    std::cout << "  PASSED" << std::endl;
}

void test_default_port() {
    std::cout << "Testing default port 80..." << std::endl;

    expect_accepted("http://example.com", "example.com", 80, "no port");
    expect_accepted("example.com", "example.com", 80, "bare hostname");
    expect_accepted("http://[::1]", "::1", 80, "ipv6 without port");

    std::cout << "  PASSED" << std::endl;
}

void test_host_port_only() {
    std::cout << "Testing bare host:port form..." << std::endl;

    expect_accepted("127.0.0.1:8080", "127.0.0.1", 8080, "no scheme");
    expect_accepted("backend.internal:9000", "backend.internal", 9000, "dotted name");

    std::cout << "  PASSED" << std::endl;
}

void test_whitespace_and_path_ignored() {
    std::cout << "Testing whitespace handling and path truncation..." << std::endl;

    expect_accepted("  http://127.0.0.1:9001  ", "127.0.0.1", 9001, "surrounding spaces");
    expect_accepted("http://127.0.0.1:9001/api/v1", "127.0.0.1", 9001, "path dropped");
    expect_accepted("http://127.0.0.1:9001?x=1", "127.0.0.1", 9001, "query dropped");

    std::cout << "  PASSED" << std::endl;
}

void test_ipv6_literal() {
    std::cout << "Testing IPv6 literals..." << std::endl;

    expect_accepted("[::1]:8080", "::1", 8080, "bracketed ipv6 with port");
    expect_accepted("http://[2001:db8::1]:8443", "2001:db8::1", 8443, "full ipv6");

    expect_rejected("::1:8080", "unbracketed ipv6");
    expect_rejected("[::1", "unterminated ipv6 literal");

    std::cout << "  PASSED" << std::endl;
}

void test_rejections() {
    std::cout << "Testing rejected configurations..." << std::endl;

    // A TLS upstream cannot be honoured: Aevrix would speak plaintext to it.
    expect_rejected("https://127.0.0.1:443", "https scheme");
    expect_rejected("ftp://127.0.0.1:21", "unsupported scheme");

    expect_rejected("", "empty value");
    expect_rejected("   ", "blank value");
    expect_rejected("http://", "scheme with no host");
    expect_rejected("http:///path", "no authority");

    expect_rejected("http://127.0.0.1:0", "port zero");
    expect_rejected("http://127.0.0.1:65536", "port above range");
    expect_rejected("http://127.0.0.1:999999", "port far above range");
    expect_rejected("http://127.0.0.1:abc", "non numeric port");
    expect_rejected("http://127.0.0.1:-1", "negative port");
    expect_rejected("http://127.0.0.1:80x", "trailing junk in port");

    // Characters that would corrupt the authority or the upstream Host header.
    expect_rejected("http://us er:80", "space in host");
    expect_rejected("http://user@host:80", "userinfo is not supported");
    expect_rejected("http://host\\evil:80", "backslash in host");

    std::cout << "  PASSED" << std::endl;
}

void test_equality() {
    std::cout << "Testing ProxyTarget equality..." << std::endl;

    std::string error;
    const auto a = parse_proxy_target("http://127.0.0.1:9001", error);
    const auto b = parse_proxy_target("127.0.0.1:9001", error);
    const auto c = parse_proxy_target("127.0.0.1:9002", error);

    assert(a.has_value() && b.has_value() && c.has_value());
    assert(*a == *b && "same target must compare equal");
    assert(*a != *c && "different ports must differ");

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "=== Reverse Proxy Target Tests ===" << std::endl;
    std::cout << std::endl;

    test_absolute_url_with_port();
    test_default_port();
    test_host_port_only();
    test_whitespace_and_path_ignored();
    test_ipv6_literal();
    test_rejections();
    test_equality();

    std::cout << std::endl;
    std::cout << "=== All Tests Passed ===" << std::endl;

    return 0;
}
