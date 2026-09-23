# Aevrix Web Server --- Architecture

## 1. Architectural Goal

Aevrix is a Linux-first C++20 HTTP/1.1 server designed around:

-   explicit ownership
-   non-blocking network I/O
-   a single event loop initially
-   connection state machines
-   bounded work queues
-   strict protocol parsing
-   isolated filesystem/application work
-   testable components

The architecture deliberately separates **transport**, **protocol**,
**application**, and **operational** concerns.

## 2. High-Level Architecture

``` text
                    ┌──────────────────────┐
                    │       Clients        │
                    │ browsers / curl / LB │
                    └──────────┬───────────┘
                               │ TCP
                               ▼
                    ┌──────────────────────┐
                    │     TCP Listener     │
                    │ socket/bind/listen   │
                    └──────────┬───────────┘
                               │ accept
                               ▼
                    ┌──────────────────────┐
                    │     Event Loop       │
                    │        epoll         │
                    └──────────┬───────────┘
                               │ events
                               ▼
                    ┌──────────────────────┐
                    │ Connection Manager   │
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │ Connection State     │
                    │ Machine              │
                    └──────────┬───────────┘
                               │ bytes
                               ▼
                    ┌──────────────────────┐
                    │ HTTP/1.1 Parser      │
                    └──────────┬───────────┘
                               │ Request
                               ▼
                    ┌──────────────────────┐
                    │ Router / Dispatcher  │
                    └──────────┬───────────┘
                               │
               ┌───────────────┴───────────────┐
               ▼                               ▼
      ┌─────────────────┐             ┌─────────────────┐
      │ Static File     │             │ Application      │
      │ Resolver        │             │ Handler          │
      └────────┬────────┘             └────────┬────────┘
               │                               │
               └───────────────┬───────────────┘
                               ▼
                    ┌──────────────────────┐
                    │ Response Builder     │
                    └──────────┬───────────┘
                               ▼
                    ┌──────────────────────┐
                    │ Write Buffer /       │
                    │ Output State         │
                    └──────────┬───────────┘
                               ▼
                              TCP
```

## 3. Process Model

### Initial model

One process:

``` text
aevrix
 └── event loop
      ├── listener
      ├── connection 1
      ├── connection 2
      └── connection N
```

This keeps debugging and learning straightforward.

### Later model

If experiments show a benefit:

``` text
              master
                │
       ┌────────┼────────┐
       ▼        ▼        ▼
    worker    worker    worker
       │        │        │
     epoll    epoll    epoll
```

Do not introduce multi-process complexity before measurements justify
it.

NGINX is a useful reference here: it separates a master process from
worker processes and uses event-driven processing in workers.

## 4. Threading Model

The first serious runtime should be:

``` text
Main/Event Thread
│
├── accept new connections
├── read readiness
├── parse protocol
├── schedule work
└── write readiness
```

Blocking work should not run directly in the event loop.

For example:

``` text
Event loop
    │
    ├── request requires filesystem metadata
    │
    ▼
Bounded worker queue
    │
    ▼
Worker thread
    │
    ▼
Result
    │
    ▼
Event loop
```

The worker pool must be bounded.

Do not create one thread per request.

## 5. Socket Lifecycle

The listening socket lifecycle:

``` text
socket()
   ↓
setsockopt()
   ↓
bind()
   ↓
listen()
   ↓
non-blocking
   ↓
epoll_ctl(ADD)
   ↓
epoll_wait()
   ↓
accept4()
   ↓
new connection
```

The Linux `accept(2)` contract matters: the returned descriptor is a new
connected socket and should have the required flags configured
explicitly.

Use RAII:

``` text
UniqueFd
  owns fd
  closes fd exactly once
```

No naked `close()` calls scattered through business logic.

## 6. Connection State Machine

A connection should have explicit state.

Example:

``` text
             ┌───────────────┐
             │     OPEN      │
             └───────┬───────┘
                     │
                     ▼
             ┌───────────────┐
             │ READING_HEAD  │
             └───────┬───────┘
                     │ headers complete
                     ▼
             ┌───────────────┐
             │ READING_BODY  │
             └───────┬───────┘
                     │ body complete
                     ▼
             ┌───────────────┐
             │  DISPATCHING  │
             └───────┬───────┘
                     │
                     ▼
             ┌───────────────┐
             │ WRITING       │
             └───────┬───────┘
                     │ response complete
                     ▼
             ┌─────────────────────┐
             │ keep-alive?         │
             └───────┬─────────────┘
                 yes  │  no
                      │
              ┌───────┴───────┐
              ▼               ▼
          READ_NEXT          CLOSE
```

