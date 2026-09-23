# Aevrix Web Server --- Master Strategy

> **Project codename:** Aevrix\
> **Repository target:** `aevrix`\
> **Language:** C++20\
> **Primary target:** Linux/POSIX\
> **Initial protocol:** HTTP/1.1\
> **License recommendation:** MIT\
> **Status:** Design document --- implementation not started

## 1. Vision

Aevrix is not intended to be another tiny "socket accepts a connection
and prints Hello World" repository.

The goal is to build a **small, understandable, measurable HTTP server
in modern C++**, while preserving the engineering qualities that make
mature servers useful:

-   explicit ownership and lifetime management
-   strict protocol parsing
-   bounded resource usage
-   predictable concurrency
-   secure static-file serving
-   structured configuration
-   observability
-   reproducible benchmarks
-   automated tests and CI
-   excellent documentation
-   a development history that shows engineering decisions rather than
    copied code

The project should remain small enough for one developer to understand
end-to-end, but serious enough that another C++ developer can inspect
the repository and learn from it.

## 2. The Product Thesis

Aevrix should occupy a deliberate space between:

1.  a tutorial toy server, and
2.  a production web server with millions of lines of code.

The project should answer:

> "How far can a disciplined C++ developer take a web server while
> keeping the architecture understandable?"

That means **depth beats feature count**.

Do not add HTTP/2, HTTP/3, a database, a web framework, a scripting VM,
or a huge plugin ecosystem simply to make the README longer. Every
feature must earn its place by teaching an important systems concept or
improving the server's usefulness.

## 3. Research Principles

The architecture is informed by several real implementations and
standards:

-   NGINX demonstrates a master/worker process model and event-driven
    request processing.
-   Caddy demonstrates a clean separation between a small core and
    extensible modules/configuration.
-   Drogon demonstrates modern C++ non-blocking I/O with epoll/kqueue
    and asynchronous programming.
-   Crow demonstrates approachable C++ HTTP routing and middleware
    concepts.
-   cpp-httplib demonstrates the value of a compact, easy-to-consume C++
    HTTP implementation, while also explicitly documenting the tradeoff
    of blocking socket I/O.
-   RFC 9110 defines HTTP semantics.
-   RFC 9112 defines HTTP/1.1 message syntax, parsing, framing,
    connection management, and related security requirements.
-   CMake/CTest provide the project's build/test foundation.

Aevrix should borrow **principles**, not source code or architecture
wholesale.

## 4. Non-Negotiable Engineering Rules

### Rule 1 --- Never claim a benchmark result without publishing the exact methodology

Every performance number must include:

-   hardware
-   kernel/OS
-   compiler
-   build flags
-   server configuration
-   workload generator
-   concurrency
-   payload size
-   keep-alive behavior
-   duration
-   warm-up
-   number of runs
-   statistical summary

### Rule 2 --- Protocol correctness before performance

A fast parser that accepts malformed HTTP is not a success.

Implement protocol rules from RFC 9110 and RFC 9112 deliberately.

### Rule 3 --- Security is a feature

Static file serving must not allow:

-   directory traversal
-   arbitrary absolute paths
-   malformed request-line abuse
-   unbounded headers
-   unbounded request bodies
-   resource exhaustion through unlimited connections
-   ambiguous message framing

### Rule 4 --- Ownership must be obvious

Every socket, buffer, thread, file, and connection object needs an
obvious owner.

Prefer RAII and small types over raw lifetime conventions.

### Rule 5 --- Keep blocking work away from the event loop

The event loop should never perform unbounded disk or application work.

### Rule 6 --- Every bug becomes a regression test

If a malformed request once crashed the server, that exact request
becomes a permanent test.

## 5. Target User Experience

The first useful version should allow:

``` bash
aevrix --config aevrix.toml
```

and serve:

``` text
./public/
├── index.html
├── style.css
└── assets/
```

A browser should be able to visit:

``` text
http://127.0.0.1:8080/
```

Later, the same binary should support:

``` bash
aevrix --root ./public --port 8080
```

and a simple configuration file.

## 6. Feature Strategy

### Tier A --- Required

-   TCP listener
-   IPv4 initially
-   clean socket RAII
-   HTTP/1.1 request parser
-   response serializer
-   status codes
-   headers
-   GET
-   HEAD
-   keep-alive
-   static files
-   MIME type detection
-   404/400/405/500 responses
-   request size limits
-   header limits
-   connection timeouts
-   structured logging
-   graceful shutdown
-   unit tests
-   integration tests
-   CTest
-   sanitizers
-   CI
-   benchmark harness

### Tier B --- Core Differentiators

-   Linux non-blocking sockets
-   epoll-based event loop
-   explicit connection state machine
-   bounded worker pool for blocking filesystem work
-   send buffering
-   zero-copy-oriented file delivery where appropriate
-   configurable limits
-   atomic configuration reload
-   access log
-   metrics endpoint
-   benchmark command
-   load-test documentation
-   security test suite

