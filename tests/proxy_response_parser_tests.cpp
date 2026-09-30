// =============================================================================
// Aevrix - Proxy Upstream Response Parser Tests (Phase 22)
// =============================================================================
// Unit tests for the incremental upstream response parser.
//
// Framing is a security boundary for a reverse proxy: if Content-Length,
// Transfer-Encoding and connection-close delimiters are not interpreted
// strictly, a hostile upstream can desynchronise the response stream. These
// tests therefore focus on framing correctness, limits, and rejection of
// ambiguous or malformed messages.
// =============================================================================

#include "aevrix/proxy_response_parser.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace aevrix;

namespace {

ProxyResponseParser::Config config_with_limits(size_t max_header = 8 * 1024,
                                               size_t max_body = 1024) {
    ProxyResponseParser::Config config;
    config.max_header_bytes = max_header;
    config.max_body_bytes = max_body;
    return config;
}

bool feed(ProxyResponseParser& parser, const std::string& data) {
    return parser.feed(data.c_str(), data.size());
}

/**
 * @brief Feed a string one byte at a time to prove incremental correctness
 */
bool feed_byte_by_byte(ProxyResponseParser& parser, const std::string& data) {
    for (char c : data) {
        if (!parser.feed(&c, 1)) {
            return false;
        }
    }
    return true;
}

}  // namespace

// =============================================================================
// Fixed-length bodies
// =============================================================================

void test_content_length_response() {
    std::cout << "Testing Content-Length response..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    assert(feed(parser, response));
    assert(parser.is_complete() && "complete response recognised");
    assert(!parser.has_error());
    assert(parser.status_code() == 200);
    assert(parser.reason_phrase() == "OK");
    assert(parser.headers().get("Content-Type") == "text/plain");
    assert(parser.body() == "hello");
    assert(parser.framing() == ProxyResponseParser::Framing::ContentLength);
    assert(parser.upstream_keep_alive() && "HTTP/1.1 without close is reusable");

    std::cout << "  PASSED" << std::endl;
}

void test_incremental_feeding() {
    std::cout << "Testing byte-by-byte feeding..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    const std::string response =
        "HTTP/1.1 201 Created\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world";

    assert(feed_byte_by_byte(parser, response));
    assert(parser.is_complete());
    assert(parser.status_code() == 201);
    assert(parser.reason_phrase() == "Created");
    assert(parser.body() == "hello world");

    std::cout << "  PASSED" << std::endl;
}

void test_body_split_across_feeds() {
    std::cout << "Testing body split across feeds..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc"));
    assert(!parser.is_complete() && "partial body is not complete");
    assert(parser.body() == "abc");
    assert(feed(parser, "defg"));
    assert(!parser.is_complete());
    assert(feed(parser, "hij"));
    assert(parser.is_complete());
    assert(parser.body() == "abcdefghij");

    std::cout << "  PASSED" << std::endl;
}

void test_zero_content_length() {
    std::cout << "Testing zero-length body..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"));
    assert(parser.is_complete());
    assert(parser.body().empty());
    assert(parser.framing() == ProxyResponseParser::Framing::ContentLength);

    std::cout << "  PASSED" << std::endl;
}

void test_bodyless_statuses() {
    std::cout << "Testing 204 and 304..." << std::endl;

    // 204 must terminate at the header block.
    ProxyResponseParser no_content(config_with_limits());
    assert(feed(no_content, "HTTP/1.1 204 No Content\r\n\r\n"));
    assert(no_content.is_complete());
    assert(no_content.framing() == ProxyResponseParser::Framing::None);
    assert(no_content.body().empty());

    // 304 may legitimately carry a Content-Length describing the cached entity.
    ProxyResponseParser not_modified(config_with_limits());
    assert(feed(not_modified,
                "HTTP/1.1 304 Not Modified\r\nContent-Length: 4096\r\n\r\n"));
    assert(not_modified.is_complete() && "304 has no body despite Content-Length");
    assert(not_modified.framing() == ProxyResponseParser::Framing::None);

    std::cout << "  PASSED" << std::endl;
}

