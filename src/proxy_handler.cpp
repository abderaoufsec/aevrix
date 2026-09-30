// =============================================================================
// Aevrix - Proxy Handler Implementation
// =============================================================================
// The upstream half of the reverse proxy. Everything here runs on the event
// loop thread and uses non-blocking sockets exclusively.
// =============================================================================

#include "aevrix/proxy_handler.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "aevrix/logger.h"
#include "aevrix/proxy_request_builder.h"

namespace aevrix {

namespace {

// Receive chunk size for upstream reads. Small enough to bound the work done
// per event (and the stack frame), large enough to keep syscall overhead low.
constexpr size_t kReadChunkSize = 16 * 1024;

/**
 * @brief Describe the client peer address for X-Forwarded-For
 */
std::string peer_address(int fd) {
    struct sockaddr_storage storage {};
    socklen_t length = static_cast<socklen_t>(sizeof(storage));

    if (::getpeername(fd, reinterpret_cast<struct sockaddr*>(&storage), &length) != 0) {
        return std::string();
    }

    char host[INET6_ADDRSTRLEN] = {0};
    const void* source = nullptr;

    if (storage.ss_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const struct sockaddr_in*>(&storage);
        source = &ipv4->sin_addr;
    } else if (storage.ss_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const struct sockaddr_in6*>(&storage);
        source = &ipv6->sin6_addr;
    } else {
        return std::string();
    }

    if (::inet_ntop(storage.ss_family, source, host, sizeof(host)) == nullptr) {
        return std::string();
    }

    return std::string(host);
}

/**
 * @brief Resolve an upstream target to a socket address
 *
 * Called once at startup: getaddrinfo() blocks, which is acceptable during
 * initialisation and unacceptable inside an event callback.
 */
bool resolve_target(const ProxyTarget& target,
                    struct sockaddr_storage& storage,
                    socklen_t& length,
                    std::string& error) {
    const std::string service = std::to_string(target.port);

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    const int status = ::getaddrinfo(target.host.c_str(), service.c_str(), &hints, &result);
    if (status != 0 || result == nullptr) {
        error = "cannot resolve upstream '" + target.authority() + "': " +
                ::gai_strerror(status);
        if (result != nullptr) {
            ::freeaddrinfo(result);
        }
        return false;
    }

    if (result->ai_addrlen > sizeof(storage)) {
        error = "resolved upstream address does not fit sockaddr_storage";
        ::freeaddrinfo(result);
        return false;
    }

    std::memcpy(&storage, result->ai_addr, result->ai_addrlen);
    length = static_cast<socklen_t>(result->ai_addrlen);
    ::freeaddrinfo(result);

    return true;
}

/**
 * @brief Build a success outcome from a completed upstream response
 */
ProxyOutcome outcome_from_parser(const ProxyResponseParser& parser) {
    ProxyOutcome outcome;
    outcome.status = ProxyOutcomeStatus::Success;
    outcome.upstream_status = parser.status_code();
    outcome.headers = parser.headers();
    outcome.body = parser.body();
    return outcome;
}

}  // namespace

// =============================================================================
// Construction
// =============================================================================

ProxyHandler::ProxyHandler(const ServerConfig& config,
                           EventLoop& event_loop,
                           ProxyCompletion on_complete)
    : config_(config)
    , event_loop_(event_loop)
    , on_complete_(std::move(on_complete)) {
    // Pool policy comes from configuration so operators can bound descriptor
    // usage per upstream.
    pool_.set_max_idle_per_target(config.proxy_max_idle_connections());
    pool_.set_idle_timeout_ms(config.proxy_idle_timeout_ms());

    if (!config.proxy_enabled()) {
        configuration_error_ = "proxy is disabled";
        return;
    }

    std::string error;
    const std::optional<ProxyTarget> parsed = parse_proxy_target(config.proxy_pass(), error);
    if (!parsed.has_value()) {
        configuration_error_ = error;
        g_logger.error("Reverse proxy disabled: " + error);
        return;
    }

    target_ = parsed.value();

    if (!resolve_target(target_, upstream_address_, upstream_address_length_, error)) {
        configuration_error_ = error;
        g_logger.error("Reverse proxy disabled: " + error);
        return;
    }

    configured_ = true;
    g_logger.info("Reverse proxy enabled: " + config.proxy_prefix() + " -> " +
                  target_.authority() +
                  (config.proxy_strip_prefix() ? " (prefix stripped)" : ""));
}

ProxyHandler::~ProxyHandler() {
    shutdown();
}

// =============================================================================
// Routing
// =============================================================================

bool ProxyHandler::handles(const http::HttpRequest& request) const {
    if (!config_.proxy_enabled()) {
        return false;
    }
    return proxy_target_matches(request.target(), config_.proxy_prefix());
}

std::chrono::steady_clock::time_point ProxyHandler::deadline_after(uint64_t timeout_ms) const {
    if (timeout_ms == 0) {
        return std::chrono::steady_clock::time_point::max();  // Timeout disabled
    }
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
}

bool ProxyHandler::start(uint64_t client_connection_id,
                         int client_fd,
                         const http::HttpRequest& request) {
    // -------------------------------------------------------------------------
    // A misconfigured upstream is a gateway problem, not a client problem.
    // -------------------------------------------------------------------------
    if (!configured_) {
        ProxyOutcome outcome;
        outcome.status = ProxyOutcomeStatus::NotConfigured;
        outcome.error_message = configuration_error_;
        g_logger.log_with_connection(LogLevel::ERR, client_connection_id,
                                     "Proxy not configured: " + configuration_error_);
        ++failed_requests_;
        if (on_complete_) {
            on_complete_(client_connection_id, std::move(outcome));
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // Acquire an upstream connection (idle reuse or non-blocking connect)
    // -------------------------------------------------------------------------
    UpstreamPool::AcquireResult acquired =
        pool_.acquire(target_, reinterpret_cast<const struct sockaddr*>(&upstream_address_),
                      upstream_address_length_);

    if (!acquired.ok) {
        ProxyOutcome outcome;
        outcome.status = ProxyOutcomeStatus::BadGateway;
        outcome.error_message = acquired.error;
        g_logger.log_with_connection(LogLevel::ERR, client_connection_id,
                                     "Upstream connection failed: " + acquired.error);
        ++failed_requests_;
        if (on_complete_) {
            on_complete_(client_connection_id, std::move(outcome));
        }
        return true;
    }

    const int upstream_fd = acquired.fd.get();
    if (acquired.reused) {
        ++connections_reused_;
    } else {
        ++connections_created_;
    }

    // Ownership moves into the pending request immediately, so every exit path
    // below releases the descriptor exactly once.
    UniqueFd owned_connection = std::move(acquired.fd);

    // -------------------------------------------------------------------------
    // Build the request that will be written once the socket is ready
    // -------------------------------------------------------------------------
    ProxyRequestOptions options;
    options.prefix = config_.proxy_prefix();
    options.strip_prefix = config_.proxy_strip_prefix();
    options.forwarded_proto = config_.tls_enabled() ? "https" : "http";
    options.client_ip = peer_address(client_fd);

    ProxyResponseParser::Config parser_config;
    parser_config.max_body_bytes = config_.proxy_max_response_bytes();
    parser_config.request_was_head = (request.method() == http::HttpMethod::HEAD);

    PendingRequest pending;
    pending.client_connection_id = client_connection_id;
    pending.client_fd = client_fd;
    pending.connection = std::move(owned_connection);
    pending.upstream_fd = upstream_fd;
    pending.phase = acquired.reused ? Phase::Writing : Phase::Connecting;
    pending.target = target_;
    pending.parser.reset(parser_config);
    pending.request_bytes = build_upstream_request(request, target_, options);
    pending.bytes_written = 0;
    pending.generation = next_generation_++;
    pending.head_request = parser_config.request_was_head;
    pending.deadline = deadline_after(acquired.reused ? config_.proxy_read_timeout_ms()
                                                      : config_.proxy_connect_timeout_ms());

    // A fresh socket needs EPOLLOUT for the connect handshake; a pooled socket
    // is already connected, so it needs both directions right away.
    const uint32_t events = (pending.phase == Phase::Writing)
                                ? static_cast<uint32_t>(EPOLLOUT | EPOLLIN | EPOLLRDHUP)
                                : static_cast<uint32_t>(EPOLLOUT | EPOLLRDHUP);

    const uint64_t generation = pending.generation;
    if (!event_loop_.add_fd(upstream_fd, events,
                            [this, upstream_fd, generation](int, EventType event) {
                                on_upstream_event(upstream_fd, generation, event);
                            })) {
        ProxyOutcome outcome;
        outcome.status = ProxyOutcomeStatus::Unavailable;
        outcome.error_message = "failed to register upstream descriptor with the event loop";
        g_logger.log_with_connection(LogLevel::ERR, client_connection_id,
                                     outcome.error_message);
        ++failed_requests_;
        if (on_complete_) {
            on_complete_(client_connection_id, std::move(outcome));
        }
        return true;  // owned_connection closes here
    }

    pending_.emplace(upstream_fd, std::move(pending));

    g_logger.log_with_connection(LogLevel::DEBUG, client_connection_id,
                                 "Proxying " + request.target() + " to " + target_.authority() +
                                     (acquired.reused ? " (pooled)" : " (new connection)"));

    return true;
}

// =============================================================================
// Event handling
// =============================================================================

void ProxyHandler::on_upstream_event(int fd, uint64_t generation, EventType event) {
    auto it = pending_.find(fd);
    if (it == pending_.end()) {
        return;  // Request already finished
    }

    if (it->second.generation != generation) {
        // Stale callback for a descriptor number that has been recycled by a
        // pooled connection. Acting on it would corrupt an unrelated request.
        return;
    }

    switch (event) {
        case EventType::Writable:
            on_writable(pending_.at(fd));
            return;

        case EventType::Readable:
            on_readable(pending_.at(fd));
            return;

        case EventType::Hangup: {
            // Drain whatever arrived before the peer closed: a close-delimited
            // body is only complete once EOF has been observed.
            auto pending_it = pending_.find(fd);
            if (pending_it == pending_.end()) {
                return;
            }

            read_available(pending_it->second);

            pending_it = pending_.find(fd);
            if (pending_it == pending_.end()) {
                return;  // Response completed while draining
            }

            // read_available() reports EOF through the parser: if the message
            // is still incomplete here, the response was truncated.
            if (!pending_it->second.parser.finish_on_eof()) {
                fail(fd, ProxyOutcomeStatus::BadGateway,
                     "truncated upstream response: " + pending_it->second.parser.error_message());
                return;
            }

            complete(fd, outcome_from_parser(pending_it->second.parser));
            return;
        }

        case EventType::Error:
            fail(fd, ProxyOutcomeStatus::BadGateway, "upstream socket error");
            return;
    }
}

void ProxyHandler::on_writable(PendingRequest& request) {
    if (request.phase == Phase::Connecting) {
        if (!finish_connect(request)) {
            return;  // Failed and already reported
        }
    }

    if (request.phase == Phase::Writing) {
        flush_request(request);
    }
}

void ProxyHandler::on_readable(PendingRequest& request) {
    if (request.phase != Phase::Reading) {
        // The socket became readable before the request was fully sent. Rather
        // than interleaving reads and writes, wait until the flush completes.
        return;
    }

    read_available(request);
}

// =============================================================================
// Phase transitions
// =============================================================================

bool ProxyHandler::finish_connect(PendingRequest& request) {
    int socket_error = 0;
    socklen_t length = static_cast<socklen_t>(sizeof(socket_error));

    if (::getsockopt(request.upstream_fd, SOL_SOCKET, SO_ERROR, &socket_error, &length) != 0) {
        fail(request.upstream_fd, ProxyOutcomeStatus::BadGateway,
             "getsockopt(SO_ERROR) failed: " + std::string(std::strerror(errno)));
        return false;
    }

    if (socket_error != 0) {
        fail(request.upstream_fd, ProxyOutcomeStatus::BadGateway,
             "upstream connect failed: " + std::string(std::strerror(socket_error)));
        return false;
    }

    request.phase = Phase::Writing;
    request.deadline = deadline_after(config_.proxy_read_timeout_ms());
    return true;
}

bool ProxyHandler::flush_request(PendingRequest& request) {
    while (request.bytes_written < request.request_bytes.size()) {
        const char* data = request.request_bytes.data() + request.bytes_written;
        const size_t remaining = request.request_bytes.size() - request.bytes_written;

        int send_flags = 0;
#ifdef MSG_NOSIGNAL
        send_flags = MSG_NOSIGNAL;  // Never take SIGPIPE for a dead upstream
#endif

        const ssize_t sent = ::send(request.upstream_fd, data, remaining, send_flags);

        if (sent > 0) {
            request.bytes_written += static_cast<size_t>(sent);
            continue;
        }

        if (sent == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;  // Send buffer full: resume on the next writable event
        }

        if (errno == EINTR) {
            continue;
        }

        fail(request.upstream_fd, ProxyOutcomeStatus::BadGateway,
             "upstream write failed: " + std::string(std::strerror(errno)));
        return false;
    }

    // Fully flushed: the request cannot be reused, and only readiness for the
    // response is interesting from now on.
    request.request_bytes.clear();
    request.phase = Phase::Reading;
    request.deadline = deadline_after(config_.proxy_read_timeout_ms());

    if (!event_loop_.modify_fd(request.upstream_fd,
                               static_cast<uint32_t>(EPOLLIN | EPOLLRDHUP))) {
        fail(request.upstream_fd, ProxyOutcomeStatus::BadGateway,
             "failed to change upstream event interest");
        return false;
    }

    return true;
}

// =============================================================================
// Reading the upstream response
// =============================================================================

void ProxyHandler::read_available(PendingRequest& request) {
    char buffer[kReadChunkSize];

    for (;;) {
        const ssize_t received = ::recv(request.upstream_fd, buffer, sizeof(buffer), 0);

        // ---------------------------------------------------------------------
        // Data
        // ---------------------------------------------------------------------
        if (received > 0) {
            if (!request.parser.feed(buffer, static_cast<size_t>(received))) {
                fail(request.upstream_fd, ProxyOutcomeStatus::BadGateway,
                     "invalid upstream response: " + request.parser.error_message());
                return;
            }

            if (request.parser.is_complete()) {
                complete(request.upstream_fd, outcome_from_parser(request.parser));
                return;
            }

            // A short read means the socket is drained.
            if (received < static_cast<ssize_t>(sizeof(buffer))) {
                return;
            }
            continue;
        }

        // ---------------------------------------------------------------------
        // EOF
        // ---------------------------------------------------------------------
        if (received == 0) {
            if (!request.parser.finish_on_eof()) {
                fail(request.upstream_fd, ProxyOutcomeStatus::BadGateway,
                     "truncated upstream response: " + request.parser.error_message());
                return;
            }

            complete(request.upstream_fd, outcome_from_parser(request.parser));
            return;
        }

        // ---------------------------------------------------------------------
        // Error
        // ---------------------------------------------------------------------
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;  // Nothing more to read right now
        }

        if (errno == EINTR) {
            continue;
        }

        fail(request.upstream_fd, ProxyOutcomeStatus::BadGateway,
             "upstream read failed: " + std::string(std::strerror(errno)));
        return;
    }
}

// =============================================================================
// Completion
// =============================================================================

void ProxyHandler::complete(int fd, ProxyOutcome outcome) {
    auto it = pending_.find(fd);
    if (it == pending_.end()) {
        return;
    }

    // Copy everything needed before the entry is destroyed.
    const uint64_t client_connection_id = it->second.client_connection_id;
    const ProxyTarget target = it->second.target;
    const bool reusable = (outcome.status == ProxyOutcomeStatus::Success) &&
                          it->second.parser.upstream_keep_alive();

    outcome.head_request = it->second.head_request;

    UniqueFd connection = std::move(it->second.connection);

    event_loop_.remove_fd(fd);
    pending_.erase(it);

    // Pool the connection only when the upstream is at a clean message
    // boundary and did not ask for the connection to be closed.
    if (reusable) {
        pool_.release(target, std::move(connection));
    }
    // Otherwise the descriptor closes here.

    ++completed_requests_;

    if (outcome.status == ProxyOutcomeStatus::Success) {
        g_logger.log_with_connection(LogLevel::DEBUG, client_connection_id,
                                     "Upstream responded " +
                                         std::to_string(outcome.upstream_status) + " (" +
                                         std::to_string(outcome.body.size()) + " bytes)" +
                                         (reusable ? ", connection pooled" : ""));
    }

    if (on_complete_) {
        on_complete_(client_connection_id, std::move(outcome));
    }
}

void ProxyHandler::fail(int fd, ProxyOutcomeStatus status, const std::string& message) {
    auto it = pending_.find(fd);
    if (it == pending_.end()) {
        return;
    }

    ProxyOutcome outcome;
    outcome.status = status;
    outcome.error_message = message;
    // Error replies to HEAD must stay bodyless as well.
    outcome.head_request = it->second.head_request;

    const uint64_t client_connection_id = it->second.client_connection_id;

    // A failed exchange never leaves a reusable connection behind.
    UniqueFd connection = std::move(it->second.connection);

    event_loop_.remove_fd(fd);
    pending_.erase(it);

    ++failed_requests_;

    g_logger.log_with_connection(LogLevel::ERR, client_connection_id,
                                 "Proxy request failed: " + message);

    if (on_complete_) {
        on_complete_(client_connection_id, std::move(outcome));
    }
}

// =============================================================================
// Maintenance
// =============================================================================

void ProxyHandler::sweep_timeouts() {
    const auto now = std::chrono::steady_clock::now();

    // Collect first: fail() mutates the map.
    std::vector<int> expired;
    for (const auto& entry : pending_) {
        const auto deadline = entry.second.deadline;
        if (deadline != std::chrono::steady_clock::time_point::max() && now >= deadline) {
            expired.push_back(entry.first);
        }
    }

    for (int fd : expired) {
        auto it = pending_.find(fd);
        if (it == pending_.end()) {
            continue;
        }

        const bool connecting = (it->second.phase == Phase::Connecting);
        fail(fd, ProxyOutcomeStatus::GatewayTimeout,
             connecting ? "upstream connect timeout" : "upstream response timeout");
    }

    pool_.sweep_idle();
}

void ProxyHandler::shutdown() {
    for (const auto& entry : pending_) {
        event_loop_.remove_fd(entry.first);
    }

    // Dropping the pending requests closes their descriptors. The client
    // connections are torn down by the server's own shutdown path, so no
    // completion callbacks are invoked here.
    pending_.clear();
    pool_.close_all();
}

}  // namespace aevrix
