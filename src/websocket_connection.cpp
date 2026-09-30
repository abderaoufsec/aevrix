// =============================================================================
// Aevrix - WebSocket Connection State Machine Implementation (RFC 6455 §7)
// =============================================================================

#include "aevrix/websocket_connection.h"

#include <utility>

namespace aevrix {
namespace ws {

namespace {

void append_close_code(std::string& payload, uint16_t code) {
    payload.push_back(static_cast<char>((code >> 8) & 0xFF));
    payload.push_back(static_cast<char>(code & 0xFF));
}

uint16_t read_close_code(const std::string& payload) {
    return static_cast<uint16_t>(
        (static_cast<uint8_t>(payload[0]) << 8) | static_cast<uint8_t>(payload[1]));
}

} // namespace

// =============================================================================
// Construction / Output
// =============================================================================

WebSocketConnection::WebSocketConnection(uint64_t max_message_bytes)
    : parser_(max_message_bytes)
    , max_message_bytes_(max_message_bytes)
    , last_ping_(std::chrono::steady_clock::now()) {
}

std::string WebSocketConnection::take_output() {
    std::string out;
    out.swap(output_);
    return out;
}

// =============================================================================
// Input Processing
// =============================================================================

void WebSocketConnection::feed(const char* data, size_t length) {
    // Once the handshake completed and output was flushed there is nothing
    // left to parse; late bytes are discarded.
    if (state_ == WebSocketState::Closed) {
        return;
    }

    parser_.feed(data, length);

    while (true) {
        if (parser_.has_error()) {
            if (protocol_error_) {
                // A violation was already reported; drop the offending bytes
                // but keep the session open long enough to receive the peer's
                // Close reply (bounded by the close timeout).
                parser_.clear();
                return;
            }
            const uint16_t code = (parser_.error() == ParseError::PayloadTooLarge)
                                      ? 1009
                                      : 1002;
            const std::string message = parser_.error_message();
            parser_.clear();
            fail_connection(code, message);
            return;
        }

        Frame frame;
        const ParseStatus status = parser_.next(frame);

        if (status == ParseStatus::NeedMore) {
            return;
        }
        if (status == ParseStatus::Error) {
            continue;  // Handled at the top of the loop.
        }

        process_frame(frame);

        if (protocol_error_) {
            // Discard anything already buffered with the offending bytes;
            // the peer's Close reply arrives in a later read.
            parser_.clear();
            return;
        }
    }
}

void WebSocketConnection::process_frame(const Frame& frame) {
    // RFC 6455 Section 5.1: client-to-server frames MUST be masked, and the
    // server MUST close the connection when they are not.
    if (!frame.masked) {
        fail_connection(1002, "client frame was not masked");
        return;
    }

    // Once a Close has been sent only the peer's Close matters; all other
    // frames are discarded (RFC 6455 Section 5.5.1). This covers both the
    // Closing state and frames parsed in the same batch that completed the
    // handshake (Closed).
    if (state_ != WebSocketState::Established) {
        if (frame.opcode == Opcode::Close && !close_received_) {
            handle_close_payload(frame.payload);
        }
        return;
    }

    switch (frame.opcode) {
        case Opcode::Close:
            handle_close_payload(frame.payload);
            break;

        case Opcode::Ping:
            queue_frame(Opcode::Pong, frame.payload);
            break;

        case Opcode::Pong:
            // Liveness only; byte activity already refreshed the deadline.
            break;

        case Opcode::Text:
        case Opcode::Binary: {
            if (fragment_in_progress_) {
                fail_connection(1002, "new data frame during a fragmented message");
                return;
            }
            if (frame.fin) {
                deliver_message(frame.opcode, frame.payload);
            } else {
                fragment_in_progress_ = true;
                fragment_opcode_ = frame.opcode;
                fragment_payload_ = frame.payload;
            }
            break;
        }

        case Opcode::Continuation: {
            if (!fragment_in_progress_) {
                fail_connection(1002, "continuation frame without a started message");
                return;
            }
            if (fragment_payload_.size() + frame.payload.size() > max_message_bytes_) {
                fail_connection(1009, "fragmented message exceeds size limit");
                return;
            }
            fragment_payload_.append(frame.payload);
            if (frame.fin) {
                fragment_in_progress_ = false;
                deliver_message(fragment_opcode_, std::move(fragment_payload_));
                fragment_payload_.clear();
            }
            break;
        }
    }
}

void WebSocketConnection::deliver_message(Opcode opcode, std::string payload) {
    if (opcode == Opcode::Text && !valid_utf8(payload)) {
        fail_connection(1007, "text message is not valid UTF-8");
        return;
    }

    // The Phase 23 endpoint echoes messages back as a single final frame.
    queue_frame(opcode, payload, /*fin=*/true);
}

// =============================================================================
// Close Handshake
// =============================================================================

void WebSocketConnection::handle_close_payload(const std::string& payload) {
    close_received_ = true;

    // A close body, when present, is exactly a 2-byte code + UTF-8 reason.
    if (payload.size() == 1) {
        fail_connection(1002, "malformed close frame payload");
        return;
    }
    if (payload.size() >= 2) {
        const uint16_t code = read_close_code(payload);
        if (!valid_close_code(code)) {
            fail_connection(1002, "invalid close status code");
            return;
        }
    }

    if (state_ == WebSocketState::Established) {
        // Peer initiated: reply with the same payload (RFC 6455 Section 7.4.1)
        // unless it carried no code, in which case an empty Close echoes back.
        queue_frame(Opcode::Close, payload);
        close_sent_ = true;
    }

    if (close_sent_ && close_received_) {
        state_ = WebSocketState::Closed;
    }
}

void WebSocketConnection::fail_connection(uint16_t code, const std::string& reason) {
    if (protocol_error_) {
        return;
    }
    protocol_error_ = true;

    if (!close_sent_) {
        std::string payload;
        append_close_code(payload, code);
        // Keep the frame within the 125-byte control-frame limit.
        payload.append(reason.substr(0, 123));
        queue_frame(Opcode::Close, payload);
        close_sent_ = true;
    }

    state_ = WebSocketState::Closing;
    fragment_in_progress_ = false;
    fragment_payload_.clear();

    if (close_sent_ && close_received_) {
        state_ = WebSocketState::Closed;
    }
}

void WebSocketConnection::initiate_close(uint16_t code) {
    if (state_ != WebSocketState::Established || close_sent_) {
        return;
    }

    std::string payload;
    append_close_code(payload, code);
    queue_frame(Opcode::Close, payload);
    close_sent_ = true;
    state_ = WebSocketState::Closing;
}

// =============================================================================
// Keepalive
// =============================================================================

bool WebSocketConnection::maybe_queue_ping(uint64_t ping_interval_ms) {
    if (ping_interval_ms == 0 || state_ != WebSocketState::Established) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_ping_ < std::chrono::milliseconds(ping_interval_ms)) {
        return false;
    }

