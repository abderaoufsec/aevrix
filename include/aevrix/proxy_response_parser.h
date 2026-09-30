// =============================================================================
// Aevrix - Proxy Upstream Response Parser
// =============================================================================
// Phase 22: an incremental, bounded parser for the HTTP/1.1 response received
// from an upstream server.
//
// Why a dedicated parser?
// A reverse proxy reads bytes from an upstream and relays them to a client, so
// message framing becomes a security boundary:
//
// - Content-Length and Transfer-Encoding must never be forwarded blindly;
//   conflicting framing is the classic request/response smuggling vector, so
//   it is rejected outright rather than "resolved".
// - The response is relayed with a regenerated Content-Length, so the body is
//   fully decoded first (chunked transfer encoding is decoded here).
// - Every buffer is bounded (max_header_bytes / max_body_bytes) so a hostile
//   or broken upstream cannot exhaust server memory.
//
// The parser is a pure state machine: feed() bytes, then ask for the result.
// It performs no I/O and owns no sockets, which makes every framing edge case
// directly unit testable.
//
// Framing rules implemented (RFC 9112 section 6):
// - HEAD requests and 204/304 responses have no body
// - "Transfer-Encoding: chunked" is accepted only if it is the sole framing
// - "Content-Length" provides a fixed-length body
// - otherwise the body is delimited by connection close
//
// 1xx interim responses are consumed and the parser continues looking for the
// final response. 101 Switching Protocols is rejected: Aevrix does not relay
// protocol upgrades (Phase 23 territory).
// =============================================================================

#pragma once

#include <cstddef>
#include <string>

#include "aevrix/http_header.h"

namespace aevrix {

/**
 * @brief Incremental parser for an upstream HTTP response
 */
class ProxyResponseParser {
public:
    /**
     * @brief Parser states
     */
    enum class State {
        StatusLine,     // Waiting for the status line
        Headers,        // Reading header fields
        Body,           // Reading a fixed-length or close-delimited body
        ChunkSize,      // Reading a chunk-size line
        ChunkData,      // Reading chunk data
        ChunkDataCrlf,  // Consuming the CRLF that terminates chunk data
        Trailer,        // Consuming trailer fields after the last chunk
        Complete,       // A full response has been parsed
        Error           // The response is unusable (see error_message())
    };

    /**
     * @brief How the response body is delimited
     */
    enum class Framing {
        None,           // No body (HEAD, 204, 304)
        ContentLength,  // Fixed-length body
        Chunked,        // Transfer-Encoding: chunked
        UntilClose      // Delimited by connection close
    };

    /**
     * @brief Parser limits and context
     */
    struct Config {
        size_t max_header_bytes = 64 * 1024;        // Status line plus headers
        size_t max_body_bytes = 4 * 1024 * 1024;    // Buffered body cap
        bool request_was_head = false;              // HEAD responses carry no body
    };

    ProxyResponseParser() = default;
    explicit ProxyResponseParser(const Config& config);

    /**
     * @brief Reset the parser for a new response on a pooled connection
     *
     * @param config Limits and context for the new response
     */
    void reset(const Config& config);

    /**
     * @brief Feed upstream bytes into the parser
     *
     * @param data The bytes received from the upstream socket
     * @param length Number of bytes to consume
     * @return true if the parser is still usable, false if it entered Error
     */
    bool feed(const char* data, size_t length);

    /**
     * @brief Signal that the upstream closed the connection
     *
     * Completes a close-delimited body, and reports an error when a
     * fixed-length or chunked response was truncated.
     *
     * @return true if the parser holds a complete response
     */
    bool finish_on_eof();

    // =========================================================================
    // Results
    // =========================================================================

    bool is_complete() const { return state_ == State::Complete; }
    bool has_error() const { return state_ == State::Error; }
    const std::string& error_message() const { return error_message_; }

    int status_code() const { return status_code_; }
    const std::string& reason_phrase() const { return reason_phrase_; }
    const http::HttpHeaders& headers() const { return headers_; }
    const std::string& body() const { return body_; }
    Framing framing() const { return framing_; }

    /**
     * @brief Whether the upstream connection may be returned to the pool
     *
     * Requires a complete response with a known message boundary, plus an
     * upstream that did not ask for the connection to be closed.
     *
     * @return true if the connection is reusable
     */
    bool upstream_keep_alive() const;

    /**
     * @brief Number of response head bytes consumed (status line plus headers)
     */
    size_t header_bytes() const { return header_bytes_; }

private:
    // =========================================================================
    // Internal helpers
    // =========================================================================

    bool process();
    bool parse_status_line();
    bool parse_header_line();
    bool parse_body();
    bool parse_chunk_size();
    bool parse_chunk_data();
    bool parse_chunk_crlf();
    bool parse_trailer_line();
    void decide_framing();
    void finish_message();

    size_t available() const { return buffer_.size() - offset_; }
    const char* data() const { return buffer_.data() + offset_; }
    void consume(size_t count);
    size_t find_crlf() const;   // Returns the CRLF index, or std::string::npos
    void fail(const std::string& message);

    // =========================================================================
    // State
    // =========================================================================

    std::string buffer_;             // Unconsumed upstream bytes
    size_t offset_ = 0;              // Read offset inside buffer_

    http::HttpHeaders headers_;
    std::string body_;
    std::string reason_phrase_;
    std::string error_message_;

    State state_ = State::StatusLine;
    Framing framing_ = Framing::None;
    Config config_{};

    int status_code_ = 0;
    size_t content_length_ = 0;
    size_t chunk_remaining_ = 0;
    size_t header_bytes_ = 0;
    bool saw_content_length_ = false;
    bool saw_transfer_encoding_ = false;
    bool close_requested_ = false;   // Upstream sent "Connection: close"
    bool is_interim_ = false;        // Currently parsing a 1xx response
};

} // namespace aevrix
