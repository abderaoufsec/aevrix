// =============================================================================
// Aevrix - WebSocket Frame & Connection Tests (Phase 23)
// =============================================================================
// Covers RFC 6455 framing (parse, serialize, masking, length forms,
// fragmentation, invalid frames) and the WebSocketConnection state machine
// (echo policy, Ping/Pong, close handshake, size/UTF-8 enforcement).
// =============================================================================

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aevrix/websocket_connection.h"
#include "aevrix/websocket_frame.h"

using namespace aevrix::ws;

namespace {

// =============================================================================
// Helpers
// =============================================================================

const std::array<uint8_t, 4> kClientKey = {0x11, 0x22, 0x33, 0x44};

/// Encode a masked client frame (what a real client puts on the wire).
std::string client_frame(Opcode opcode, const std::string& payload, bool fin = true) {
    return encode_frame(opcode, payload, fin, /*mask=*/true, kClientKey);
}

/// Parse every complete frame in `bytes`.
std::vector<Frame> parse_all(const std::string& bytes, uint64_t max_payload = 1 << 20) {
    FrameParser parser(max_payload);
    parser.feed(bytes.data(), bytes.size());

    std::vector<Frame> frames;
    while (true) {
        Frame frame;
        const ParseStatus status = parser.next(frame);
        if (status == ParseStatus::FrameReady) {
            frames.push_back(frame);
            continue;
        }
        assert(status == ParseStatus::NeedMore &&
               "unexpected parse error while decoding output");
        break;
    }
    return frames;
}

/// 2-byte big-endian close code encoding (plus optional reason).
std::string close_payload(uint16_t code, const std::string& reason = "") {
    std::string payload;
    payload.push_back(static_cast<char>((code >> 8) & 0xFF));
    payload.push_back(static_cast<char>(code & 0xFF));
    payload += reason;
    return payload;
}

uint16_t close_code_of(const std::string& frame_bytes) {
    const std::vector<Frame> frames = parse_all(frame_bytes);
    assert(frames.size() == 1);
    assert(frames[0].opcode == Opcode::Close);
    assert(frames[0].payload.size() >= 2);
    return static_cast<uint16_t>(
        (static_cast<uint8_t>(frames[0].payload[0]) << 8) |
        static_cast<uint8_t>(frames[0].payload[1]));
}

// =============================================================================
// Frame Parser Tests
// =============================================================================

void test_opcode_helpers() {
    std::cout << "Testing opcode helpers..." << std::endl;
    assert(is_control_opcode(Opcode::Close));
    assert(is_control_opcode(Opcode::Ping));
    assert(is_control_opcode(Opcode::Pong));
    assert(!is_control_opcode(Opcode::Text));
    assert(is_data_opcode(Opcode::Text));
    assert(is_data_opcode(Opcode::Binary));
    assert(is_data_opcode(Opcode::Continuation));
    assert(!is_data_opcode(Opcode::Ping));
    assert(std::string(opcode_name(Opcode::Text)) == "text");
    assert(std::string(opcode_name(Opcode::Close)) == "close");
}

void test_roundtrip_small_frame() {
    std::cout << "Testing small frame roundtrip..." << std::endl;
    const std::string wire = encode_frame(Opcode::Text, "hello", true);
    assert(wire.size() == 7);  // 2 header + 5 payload

    const std::vector<Frame> frames = parse_all(wire);
    assert(frames.size() == 1);
    assert(frames[0].fin);
    assert(frames[0].opcode == Opcode::Text);
    assert(!frames[0].masked);          // Server frames are unmasked
    assert(frames[0].payload == "hello");
}

void test_roundtrip_16bit_length() {
    std::cout << "Testing 16-bit length frame..." << std::endl;
    const std::string payload(300, 'x');
    const std::string wire = encode_frame(Opcode::Binary, payload, true);
    assert(wire.size() == 2 + 2 + 300);  // header + ext length + payload
    assert(static_cast<uint8_t>(wire[1] & 0x7F) == 126);

    const std::vector<Frame> frames = parse_all(wire);
    assert(frames.size() == 1);
    assert(frames[0].opcode == Opcode::Binary);
    assert(frames[0].payload == payload);
}

void test_roundtrip_64bit_length() {
    std::cout << "Testing 64-bit length frame..." << std::endl;
    const std::string payload(70000, 'y');
    const std::string wire = encode_frame(Opcode::Binary, payload, true);
    assert(wire.size() == 2 + 8 + 70000);
    assert(static_cast<uint8_t>(wire[1] & 0x7F) == 127);

    const std::vector<Frame> frames = parse_all(wire);
    assert(frames.size() == 1);
    assert(frames[0].payload.size() == 70000);
    assert(frames[0].payload == payload);
}

void test_masked_roundtrip() {
    std::cout << "Testing client masking..." << std::endl;
    const std::string payload = "masked payload";
    const std::string wire = encode_frame(Opcode::Text, payload, true, true, kClientKey);
    assert(static_cast<uint8_t>(wire[1]) & 0x80);

    const std::vector<Frame> frames = parse_all(wire);
    assert(frames.size() == 1);
    assert(frames[0].masked);
    assert(frames[0].mask_key == kClientKey);
    assert(frames[0].payload == payload);  // Unmasked during parse
}

void test_incremental_byte_by_byte_feed() {
    std::cout << "Testing byte-by-byte incremental feed..." << std::endl;
    const std::string wire = client_frame(Opcode::Text, "incremental");

    FrameParser parser(1024);
    Frame frame;
    ParseStatus last = ParseStatus::NeedMore;
    size_t fed = 0;
    for (char c : wire) {
        parser.feed(&c, 1);
        last = parser.next(frame);
        ++fed;
        if (fed < wire.size()) {
            assert(last != ParseStatus::FrameReady);  // Never completes early
        }
    }
    assert(last == ParseStatus::FrameReady);
    assert(frame.payload == "incremental");
}

void test_truncated_frame_needs_more() {
    std::cout << "Testing truncated frames report NeedMore..." << std::endl;
    const std::string wire = client_frame(Opcode::Text, "truncated");

    FrameParser parser(1024);
    parser.feed(wire.data(), wire.size() - 3);  // Chop the tail

    Frame frame;
    assert(parser.next(frame) == ParseStatus::NeedMore);
    assert(!parser.has_error());
}

void test_reserved_opcode_rejected() {
    std::cout << "Testing reserved opcode rejection..." << std::endl;
    // FIN + reserved opcode 3, masked empty payload (key 0000).
    const std::string wire = std::string("\x83\x80", 2) + std::string(4, '\0');
    FrameParser parser(1024);
    parser.feed(wire.data(), wire.size());
    Frame frame;
    assert(parser.next(frame) == ParseStatus::Error);
    assert(parser.error() == ParseError::ProtocolViolation);
}

void test_rsv_bits_rejected() {
    std::cout << "Testing RSV bit rejection..." << std::endl;
    // FIN + RSV1 + Text, masked empty payload (key 0000).
    const std::string wire = std::string("\xC1\x80", 2) + std::string(4, '\0');
    FrameParser parser(1024);
    parser.feed(wire.data(), wire.size());
    Frame frame;
    assert(parser.next(frame) == ParseStatus::Error);
    assert(parser.error() == ParseError::ProtocolViolation);
}

void test_control_frame_limits() {
    std::cout << "Testing control frame size/fragment limits..." << std::endl;

    // Control frame with a 126-byte payload.
    const std::string oversized_ping = encode_frame(Opcode::Ping, std::string(126, 'p'), true);
    {
        FrameParser parser(1024);
        parser.feed(oversized_ping.data(), oversized_ping.size());
        Frame frame;
        assert(parser.next(frame) == ParseStatus::Error);
        assert(parser.error() == ParseError::ProtocolViolation);
    }

    // Fragmented control frame (FIN clear).
    const std::string fragmented_ping = encode_frame(Opcode::Ping, "x", false);
    {
        FrameParser parser(1024);
        parser.feed(fragmented_ping.data(), fragmented_ping.size());
        Frame frame;
        assert(parser.next(frame) == ParseStatus::Error);
        assert(parser.error() == ParseError::ProtocolViolation);
    }
}

void test_oversized_declaration_rejected() {
    std::cout << "Testing oversized payload rejection before buffering..." << std::endl;
    // Declare a 1 MB payload against a 64 KB ceiling: only headers are read.
    std::string wire;
    wire.push_back(static_cast<char>(0x82));  // FIN + Binary
    wire.push_back(static_cast<char>(127));   // 64-bit length
    wire += std::string(5, '\0');
    wire.push_back(static_cast<char>(0x10));  // 0x0000000000100000 = 1048576
    wire += std::string(2, '\0');

    FrameParser parser(65536);
    parser.feed(wire.data(), wire.size());
    Frame frame;
    assert(parser.next(frame) == ParseStatus::Error);
    assert(parser.error() == ParseError::PayloadTooLarge);
    assert(parser.buffered_bytes() == wire.size());  // No payload was accepted
}

void test_64bit_msb_rejected() {
    std::cout << "Testing 64-bit length MSB rejection..." << std::endl;
    std::string wire;
    wire.push_back(static_cast<char>(0x82));
    wire.push_back(static_cast<char>(127));
    wire.push_back(static_cast<char>(0x80));  // MSB set
    wire += std::string(7, '\0');

    FrameParser parser(1 << 20);
    parser.feed(wire.data(), wire.size());
    Frame frame;
    assert(parser.next(frame) == ParseStatus::Error);
    assert(parser.error() == ParseError::ProtocolViolation);
}

// =============================================================================
// WebSocketConnection Tests
// =============================================================================

void test_text_echo() {
    std::cout << "Testing text message echo..." << std::endl;
    WebSocketConnection ws(1 << 20);
    ws.feed(nullptr, 0);  // no-op safety

    const std::string request = client_frame(Opcode::Text, "echo me");
    ws.feed(request.data(), request.size());

    assert(ws.has_pending_output());
    const std::vector<Frame> frames = parse_all(ws.take_output());
    assert(frames.size() == 1);
    assert(frames[0].opcode == Opcode::Text);
    assert(frames[0].fin);
    assert(!frames[0].masked);                 // Server frames are unmasked
    assert(frames[0].payload == "echo me");
    assert(!ws.had_protocol_error());
    assert(ws.state() == WebSocketState::Established);
}

void test_binary_echo() {
    std::cout << "Testing binary message echo..." << std::endl;
    WebSocketConnection ws(1 << 20);

    std::string payload;
    for (int i = 0; i < 1000; ++i) {
        payload.push_back(static_cast<char>(i & 0xFF));
    }
    const std::string request = client_frame(Opcode::Binary, payload);
    ws.feed(request.data(), request.size());

    const std::vector<Frame> frames = parse_all(ws.take_output());
    assert(frames.size() == 1);
    assert(frames[0].opcode == Opcode::Binary);
    assert(frames[0].payload == payload);
}

void test_ping_reply_is_pong() {
    std::cout << "Testing Ping is answered with Pong..." << std::endl;
    WebSocketConnection ws(1 << 20);

    const std::string request = client_frame(Opcode::Ping, "ping-payload");
    ws.feed(request.data(), request.size());

    const std::vector<Frame> frames = parse_all(ws.take_output());
    assert(frames.size() == 1);
    assert(frames[0].opcode == Opcode::Pong);
    assert(frames[0].payload == "ping-payload");  // Same payload per RFC 6455
    assert(!frames[0].masked);
}

void test_close_handshake_peer_initiated() {
    std::cout << "Testing close handshake (peer initiated)..." << std::endl;
    WebSocketConnection ws(1 << 20);
    assert(!ws.close_sent());
    assert(!ws.close_received());
    assert(!ws.ready_to_close());

    const std::string request = client_frame(Opcode::Close, close_payload(1000));
    ws.feed(request.data(), request.size());

    assert(ws.close_received());
    assert(ws.close_sent());
    assert(ws.state() == WebSocketState::Closed);
    assert(!ws.ready_to_close());  // Close reply still queued for flush
    assert(close_code_of(ws.take_output()) == 1000);
    assert(ws.ready_to_close());
    assert(!ws.had_protocol_error());
}

void test_close_handshake_server_initiated() {
    std::cout << "Testing close handshake (server initiated)..." << std::endl;
    WebSocketConnection ws(1 << 20);

    ws.initiate_close(1001);
    assert(ws.close_sent());
    assert(!ws.close_received());
    assert(ws.state() == WebSocketState::Closing);
    assert(!ws.ready_to_close());          // Waiting for the peer's Close
    assert(close_code_of(ws.take_output()) == 1001);

    // Peer replies with Close(1000).
    const std::string request = client_frame(Opcode::Close, close_payload(1000));
    ws.feed(request.data(), request.size());
    assert(ws.close_received());
    assert(ws.ready_to_close());
    assert(ws.state() == WebSocketState::Closed);

    // Duplicate initiate is a no-op; no second Close is queued.
    ws.initiate_close(1002);
    assert(!ws.has_pending_output());
}

void test_unmasked_client_frame_rejected() {
    std::cout << "Testing unmasked client frame rejection..." << std::endl;
    WebSocketConnection ws(1 << 20);

    // A client frame without the mask bit violates RFC 6455 Section 5.1.
    const std::string request = encode_frame(Opcode::Text, "sneaky", true, /*mask=*/false);
    ws.feed(request.data(), request.size());

    assert(ws.had_protocol_error());
    assert(ws.state() == WebSocketState::Closing);
    assert(close_code_of(ws.take_output()) == 1002);
}

void test_rsv_frame_connection_failure() {
    std::cout << "Testing RSV frame fails the connection..." << std::endl;
    WebSocketConnection ws(1 << 20);

    // FIN + RSV1 + Text, masked empty payload.
    const std::string request = std::string("\xC1\x80", 2) + std::string(4, '\0');
    ws.feed(request.data(), request.size());

    assert(ws.had_protocol_error());
    assert(close_code_of(ws.take_output()) == 1002);
}

void test_oversized_message_fails_connection() {
    std::cout << "Testing oversized message fails the connection..." << std::endl;
    WebSocketConnection ws(1024);  // 1 KB ceiling

    // Declare a 4096-byte payload against the 1 KB ceiling: the length alone
    // must fail before any payload (or even the mask key) is buffered.
    std::string request;
    request.push_back(static_cast<char>(0x82));  // FIN + Binary
    request.push_back(static_cast<char>(0xFE));  // masked, 16-bit length marker
    request.push_back('\x10');
    request.push_back('\x00');                   // length = 4096

    ws.feed(request.data(), request.size());
    assert(ws.had_protocol_error());
    assert(close_code_of(ws.take_output()) == 1009);
}

void test_fragmented_message_reassembled() {
    std::cout << "Testing fragmented message reassembly and echo..." << std::endl;
    WebSocketConnection ws(1 << 20);

    const std::string part1 = client_frame(Opcode::Text, "frag-", false);
    const std::string part2 = client_frame(Opcode::Continuation, "mented", false);
    const std::string part3 = client_frame(Opcode::Continuation, "-message", true);

    ws.feed(part1.data(), part1.size());
    assert(!ws.has_pending_output());  // Incomplete message, nothing echoed yet
    ws.feed(part2.data(), part2.size());
    assert(!ws.has_pending_output());
    ws.feed(part3.data(), part3.size());

    const std::vector<Frame> frames = parse_all(ws.take_output());
    assert(frames.size() == 1);
    assert(frames[0].opcode == Opcode::Text);
    assert(frames[0].fin);
    assert(frames[0].payload == "frag-mented-message");
    assert(!ws.had_protocol_error());
}

void test_control_frame_interleaved_between_fragments() {
    std::cout << "Testing Ping interleaved between fragments..." << std::endl;
    WebSocketConnection ws(1 << 20);

    const std::string start = client_frame(Opcode::Binary, "abc", false);
    const std::string ping = client_frame(Opcode::Ping, "keep");
    const std::string end = client_frame(Opcode::Continuation, "def", true);

    ws.feed(start.data(), start.size());
    ws.feed(ping.data(), ping.size());
    ws.feed(end.data(), end.size());

    const std::vector<Frame> frames = parse_all(ws.take_output());
    assert(frames.size() == 2);
    assert(frames[0].opcode == Opcode::Pong);        // Pong first
    assert(frames[0].payload == "keep");
    assert(frames[1].opcode == Opcode::Binary);      // Then the echoed message
    assert(frames[1].payload == "abcdef");
}

void test_fragmentation_violations() {
    std::cout << "Testing fragmentation sequence violations..." << std::endl;

    // Continuation without a started message.
    {
        WebSocketConnection ws(1 << 20);
        const std::string request = client_frame(Opcode::Continuation, "orphan", true);
        ws.feed(request.data(), request.size());
        assert(ws.had_protocol_error());
        assert(close_code_of(ws.take_output()) == 1002);
    }

    // New data frame while a fragmented message is still in progress.
    {
        WebSocketConnection ws(1 << 20);
        const std::string start = client_frame(Opcode::Text, "part1", false);
        const std::string intruder = client_frame(Opcode::Text, "part2", true);
        ws.feed(start.data(), start.size());
        ws.feed(intruder.data(), intruder.size());
        assert(ws.had_protocol_error());
        assert(close_code_of(ws.take_output()) == 1002);
    }
}

void test_invalid_utf8_text_fails_connection() {
    std::cout << "Testing invalid UTF-8 text fails the connection..." << std::endl;
    WebSocketConnection ws(1 << 20);

    // 0xFF is never valid in UTF-8.
    const std::string request = client_frame(Opcode::Text, std::string("\xFF\xFE", 2));
    ws.feed(request.data(), request.size());

    assert(ws.had_protocol_error());
    assert(close_code_of(ws.take_output()) == 1007);
}

void test_invalid_close_code_rejected() {
    std::cout << "Testing invalid close status code rejection..." << std::endl;
    WebSocketConnection ws(1 << 20);

    // 1005 must never appear on the wire (RFC 6455 Section 7.4.1).
    const std::string request = client_frame(Opcode::Close, close_payload(1005));
    ws.feed(request.data(), request.size());

    assert(ws.had_protocol_error());
    assert(close_code_of(ws.take_output()) == 1002);  // Fails with protocol error
}

void test_frames_after_close_are_ignored() {
    std::cout << "Testing frames after the close handshake are ignored..." << std::endl;
    WebSocketConnection ws(1 << 20);

    const std::string close = client_frame(Opcode::Close, close_payload(1000));
    ws.feed(close.data(), close.size());
    ws.take_output();  // Drain the Close reply; session is now Closed

    assert(ws.state() == WebSocketState::Closed);
    const std::string late = client_frame(Opcode::Text, "late message");
    ws.feed(late.data(), late.size());
    assert(!ws.has_pending_output());  // Discarded, not echoed
}

void test_server_ping_interval() {
    std::cout << "Testing server ping interval..." << std::endl;
    WebSocketConnection ws(1 << 20);

    assert(!ws.maybe_queue_ping(0));       // Disabled
    assert(!ws.maybe_queue_ping(60000));   // Interval not elapsed yet

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(ws.maybe_queue_ping(1));        // Interval elapsed

    const std::vector<Frame> frames = parse_all(ws.take_output());
    assert(frames.size() == 1);
    assert(frames[0].opcode == Opcode::Ping);
    assert(!frames[0].masked);

    assert(!ws.maybe_queue_ping(60000));   // Timer reset by the send
}

} // namespace

