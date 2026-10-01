# HTTP behaviour

Aevrix speaks HTTP/1.1 and always answers with an HTTP/1.1 status line.
Responses carry `Server: Aevrix/1.0.0` and never a `Date` header.

## Methods

The parser accepts GET, HEAD, POST, PUT, DELETE, OPTIONS and PATCH
(case-insensitive). Anything else, TRACE and CONNECT included, is a parse
error and closes the connection. Dispatch branches once: a route
registered for that exact method runs, otherwise the target is served as
a file. GET and HEAD are the only methods with distinct static
behaviour, so POST, PUT, DELETE and OPTIONS on a static path return the
file with `200`; HEAD suppresses the body and drops `Content-Length`.

## Framing

Length comes from `Content-Length` only. `Transfer-Encoding` is not parsed
on requests, so a chunked request body arrives as no body at all. A
duplicated `Content-Length` is not rejected, the last value wins, and a
request carrying both `Content-Length` and `Transfer-Encoding` is not
rejected either; conflicts in upstream responses are, see
[ARCHITECTURE.md](ARCHITECTURE.md). `Content-Length` is read as a prefix
integer, so `3abc` is 3 and `0x10` is 0. Requests beyond the parser's
fixed limits close the connection without a status code: 8 KiB request
line, 8 KiB header field, 100 headers, 64 KiB of header bytes, 1 MiB body
(see [CONFIGURATION.md](CONFIGURATION.md)). `Expect: 100-continue` is
ignored. Responses are delimited by `Content-Length`; chunked is never
emitted.

## Keep-alive

HTTP/1.1 defaults to keep-alive, `Connection: close` closes the connection,
and HTTP/1.0 stays open only with `Connection: keep-alive`. Router
responses always advertise `Connection: keep-alive`, even when the client
asked to close. One request is handled per read event, so a second request
pipelined in the same segment waits in the buffer until the keep-alive
timeout; sequential requests on one connection work normally.

## Caching and ranges

`ETag`, `Last-Modified` and `Accept-Ranges` are never sent, `Range` is
ignored (always 200, never 206), and `If-None-Match` / `If-Modified-Since`
never produce `304`. `include/aevrix/http_cache.h` holds tested helpers for
those responses, but the server does not call them.

## Deviations from RFC 9110, 9111, 9112

1. Space before the header colon is accepted (9112 section 5.1).
2. `Content-Length` accepts a non-digit suffix (9110 section 8.6).
3. Duplicate `Content-Length`, and request-side `Content-Length` with
   `Transfer-Encoding`, are not rejected (9112 sections 6.1, 6.3).
4. Unknown methods close the connection instead of 501 or 405 (9110
   sections 15.5.6, 15.5.7).
5. `HEAD` omits `Content-Length` (9110 section 9.3.2).
6. No `Date` header (9110 section 6.6.1).
7. HTTP/1.0 requests receive an HTTP/1.1 status line.
8. Only the first request of a pipelined burst runs (9112 section 9.3).
9. `Range` and conditional requests are not implemented (9110 section 14, 9111).
