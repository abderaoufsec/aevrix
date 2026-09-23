# Aevrix Web Server --- Implementation Roadmap

## How to Use This Roadmap

Build Aevrix in phases.

Do **not** jump ahead because a later feature looks exciting.

Each phase has:

-   objective
-   implementation scope
-   tests
-   acceptance criteria
-   Git milestone

A phase is complete only when its acceptance criteria are satisfied.

------------------------------------------------------------------------

# Phase 0 --- Project Foundation

## Objective

Create a professional C++ repository before writing networking code.

## Tasks

-   initialize Git repository
-   create CMake project
-   require C++20
-   add Debug and Release presets
-   enable warnings
-   create `src/`, `include/`, `tests/`, `docs/`
-   configure CTest
-   add `.gitignore`
-   add MIT license
-   add README skeleton
-   add CI workflow
-   add sanitizer configuration
-   add formatting/linting policy

## Acceptance

``` text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

all succeed.

## Commit

``` text
chore: establish aevrix project foundation
```

------------------------------------------------------------------------

# Phase 1 --- RAII File Descriptors

## Objective

Make OS resources safe before networking complexity arrives.

## Implement

``` text
UniqueFd
```

Requirements:

-   move-only
-   no copy
-   closes exactly once
-   supports `release()`
-   supports `reset()`
-   exposes `get()`

## Tests

-   default invalid descriptor
-   ownership transfer
-   move construction
-   move assignment
-   reset
-   release
-   destructor closes descriptor

## Acceptance

No manually managed listening/client fd in higher-level code.

## Commit

``` text
feat: add RAII file descriptor ownership
```

------------------------------------------------------------------------

# Phase 2 --- TCP Listener

## Objective

Create the smallest possible real server.

## Implement

``` text
socket
bind
listen
accept
```

Use:

``` text
127.0.0.1:8080
```

initially.

Do not add epoll yet.

## First manual test

Run:

``` bash
./aevrix
```

Then:

``` bash
curl -v http://127.0.0.1:8080/
```

At first, the response can be a minimal hard-coded HTTP response.

## Acceptance

-   server starts
-   binds configured port
-   accepts client
-   does not leak descriptors
-   exits cleanly

## Commit

``` text
feat: add tcp listener and connection acceptance
```

------------------------------------------------------------------------

# Phase 3 --- HTTP Response Serializer

## Objective

Stop treating HTTP as a string literal.

## Implement

``` text
StatusCode
Header
Response
ResponseSerializer
```

Support:

``` text
200
400
404
405
500
```

## Tests

Verify:

``` text
status line
headers
CRLF
content length
body
```

## Acceptance

Every response is generated from structured objects.

## Commit

``` text
feat: add structured http response serialization
```

------------------------------------------------------------------------

# Phase 4 --- HTTP Request Parser

## Objective

Parse a real HTTP request.

## Parser states

``` text
RequestLine
Headers
Body
Complete
Error
```

## First supported request

``` http
GET / HTTP/1.1
Host: localhost
```

## Limits

Define constants/configuration for:

``` text
max_request_line_bytes
max_header_bytes
max_header_count
max_field_name_bytes
max_field_value_bytes
max_body_bytes
```

## Critical correctness rules

Follow RFC 9112.

In particular:

-   parse bytes rather than assuming Unicode text
-   require correct line termination
-   reject invalid header-name syntax
-   reject whitespace between header field name and colon
-   define body framing explicitly
-   avoid ambiguous `Content-Length` / `Transfer-Encoding` behavior

## Tests

At least:

``` text
valid GET
missing method
missing target
bad HTTP version
empty header value
duplicate headers
oversized header
invalid header name
invalid whitespace
malformed CRLF
partial input
request split across multiple reads
```

## Acceptance

The parser can handle a request arriving in arbitrary TCP-sized chunks.

## Commit

``` text
feat: implement incremental http request parser
```

------------------------------------------------------------------------

# Phase 5 --- Real Request/Response Loop

## Objective

Connect the parser and serializer.

``` text
socket
 ↓
recv
 ↓
parser
 ↓
Request
 ↓
handler
 ↓
Response
 ↓
serializer
 ↓