// =============================================================================
// Test Runner
// =============================================================================

int main() {
    std::cout << "=== WebSocket Frame Tests (Phase 23) ===" << std::endl;

    // Frame parser / serializer
    test_opcode_helpers();
    test_roundtrip_small_frame();
    test_roundtrip_16bit_length();
    test_roundtrip_64bit_length();
    test_masked_roundtrip();
    test_incremental_byte_by_byte_feed();
    test_truncated_frame_needs_more();
    test_reserved_opcode_rejected();
    test_rsv_bits_rejected();
    test_control_frame_limits();
    test_oversized_declaration_rejected();
    test_64bit_msb_rejected();

    // Connection state machine
    test_text_echo();
    test_binary_echo();
    test_ping_reply_is_pong();
    test_close_handshake_peer_initiated();
    test_close_handshake_server_initiated();
    test_unmasked_client_frame_rejected();
    test_rsv_frame_connection_failure();
    test_oversized_message_fails_connection();
    test_fragmented_message_reassembled();
    test_control_frame_interleaved_between_fragments();
    test_fragmentation_violations();
    test_invalid_utf8_text_fails_connection();
    test_invalid_close_code_rejected();
    test_frames_after_close_are_ignored();
    test_server_ping_interval();

    std::cout << "All WebSocket frame tests passed!" << std::endl;
    return 0;
}
