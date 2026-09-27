// =============================================================================
// Aevrix - HTTP Cache Control Tests
// =============================================================================
// Unit tests for HTTP cache control features including ETag generation,
// HTTP date formatting, conditional request validation, and range request parsing.
// =============================================================================

#include "aevrix/http_cache.h"
#include "aevrix/http_response.h"
#include "aevrix/http_request.h"
#include <cassert>
#include <iostream>

using namespace aevrix::http;

// =============================================================================
// Test Helper Functions
// =============================================================================

void test_http_date_formatting() {
    std::cout << "Testing HTTP date formatting..." << std::endl;
    
    // Test current date formatting
    std::string current_date = current_http_date();
    assert(!current_date.empty());
    assert(current_date.length() >= 29); // "Wed, 21 Oct 2015 07:28:00 GMT" = 29 chars
    
    // Test that date contains expected format elements
    assert(current_date.find(",") != std::string::npos);
    assert(current_date.find("GMT") != std::string::npos);
    
    std::cout << "  current_http_date() works: " << current_date << std::endl;
    
    // Test formatting a specific time
    time_t test_time = 1445405280; // Wed, 21 Oct 2015 07:28:00 GMT
    std::string formatted = format_http_date(test_time);
    assert(!formatted.empty());
    
    std::cout << "  format_http_date() works: " << formatted << std::endl;
}

void test_http_date_parsing() {
    std::cout << "Testing HTTP date parsing..." << std::endl;
    
    // Test parsing a valid date
    std::string test_date = "Wed, 21 Oct 2015 07:28:00 GMT";
    time_t parsed = parse_http_date(test_date);
    assert(parsed != -1);
    
    std::cout << "  parse_http_date() works for valid date" << std::endl;
    
    // Test parsing an empty date
    time_t empty_parsed = parse_http_date("");
    assert(empty_parsed == -1);
    
    std::cout << "  parse_http_date() returns -1 for empty date" << std::endl;
    
    // Test parsing an invalid date
    time_t invalid_parsed = parse_http_date("invalid-date");
    assert(invalid_parsed == -1);
    
    std::cout << "  parse_http_date() returns -1 for invalid date" << std::endl;
}

void test_etag_generation() {
    std::cout << "Testing ETag generation..." << std::endl;
    
    // Test generating ETag from content
    std::string content1 = "Hello, World!";
    std::string etag1 = generate_etag(content1);
    assert(!etag1.empty());
    
    std::cout << "  generate_etag() works: " << etag1 << std::endl;
    
    // Test that same content generates same ETag
    std::string etag1_again = generate_etag(content1);
    assert(etag1 == etag1_again);
    
    std::cout << "  Same content generates same ETag" << std::endl;
    
    // Test that different content generates different ETag
    std::string content2 = "Different content";
    std::string etag2 = generate_etag(content2);
    assert(etag1 != etag2);
    
    std::cout << "  Different content generates different ETag" << std::endl;
    
    // Test weak ETag generation
    std::string weak_etag = generate_weak_etag(content1);
    assert(!weak_etag.empty());
    assert(weak_etag.substr(0, 2) == "W/");
    
    std::cout << "  generate_weak_etag() works: " << weak_etag << std::endl;
}

void test_etag_matching() {
    std::cout << "Testing ETag matching..." << std::endl;
    
    // Test matching ETags
    std::string etag1 = "abc123";
    std::string etag2 = "abc123";
    assert(etag_matches(etag1, etag2));
    
    std::cout << "  etag_matches() returns true for matching ETags" << std::endl;
    
    // Test non-matching ETags
    std::string etag3 = "abc123";
    std::string etag4 = "def456";
    assert(!etag_matches(etag3, etag4));
    
    std::cout << "  etag_matches() returns false for non-matching ETags" << std::endl;
    
    // Test weak ETag matching
    std::string weak_etag1 = "W/abc123";
    std::string weak_etag2 = "W/abc123";
    assert(etag_matches(weak_etag1, weak_etag2));
    
    std::cout << "  etag_matches() matches weak ETags" << std::endl;
    
    // Test weak vs strong ETag matching
    std::string strong_etag = "abc123";
    assert(etag_matches(weak_etag1, strong_etag));
    
    std::cout << "  etag_matches() matches weak to strong ETag" << std::endl;
    
    // Test is_weak_etag
    assert(is_weak_etag("W/abc123"));
    assert(!is_weak_etag("abc123"));
    
    std::cout << "  is_weak_etag() works correctly" << std::endl;
}