    last_ping_ = now;
    queue_frame(Opcode::Ping, {});
    return true;
}

// =============================================================================
// Helpers
// =============================================================================

void WebSocketConnection::queue_frame(Opcode opcode, std::string_view payload, bool fin) {
    // Server frames are never masked (RFC 6455 Section 5.1).
    output_ += encode_frame(opcode, payload, fin, /*mask=*/false);
}

bool WebSocketConnection::valid_close_code(uint16_t code) {
    // 1004, 1005, and 1006 must never appear on the wire, and values below
    // 1000 / above 4999 are invalid (RFC 6455 Section 7.4.1).
    if (code < 1000 || code > 4999) {
        return false;
    }
    return code != 1004 && code != 1005 && code != 1006 && code != 1015;
}

bool WebSocketConnection::valid_utf8(std::string_view text) {
    size_t i = 0;
    const size_t n = text.size();

    while (i < n) {
        const uint8_t byte = static_cast<uint8_t>(text[i]);
        size_t extra = 0;
        uint32_t code_point = 0;
        uint32_t min = 0;

        if (byte <= 0x7F) {
            i += 1;
            continue;
        } else if ((byte & 0xE0) == 0xC0) {
            extra = 1;
            code_point = byte & 0x1F;
            min = 0x80;
        } else if ((byte & 0xF0) == 0xE0) {
            extra = 2;
            code_point = byte & 0x0F;
            min = 0x800;
        } else if ((byte & 0xF8) == 0xF0) {
            extra = 3;
            code_point = byte & 0x07;
            min = 0x10000;
        } else {
            return false;
        }

        if (i + extra >= n) {
            return false;
        }
        for (size_t j = 1; j <= extra; ++j) {
            const uint8_t cont = static_cast<uint8_t>(text[i + j]);
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (cont & 0x3F);
        }

        // Reject overlong encodings, surrogates, and out-of-range values.
        if (code_point < min || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }

        i += extra + 1;
    }

    return true;
}

} // namespace ws
} // namespace aevrix
