// =============================================================================
// Aevrix - Proxy Upstream Response Parser Implementation
// =============================================================================
// Incremental, bounded state machine for upstream responses. No I/O.
// =============================================================================

#include "aevrix/proxy_response_parser.h"

#include <cctype>
#include <cstdlib>

namespace aevrix {

namespace {

constexpr const char* kCrlf = "\r\n";
constexpr size_t kCrlfLength = 2;

// Upper bound for a single protocol line (status line, header, chunk size).
constexpr size_t kMaxLineLength = 8 * 1024;

// Compact the receive buffer once this many consumed bytes accumulate.
constexpr size_t kCompactThreshold = 8 * 1024;

/**
 * @brief Case-insensitive ASCII comparison of a single character
 */
bool ascii_iequals(char a, char b) {
    const unsigned char ua = static_cast<unsigned char>(a);
    const unsigned char ub = static_cast<unsigned char>(b);
    return std::tolower(ua) == std::tolower(ub);
}

/**
 * @brief Case-insensitive search for a token inside a header value
 *
 * Used for "chunked" and "close", where the value may be a comma separated
 * list such as "gzip, chunked" or "keep-alive, close".
 *
 * @param value The header value
 * @param token The token to look for
 * @return true if the token appears as a comma separated item
 */
bool contains_token(const std::string& value, const std::string& token) {
    if (token.empty() || value.size() < token.size()) {
        return false;
    }

    const size_t span = token.size();
    for (size_t i = 0; i + span <= value.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < span; ++j) {
            if (!ascii_iequals(value[i + j], token[j])) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }

        // Check the token boundaries: only separators or string ends may touch.
        const bool left_ok = (i == 0) || value[i - 1] == ',' ||
                             value[i - 1] == ' ' || value[i - 1] == '\t';
        const size_t after = i + span;
        const bool right_ok = (after >= value.size()) || value[after] == ',' ||
                              value[after] == ' ' || value[after] == '\t';
        if (left_ok && right_ok) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Trim leading and trailing optional whitespace (RFC 9112 OWS)
 */
std::string trim_ows(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) {
        --end;
    }

    return value.substr(start, end - start);
}

}  // namespace

// =============================================================================
// Construction and reset
// =============================================================================

ProxyResponseParser::ProxyResponseParser(const Config& config) : config_(config) {}

void ProxyResponseParser::reset(const Config& config) {
    buffer_.clear();
    offset_ = 0;
    headers_.clear();
    body_.clear();
    reason_phrase_.clear();
    error_message_.clear();
    state_ = State::StatusLine;
    framing_ = Framing::None;
    config_ = config;
    status_code_ = 0;
    content_length_ = 0;
    chunk_remaining_ = 0;
    header_bytes_ = 0;
    saw_content_length_ = false;
    saw_transfer_encoding_ = false;
    close_requested_ = false;
    is_interim_ = false;
}

// =============================================================================
// Feeding
// =============================================================================

bool ProxyResponseParser::feed(const char* data, size_t length) {
    if (state_ == State::Error) {
        return false;
    }
    if (state_ == State::Complete || data == nullptr || length == 0) {
        return state_ != State::Error;
    }

    // Bound the outstanding buffer: body bytes are moved out into body_, so
    // only a partially received head or chunk line can accumulate here.
    if (available() + length > config_.max_header_bytes + config_.max_body_bytes) {
        fail("upstream response buffer limit exceeded");
        return false;
    }

    buffer_.append(data, length);
    return process();
}

bool ProxyResponseParser::finish_on_eof() {
    if (state_ == State::Complete) {
        return true;
    }
    if (state_ == State::Error) {
        return false;
    }

    // A close-delimited body ends exactly at EOF.
    if (state_ == State::Body && framing_ == Framing::UntilClose) {
        buffer_.clear();
        offset_ = 0;
        finish_message();
        return true;
    }

    fail("upstream closed the connection before the response was complete");
    return false;
}

bool ProxyResponseParser::process() {
    bool progressed = true;

    while (progressed && state_ != State::Complete && state_ != State::Error) {
        switch (state_) {
            case State::StatusLine:
                progressed = parse_status_line();
                break;
            case State::Headers:
                progressed = parse_header_line();
                break;
            case State::Body:
                progressed = parse_body();
                break;
            case State::ChunkSize:
                progressed = parse_chunk_size();
                break;
            case State::ChunkData:
                progressed = parse_chunk_data();
                break;
            case State::ChunkDataCrlf:
                progressed = parse_chunk_crlf();
                break;
            case State::Trailer:
                progressed = parse_trailer_line();
                break;
            case State::Complete:
            case State::Error:
            default:
                progressed = false;
                break;
        }
    }

    return state_ != State::Error;
}

// =============================================================================
// Status line and headers
// =============================================================================

bool ProxyResponseParser::parse_status_line() {
    const size_t crlf = find_crlf();
    if (crlf == std::string::npos) {
        // The head must fit in the configured budget.
        if (available() > config_.max_header_bytes) {
            fail("upstream response header limit exceeded");
        }
        return false;
    }

    const std::string line(buffer_.data() + offset_, crlf);
    consume(crlf + kCrlfLength);
    header_bytes_ += line.size() + kCrlfLength;

    // ---------------------------------------------------------------------
    // "HTTP/1.1 200 OK"  ->  version SP status-code [SP reason-phrase]
    // ---------------------------------------------------------------------
    const size_t first_space = line.find(' ');
    if (first_space == std::string::npos) {
        fail("malformed upstream status line");
        return false;
    }

    const std::string version = line.substr(0, first_space);
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        fail("unsupported upstream protocol version '" + version + "'");
        return false;
    }