void test_conditional_request_etag() {
    std::cout << "Testing conditional request with ETag..." << std::endl;
    
    // Test matching ETag should return 304
    std::string if_none_match = "\"abc123\"";
    std::string current_etag = "abc123";
    assert(should_return_not_modified_etag(if_none_match, current_etag));
    
    std::cout << "  should_return_not_modified_etag() returns true for matching ETag" << std::endl;
    
    // Test non-matching ETag should not return 304
    std::string if_none_match2 = "\"abc123\"";
    std::string current_etag2 = "def456";
    assert(!should_return_not_modified_etag(if_none_match2, current_etag2));
    
    std::cout << "  should_return_not_modified_etag() returns false for non-matching ETag" << std::endl;
    
    // Test empty headers should not return 304
    assert(!should_return_not_modified_etag("", "abc123"));
    assert(!should_return_not_modified_etag("\"abc123\"", ""));
    
    std::cout << "  should_return_not_modified_etag() returns false for empty headers" << std::endl;
}

void test_conditional_request_date() {
    std::cout << "Testing conditional request with date..." << std::endl;
    
    // Test if resource hasn't been modified
    time_t last_modified = 1445405280; // Wed, 21 Oct 2015 07:28:00 GMT
    std::string if_modified_since = "Wed, 21 Oct 2015 07:28:00 GMT";
    assert(should_return_not_modified_date(if_modified_since, last_modified));
    
    std::cout << "  should_return_not_modified_date() returns true when not modified" << std::endl;
    
    // Test if resource has been modified
    time_t last_modified2 = 1445405280;
    std::string if_modified_since2 = "Wed, 20 Oct 2015 07:28:00 GMT";
    assert(!should_return_not_modified_date(if_modified_since2, last_modified2));
    
    std::cout << "  should_return_not_modified_date() returns false when modified" << std::endl;
    
    // Test empty headers should not return 304
    assert(!should_return_not_modified_date("", last_modified));
    assert(!should_return_not_modified_date("Wed, 21 Oct 2015 07:28:00 GMT", -1));
    
    std::cout << "  should_return_not_modified_date() returns false for empty headers" << std::endl;
}

void test_range_request_parsing() {
    std::cout << "Testing range request parsing..." << std::endl;
    
    // Test valid range
    std::string range_header = "bytes=0-1023";
    size_t file_size = 2048;
    size_t start, end;
    assert(parse_range_header(range_header, file_size, start, end));
    assert(start == 0);
    assert(end == 1023);
    
    std::cout << "  parse_range_header() works for valid range: " << start << "-" << end << std::endl;
    
    // Test range without end
    std::string range_header2 = "bytes=0-";
    assert(parse_range_header(range_header2, file_size, start, end));
    assert(start == 0);
    assert(end == file_size - 1);
    
    std::cout << "  parse_range_header() works for range without end" << std::endl;
    
    // Test invalid range (start > end)
    std::string range_header3 = "bytes=1023-0";
    assert(!parse_range_header(range_header3, file_size, start, end));
    
    std::cout << "  parse_range_header() returns false for invalid range (start > end)" << std::endl;
    
    // Test invalid range (out of bounds)
    std::string range_header4 = "bytes=0-3000";
    assert(!parse_range_header(range_header4, file_size, start, end));
    
    std::cout << "  parse_range_header() returns false for out-of-bounds range" << std::endl;
    
    // Test empty range header
    assert(!parse_range_header("", file_size, start, end));
    
    std::cout << "  parse_range_header() returns false for empty header" << std::endl;
}