void test_head_request_has_no_body() {
    std::cout << "Testing HEAD response framing..." << std::endl;

    ProxyResponseParser::Config config = config_with_limits();
    config.request_was_head = true;

    ProxyResponseParser parser(config);
    assert(feed(parser,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 1234\r\n"
                "\r\n"));
    assert(parser.is_complete() && "HEAD response ends at the header block");
    assert(parser.framing() == ProxyResponseParser::Framing::None);
    assert(parser.body().empty());
    assert(parser.headers().get("Content-Length") == "1234" &&
           "framing of the equivalent GET is preserved");

    std::cout << "  PASSED" << std::endl;
}

void test_http_10_response() {
    std::cout << "Testing HTTP/1.0 response..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nhi"));
    assert(parser.is_complete());
    assert(parser.status_code() == 200);

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Chunked transfer coding
// =============================================================================

void test_chunked_response() {
    std::cout << "Testing chunked response..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n\r\n";

    assert(feed(parser, response));
    assert(parser.is_complete() && "chunked message terminated by the zero chunk");
    assert(parser.framing() == ProxyResponseParser::Framing::Chunked);
    assert(parser.body() == "hello world" && "chunks decoded into a plain body");
    assert(parser.upstream_keep_alive() && "terminated chunked message is reusable");

    std::cout << "  PASSED" << std::endl;
}

void test_chunked_extensions_and_trailers() {
    std::cout << "Testing chunk extensions and trailers..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "A;name=value\r\n0123456789\r\n"
        "0\r\n"
        "X-Checksum: abc123\r\n"
        "\r\n";

    assert(feed(parser, response));
    assert(parser.is_complete());
    assert(parser.body() == "0123456789" && "extension ignored, data kept");
    assert(parser.headers().get("X-Checksum").empty() &&
           "trailer fields are decoded but not relayed");

    std::cout << "  PASSED" << std::endl;
}

void test_chunked_incremental() {
    std::cout << "Testing chunked byte-by-byte..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    const std::string response =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\nabc\r\n3\r\ndef\r\n0\r\n\r\n";

    assert(feed_byte_by_byte(parser, response));
    assert(parser.is_complete());
    assert(parser.body() == "abcdef");

    std::cout << "  PASSED" << std::endl;
}

void test_chunked_zero_length_first() {
    std::cout << "Testing empty chunked body..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"));
    assert(parser.is_complete());
    assert(parser.body().empty());

    std::cout << "  PASSED" << std::endl;
}

void test_chunked_malformed_terminator() {
    std::cout << "Testing malformed chunk terminator..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser,
                 "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                 "5\r\nhelloXX"));
    assert(parser.has_error());
    assert(!parser.is_complete());

    std::cout << "  PASSED" << std::endl;
}

void test_chunked_invalid_size() {
    std::cout << "Testing malformed chunk size..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\n"));
    assert(parser.has_error());
    assert(!parser.error_message().empty());

    std::cout << "  PASSED" << std::endl;
}

void test_chunked_truncated_on_eof() {
    std::cout << "Testing truncated chunked response..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel"));
    assert(!parser.is_complete());
    assert(!parser.finish_on_eof() && "EOF mid-chunk is a truncated response");
    assert(parser.has_error());

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Close-delimited bodies
// =============================================================================

void test_until_close_body() {
    std::cout << "Testing close-delimited body..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\npartial"));
    assert(!parser.is_complete() && "close-delimited body needs EOF");
    assert(parser.body() == "partial");

    assert(parser.finish_on_eof() && "EOF terminates the message");
    assert(parser.is_complete());
    assert(parser.framing() == ProxyResponseParser::Framing::UntilClose);
    assert(parser.body() == "partial");
    assert(!parser.upstream_keep_alive() && "unknown boundary: never reused");

    std::cout << "  PASSED" << std::endl;
}

void test_truncated_content_length_on_eof() {
    std::cout << "Testing truncated Content-Length body..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc"));
    assert(!parser.is_complete());
    assert(!parser.finish_on_eof() && "short body is a truncated response");
    assert(parser.has_error());

    std::cout << "  PASSED" << std::endl;
}

void test_truncated_headers_on_eof() {
    std::cout << "Testing truncated headers..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nContent-Ty"));
    assert(!parser.finish_on_eof() && "incomplete head is an error");
    assert(parser.has_error());

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Framing security
// =============================================================================

