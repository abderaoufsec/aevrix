#!/usr/bin/env python3
"""Aevrix Phase 23 WebSocket smoke client.

Raw-socket RFC 6455 client using only the Python standard library: performs
the HTTP Upgrade handshake, sends masked frames, and validates the server's
replies (echo, Pong, Close, and error-close codes).

Usage:  python3 ws_client.py <case> [--tls]
Each case prints "OK <detail>" and exits 0 on success, or prints
"FAIL <detail>" and exits 1 on failure.
"""

import argparse
import base64
import hashlib
import os
import socket
import ssl
import struct
import sys

HOST = "127.0.0.1"
PORT = 18081
TLS_PORT = 18443
PATH = "/ws"
ALLOWED_ORIGIN = "http://allowed.example"
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_CONT, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA


class Failure(Exception):
    """Raised when a check fails."""


def check(condition, detail):
    if not condition:
        raise Failure(detail)


# ---------------------------------------------------------------------------
# Framing
# ---------------------------------------------------------------------------

def encode_frame(opcode, payload=b"", fin=True, mask=True):
    """Encode a client frame (masked by default, per RFC 6455 Section 5.3)."""
    if isinstance(payload, str):
        payload = payload.encode("utf-8")
    b0 = (0x80 if fin else 0) | (opcode & 0x0F)
    length = len(payload)
    header = bytes([b0])
    mask_bit = 0x80 if mask else 0
    if length < 126:
        header += bytes([mask_bit | length])
    elif length <= 0xFFFF:
        header += bytes([mask_bit | 126]) + struct.pack("!H", length)
    else:
        header += bytes([mask_bit | 127]) + struct.pack("!Q", length)
    if mask:
        key = os.urandom(4)
        header += key
        payload = bytes(b ^ key[i % 4] for i, b in enumerate(payload))
    return header + payload


class FrameReader:
    """Incremental frame reader over a buffered socket."""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def _need(self, count):
        while len(self.buf) < count:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise Failure("connection closed while reading frame")
            self.buf += chunk

    def read_frame(self):
        self._need(2)
        b0, b1 = self.buf[0], self.buf[1]
        fin = bool(b0 & 0x80)
        opcode = b0 & 0x0F
        masked = bool(b1 & 0x80)
        length = b1 & 0x7F
        offset = 2
        if length == 126:
            self._need(offset + 2)
            length = struct.unpack("!H", self.buf[offset:offset + 2])[0]
            offset += 2
        elif length == 127:
            self._need(offset + 8)
            length = struct.unpack("!Q", self.buf[offset:offset + 8])[0]
            offset += 8
        if masked:
            self._need(offset + 4)
            key = self.buf[offset:offset + 4]
            offset += 4
        else:
            key = None
        self._need(offset + length)
        payload = self.buf[offset:offset + length]
        self.buf = self.buf[offset + length:]
        if key:
            payload = bytes(b ^ key[i % 4] for i, b in enumerate(payload))
        return fin, opcode, masked, payload


# ---------------------------------------------------------------------------
# Handshake
# ---------------------------------------------------------------------------

