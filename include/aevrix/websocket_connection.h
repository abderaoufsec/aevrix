// =============================================================================
// Aevrix - WebSocket Connection State Machine (RFC 6455 Section 7)
// =============================================================================
// WebSocketConnection owns everything that happens on a connection after the
// HTTP Upgrade handshake succeeds:
//
//   Established ──peer Close──> Closing ──our Close flushed──> Closed
//        │                        ▲
//        └──we initiate Close─────┘   (wait for peer Close or close timeout)
//
// Responsibilities:
// - Frame sequencing: masking requirement (client frames MUST be masked),
//   fragmentation reassembly, control-frame semantics (Ping/Pong/Close)
// - Message policy: echo of text/binary messages, automatic Pong replies,
//   UTF-8 validation of text messages (Close 1007 on failure)
// - Limits: a single frame or assembled message larger than the configured
//   maximum ends the connection with Close 1009
// - Close handshake: Close frames are exchanged exactly once per side;
//   ready_to_close() reports when the TCP connection may be dropped
// - Keepalive: optional server-initiated Ping frames (ping interval config)
//
// The class performs no socket I/O itself; the event loop feeds bytes in via
// feed() and drains serialized frames via take_output(), which keeps all
// existing nonblocking/TLS write paths reusable.
// =============================================================================

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "aevrix/websocket_frame.h"

namespace aevrix {
namespace ws {

// =============================================================================
// Connection State
// =============================================================================
enum class WebSocketState {
    Established,  // Post-upgrade, frames flowing
    Closing,      // Close frame sent, awaiting peer Close (or already exchanged)
    Closed        // Close handshake complete; TCP may be dropped after flush
};

// =============================================================================
// WebSocketConnection
// =============================================================================
class WebSocketConnection {
public:
    /**
     * @brief Construct a WebSocket session
     *
     * @param max_message_bytes Ceiling for a single frame or assembled message
     */
    explicit WebSocketConnection(uint64_t max_message_bytes);

    /**
     * @brief Feed raw bytes received from the client
     *
     * Parses zero or more frames, applies echo/Pong/Close policy, and queues
     * any reply frames into the pending output.
     *
     * @param data   Byte source
     * @param length Number of bytes
     */
    void feed(const char* data, size_t length);

    /// True when serialized frames are waiting to be written to the client.
    bool has_pending_output() const { return !output_.empty(); }

    /// Move out and clear the pending output bytes.
    std::string take_output();

    /// Current session state.
    WebSocketState state() const { return state_; }

    /// A Close frame has been sent by this side.
    bool close_sent() const { return close_sent_; }

    /// A Close frame has been received from the peer.
    bool close_received() const { return close_received_; }

    /// True when both Close frames have been exchanged; the TCP connection
    /// may be closed once all pending output has been flushed.
    bool ready_to_close() const {
        return close_sent_ && close_received_ && output_.empty();
    }

    /// True when the session was ended by a protocol/policy violation.
    bool had_protocol_error() const { return protocol_error_; }

    /// Configured message size ceiling.
    uint64_t max_message_bytes() const { return max_message_bytes_; }

    /**
     * @brief Start a server-initiated close handshake
     *
     * Queues a Close frame (code 1001 "going away" is typical for shutdown).
     * No-op once a Close has already been sent.
     *
     * @param code WebSocket close status code (RFC 6455 Section 7.4.1)
     */
    void initiate_close(uint16_t code);

    /**
     * @brief Periodic keepalive maintenance
     *
     * Queues a Ping frame when at least ping_interval_ms has elapsed since
     * the previous Ping. Intervals of 0 disable server pings.
     *
     * @param ping_interval_ms Configured ping interval (0 = disabled)
     * @return true if a Ping frame was queued
     */
    bool maybe_queue_ping(uint64_t ping_interval_ms);

private:
    // Frame handling ---------------------------------------------------------
    void process_frame(const Frame& frame);
    void handle_close_payload(const std::string& payload);
    void deliver_message(Opcode opcode, std::string payload);

    // Policy helpers ---------------------------------------------------------
    void fail_connection(uint16_t code, const std::string& reason);
    void queue_frame(Opcode opcode, std::string_view payload, bool fin = true);

    static bool valid_close_code(uint16_t code);
    static bool valid_utf8(std::string_view text);

    // State ------------------------------------------------------------------
    FrameParser parser_;                             // Incremental frame parser
    uint64_t max_message_bytes_;                     // Frame/message ceiling
    std::string output_;                             // Serialized reply frames
    WebSocketState state_ = WebSocketState::Established;
    bool close_sent_ = false;
    bool close_received_ = false;
    bool protocol_error_ = false;

    // Fragmentation reassembly ----------------------------------------------
    bool fragment_in_progress_ = false;
    Opcode fragment_opcode_ = Opcode::Text;
    std::string fragment_payload_;

    // Keepalive --------------------------------------------------------------
    std::chrono::steady_clock::time_point last_ping_;
};

} // namespace ws
} // namespace aevrix
