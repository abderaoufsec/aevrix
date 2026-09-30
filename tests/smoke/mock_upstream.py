#!/usr/bin/env python3
"""Mock HTTP upstream for Aevrix Phase 22 live proxy verification.

Speaks just enough HTTP/1.1 (keep-alive, Content-Length, chunked,
close-delimited, stalled, oversized, interim and malformed responses) to
exercise the reverse proxy's framing, timeout and limit paths. Requests are
logged to stderr so the test can assert on prefix stripping and forwarding.
"""
import socket
import sys
import threading
import time

CRLF = b"\r\n"


def read_head(stream):
    """Read one request head; return its lines, or None at EOF."""
    lines = []
    while True:
        line = stream.readline()
        if not line:
            return None
        line = line.rstrip(b"\r\n")
        if not line:
            return lines
        lines.append(line.decode("latin1"))


def head(status, extra=(), body_len=None, chunked=False, close=False):
    """Serialise a response head."""
    out = [f"HTTP/1.1 {status}".encode()]
    for name, value in extra:
        out.append(f"{name}: {value}".encode())
    if chunked:
        out.append(b"Transfer-Encoding: chunked")
    elif body_len is not None:
        out.append(f"Content-Length: {body_len}".encode())
    if close:
        out.append(b"Connection: close")
    return CRLF.join(out) + CRLF + CRLF


def respond(conn, method, target, headers):
    """Write one response; return False when the connection must close."""
    keep_alive = "close" not in headers.get("connection", "").lower()
    skip_body = method == "HEAD"
    close = not keep_alive

    def body(text, status="200 OK", content_type="text/plain"):
        payload = text.encode()
        conn.sendall(head(status, [("Content-Type", content_type)], len(payload),
                          close=close))
        if not skip_body:
            conn.sendall(payload)

    if target.startswith("/echo"):
        body(f"target={target}\nxff={headers.get('x-forwarded-for', '-')}\n"
             f"xfp={headers.get('x-forwarded-proto', '-')}\n")

    elif target.startswith("/chunked"):
        conn.sendall(head("200 OK", [("Content-Type", "text/plain")], chunked=True,
                          close=close))
        if not skip_body:
            for chunk in (b"chunk-one-", b"chunk-two-", b"chunk-three"):
                conn.sendall(f"{len(chunk):x}".encode() + CRLF + chunk + CRLF)
            conn.sendall(b"0" + CRLF + CRLF)

    elif target.startswith("/closedelimited"):
        conn.sendall(head("200 OK", [("Content-Type", "text/plain")], close=True))
        if not skip_body:
            conn.sendall(b"body-until-close")
        return False

    elif target.startswith("/stall"):
        conn.sendall(head("200 OK", [("Content-Type", "text/plain")], 10))
        if not skip_body:
            conn.sendall(b"abc")
            # The body never completes: a HEAD request has nothing left to
            # send, so it must not pin the connection either.
            time.sleep(30)
            return False
        return keep_alive

    elif target.startswith("/huge"):
        conn.sendall(head("200 OK", [("Content-Type", "text/plain")], 4_000_000))
        if not skip_body:
            conn.sendall(b"x" * 4096)
            time.sleep(1)
            return False
        return keep_alive

    elif target.startswith("/interim"):
        conn.sendall(b"HTTP/1.1 103 Early Hints" + CRLF +
                     b"Link: </style.css>; rel=preload" + CRLF + CRLF)
        body("final response\n")

    elif target.startswith("/upgrade"):
        conn.sendall(b"HTTP/1.1 101 Switching Protocols" + CRLF +
                     b"Upgrade: websocket" + CRLF + b"Connection: Upgrade" + CRLF + CRLF)
        return False

    elif target.startswith("/smuggle"):
        conn.sendall(b"HTTP/1.1 200 OK" + CRLF + b"Content-Length: 5" + CRLF +
                     b"Transfer-Encoding: chunked" + CRLF + CRLF + b"0" + CRLF + CRLF)
        return False

    elif target.startswith("/truncate"):
        conn.sendall(head("200 OK", [("Content-Type", "text/plain")], 24))
        if not skip_body:
            conn.sendall(b"declared-but-not-sent")
        return False

    elif target.startswith("/status/"):
        code = target.rsplit("/", 1)[-1]
        if not code.isdigit():
            code = "404"
        body(f"status {code}\n", status=f"{code} Custom")

    else:
        body("hello upstream\n")

    return keep_alive


def serve(client):
    try:
        stream = client.makefile("rb")
        while True:
            request_head = read_head(stream)
            if request_head is None:
                break
            parts = request_head[0].split(" ")
            if len(parts) < 2:
                break
            method, target = parts[0], parts[1]
            headers = {}
            for line in request_head[1:]:
                name, _, value = line.partition(":")
                headers[name.strip().lower()] = value.strip()

            print(f"UPSTREAM {method} {target} xff={headers.get('x-forwarded-for', '-')} "
                  f"conn={headers.get('connection', '-')}", flush=True)

            if not respond(client, method, target, headers):
                break
    except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError):
        pass
    finally:
        try:
            client.close()
        except OSError:
            pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 19000
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(64)
    print(f"MOCK UPSTREAM READY 127.0.0.1:{port}", flush=True)

    while True:
        client, _ = listener.accept()
        client.settimeout(35)
        threading.Thread(target=serve, args=(client,), daemon=True).start()


if __name__ == "__main__":
    main()