void test_content_range_formatting() {
    std::cout << "Testing Content-Range formatting..." << std::endl;
    
    // Test formatting a content range
    std::string content_range = format_content_range(0, 1023, 2048);
    assert(!content_range.empty());
    assert(content_range == "bytes 0-1023/2048");
    
    std::cout << "  format_content_range() works: " << content_range << std::endl;
}

void test_http_response_cache_headers() {
    std::cout << "Testing HTTP response cache headers..." << std::endl;
    
    // Test setting ETag
    HttpResponse response;
    response.set_etag("abc123");
    std::string etag = response.get_etag();
    assert(etag == "abc123");
    
    std::cout << "  HttpResponse::set_etag() and get_etag() work" << std::endl;
    
    // Test setting Last-Modified
    response.set_last_modified("Wed, 21 Oct 2015 07:28:00 GMT");
    std::string last_modified = response.get_last_modified();
    assert(last_modified == "Wed, 21 Oct 2015 07:28:00 GMT");
    
    std::cout << "  HttpResponse::set_last_modified() and get_last_modified() work" << std::endl;
    
    // Test clearing ETag
    response.set_etag("");
    assert(response.get_etag().empty());
    
    std::cout << "  HttpResponse::set_etag(\"\") clears ETag" << std::endl;
    
    // Test clearing Last-Modified
    response.set_last_modified("");
    assert(response.get_last_modified().empty());
    
    std::cout << "  HttpResponse::set_last_modified(\"\") clears Last-Modified" << std::endl;
}

void test_http_request_cache_headers() {
    std::cout << "Testing HTTP request cache headers..." << std::endl;
    
    // Test getting If-None-Match
    HttpRequest request;
    request.set_header("If-None-Match", "\"abc123\"");
    std::string if_none_match = request.get_if_none_match();
    assert(if_none_match == "\"abc123\"");
    
    std::cout << "  HttpRequest::get_if_none_match() works" << std::endl;
    
    // Test getting If-Modified-Since
    request.set_header("If-Modified-Since", "Wed, 21 Oct 2015 07:28:00 GMT");
    std::string if_modified_since = request.get_if_modified_since();
    assert(if_modified_since == "Wed, 21 Oct 2015 07:28:00 GMT");
    
    std::cout << "  HttpRequest::get_if_modified_since() works" << std::endl;
    
    // Test getting Range
    request.set_header("Range", "bytes=0-1023");
    std::string range = request.get_range();
    assert(range == "bytes=0-1023");
    
    std::cout << "  HttpRequest::get_range() works" << std::endl;
    
    // Test empty headers
    HttpRequest request2;
    assert(request2.get_if_none_match().empty());
    assert(request2.get_if_modified_since().empty());
    assert(request2.get_range().empty());
    
    std::cout << "  HttpRequest cache header getters return empty when not set" << std::endl;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "=== HTTP Cache Control Tests ===" << std::endl;
    std::cout << std::endl;
    
    test_http_date_formatting();
    std::cout << std::endl;
    
    test_http_date_parsing();
    std::cout << std::endl;
    
    test_etag_generation();
    std::cout << std::endl;
    
    test_etag_matching();
    std::cout << std::endl;
    
    test_conditional_request_etag();
    std::cout << std::endl;
    
    test_conditional_request_date();
    std::cout << std::endl;
    
    test_range_request_parsing();
    std::cout << std::endl;
    
    test_content_range_formatting();
    std::cout << std::endl;
    
    test_http_response_cache_headers();
    std::cout << std::endl;
    
    test_http_request_cache_headers();
    std::cout << std::endl;
    
    std::cout << "=== All HTTP Cache Control Tests Passed ===" << std::endl;
    return 0;
}
