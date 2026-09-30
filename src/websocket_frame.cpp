// =============================================================================
// Aevrix - WebSocket Frame Parser/Serializer Implementation (RFC 6455)
// =============================================================================

#include "aevrix/websocket_frame.h"

#include <cstring>

namespace aevrix {
namespace ws {

// =============================================================================
// Opcode Helpers
// =============================================================================

bool is_control_opcode(Opcode opcode) {
    const uint8_t value = static_cast<uint8_t>(opcode);
    return (value & 0x08) != 0;
}

bool is_data_opcode(Opcode opcode) {
    const uint8_t value = static_cast<uint8_t>(opcode);
    return value <= 0x02;
}

const char* opcode_name(Opcode opcode) {
    switch (opcode) {
        case Opcode::Continuation: return "continuation";
        case Opcode::Text:         return "text";
        case Opcode::Binary:       return "binary";
        case Opcode::Close:        return "close";
        case Opcode::Ping:         return "ping";
        case Opcode::Pong:         return "pong";
    }
    return "unknown";
}

// =============================================================================
// Internal Helpers
// =============================================================================

namespace {

bool is_known_opcode(uint8_t raw) {
    switch (raw) {
        case 0x0: case 0x1: case 0x2:
        case 0x8: case 0x9: case 0xA:
            return true;
        default:
            return false;   // 0x3-0x7 and 0xB-0xF are reserved
    }
}

// Big-endian readers over the buffer.
uint16_t read_be16(const char* data) {
    return static_cast<uint16_t>(
        (static_cast<uint8_t>(data[0]) << 8) | static_cast<uint8_t>(data[1]));
}

uint64_t read_be64(const char* data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<uint8_t>(data[i]);
    }
    return value;
}

void append_be16(std::string& out, uint16_t value) {
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void append_be64(std::string& out, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
}

} // namespace

// =============================================================================
// FrameParser
// =============================================================================

FrameParser::FrameParser(uint64_t max_payload_bytes)
    : max_payload_bytes_(max_payload_bytes) {
}

void FrameParser::feed(const char* data, size_t length) {
    if (error_ != ParseError::None || data == nullptr || length == 0) {
        return;
    }
    buffer_.append(data, length);
}

void FrameParser::clear() {
    buffer_.clear();
    error_ = ParseError::None;
    error_message_.clear();
}

bool FrameParser::fail(ParseError error, const std::string& message) {
    error_ = error;
    error_message_ = message;
    return false;
}

ParseStatus FrameParser::next(Frame& out) {
    if (error_ != ParseError::None) {
        return ParseStatus::Error;
    }

    // Need at least the two fixed header bytes.
    if (buffer_.size() < 2) {
        return ParseStatus::NeedMore;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(buffer_.data());

    const bool fin = (bytes[0] & 0x80) != 0;
    const uint8_t rsv = static_cast<uint8_t>((bytes[0] & 0x70) >> 4);
    const uint8_t raw_opcode = static_cast<uint8_t>(bytes[0] & 0x0F);
    const bool masked = (bytes[1] & 0x80) != 0;
    const uint8_t len7 = static_cast<uint8_t>(bytes[1] & 0x7F);

    // No WebSocket extensions are negotiated, so RSV must always be zero.
    if (rsv != 0) {
        fail(ParseError::ProtocolViolation, "RSV bits set without a negotiated extension");
        return ParseStatus::Error;
    }

    if (!is_known_opcode(raw_opcode)) {
        fail(ParseError::ProtocolViolation,
             "reserved opcode " + std::to_string(static_cast<int>(raw_opcode)));
        return ParseStatus::Error;
    }

    // Decode the payload length (7-bit, 16-bit, or 64-bit form).
    uint64_t payload_length = len7;
    size_t offset = 2;

    if (len7 == 126) {
        if (buffer_.size() < offset + 2) {
            return ParseStatus::NeedMore;
        }
        payload_length = read_be16(buffer_.data() + offset);
        offset += 2;
    } else if (len7 == 127) {
        if (buffer_.size() < offset + 8) {
            return ParseStatus::NeedMore;
        }
        const uint64_t value = read_be64(buffer_.data() + offset);
        offset += 8;
        // RFC 6455: the most significant bit of a 64-bit length must be 0.
        if ((value & 0x8000000000000000ULL) != 0) {
            fail(ParseError::ProtocolViolation, "64-bit payload length has MSB set");
            return ParseStatus::Error;
        }
        payload_length = value;
    }

    const Opcode opcode = static_cast<Opcode>(raw_opcode);

    // Control frames: FIN must be set and payload limited to 125 bytes.
    if (is_control_opcode(opcode)) {
        if (!fin) {
            fail(ParseError::ProtocolViolation, "control frames must not be fragmented");
            return ParseStatus::Error;
        }
        if (payload_length > 125) {
            fail(ParseError::ProtocolViolation, "control frame payload exceeds 125 bytes");
            return ParseStatus::Error;
        }
    }

    // Reject oversized declarations before buffering a single payload byte.
    if (payload_length > max_payload_bytes_) {
        fail(ParseError::PayloadTooLarge,
             "declared payload length " + std::to_string(payload_length) +
             " exceeds limit " + std::to_string(max_payload_bytes_));
        return ParseStatus::Error;
    }

    // Masking key (present on client-to-server frames).
    std::array<uint8_t, 4> mask_key{};
    if (masked) {
        if (buffer_.size() < offset + 4) {
            return ParseStatus::NeedMore;
        }
        std::memcpy(mask_key.data(), buffer_.data() + offset, 4);
        offset += 4;
    }

    // Full payload must be present before the frame is emitted.
    if (buffer_.size() < offset + payload_length) {
        return ParseStatus::NeedMore;
    }

    out.fin = fin;
    out.rsv = rsv;
    out.opcode = opcode;
    out.masked = masked;
    out.mask_key = mask_key;
    out.payload.assign(buffer_.data() + offset, payload_length);

    if (masked) {
        for (size_t i = 0; i < out.payload.size(); ++i) {
            out.payload[i] = static_cast<char>(
                static_cast<uint8_t>(out.payload[i]) ^ mask_key[i % 4]);
        }
    }

    buffer_.erase(0, offset + payload_length);
    return ParseStatus::FrameReady;
}

// =============================================================================
// Encoding
// =============================================================================

std::string encode_frame(Opcode opcode,
                         std::string_view payload,
                         bool fin,
                         bool mask,
                         const std::array<uint8_t, 4>& mask_key) {
    std::string out;
    const size_t length = payload.size();

    // Base header: FIN + opcode.
    uint8_t byte0 = static_cast<uint8_t>(opcode) & 0x0F;
    if (fin) {
        byte0 |= 0x80;
    }
    out.push_back(static_cast<char>(byte0));

    // Length indicator: 7-bit, 16-bit, or 64-bit form.
    uint8_t byte1 = mask ? 0x80 : 0x00;
    if (length < 126) {
        byte1 |= static_cast<uint8_t>(length);
        out.push_back(static_cast<char>(byte1));
    } else if (length <= 0xFFFF) {
        byte1 |= 126;
        out.push_back(static_cast<char>(byte1));
        append_be16(out, static_cast<uint16_t>(length));
    } else {
        byte1 |= 127;
        out.push_back(static_cast<char>(byte1));
        append_be64(out, static_cast<uint64_t>(length));
    }

    // Masking key + masked payload (client role only).
    if (mask) {
        out.append(reinterpret_cast<const char*>(mask_key.data()), 4);
        for (size_t i = 0; i < length; ++i) {
            out.push_back(static_cast<char>(
                static_cast<uint8_t>(payload[i]) ^ mask_key[i % 4]));
        }
    } else {
        out.append(payload.data(), length);
    }

    return out;
}

} // namespace ws
} // namespace aevrix