send
```

## Implement

-   partial reads
-   partial writes
-   close handling
-   error responses

## Acceptance

``` bash
curl -v http://127.0.0.1:8080/
```

returns a valid response.

Test with:

``` bash
curl -v http://127.0.0.1:8080/does-not-exist
```

and verify a controlled response.

## Commit

``` text
feat: connect http parsing and response pipeline
```

------------------------------------------------------------------------

# Phase 6 --- Static File Server

## Objective

Turn Aevrix into a useful server.

## Implement

``` text
document root
path mapping
file opening
file metadata
MIME detection
file response
404
403
```

## Security requirements

Never directly concatenate:

``` text
document_root + request_target
```

without normalization and validation.

Test:

``` text
../
..%2f
%2e%2e/
nested traversal
absolute path
encoded separators
```

## Acceptance

``` bash
aevrix --root ./public
```

serves:

``` text
public/index.html
public/style.css
public/assets/*
```

and cannot escape the root.

## Commit

``` text
feat: add secure static file serving
```

------------------------------------------------------------------------

# Phase 7 --- HEAD and Keep-Alive

## Objective

Implement important HTTP connection semantics.

## Support

``` text
GET
HEAD
```

Keep-alive:

``` text
request 1
response 1
request 2
response 2
```

on the same TCP connection.

## Tests

Use a raw TCP test client so that the test controls connection reuse.

## Acceptance

``` bash
curl -v http://127.0.0.1:8080/
curl -I http://127.0.0.1:8080/
```

work correctly.

## Commit

``` text
feat: add head and persistent http connections
```

------------------------------------------------------------------------

# Phase 8 --- Non-Blocking Sockets + epoll

## Objective

Move from a blocking prototype to an event-driven runtime.

## Implement

``` text
non-blocking listener
epoll_create
epoll_ctl
epoll_wait
```

Linux-first.

The event loop should handle:

``` text
listen fd
connection readable
connection writable
hangup
error
```

## Architecture

``` text
epoll_wait
     │
     ├── listener → accept loop
     │
     ├── readable → connection.on_readable()
     │
     ├── writable → connection.on_writable()
     │
     └── error → connection.close()
```

## Important

With non-blocking sockets, handle:

``` text
EAGAIN
EWOULDBLOCK
EINTR
```

correctly.

Do not treat EAGAIN as a fatal connection failure.

## Acceptance

The server can handle hundreds/thousands of idle local connections
without one thread per connection.

## Commit

``` text
feat: replace blocking loop with epoll event runtime
```

------------------------------------------------------------------------

# Phase 9 --- Connection State Machine

## Objective

Make event-driven behavior explicit.

Implement:

``` text
ConnectionState
ReadState
WriteState
TimeoutState
```

The connection must know:

-   input buffer
-   parser state
-   output buffer
-   current response
-   keep-alive decision
-   timestamps
-   request id

## Acceptance

No scattered "magic boolean" connection states.

## Commit

``` text
refactor: formalize connection state machine
```

------------------------------------------------------------------------

# Phase 10 --- Timeouts and Resource Limits

## Objective

Prevent slow-client resource exhaustion.

Implement:

``` text
header timeout
body timeout
keep-alive timeout
write timeout
max connections
max buffers
max request body
```

## Tests

Create slow clients that:

-   send one byte at a time
-   stop halfway through headers
-   stop halfway through a body
-   never read the response

## Acceptance

The server eventually frees stalled resources.

## Commit

``` text
feat: add connection timeouts and resource limits
```

------------------------------------------------------------------------

# Phase 11 --- Worker Pool

## Objective

Keep blocking filesystem/application work out of the event loop.

Implement:

``` text
BoundedQueue<Job>
WorkerPool
JobResult
```

Rules:

-   bounded queue
-   clean shutdown
-   no detached threads
-   no unbounded task creation

## Acceptance

The event loop remains responsive while filesystem work occurs.

## Commit

``` text
feat: add bounded worker pool for blocking work
```

------------------------------------------------------------------------

# Phase 12 --- Router

## Objective

Introduce application-level routing without turning Aevrix into a
framework.

Start with:

``` text
GET /
GET /health
GET /metrics
```

Later:

``` text
GET /users/:id
POST /users
```

## Design rule

Routing must not know about sockets.

It should consume:

``` text
Request
```

and produce:

``` text
Handler / Response
```

## Commit

``` text
feat: add lightweight request router
```

------------------------------------------------------------------------

# Phase 13 --- Configuration System

## Objective

Move runtime policy out of hard-coded constants.

Configuration should cover:

``` text
host
port
workers
document root
limits
timeouts
logging
```

## Requirements

-   typed configuration object
-   defaults
-   validation
-   clear startup errors

Bad configuration should fail before the server begins accepting
connections.

## Commit

``` text
feat: add validated server configuration
```

------------------------------------------------------------------------

# Phase 14 --- Structured Logging

## Objective

Make failures diagnosable.

Implement:

``` text
logger
log levels
request ids
connection ids
access logging
```

Example:

``` text
INFO conn=12 req=48 GET / 200 1254B 312us
```

## Commit

``` text
feat: add structured request logging
```

------------------------------------------------------------------------

# Phase 15 --- Graceful Shutdown

## Objective

Make shutdown safe and observable.

Implement:

``` text
SIGINT
SIGTERM
```

Sequence:

``` text
stop accepting
↓
finish safe work
↓
close connections
↓
stop workers
↓
flush logs
↓
exit
```

## Acceptance

Pressing Ctrl-C never corrupts internal state or leaves worker threads
running.

## Commit

``` text
feat: implement graceful server shutdown
```

------------------------------------------------------------------------

# Phase 16 --- Test Pyramid

## Objective

Turn correctness into an automated contract.

## Unit

Parser:

``` text
50+ cases
```

Router:

``` text
20+ cases
```

Path security:

``` text
30+ cases
```

## Integration

Test:

``` text
startup
GET
HEAD
404
405
keep-alive
timeouts
large headers
concurrency
shutdown
```

## Protocol tests

Create raw byte sequences rather than only using curl.

## Security tests

Include:

``` text
path traversal
invalid framing
oversized inputs
slow clients
connection floods
malformed headers
```

## Acceptance

``` bash
ctest --test-dir build --output-on-failure
```

passes.

## Commit

``` text
test: add protocol integration and security suite
```

------------------------------------------------------------------------

# Phase 17 --- Sanitizers and Static Analysis

## Objective

Find bugs that ordinary tests miss.

Run:

``` text
AddressSanitizer
UndefinedBehaviorSanitizer
ThreadSanitizer
```

where the selected runtime/build configuration supports them.

Also use:

``` text
-Wall
-Wextra
-Wpedantic
```

and appropriate additional warnings.

## Acceptance

No sanitizer failures in the supported test suite.

## Commit

``` text
ci: add sanitizer and static analysis gates
```

------------------------------------------------------------------------

# Phase 18 --- Benchmark Harness

## Objective

Make performance measurable.

Create:

``` text
benchmarks/
├── static_small
├── static_large
├── keepalive
├── concurrent
└── parser
```

Measure:

``` text
requests/sec
latency
p50
p95
p99
throughput
CPU
memory
open connections
```

Never publish a number without its environment.

## Baselines

Compare:

``` text
Aevrix blocking prototype
Aevrix epoll version
```

Do not compare against NGINX or other mature servers without matching
methodology and clearly explaining that the comparison is not an
apples-to-apples product benchmark.

## Commit

``` text
perf: add reproducible benchmark harness
```

------------------------------------------------------------------------

# Phase 19 --- HTTP Correctness Expansion

Implement one feature at a time:

``` text
conditional requests
ETag
Last-Modified
If-None-Match
If-Modified-Since
Range requests
206 Partial Content
304 Not Modified
```

Each feature gets:

``` text
spec notes
unit tests
integration tests
regression tests
documentation
```

## Commit pattern

``` text
feat: add etag support
test: cover conditional requests
docs: document cache validation
```

------------------------------------------------------------------------

# Phase 20 --- Observability

Add:

``` text
/metrics
/health
/server-info
```

Metrics should be useful for:

``` text
requests
connections
errors
latency
bytes
parser failures
```

## Acceptance

A developer can understand server behavior without attaching a debugger.

## Commit

``` text
feat: add runtime metrics and diagnostics
```

------------------------------------------------------------------------

# Phase 21 --- TLS

Only after the HTTP/1.1 core is stable.

Architecture:

``` text
TCP
 ↓
TLS layer
 ↓
HTTP bytes
 ↓
HTTP parser
```

Do not mix TLS logic into the HTTP parser.

## Requirements

-   certificate loading
-   private key loading
-   handshake errors
-   clean shutdown
-   configuration validation

## Security

Document TLS library/version assumptions.

## Commit

``` text
feat: add tls transport layer
```

------------------------------------------------------------------------

# Phase 22 --- Reverse Proxy

Implement:

``` text
client
 ↓
Aevrix
 ↓
upstream
```

Features:

``` text
forward request
read upstream response
connection pooling
timeouts
upstream failure handling
```

This phase turns Aevrix from a static server into a more
infrastructure-oriented project.

## Commit

``` text
feat: add reverse proxy transport
```

------------------------------------------------------------------------

# Phase 23 --- WebSocket Upgrade

Only after HTTP connection handling is mature.

Implement:

``` text
HTTP Upgrade
     ↓
protocol switch
     ↓
WebSocket framing
```

Keep WebSocket code outside the HTTP request parser.

## Commit

``` text
feat: add websocket upgrade transport
```

------------------------------------------------------------------------

# Phase 24 --- Configuration Reload

Implement:

``` text
signal/API
   ↓
parse new config
   ↓
validate
   ↓
construct new runtime config
   ↓
atomically swap
```

Do not mutate configuration objects randomly from multiple threads.

## Commit

``` text
feat: add atomic configuration reload
```

------------------------------------------------------------------------

# Phase 25 --- Release Engineering

Before v1.0:

## Repository

``` text
README.md
LICENSE
SECURITY.md
CONTRIBUTING.md
CHANGELOG.md
CODE_OF_CONDUCT.md
```

## GitHub

Add:

-   CI badge
-   release workflow
-   issue templates
-   pull request template
-   security policy
-   discussions if useful
-   benchmark documentation
-   architecture documentation

## Releases

Publish:

``` text
source archive
Linux binary where practical
checksums
release notes
```

Every release must identify:

``` text
version
commit
compiler
build mode
supported platform
known limitations
```

------------------------------------------------------------------------

# Phase 26 --- v1.0 Definition of Done

Aevrix v1.0 should satisfy:

## Correctness

``` text
[ ] HTTP/1.1 core behavior documented
[ ] parser heavily tested
[ ] malformed input handled
[ ] keep-alive works
[ ] partial I/O works
```

## Security

``` text
[ ] path traversal tests pass
[ ] request limits enforced
[ ] timeout policy enforced
[ ] ambiguous framing rejected safely
[ ] no known sanitizer failures
```

## Runtime

``` text
[ ] non-blocking networking
[ ] event loop
[ ] bounded workers
[ ] graceful shutdown
```

## Operations

``` text
[ ] structured logs
[ ] metrics
[ ] configuration
[ ] health endpoint
```

## Quality

``` text
[ ] CI
[ ] CTest
[ ] sanitizers
[ ] benchmark harness
[ ] documentation
```

------------------------------------------------------------------------

# Recommended Git Milestone Structure

Use milestone tags:

``` text
v0.1.0  TCP + hard-coded HTTP
v0.2.0  HTTP parser
v0.3.0  static files
v0.4.0  keep-alive + timeouts
v0.5.0  epoll runtime
v0.6.0  worker pool + routing
v0.7.0  config + observability
v0.8.0  security hardening
v0.9.0  benchmark + interoperability
v1.0.0  stable HTTP/1.1 server
```

The exact versioning can change, but milestones should tell a coherent
engineering story.

------------------------------------------------------------------------

# Daily Development Loop

For every feature:

``` text
1. Read the relevant specification.
2. Write the design note.
3. Write failing tests.
4. Implement the smallest correct version.
5. Run unit tests.
6. Run integration tests.
7. Run sanitizers.
8. Test manually.
9. Benchmark if performance-related.
10. Update documentation.
11. Commit.
12. Push.
```

Never accumulate weeks of uncommitted changes.

------------------------------------------------------------------------

# Final Project Narrative

The repository should eventually tell this story:

``` text
I wanted to understand web servers.

So I started at the socket.

Then I learned HTTP.

Then I built a parser.

Then I learned connection state.

Then I learned non-blocking I/O.

Then I learned epoll.

Then I learned backpressure.

Then I learned concurrency.

Then I learned protocol security.

Then I measured performance.

Then I hardened the system.

Aevrix is the result.
```

That story is more valuable than claiming that the project is "the
fastest web server on GitHub."