void test_content_length_and_transfer_encoding_conflict() {
    std::cout << "Testing CL+TE conflict rejection..." << std::endl;

    // The classic request/response smuggling setup: two framing mechanisms.
    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser,
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: 5\r\n"
                 "Transfer-Encoding: chunked\r\n"
                 "\r\n"
                 "0\r\n\r\n"));
    assert(parser.has_error() && "ambiguous framing must be rejected");
    assert(!parser.is_complete());

    std::cout << "  PASSED" << std::endl;
}

void test_conflicting_content_lengths() {
    std::cout << "Testing conflicting Content-Length headers..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n"));
    assert(parser.has_error());

    std::cout << "  PASSED" << std::endl;
}

void test_identical_content_lengths_are_allowed() {
    std::cout << "Testing identical duplicate Content-Length..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello"));
    assert(parser.is_complete() && "identical duplicates are unambiguous");
    assert(parser.body() == "hello");

    std::cout << "  PASSED" << std::endl;
}

void test_unsupported_transfer_encoding() {
    std::cout << "Testing unsupported Transfer-Encoding..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser, "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n"));
    assert(parser.has_error() && "only chunked is understood");

    std::cout << "  PASSED" << std::endl;
}

void test_upgrade_is_rejected() {
    std::cout << "Testing 101 upgrade rejection..." << std::endl;

    // A plain HTTP proxy must never silently relay a protocol upgrade.
    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser,
                 "HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "\r\n"));
    assert(parser.has_error());

    std::cout << "  PASSED" << std::endl;
}

void test_interim_response_is_consumed() {
    std::cout << "Testing 1xx interim response handling..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser,
                "HTTP/1.1 100 Continue\r\n"
                "\r\n"
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 2\r\n"
                "\r\n"
                "ok"));
    assert(parser.is_complete() && "the final response is the one reported");
    assert(parser.status_code() == 200);
    assert(parser.body() == "ok");
    assert(parser.headers().get("Content-Length") == "2" &&
           "interim headers do not leak into the final response");

    std::cout << "  PASSED" << std::endl;
}

void test_connection_close_prevents_reuse() {
    std::cout << "Testing Connection: close..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 2\r\n"
                "Connection: close\r\n"
                "\r\n"
                "hi"));
    assert(parser.is_complete());
    assert(!parser.upstream_keep_alive() && "upstream asked for a close");

    std::cout << "  PASSED" << std::endl;
}


// =============================================================================
// Malformed input and resource limits
// =============================================================================

void test_malformed_status_line() {
    std::cout << "Testing malformed status lines..." << std::endl;

    ProxyResponseParser no_version(config_with_limits());
    assert(!feed(no_version, "garbage\r\n\r\n"));
    assert(no_version.has_error());

    ProxyResponseParser no_code(config_with_limits());
    assert(!feed(no_code, "HTTP/1.1 OK\r\n\r\n"));
    assert(no_code.has_error());

    ProxyResponseParser bad_code(config_with_limits());
    assert(!feed(bad_code, "HTTP/1.1 2x0 OK\r\n\r\n"));
    assert(bad_code.has_error());

    ProxyResponseParser out_of_range(config_with_limits());
    assert(!feed(out_of_range, "HTTP/1.1 999 Nope\r\n\r\n"));
    assert(out_of_range.has_error());

    std::cout << "  PASSED" << std::endl;
}

void test_unsupported_protocol_version() {
    std::cout << "Testing unsupported protocol versions..." << std::endl;

    ProxyResponseParser http2(config_with_limits());
    assert(!feed(http2, "HTTP/2 200 OK\r\n\r\n"));
    assert(http2.has_error());

    ProxyResponseParser other(config_with_limits());
    assert(!feed(other, "ICY 200 OK\r\n\r\n"));
    assert(other.has_error());

    std::cout << "  PASSED" << std::endl;
}

void test_obsolete_line_folding_rejected() {
    std::cout << "Testing obsolete header folding..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser,
                 "HTTP/1.1 200 OK\r\n"
                 "X-Folded: first\r\n"
                 " second\r\n"
                 "\r\n"));
    assert(parser.has_error() && "obs-fold is a smuggling primitive");

    std::cout << "  PASSED" << std::endl;
}

