// =============================================================================
// Aevrix - Upstream Connection Pool Implementation
// =============================================================================
// Idle keep-alive connection bookkeeping for the reverse proxy. POSIX sockets.
// =============================================================================

#include "aevrix/upstream_pool.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifndef _WIN32
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace aevrix {

// =============================================================================
// Lifetime
// =============================================================================

UpstreamPool::~UpstreamPool() {
    // UniqueFd members close their descriptors as the map is destroyed.
    close_all();
}

// =============================================================================
// Acquire
// =============================================================================

UpstreamPool::AcquireResult UpstreamPool::acquire(const ProxyTarget& target,
                                                  const struct sockaddr* address,
                                                  socklen_t address_length) {
    const std::string key = target.authority();

    auto it = idle_.find(key);
    if (it != idle_.end() && !it->second.empty()) {
        IdleConnection connection = std::move(it->second.back());
        it->second.pop_back();

        if (it->second.empty()) {
            idle_.erase(it);
        }

        AcquireResult result;
        result.ok = true;
        result.reused = true;
        result.fd = std::move(connection.fd);
        return result;
    }

    return create(address, address_length);
}

UpstreamPool::AcquireResult UpstreamPool::create(const struct sockaddr* address,
                                                 socklen_t address_length) {
    AcquireResult result;

    const int raw_fd = ::socket(address->sa_family,
                                SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                0);
    if (raw_fd < 0) {
        result.error = "socket() failed: " + std::string(std::strerror(errno));
        return result;
    }

    // From here on the descriptor is owned and closed on every exit path.
    UniqueFd fd(raw_fd);

    // Proxied request/response exchanges are small and latency sensitive.
    int nodelay = 1;
    (void)::setsockopt(raw_fd, IPPROTO_TCP, TCP_NODELAY,
                       &nodelay, static_cast<socklen_t>(sizeof(nodelay)));

    // Non-blocking connect: EINPROGRESS is the expected outcome and means the
    // caller must wait for the descriptor to become writable.
    if (::connect(raw_fd, address, address_length) < 0 && errno != EINPROGRESS) {
        result.error = "connect() failed: " + std::string(std::strerror(errno));
        return result;
    }

    result.ok = true;
    result.reused = false;
    result.fd = std::move(fd);
    return result;
}

// =============================================================================
// Release
// =============================================================================

void UpstreamPool::release(const ProxyTarget& target, UniqueFd fd) {
    if (fd.get() < 0 || max_idle_per_target_ == 0) {
        return;  // Descriptor closes here
    }

    const std::string key = target.authority();
    auto it = idle_.find(key);

    if (it != idle_.end()) {
        if (it->second.size() >= max_idle_per_target_) {
            return;  // Pool is full for this upstream: close the connection
        }
        it->second.emplace_back(std::move(fd), std::chrono::steady_clock::now());
        return;
    }

    std::vector<IdleConnection> bucket;
    bucket.reserve(1);
    bucket.emplace_back(std::move(fd), std::chrono::steady_clock::now());
    idle_.emplace(key, std::move(bucket));
}

// =============================================================================
// Maintenance
// =============================================================================

size_t UpstreamPool::sweep_idle() {
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = static_cast<long long>(idle_timeout_ms_);

    size_t removed = 0;

    for (auto it = idle_.begin(); it != idle_.end();) {
        std::vector<IdleConnection>& bucket = it->second;
        const size_t before = bucket.size();

        bucket.erase(
            std::remove_if(bucket.begin(), bucket.end(),
                           [now, timeout](const IdleConnection& connection) {
                               const auto idle_ms =
                                   std::chrono::duration_cast<std::chrono::milliseconds>(
                                       now - connection.since)
                                       .count();
                               return idle_ms >= timeout;
                           }),
            bucket.end());

        removed += before - bucket.size();

        if (bucket.empty()) {
            it = idle_.erase(it);
        } else {
            ++it;
        }
    }

    return removed;
}

void UpstreamPool::close_all() {
    idle_.clear();
}

size_t UpstreamPool::idle_count() const {
    size_t total = 0;
    for (const auto& entry : idle_) {
        total += entry.second.size();
    }
    return total;
}

}  // namespace aevrix