    const size_t second_space = line.find(' ', first_space + 1);
    const std::string code_text =
        line.substr(first_space + 1, (second_space == std::string::npos)
                                        ? std::string::npos
                                        : second_space - (first_space + 1));

    if (code_text.size() != 3) {
        fail("malformed upstream status code '" + code_text + "'");
        return false;
    }

    int code = 0;
    for (char c : code_text) {
        if (c < '0' || c > '9') {
            fail("malformed upstream status code '" + code_text + "'");
            return false;
        }
        code = code * 10 + (c - '0');
    }

    if (code < 100 || code > 599) {
        fail("upstream status code out of range");
        return false;
    }

    status_code_ = code;
    reason_phrase_ = (second_space == std::string::npos)
                         ? std::string()
                         : trim_ows(line.substr(second_space + 1));

    // ---------------------------------------------------------------------
    // Interim (1xx) responses: consume the whole interim message and then look
    // for the final response. Only 101 is refused outright.
    // ---------------------------------------------------------------------
    if (code >= 100 && code < 200) {
        if (code == 101) {
            // Relaying a protocol upgrade is Phase 23 work and must never be
            // forwarded implicitly by a plain HTTP proxy.
            fail("upstream requested a protocol upgrade (101)");
            return false;
        }

        is_interim_ = true;
        headers_.clear();
        saw_content_length_ = false;
        saw_transfer_encoding_ = false;
        close_requested_ = false;
        state_ = State::Headers;  // Consume the interim header block
        return true;
    }

    is_interim_ = false;
    state_ = State::Headers;
    return true;
}