void test_invalid_header_name_rejected() {
    std::cout << "Testing invalid header name..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser, "HTTP/1.1 200 OK\r\nBad Header: value\r\n\r\n"));
    assert(parser.has_error());

    std::cout << "  PASSED" << std::endl;
}

void test_missing_colon_rejected() {
    std::cout << "Testing header without colon..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(!feed(parser, "HTTP/1.1 200 OK\r\nNotAHeader\r\n\r\n"));
    assert(parser.has_error());

    std::cout << "  PASSED" << std::endl;
}

void test_lf_only_line_endings_not_accepted() {
    std::cout << "Testing LF-only line endings..." << std::endl;

    // Strict CRLF framing: an LF-only message must never be accepted, because
    // accepting both terminators is a well known smuggling vector.
    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\nContent-Length: 2\n\nhi"));
    assert(!parser.is_complete() && "LF-only framing is not a complete response");
    assert(!parser.has_error() && "the parser simply waits for CRLF data");

    std::cout << "  PASSED" << std::endl;
}

void test_header_budget() {
    std::cout << "Testing header size limit..." << std::endl;

    ProxyResponseParser::Config config = config_with_limits(128, 64);
    ProxyResponseParser parser(config);

    const std::string oversized =
        "HTTP/1.1 200 OK\r\nX-Padding: " + std::string(200, 'a') + "\r\n\r\n";
    assert(!feed(parser, oversized));
    assert(parser.has_error() && "oversized head must be refused");

    std::cout << "  PASSED" << std::endl;
}

void test_body_budget_content_length() {
    std::cout << "Testing body limit (Content-Length)..." << std::endl;

    ProxyResponseParser::Config config = config_with_limits(1024, 16);
    ProxyResponseParser parser(config);

    assert(!feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 17\r\n\r\n"));
    assert(parser.has_error() && "declared body above the cap must be refused");

    std::cout << "  PASSED" << std::endl;
}

void test_body_budget_enforced_while_streaming() {
    std::cout << "Testing body limit while streaming..." << std::endl;

    ProxyResponseParser::Config config = config_with_limits(1024, 8);
    ProxyResponseParser parser(config);

    assert(feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\n1234"));
    assert(!parser.is_complete());
    assert(feed(parser, "5678"));
    assert(parser.is_complete() && "body exactly at the cap is accepted");

    std::cout << "  PASSED" << std::endl;
}

void test_extra_bytes_after_completion_ignored() {
    std::cout << "Testing trailing bytes after completion..." << std::endl;

    ProxyResponseParser parser(config_with_limits());
    assert(feed(parser, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"));
    assert(parser.is_complete());

    // One request is in flight per connection, so trailing bytes indicate a
    // desynchronised peer: they must be ignored, not appended to the body.
    assert(feed(parser, "GARBAGE"));
    assert(parser.is_complete() && !parser.has_error());
    assert(parser.body() == "hi");

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "=== Proxy Upstream Response Parser Tests ===" << std::endl;
    std::cout << std::endl;

    test_content_length_response();
    test_incremental_feeding();
    test_body_split_across_feeds();
    test_zero_content_length();
    test_bodyless_statuses();
    test_head_request_has_no_body();
    test_http_10_response();
    test_chunked_response();
    test_chunked_extensions_and_trailers();
    test_chunked_incremental();
    test_chunked_zero_length_first();
    test_chunked_malformed_terminator();
    test_chunked_invalid_size();
    test_chunked_truncated_on_eof();
    test_until_close_body();
    test_truncated_content_length_on_eof();
    test_truncated_headers_on_eof();
    test_content_length_and_transfer_encoding_conflict();
    test_conflicting_content_lengths();
    test_identical_content_lengths_are_allowed();
    test_unsupported_transfer_encoding();
    test_upgrade_is_rejected();
    test_interim_response_is_consumed();
    test_connection_close_prevents_reuse();
    test_malformed_status_line();
    test_unsupported_protocol_version();
    test_obsolete_line_folding_rejected();
    test_invalid_header_name_rejected();
    test_missing_colon_rejected();
    test_lf_only_line_endings_not_accepted();
    test_header_budget();
    test_body_budget_content_length();
    test_body_budget_enforced_while_streaming();
    test_extra_bytes_after_completion_ignored();

    std::cout << std::endl;
    std::cout << "=== All Tests Passed ===" << std::endl;

    return 0;
}