This avoids hidden control flow.

## 7. HTTP Parser Design

The parser should operate on bytes.

RFC 9112 explicitly describes HTTP/1.1 messages as a start-line followed
by CRLF-delimited fields and an optional body. It also specifies message
framing and warns about ambiguous parsing.

Do not build the parser around arbitrary "split string" operations.

Recommended conceptual API:

``` text
Parser
 ├── feed(bytes)
 ├── state()
 ├── request_complete()
 ├── error()
 └── take_request()
```

Parser states:

``` text
RequestLine
Headers
Body
Complete
Error
```

### Request-line

Example:

``` text
GET /index.html HTTP/1.1
```

Parse:

``` text
method
target
version
```

### Headers

Store normalized field names while preserving values according to the
project's chosen representation.

Enforce:

-   maximum header count
-   maximum field-name length
-   maximum field-value length
-   maximum total header bytes
-   no invalid whitespace between field name and colon
-   no obsolete line folding acceptance unless explicitly implemented

### Body framing

Initially support:

-   no body
-   `Content-Length`

Treat `Transfer-Encoding` carefully and explicitly.

Do not accept ambiguous combinations.

RFC 9112 specifies that request body framing is controlled by
`Content-Length` or `Transfer-Encoding`, and discusses the security
implications of conflicting framing.

## 8. HTTP Request Representation

Suggested conceptual model:

``` text
Request
├── method
├── target
├── version
├── headers
├── body
└── connection_policy
```

Avoid storing redundant parsed values unless they improve performance
measurably.

## 9. HTTP Response Representation

``` text
Response
├── status
├── headers
├── body
├── content_length
└── connection_policy
```

Serializer:

``` text
HTTP/1.1 200 OK\r\n
Content-Type: text/plain\r\n
Content-Length: 18\r\n
Connection: keep-alive\r\n
\r\n
Hello from Aevrix!
```

The response serializer should own correctness for:

-   status line
-   CRLF
-   headers
-   body framing
-   connection semantics

## 10. Routing

Initial router:

``` text
GET /
GET /health
GET /static/*
```

Later:

``` text
GET /users/:id
POST /users
```

Do not build a complicated routing DSL at the beginning.

The router should receive a parsed request and produce a handler
decision.

## 11. Static File Architecture

The static-file path is security-sensitive.

``` text
HTTP target
     │
     ▼
Validate target
     │
     ▼
Normalize URL path
     │
     ▼
Reject traversal/invalid forms
     │
     ▼
Map inside configured document root
     │
     ▼
Open file safely
     │
     ▼
Determine metadata
     │
     ▼
Build response
```

Never concatenate an untrusted URL directly into a filesystem path.

OWASP documents path traversal as a class of attacks that can escape an
intended web root and access unauthorized files.

The design should also decide how symlinks are handled. A secure default
is preferable to silently following a symlink outside the configured
root.

## 12. MIME Types

Start with a small table:

``` text
.html  text/html
.css   text/css
.js    text/javascript
.json  application/json
.txt   text/plain
.svg   image/svg+xml
.png   image/png
.jpg   image/jpeg
.webp  image/webp
.ico   image/x-icon
```

Unknown extensions:

``` text
application/octet-stream
```

Do not infer MIME type from user-controlled content unless the behavior
is explicitly designed.

## 13. Keep-Alive

HTTP/1.1 commonly reuses connections.

Therefore:

``` text
connection
   │
   ├── request 1
   │      ↓
   │   response 1
   │
   ├── request 2
   │      ↓
   │   response 2
   │
   └── close
```

The parser must reset request state without destroying the underlying
connection.

Every connection needs a timeout policy.

## 14. Output Buffering

Do not assume one `send()` sends the entire response.

Conceptually:

``` text
Response
   ↓
serialized bytes
   ↓
output buffer
   ↓
send()
   ↓
partial write?
   ├── yes → retain remainder
   └── no  → complete
```

When the output buffer is non-empty, the event loop should monitor
writability.

## 15. Backpressure

Aevrix must never allow a slow client to consume unlimited memory.

Define:

-   maximum input buffer
-   maximum output buffer
-   maximum body size
-   maximum concurrent connections
-   maximum queued filesystem jobs

If a client exceeds a limit:

``` text
400 Bad Request
413 Content Too Large
408 Request Timeout
429 Too Many Requests
503 Service Unavailable
```

Use each response only when its semantics are appropriate.

## 16. Timeouts

At minimum:

``` text
header_read_timeout
body_read_timeout
keep_alive_timeout
write_timeout
```

Timers should be integrated with the event system rather than
implemented through arbitrary sleeping threads.