bool ProxyResponseParser::parse_header_line() {
    const size_t crlf = find_crlf();
    if (crlf == std::string::npos) {
        if (available() > config_.max_header_bytes) {
            fail("upstream response header limit exceeded");
        }
        return false;
    }

    // The empty line terminates the header section.
    if (crlf == 0) {
        consume(kCrlfLength);
        header_bytes_ += kCrlfLength;

        // An interim response ends here: its headers are discarded and the
        // parser goes back to looking for the final status line.
        if (is_interim_) {
            is_interim_ = false;
            headers_.clear();
            state_ = State::StatusLine;
            return true;
        }

        decide_framing();
        return state_ != State::Complete;
    }

    const std::string line(buffer_.data() + offset_, crlf);

    // Obs-fold continuation lines are obsolete (RFC 9112 section 5.2) and are a
    // known smuggling primitive, so they are rejected rather than unfolded.
    if (line[0] == ' ' || line[0] == '\t') {
        fail("obsolete header line folding in upstream response");
        return false;
    }

    consume(crlf + kCrlfLength);
    header_bytes_ += line.size() + kCrlfLength;

    if (header_bytes_ > config_.max_header_bytes) {
        fail("upstream response header limit exceeded");
        return false;
    }

    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) {
        fail("malformed upstream header line");
        return false;
    }

    const std::string name = line.substr(0, colon);
    const std::string value = trim_ows(line.substr(colon + 1));

    const http::HttpHeader header(name, value);
    if (!header.is_valid()) {
        fail("invalid upstream header '" + name + "'");
        return false;
    }

    const std::string& normalized = header.normalized_name();

    // ---------------------------------------------------------------------
    // Framing-relevant headers are validated here, never relayed blindly.
    // ---------------------------------------------------------------------
    if (normalized == "content-length") {
        if (saw_content_length_ && headers_.get("Content-Length") != value) {
            fail("conflicting Content-Length headers from upstream");
            return false;
        }

        // Numeric sanity: 19 digits can never overflow a 64-bit size_t.
        if (value.empty() || value.size() > 19) {
            fail("malformed upstream Content-Length '" + value + "'");
            return false;
        }

        size_t length = 0;
        for (char c : value) {
            if (c < '0' || c > '9') {
                fail("malformed upstream Content-Length '" + value + "'");
                return false;
            }
            length = length * 10U + static_cast<size_t>(c - '0');
        }

        // A bodyless response (HEAD, 204, 304) uses Content-Length only to
        // describe the selected representation, so the body budget does not
        // apply and the value must be preserved rather than rejected.
        const bool bodyless = config_.request_was_head || status_code_ == 204 ||
                              status_code_ == 304;
        if (!bodyless && length > config_.max_body_bytes) {
            fail("upstream Content-Length exceeds the configured limit");
            return false;
        }

        content_length_ = length;
        saw_content_length_ = true;
    } else if (normalized == "transfer-encoding") {
        if (!contains_token(value, "chunked")) {
            fail("unsupported upstream Transfer-Encoding '" + value + "'");
            return false;
        }
        saw_transfer_encoding_ = true;
    } else if (normalized == "connection") {
        if (contains_token(value, "close")) {
            close_requested_ = true;
        }
    }

    headers_.set(name, value);
    return true;
}

void ProxyResponseParser::decide_framing() {
    // Conflicting framing is never "resolved": it is rejected. This is the
    // primary response-smuggling defence for a proxy.
    if (saw_transfer_encoding_ && saw_content_length_) {
        fail("upstream sent both Content-Length and Transfer-Encoding");
        return;
    }

    if (config_.request_was_head || status_code_ == 204 || status_code_ == 304) {
        framing_ = Framing::None;
        finish_message();
        return;
    }

    if (saw_transfer_encoding_) {
        framing_ = Framing::Chunked;
        state_ = State::ChunkSize;
        return;
    }

    if (saw_content_length_) {
        framing_ = Framing::ContentLength;
        if (content_length_ == 0) {
            finish_message();
            return;
        }
        state_ = State::Body;
        return;
    }

    // No framing headers at all: the body runs until the upstream closes.
    framing_ = Framing::UntilClose;
    state_ = State::Body;
}

// =============================================================================
// Body
// =============================================================================

bool ProxyResponseParser::parse_body() {
    if (framing_ == Framing::ContentLength) {
        const size_t remaining = content_length_ - body_.size();
        if (remaining == 0) {
            finish_message();
            return false;
        }

        const size_t take = (remaining < available()) ? remaining : available();
        if (take == 0) {
            return false;  // Wait for more bytes
        }

        body_.append(data(), take);
        consume(take);

        if (body_.size() == content_length_) {
            finish_message();
        }
        return false;
    }

    // Framing::UntilClose: take everything and wait for EOF.
    const size_t take = available();
    if (take == 0) {
        return false;
    }

    if (body_.size() + take > config_.max_body_bytes) {
        fail("upstream response body exceeds the configured limit");
        return false;
    }

    body_.append(data(), take);
    consume(take);
    return false;
}

// =============================================================================
// Chunked transfer coding
// =============================================================================

