// =============================================================================
// Aevrix - Proxy Request Builder Tests (Phase 22)
// =============================================================================
// Unit tests for the request-rewriting half of the reverse proxy. These cover
// the security-relevant behaviour: hop-by-hop header stripping, framing
// regeneration, and forwarding-header construction.
// =============================================================================

#include "aevrix/proxy_request_builder.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace aevrix;
using aevrix::http::HttpHeaders;
using aevrix::http::HttpMethod;
using aevrix::http::HttpRequest;

namespace {

HttpRequest make_request(HttpMethod method,
                         const std::string& target,
                         const std::string& body = std::string()) {
    HttpRequest request;
    request.set_method(method);
    request.set_target(target);
    request.set_version("HTTP/1.1");
    if (!body.empty()) {
        request.set_body(body);
    }
    return request;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

ProxyRequestOptions default_options() {
    ProxyRequestOptions options;
    options.prefix = "/api";
    options.strip_prefix = true;
    options.forwarded_proto = "http";
    options.client_ip = "10.0.0.7";
    return options;
}

}  // namespace

// =============================================================================
// Routing
// =============================================================================

void test_target_matching() {
    std::cout << "Testing prefix matching..." << std::endl;

    // Boundary aware: "/proxyfoo" must not match the "/proxy" prefix.
    assert(proxy_target_matches("/proxy", "/proxy"));
    assert(proxy_target_matches("/proxy/", "/proxy"));
    assert(proxy_target_matches("/proxy/users/1", "/proxy"));
    assert(proxy_target_matches("/proxy?x=1", "/proxy"));
    assert(!proxy_target_matches("/proxyfoo", "/proxy"));
    assert(!proxy_target_matches("/other", "/proxy"));
    assert(!proxy_target_matches("/", "/proxy"));
    assert(!proxy_target_matches("/api", ""));

    // Prefix "/" proxies every absolute path.
    assert(proxy_target_matches("/anything", "/"));
    assert(proxy_target_matches("/", "/"));
    assert(!proxy_target_matches("", "/"));

    // Trailing slash prefixes match everything below them.
    assert(proxy_target_matches("/api/v1", "/api/"));
    assert(proxy_target_matches("/api/x", "/api/"));

    std::cout << "  PASSED" << std::endl;
}

void test_path_rewriting() {
    std::cout << "Testing path rewriting..." << std::endl;

    assert(proxy_upstream_path("/api/users", "/api", true) == "/users");
    assert(proxy_upstream_path("/api", "/api", true) == "/");
    assert(proxy_upstream_path("/api/", "/api", true) == "/");
    assert(proxy_upstream_path("/api/users?q=1", "/api", true) == "/users?q=1");
    assert(proxy_upstream_path("/api", "/api", false) == "/api");
    assert(proxy_upstream_path("/api?q=1", "/api", false) == "/api?q=1");
    assert(proxy_upstream_path("/other/users", "/api", true) == "/other/users");
    assert(proxy_upstream_path("/api/users", "/", true) == "/api/users");
    assert(proxy_upstream_path("", "/api", true) == "/");

    std::cout << "  PASSED" << std::endl;
}

void test_hop_by_hop_identification() {
    std::cout << "Testing hop-by-hop header identification..." << std::endl;

    assert(is_hop_by_hop_header("connection"));
    assert(is_hop_by_hop_header("keep-alive"));
    assert(is_hop_by_hop_header("transfer-encoding"));
    assert(is_hop_by_hop_header("te"));
    assert(is_hop_by_hop_header("trailer"));
    assert(is_hop_by_hop_header("upgrade"));
    assert(is_hop_by_hop_header("proxy-authenticate"));
    assert(is_hop_by_hop_header("proxy-authorization"));

    assert(!is_hop_by_hop_header("content-length"));
    assert(!is_hop_by_hop_header("accept"));
    assert(!is_hop_by_hop_header("authorization"));
    assert(!is_hop_by_hop_header("cookie"));

    std::cout << "  PASSED" << std::endl;
}

void test_request_line_and_framing() {
    std::cout << "Testing request line, Host and forwarding headers..." << std::endl;

    const ProxyTarget target{"127.0.0.1", 9001};
    HttpRequest request = make_request(HttpMethod::GET, "/api/users?page=2");
    request.set_header("Host", "client.example:8080");
    request.set_header("Accept", "application/json");

    const std::string out = build_upstream_request(request, target, default_options());

    assert(out.rfind("GET /users?page=2 HTTP/1.1\r\n", 0) == 0 && "request line rewritten");
    assert(contains(out, "\r\nHost: 127.0.0.1:9001\r\n") && "Host points at the upstream");
    assert(contains(out, "\r\nAccept: application/json\r\n") && "end-to-end header kept");
    assert(contains(out, "\r\nX-Forwarded-For: 10.0.0.7\r\n") && "client address forwarded");
    assert(contains(out, "\r\nX-Forwarded-Proto: http\r\n") && "protocol forwarded");
    assert(contains(out, "\r\nX-Forwarded-Host: client.example:8080\r\n") && "host forwarded");
    assert(out.compare(out.size() - 4, 4, "\r\n\r\n") == 0 && "empty body has no payload");

    std::cout << "  PASSED" << std::endl;
}

void test_post_body_and_content_length() {
    std::cout << "Testing body framing regeneration..." << std::endl;

    const ProxyTarget target{"127.0.0.1", 9001};
    HttpRequest request = make_request(HttpMethod::POST, "/api/submit", "hello=world");
    request.set_header("Host", "client.example");
    // A stale inbound Content-Length must never be relayed verbatim.
    request.set_header("Content-Length", "999");

    const std::string out = build_upstream_request(request, target, default_options());

    assert(out.rfind("POST /submit HTTP/1.1\r\n", 0) == 0);
    assert(contains(out, "\r\nContent-Length: 11\r\n") && "length regenerated from the body");
    assert(!contains(out, "999") && "inbound Content-Length dropped");
    assert(out.compare(out.size() - 11, 11, "hello=world") == 0 && "body forwarded last");

    std::cout << "  PASSED" << std::endl;
}

void test_hop_by_hop_stripping() {
    std::cout << "Testing hop-by-hop header stripping..." << std::endl;

    const ProxyTarget target{"127.0.0.1", 9001};
    HttpRequest request = make_request(HttpMethod::GET, "/api/x");
    request.set_header("Host", "client.example");
    request.set_header("Connection", "close");
    request.set_header("Keep-Alive", "timeout=5");
    request.set_header("Transfer-Encoding", "chunked");
    request.set_header("TE", "trailers");
    request.set_header("Trailer", "X-Checksum");
    request.set_header("Upgrade", "h2c");
    request.set_header("Proxy-Authorization", "Basic c2VjcmV0");
    request.set_header("Authorization", "Bearer keep-me");

    const std::string out = build_upstream_request(request, target, default_options());

    assert(!contains(out, "Keep-Alive") && "keep-alive dropped");
    assert(!contains(out, "Transfer-Encoding") && "transfer-encoding dropped");
    assert(!contains(out, "Trailer") && "trailer dropped");
    assert(!contains(out, "Upgrade") && "upgrade dropped");
    assert(!contains(out, "Proxy-Authorization") && "proxy credentials never forwarded");
    assert(!contains(out, "c2VjcmV0") && "proxy credential value never forwarded");
    assert(contains(out, "Authorization: Bearer keep-me") && "end-to-end auth kept");
    assert(contains(out, "Connection: keep-alive") && "pooled connection requested");

    std::cout << "  PASSED" << std::endl;
}

void test_forwarded_for_appending() {
    std::cout << "Testing X-Forwarded-For appending..." << std::endl;

    const ProxyTarget target{"127.0.0.1", 9001};
    HttpRequest request = make_request(HttpMethod::GET, "/api/x");
    request.set_header("Host", "client.example");
    request.set_header("X-Forwarded-For", "192.168.1.1");

    const std::string out = build_upstream_request(request, target, default_options());

    assert(contains(out, "\r\nX-Forwarded-For: 192.168.1.1, 10.0.0.7\r\n") &&
           "existing chain extended");

    // Exactly one occurrence: the inbound header must not be copied as well.
    const size_t first = out.find("X-Forwarded-For");
    assert(first != std::string::npos);
    assert(out.find("X-Forwarded-For", first + 1) == std::string::npos &&
           "forwarded header must not be duplicated");

    std::cout << "  PASSED" << std::endl;
}

void test_omitted_forwarding_headers() {
    std::cout << "Testing omitted forwarding headers..." << std::endl;

    const ProxyTarget target{"127.0.0.1", 9001};
    HttpRequest request = make_request(HttpMethod::GET, "/api/x");

    ProxyRequestOptions options = default_options();
    options.client_ip.clear();
    options.forwarded_proto.clear();

    const std::string out = build_upstream_request(request, target, options);

    assert(!contains(out, "X-Forwarded-For") && "no peer address, no header");
    assert(!contains(out, "X-Forwarded-Proto") && "no protocol, no header");
    assert(!contains(out, "X-Forwarded-Host") && "no inbound Host, no header");
    assert(contains(out, "\r\nHost: 127.0.0.1:9001\r\n") && "Host is always rewritten");

    std::cout << "  PASSED" << std::endl;
}

void test_header_injection_is_dropped() {
    std::cout << "Testing header injection defence..." << std::endl;

    const ProxyTarget target{"127.0.0.1", 9001};
    HttpRequest request = make_request(HttpMethod::GET, "/api/x");
    request.set_header("Host", "client.example");
    // A raw CRLF inside a value would otherwise start a new header line.
    request.set_header("X-Evil", "value\r\nInjected: yes");

    const std::string out = build_upstream_request(request, target, default_options());

    assert(!contains(out, "Injected") && "injected header line must not appear");
    assert(!contains(out, "X-Evil") && "malformed header dropped entirely");
    assert(contains(out, "HTTP/1.1\r\n") && "request still well formed");

    std::cout << "  PASSED" << std::endl;
}

void test_prefix_preserved() {
    std::cout << "Testing prefix preservation..." << std::endl;

    const ProxyTarget target{"127.0.0.1", 9001};
    HttpRequest request = make_request(HttpMethod::HTTP_PATCH, "/api/thing");

    ProxyRequestOptions options = default_options();
    options.strip_prefix = false;

    const std::string out = build_upstream_request(request, target, options);

    assert(out.rfind("PATCH /api/thing HTTP/1.1\r\n", 0) == 0 &&
           "method and unstripped path preserved");

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "=== Proxy Request Builder Tests ===" << std::endl;
    std::cout << std::endl;

    test_target_matching();
    test_path_rewriting();
    test_hop_by_hop_identification();
    test_request_line_and_framing();
    test_post_body_and_content_length();
    test_hop_by_hop_stripping();
    test_forwarded_for_appending();
    test_omitted_forwarding_headers();
    test_header_injection_is_dropped();
    test_prefix_preserved();

    std::cout << std::endl;
    std::cout << "=== All Tests Passed ===" << std::endl;

    return 0;
}