## 17. Observability

Structured logs should contain:

``` text
timestamp
request_id
remote_address
method
target
status
bytes_in
bytes_out
duration
connection_id
```

Avoid logging secrets or full request bodies by default.

Example:

``` text
2026-09-23T21:00:04Z
conn=42
req=8
method=GET
target=/
status=200
bytes_out=1254
duration_us=312
```

## 18. Metrics

Initial metrics:

``` text
aevrix_connections_total
aevrix_connections_active
aevrix_requests_total
aevrix_responses_2xx
aevrix_responses_4xx
aevrix_responses_5xx
aevrix_bytes_received
aevrix_bytes_sent
aevrix_request_duration
aevrix_parser_errors
aevrix_open_fds
```

A `/metrics` endpoint can be added later.

## 19. Configuration

Recommended initial configuration:

``` toml
[server]
host = "127.0.0.1"
port = 8080
workers = 2

[http]
keep_alive = true
max_header_bytes = 16384
max_body_bytes = 1048576
header_timeout_ms = 5000
keep_alive_timeout_ms = 5000

[static]
root = "./public"
directory_listing = false

[logging]
level = "info"
access_log = true
```

The exact configuration format can change. The important architectural
requirement is that configuration be represented by a typed C++
structure after parsing.

## 20. Graceful Shutdown

Shutdown sequence:

``` text
signal received
     ↓
stop accepting new connections
     ↓
stop scheduling new work
     ↓
finish safe in-flight responses
     ↓
close remaining connections
     ↓
stop workers
     ↓
flush logs
     ↓
exit
```

Never rely on `kill -9` as the normal shutdown mechanism.

## 21. Error Boundaries

Errors should be classified:

``` text
Network error
Protocol error
Application error
Filesystem error
Configuration error
Internal invariant violation
```

Do not convert every internal failure into a generic `500` without
logging the underlying reason.

Do not leak internal filesystem paths to clients.

## 22. Suggested Source Layout

``` text
aevrix/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
├── SECURITY.md
├── CONTRIBUTING.md
├── CHANGELOG.md
│
├── docs/
│   ├── MASTER_STRATEGY.md
│   ├── ARCHITECTURE.md
│   ├── IMPLEMENTATION_ROADMAP.md
│   ├── decisions/
│   ├── protocol/
│   ├── security/
│   └── benchmarks/
│
├── include/aevrix/
│   ├── net/
│   ├── http/
│   ├── runtime/
│   ├── routing/
│   ├── static/
│   ├── config/
│   └── observability/
│
├── src/
│   ├── net/
│   ├── http/
│   ├── runtime/
│   ├── routing/
│   ├── static/
│   ├── config/
│   └── observability/
│
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── protocol/
│   ├── security/
│   └── fixtures/
│
├── benchmarks/
│
├── examples/
│
└── public/
    └── index.html
```

## 23. Dependency Policy

Prefer the C++ standard library and POSIX/Linux APIs for the core.

External libraries should be added only when they solve a clearly
defined problem.

Good candidates later:

-   TLS library
-   benchmark framework
-   test framework if the project outgrows simple CTest executables
-   compression library

Do not import a complete web framework. The point of Aevrix is to
implement the server.

## 24. Testing Architecture

### Unit tests

Test:

``` text
HTTP parser
header map
status codes
serializer
router
path normalization
MIME detection
configuration parsing
```

### Integration tests

Start the real server and test:

``` text
GET /
GET missing file
HEAD /
keep-alive
multiple requests
malformed request
large headers
timeouts
concurrent connections
```

### Security tests

Test:

``` text
../
..%2f
encoded traversal
absolute paths
double encoding
NUL-like input handling
oversized headers
oversized bodies
conflicting framing
invalid header syntax
slow clients
```

## 25. Performance Architecture

Do not optimize everything immediately.

Measure:

``` text
baseline blocking server
        ↓
threaded server
        ↓
non-blocking event loop
        ↓
buffer optimizations
        ↓
filesystem optimizations
        ↓
advanced concurrency
```

This produces a useful engineering narrative and makes performance
changes attributable.

## 26. Architectural Invariants

These should become permanent rules:

1.  Event loop never performs unbounded blocking work.
2.  No connection owns another connection.
3.  Every file descriptor has one clear owner.
4.  Parser never reads beyond configured limits.
5.  Response framing is determined centrally.
6.  Static file access is confined to the configured root.
7.  Shutdown is explicit and ordered.
8.  Worker queues are bounded.
9.  Metrics/logging cannot crash the server.
10. Every protocol bug gets a regression test.
