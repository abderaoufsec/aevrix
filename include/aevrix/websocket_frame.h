// =============================================================================
// Aevrix - WebSocket Frame Parser/Serializer (RFC 6455)
// =============================================================================
// This file provides the wire-level framing layer for WebSocket (RFC 6455):
//
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-------+-+-------------+-------------------------------+
//  |F|R|R|R| Opcode|M| Payload len |    Extended payload length    |
//  |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
//  |N|V|V|V|       |S|             |   (if payload len==126/127)   |
//  | |1|2|3|       |K|             |                               |
//  +-+-+-+-+-------+-+-------------+-------------------------------+
//
// Key properties:
// - Incremental: feed() accepts arbitrary chunks; next() returns complete
//   frames only when every byte is available (NeedMore otherwise).
// - Bounded: a declared payload larger than the configured maximum fails
//   immediately, before any payload bytes are buffered.
// - Strict: RSV bits and reserved opcodes are rejected (no extensions are
//   negotiated by Aevrix), as are fragmented control frames and control
//   payloads above 125 bytes.
// - Masking: parse un-masks in place; encode can mask (client role) or not
//   (server role). The caller enforces RFC 6455 Section 5.1 direction rules.
//
// This layer is deliberately independent of the HTTP parser: it is only ever
// activated after a successful HTTP Upgrade handshake (Phase 23).
// =============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace aevrix {
namespace ws {

// =============================================================================
// Opcodes (RFC 6455 Section 5.5)
// =============================================================================
enum class Opcode : uint8_t {
    Continuation = 0x0,  // Continuation of a fragmented message
    Text         = 0x1,  // Text data frame (UTF-8)
    Binary       = 0x2,  // Binary data frame
    Close        = 0x8,  // Close handshake
    Ping         = 0x9,  // Ping control frame
    Pong         = 0xA   // Pong control frame
};

/// True for Close/Ping/Pong (0x8-0xF).
bool is_control_opcode(Opcode opcode);

/// True for Continuation/Text/Binary (0x0-0x2).
bool is_data_opcode(Opcode opcode);

/// Human-readable opcode name for logs/tests ("text", "ping", ...).
const char* opcode_name(Opcode opcode);

// =============================================================================
// Frame
// =============================================================================
/// A single parsed frame. `payload` is always unmasked.
struct Frame {
    bool fin = true;                          // Final fragment?
    uint8_t rsv = 0;                          // RSV1..RSV3 bits (must be 0)
    Opcode opcode = Opcode::Text;             // Frame opcode
    bool masked = false;                      // Was a masking key present?
    std::array<uint8_t, 4> mask_key{};        // Masking key (if masked)
    std::string payload;                      // Unmasked payload bytes
};

// =============================================================================
// Parser Result Types
// =============================================================================
enum class ParseStatus {
    NeedMore,     // Wait for additional bytes
    FrameReady,   // A complete frame was written to the output Frame
    Error         // Protocol violation (see error()/error_message())
};

enum class ParseError {
    None,
    ProtocolViolation,   // Malformed or forbidden frame structure
    PayloadTooLarge      // Declared payload exceeds the configured maximum
};


// =============================================================================
// FrameParser
// =============================================================================
/**
 * @brief Incremental RFC 6455 frame parser.
 *
 * The parser accumulates raw bytes and hands back one complete frame at a
 * time. It enforces structural rules that do not depend on connection state
 * (RSV bits, opcodes, control-frame limits, payload maximum); semantic rules
 * (masking direction, fragmentation sequencing, close codes) are enforced by
 * WebSocketConnection.
 */
class FrameParser {
public:
    /**
     * @brief Construct a parser with a payload ceiling
     *
     * @param max_payload_bytes Maximum accepted declared payload length
     */
    explicit FrameParser(uint64_t max_payload_bytes);

    /**
     * @brief Append raw bytes to the parse buffer
     *
     * @param data   Byte source
     * @param length Number of bytes
     */
    void feed(const char* data, size_t length);

    /**
     * @brief Try to parse the next complete frame
     *
     * @param out Receives the parsed frame on FrameReady
     * @return ParseStatus::NeedMore, FrameReady, or Error
     */
    ParseStatus next(Frame& out);

    /// True once a fatal parse error has been recorded.
    bool has_error() const { return error_ != ParseError::None; }

    /// The recorded error kind.
    ParseError error() const { return error_; }

    /// Description of the recorded error (empty when there is none).
    const std::string& error_message() const { return error_message_; }

    /// Bytes currently buffered awaiting a complete frame.
    size_t buffered_bytes() const { return buffer_.size(); }

    /// Discard buffered bytes and reset the error state.
    void clear();

private:
    bool fail(ParseError error, const std::string& message);

    std::string buffer_;               // Raw byte accumulation buffer
    uint64_t max_payload_bytes_;       // Declared payload ceiling
    ParseError error_ = ParseError::None;
    std::string error_message_;
};

// =============================================================================
// Encoding
// =============================================================================

/**
 * @brief Serialize a single WebSocket frame
 *
 * Server-to-client frames MUST NOT be masked; client-to-server frames MUST be
 * masked with a fresh random key. Aevrix only ever sends unmasked frames;
 * the masking path exists for tests and future client-side use.
 *
 * @param opcode   Frame opcode
 * @param payload  Payload bytes
 * @param fin      FIN bit
 * @param mask     Apply a masking key (client role)
 * @param mask_key Masking key (used when mask is true)
 * @return Encoded frame bytes
 */
std::string encode_frame(Opcode opcode,
                         std::string_view payload,
                         bool fin,
                         bool mask = false,
                         const std::array<uint8_t, 4>& mask_key = {});

} // namespace ws
} // namespace aevrix