bool ProxyResponseParser::parse_chunk_size() {
    const size_t crlf = find_crlf();
    if (crlf == std::string::npos) {
        if (available() > kMaxLineLength) {
            fail("malformed upstream chunk size line");
        }
        return false;
    }

    std::string line(buffer_.data() + offset_, crlf);
    consume(crlf + kCrlfLength);

    // Chunk extensions (";name=value") are not forwarded.
    const size_t semicolon = line.find(';');
    if (semicolon != std::string::npos) {
        line = line.substr(0, semicolon);
    }
    line = trim_ows(line);

    if (line.empty() || line.size() > 8) {
        fail("malformed upstream chunk size line");
        return false;
    }

    size_t value = 0;
    for (char c : line) {
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = (c - 'a') + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = (c - 'A') + 10;
        } else {
            fail("malformed upstream chunk size '" + line + "'");
            return false;
        }

        value = value * 16U + static_cast<size_t>(digit);
        if (value > config_.max_body_bytes) {
            fail("upstream chunk size exceeds the configured limit");
            return false;
        }
    }

    if (value == 0) {
        // Last chunk: consume trailer fields, then the message is complete.
        state_ = State::Trailer;
        return true;
    }

    chunk_remaining_ = value;
    state_ = State::ChunkData;
    return true;
}

bool ProxyResponseParser::parse_chunk_data() {
    if (chunk_remaining_ == 0) {
        state_ = State::ChunkDataCrlf;
        return true;
    }

    const size_t take = (chunk_remaining_ < available()) ? chunk_remaining_ : available();
    if (take == 0) {
        return false;  // Wait for more bytes
    }

    if (body_.size() + take > config_.max_body_bytes) {
        fail("upstream response body exceeds the configured limit");
        return false;
    }

    body_.append(data(), take);
    consume(take);
    chunk_remaining_ -= take;

    if (chunk_remaining_ == 0) {
        state_ = State::ChunkDataCrlf;
    }
    return true;
}

bool ProxyResponseParser::parse_chunk_crlf() {
    if (available() < kCrlfLength) {
        return false;
    }

    if (buffer_[offset_] != '\r' || buffer_[offset_ + 1] != '\n') {
        fail("malformed chunk terminator in upstream response");
        return false;
    }

    consume(kCrlfLength);
    state_ = State::ChunkSize;
    return true;
}

bool ProxyResponseParser::parse_trailer_line() {
    const size_t crlf = find_crlf();
    if (crlf == std::string::npos) {
        if (available() > config_.max_header_bytes) {
            fail("upstream response header limit exceeded");
        }
        return false;
    }

    // An empty line ends the trailer section and therefore the message.
    if (crlf == 0) {
        consume(kCrlfLength);
        finish_message();
        return false;
    }

    const std::string line(buffer_.data() + offset_, crlf);
    if (line[0] == ' ' || line[0] == '\t') {
        fail("obsolete header line folding in upstream trailer");
        return false;
    }

    if (line.find(':') == std::string::npos) {
        fail("malformed upstream trailer line");
        return false;
    }

    consume(crlf + kCrlfLength);
    header_bytes_ += line.size() + kCrlfLength;

    if (header_bytes_ > config_.max_header_bytes) {
        fail("upstream response header limit exceeded");
        return false;
    }

    // Trailers are decoded but intentionally not relayed: the relayed response
    // is re-framed with an explicit Content-Length, and relaying trailer fields
    // is optional for a non-transparent proxy (RFC 9110 section 6.5.1).
    return true;
}

// =============================================================================
// Completion
// =============================================================================

void ProxyResponseParser::finish_message() {
    state_ = State::Complete;
    // Discard anything left over: one request is in flight per upstream
    // connection, so trailing bytes would indicate a desynchronised peer.
    buffer_.clear();
    offset_ = 0;
}

bool ProxyResponseParser::upstream_keep_alive() const {
    if (state_ != State::Complete) {
        return false;
    }
    if (close_requested_) {
        return false;
    }
    // A close-delimited body leaves the connection in an unknown state.
    return framing_ != Framing::UntilClose;
}

// =============================================================================
// Buffer helpers
// =============================================================================

size_t ProxyResponseParser::find_crlf() const {
    const size_t position = buffer_.find(kCrlf, offset_);
    if (position == std::string::npos) {
        return std::string::npos;
    }
    return position - offset_;
}

void ProxyResponseParser::consume(size_t count) {
    offset_ += count;

    if (offset_ >= buffer_.size()) {
        buffer_.clear();
        offset_ = 0;
    } else if (offset_ >= kCompactThreshold) {
        // Compact rarely so that feeding large bodies stays linear.
        buffer_.erase(0, offset_);
        offset_ = 0;
    }
}

void ProxyResponseParser::fail(const std::string& message) {
    state_ = State::Error;
    error_message_ = message;
    body_.clear();
    buffer_.clear();
    offset_ = 0;
}

}  // namespace aevrix