### Tier C --- Advanced

-   routing
-   reverse proxy
-   gzip/brotli compression
-   TLS
-   WebSocket upgrade
-   HTTP range requests
-   conditional requests
-   caching
-   graceful worker replacement
-   plugin/module API

These are deliberately later. Do not allow Tier C features to
destabilize Tier A/B.

## 7. What Makes the Repository Stand Out

Aevrix should be recognizable from the GitHub landing page in less than
30 seconds.

The README should eventually contain:

1.  one-sentence description
2.  architecture diagram
3.  feature matrix
4.  tiny quick-start
5.  terminal/browser demo
6.  benchmark methodology
7.  security model
8.  test instructions
9.  roadmap
10. design-document index

### Recommended tagline

> **Aevrix --- a small, fast, security-conscious HTTP/1.1 server written
> from scratch in C++20.**

Do not claim "production-ready" until the project has earned that label
through testing, security review, interoperability testing, and
sustained use.

## 8. "Viral" Strategy Without Chasing Virality

No repository can guarantee going viral.

The controllable objective is to make the project **interesting enough
to share**.

Build shareable engineering artifacts:

### Artifact A --- Architecture visualization

Show:

``` text
             ┌──────────────┐
             │ TCP Listener │
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │ epoll loop   │
             └──────┬───────┘
                    │
                    ▼
          ┌────────────────────┐
          │ Connection FSM     │
          └─────────┬──────────┘
                    │
                    ▼
          ┌────────────────────┐
          │ HTTP/1.1 Parser    │
          └─────────┬──────────┘
                    │
                    ▼
          ┌────────────────────┐
          │ Router / File      │
          │ Resolver           │
          └─────────┬──────────┘
                    │
                    ▼
          ┌────────────────────┐
          │ Response Builder   │
          └─────────┬──────────┘
                    │
                    ▼
                 Client
```

### Artifact B --- Benchmark dashboard

Publish repeatable results, not marketing claims.

### Artifact C --- Protocol torture tests

Demonstrate what happens when clients send malformed or adversarial
requests.

### Artifact D --- "From socket to server" documentation

Explain every layer from:

``` text
socket()
→ bind()
→ listen()
→ accept()
→ epoll
→ bytes
→ HTTP parser
→ routing
→ response
```

### Artifact E --- Engineering diary

Use ADRs (Architecture Decision Records) for decisions such as:

-   why epoll
-   why C++20
-   why Linux-first
-   why a bounded worker pool
-   why parser limits exist
-   why TLS is not in the first milestone

## 9. Quality Gates

A milestone cannot be marked complete unless:

``` text
[ ] Builds with warnings enabled
[ ] Unit tests pass
[ ] Integration tests pass
[ ] CTest passes
[ ] ASan passes
[ ] UBSan passes
[ ] No obvious FD leaks
[ ] Malformed input tests pass
[ ] Load test completes
[ ] Documentation updated
[ ] README feature list is truthful
[ ] Git history explains the milestone
```

## 10. Definition of Done

Aevrix v1.0 should not mean "has many features".

It should mean:

> The implemented feature set is well specified, tested, observable,
> documented, and reproducible.

## 11. Long-Term Direction

After the HTTP/1.1 core is stable:

``` text
Aevrix
│
├── Core
│   ├── Event loop
│   ├── Connection state machine
│   ├── HTTP parser
│   ├── HTTP serializer
│   └── Resource limits
│
├── HTTP
│   ├── Router
│   ├── Static files
│   ├── Compression
│   ├── Range requests
│   └── Conditional requests
│
├── Runtime
│   ├── Worker pool
│   ├── Timers
│   └── Graceful shutdown
│
├── Observability
│   ├── Logs
│   ├── Metrics
│   └── Diagnostics
│
└── Extensions
    ├── TLS
    ├── Reverse proxy
    └── WebSocket
```

The project should remain understandable even as these layers grow.

## 12. Research References

Primary references used for this strategy:

-   IETF RFC 9110 --- HTTP Semantics
-   IETF RFC 9112 --- HTTP/1.1
-   NGINX documentation --- architecture and event-driven processing
-   Caddy documentation --- architecture and modular configuration
-   Drogon documentation/repository --- non-blocking C++ networking
-   Crow repository --- C++ HTTP/web-service API design
-   cpp-httplib repository --- compact C++ HTTP implementation and
    documented tradeoffs
-   Linux `socket(7)`, `accept(2)`, and `epoll_wait(2)` documentation
-   CMake/CTest documentation
-   OWASP Path Traversal guidance

## 13. First Milestone

Do not start with epoll.

Start with the smallest correct walking skeleton:

``` text
TCP listener
    ↓
accept
    ↓
read request
    ↓
parse one request
    ↓
return one valid HTTP response
```

Once this works and is tested, evolve the architecture.

The guiding principle is:

> **Make it correct. Make it observable. Make it measurable. Then make
> it fast.**