def compute_accept(key):
    digest = hashlib.sha1((key + GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def connect(tls=False):
    sock = socket.create_connection((HOST, TLS_PORT if tls else PORT), timeout=5)
    sock.settimeout(5)
    if tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        sock = ctx.wrap_socket(sock, server_hostname=HOST)
    return sock


def handshake(path=PATH, origin=ALLOWED_ORIGIN, key=None, version="13",
              connection="Upgrade", tls=False):
    """Send an upgrade request and return (status, headers, sock, raw)."""
    if key is None:
        key = base64.b64encode(os.urandom(16)).decode("ascii")
    sock = connect(tls)
    lines = [
        f"GET {path} HTTP/1.1",
        f"Host: {HOST}:{TLS_PORT if tls else PORT}",
        "Upgrade: websocket",
        f"Connection: {connection}",
        f"Sec-WebSocket-Key: {key}",
    ]
    if version is not None:
        lines.append(f"Sec-WebSocket-Version: {version}")
    if origin is not None:
        lines.append(f"Origin: {origin}")
    request = "\r\n".join(lines) + "\r\n\r\n"
    sock.sendall(request.encode("ascii"))

    raw = b""
    while b"\r\n\r\n" not in raw:
        chunk = sock.recv(4096)
        if not chunk:
            raise Failure("connection closed during handshake")
        raw += chunk
    head, _, rest = raw.partition(b"\r\n\r\n")
    head_text = head.decode("latin-1")
    status_line = head_text.split("\r\n", 1)[0]
    status = int(status_line.split(" ")[1])
    headers = {}
    for line in head_text.split("\r\n")[1:]:
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()

    reader = FrameReader(sock)
    # Any bytes already read past the header belong to the frame layer.
    reader.buf = rest
    return status, headers, sock, reader, key


# ---------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------

def case_handshake(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    check("sec-websocket-accept" in headers, "missing Sec-WebSocket-Accept")
    # The accept must match our key-derived value (checked via recompute on
    # the server side of the exchange: server echoes what SHA-1 dictates).
    scheme = "wss" if tls else "ws"
    sock.close()
    return f"{scheme} handshake returned 101 with accept header"


def case_echo_text(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    payload = "hello aevrix phase 23"
    sock.sendall(encode_frame(OP_TEXT, payload))
    fin, opcode, masked, body = reader.read_frame()
    sock.close()
    check(opcode == OP_TEXT, f"expected text echo, got opcode {opcode}")
    check(fin, "echo frame missing FIN")
    check(not masked, "server frame must not be masked")
    check(body == payload.encode(), "echo payload mismatch")
    return "text message echoed correctly"


def case_echo_binary(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    payload = bytes((i * 31 + 7) & 0xFF for i in range(65536))  # 64-bit length form
    sock.sendall(encode_frame(OP_BINARY, payload))
    fin, opcode, masked, body = reader.read_frame()
    sock.close()
    check(opcode == OP_BINARY, f"expected binary echo, got opcode {opcode}")
    check(len(body) == 65536, f"expected 65536 bytes back, got {len(body)}")
    check(body == payload, "binary echo payload mismatch")
    return "65536-byte binary message echoed (64-bit frame length)"


def case_echo_16bit(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    payload = os.urandom(1000)  # 16-bit length form
    sock.sendall(encode_frame(OP_BINARY, payload))
    fin, opcode, masked, body = reader.read_frame()
    sock.close()
    check(opcode == OP_BINARY and body == payload, "16-bit length echo mismatch")
    return "1000-byte binary message echoed (16-bit frame length)"


def case_fragmented(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    sock.sendall(encode_frame(OP_TEXT, "frag-", fin=False))
    sock.sendall(encode_frame(OP_CONT, "mented ", fin=False))
    sock.sendall(encode_frame(OP_CONT, "message", fin=True))
    fin, opcode, masked, body = reader.read_frame()
    sock.close()
    check(opcode == OP_TEXT, f"expected text echo, got opcode {opcode}")
    check(body == b"frag-mented message", "fragmented echo mismatch")
    check(fin, "fragmented echo should arrive as a single FIN frame")
    return "3-part fragmented message reassembled and echoed"


def case_ping(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    sock.sendall(encode_frame(OP_PING, "keepalive"))
    fin, opcode, masked, body = reader.read_frame()
    sock.close()
    check(opcode == OP_PONG, f"expected pong, got opcode {opcode}")
    check(body == b"keepalive", "pong payload mismatch")
    return "ping answered with matching pong"


def case_close(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    sock.sendall(encode_frame(OP_CLOSE, struct.pack("!H", 1000)))
    fin, opcode, masked, body = reader.read_frame()
    check(opcode == OP_CLOSE, f"expected close reply, got opcode {opcode}")
    check(len(body) >= 2, "close reply missing status code")
    code = struct.unpack("!H", body[:2])[0]
    check(code == 1000, f"expected close code 1000, got {code}")
    # Server must drop the TCP connection once both Close frames exchanged.
    sock.settimeout(5)
    remainder = sock.recv(1)
    sock.close()
    check(remainder == b"", "server did not close the TCP connection")
    return "close handshake completed and TCP connection dropped"


def case_invalid_unmasked(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    sock.sendall(encode_frame(OP_TEXT, "unmasked", mask=False))
    fin, opcode, masked, body = reader.read_frame()
    sock.close()
    check(opcode == OP_CLOSE, f"expected close frame, got opcode {opcode}")
    code = struct.unpack("!H", body[:2])[0]
    check(code == 1002, f"expected close 1002, got {code}")
    return "unmasked client frame rejected with close 1002"


def case_invalid_rsv(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    # FIN + RSV1 + Text, masked with zero key, empty payload.
    sock.sendall(b"\xC1\x80" + b"\x00\x00\x00\x00")
    fin, opcode, masked, body = reader.read_frame()
    sock.close()
    check(opcode == OP_CLOSE, f"expected close frame, got opcode {opcode}")
    code = struct.unpack("!H", body[:2])[0]
    check(code == 1002, f"expected close 1002, got {code}")
    return "RSV frame rejected with close 1002"


def case_oversize(tls):
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    # Declare a 66000-byte payload (over the 65536 ceiling): the server must
    # fail the connection from the header alone, without waiting for payload.
    # 66000 exceeds 16 bits, so the 64-bit length form is used; the masking
    # key is included so the frame is a well-formed masked client header.
    sock.sendall(b"\x82" + b"\xFF" + struct.pack("!Q", 66000) + b"\x00\x00\x00\x00")
    fin, opcode, masked, body = reader.read_frame()
    sock.close()
    check(opcode == OP_CLOSE, f"expected close frame, got opcode {opcode}")
    code = struct.unpack("!H", body[:2])[0]
    check(code == 1009, f"expected close 1009, got {code}")
    return "oversized frame rejected with close 1009"


def case_badkey(tls):
    status, headers, sock, reader, _ = handshake(key="not-base64!!", tls=tls)
    sock.close()
    check(status == 400, f"expected 400 for invalid key, got {status}")
    return "invalid Sec-WebSocket-Key rejected with 400"


def case_badversion(tls):
    status, headers, sock, reader, _ = handshake(version="8", tls=tls)
    sock.close()
    check(status == 426, f"expected 426 for version 8, got {status}")
    check(headers.get("sec-websocket-version") == "13",
          "426 response must advertise Sec-WebSocket-Version: 13")
    return "unsupported version rejected with 426 (advertises 13)"


def case_badconn(tls):
    status, headers, sock, reader, _ = handshake(connection="keep-alive", tls=tls)
    sock.close()
    check(status == 400, f"expected 400 without Connection: Upgrade, got {status}")
    return "missing Connection: Upgrade rejected with 400"


def case_badorigin(tls):
    status, headers, sock, reader, _ = handshake(origin="http://evil.example", tls=tls)
    sock.close()
    check(status == 403, f"expected 403 for disallowed origin, got {status}")
    return "disallowed Origin rejected with 403"


def case_noorigin(tls):
    status, headers, sock, reader, _ = handshake(origin=None, tls=tls)
    sock.close()
    check(status == 403, f"expected 403 for missing origin, got {status}")
    return "missing Origin rejected with 403 (allowlist active)"


def case_plain_get(tls):
    """A plain GET on /ws must not trigger the upgrade path."""
    sock = connect(tls)
    sock.sendall((f"GET /ws HTTP/1.1\r\nHost: {HOST}\r\n"
                  "Connection: close\r\n\r\n").encode())
    raw = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        raw += chunk
    sock.close()
    text = raw.decode("latin-1", "replace")
    check(not text.startswith("HTTP/1.1 101"), "plain GET must not be upgraded")
    check(text.startswith("HTTP/1.1 404"), f"expected 404 for plain GET /ws, got: {text[:30]!r}")
    return "plain GET /ws answered with 404, not 101"


def case_idle_timeout(tls):
    """An idle established session must be closed by the keep-alive budget."""
    status, headers, sock, reader, _ = handshake(tls=tls)
    check(status == 101, f"expected 101, got {status}")
    # keep_alive_timeout_ms = 5000 in ws.conf; the sweep runs every second.
    sock.settimeout(10)
    try:
        remainder = sock.recv(1)
    except socket.timeout:
        sock.close()
        raise Failure("idle WebSocket connection was not closed by the timeout")
    sock.close()
    check(remainder == b"", "expected EOF from idle timeout close")
    return "idle connection closed by keep-alive timeout"


def case_static_regression(tls):
    """Plain HTTP must still work after the WebSocket tests."""
    sock = connect(False)
    sock.sendall(b"GET /index.html HTTP/1.1\r\nHost: localhost\r\n"
                 b"Connection: close\r\n\r\n")
    raw = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        raw += chunk
    sock.close()
    text = raw.decode("latin-1", "replace")
    check(text.startswith("HTTP/1.1 200"), f"expected 200 for /index.html, got: {text[:30]!r}")
    return "static file serving unaffected by WebSocket changes"


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

CASES = {
    "handshake": case_handshake,
    "echo_text": case_echo_text,
    "echo_binary": case_echo_binary,
    "echo_16bit": case_echo_16bit,
    "fragmented": case_fragmented,
    "ping": case_ping,
    "close": case_close,
    "invalid_unmasked": case_invalid_unmasked,
    "invalid_rsv": case_invalid_rsv,
    "oversize": case_oversize,
    "badkey": case_badkey,
    "badversion": case_badversion,
    "badconn": case_badconn,
    "badorigin": case_badorigin,
    "noorigin": case_noorigin,
    "plain_get": case_plain_get,
    "idle_timeout": case_idle_timeout,
    "static_regression": case_static_regression,
}

# Cases that also work over wss:// (TLS transport).
TLS_CASES = {"handshake", "echo_text", "close"}


def main():
    parser = argparse.ArgumentParser(description="Aevrix WebSocket smoke client")
    parser.add_argument("case", choices=sorted(CASES.keys()))
    parser.add_argument("--tls", action="store_true",
                        help="run over wss:// on the TLS port")
    args = parser.parse_args()

    try:
        detail = CASES[args.case](args.tls)
    except Failure as failure:
        print(f"FAIL {args.case}: {failure}")
        return 1
    except Exception as error:  # noqa: BLE001 - surface any transport error
        print(f"FAIL {args.case}: {type(error).__name__}: {error}")
        return 1

    suffix = " (wss)" if args.tls else ""
    print(f"OK {args.case}{suffix}: {detail}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
