// =============================================================================
// Aevrix - Upstream Connection Pool Tests (Phase 22)
// =============================================================================
// Tests for the reverse proxy's idle upstream connection pool using real
// loopback sockets. The focus is ownership: every acquired descriptor must be
// accounted for, pooling must be bounded, and stale connections must be
// evicted rather than accumulated.
// =============================================================================

#include "aevrix/upstream_pool.h"

#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace aevrix;

namespace {

// =============================================================================
// Test Helpers
// =============================================================================

/**
 * @brief Count open descriptors of this process
 *
 * Used to prove that pooling does not leak sockets.
 */
size_t count_open_fds() {
    DIR* dir = ::opendir("/proc/self/fd");
    if (dir == nullptr) {
        return 0;
    }

    size_t count = 0;
    while (::readdir(dir) != nullptr) {
        ++count;
    }
    ::closedir(dir);
    return count;
}

/**
 * @brief A loopback listener that accepts connections
 *
 * Proxied connects must succeed. Accepted sockets can either be held open (so
 * pooled descriptors stay genuinely usable for I/O tests) or closed straight
 * away, which keeps the listener's own descriptor usage flat - required by the
 * descriptor-accounting test.
 */
class TestListener {
public:
    explicit TestListener(bool hold_connections = true) {
        hold_connections_ = hold_connections;
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(fd_ >= 0);

        int enable = 1;
        assert(::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enable,
                            static_cast<socklen_t>(sizeof(enable))) == 0);

        struct sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        assert(::bind(fd_, reinterpret_cast<struct sockaddr*>(&address),
                      static_cast<socklen_t>(sizeof(address))) == 0);
        assert(::listen(fd_, 32) == 0);

        socklen_t length = static_cast<socklen_t>(sizeof(address));
        assert(::getsockname(fd_, reinterpret_cast<struct sockaddr*>(&address), &length) == 0);
        port_ = ::ntohs(address.sin_port);

        accepting_ = true;
        accept_thread_ = std::thread([this]() { accept_loop(); });
    }

    ~TestListener() {
        accepting_ = false;
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (int fd : accepted_) {
                ::close(fd);
            }
            accepted_.clear();
        }

        ::close(fd_);
    }

    TestListener(const TestListener&) = delete;
    TestListener& operator=(const TestListener&) = delete;

    uint16_t port() const { return port_; }

    /**
     * @brief Number of connections completed by the kernel listener
     *
     * Only tracked while connections are held; the accounting test closes
     * accepted sockets and must not count them as this process's descriptors.
     */
    size_t accepted_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return accepted_count_;
    }

    /**
     * @brief True once every accepted socket has been closed again
     *
     * Lets the descriptor-accounting test wait for the listener to settle
     * before comparing descriptor counts.
     */
    bool drained() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return accepted_count_ == closed_count_;
    }

private:
    void accept_loop() {
        while (accepting_) {
            struct pollfd descriptor {};
            descriptor.fd = fd_;
            descriptor.events = POLLIN;

            const int ready = ::poll(&descriptor, 1, 50);
            if (ready <= 0) {
                continue;
            }

            const int client = ::accept(fd_, nullptr, nullptr);
            if (client < 0) {
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++accepted_count_;
                if (hold_connections_) {
                    accepted_.push_back(client);
                    continue;
                }
            }

            ::close(client);  // Keep the listener's descriptor usage flat

            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++closed_count_;
            }
        }
    }

    int fd_ = -1;
    uint16_t port_ = 0;
    bool accepting_ = false;
    bool hold_connections_ = true;
    std::thread accept_thread_;
    mutable std::mutex mutex_;
    size_t accepted_count_ = 0;
    size_t closed_count_ = 0;
    std::vector<int> accepted_;
};

/**
 * @brief Build a sockaddr_in for 127.0.0.1:port
 */
struct sockaddr_in loopback_address(uint16_t port) {
    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    address.sin_port = ::htons(port);
    return address;
}

/**
 * @brief Wait until a connecting socket becomes writable and report SO_ERROR
 */
int wait_for_connect_result(int fd, int timeout_ms) {
    struct pollfd descriptor {};
    descriptor.fd = fd;
    descriptor.events = POLLOUT;

    if (::poll(&descriptor, 1, timeout_ms) <= 0) {
        return -1;  // Timed out
    }

    int socket_error = 0;
    socklen_t length = static_cast<socklen_t>(sizeof(socket_error));
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length) != 0) {
        return -1;
    }
    return socket_error;
}

/**
 * @brief Reserve an ephemeral port that is then released
 *
 * Used to obtain a port that very probably has no listener.
 */
uint16_t find_unused_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    assert(::bind(fd, reinterpret_cast<struct sockaddr*>(&address),
                  static_cast<socklen_t>(sizeof(address))) == 0);

    socklen_t length = static_cast<socklen_t>(sizeof(address));
    assert(::getsockname(fd, reinterpret_cast<struct sockaddr*>(&address), &length) == 0);
    const uint16_t port = ::ntohs(address.sin_port);

    ::close(fd);
    return port;
}

}  // namespace

// =============================================================================
// Acquisition and reuse
// =============================================================================

void test_acquire_creates_connectable_socket() {
    std::cout << "Testing acquire creates a usable connection..." << std::endl;

    TestListener listener;
    UpstreamPool pool;
    const ProxyTarget target{"127.0.0.1", listener.port()};
    const struct sockaddr_in address = loopback_address(listener.port());

    UpstreamPool::AcquireResult result =
        pool.acquire(target, reinterpret_cast<const struct sockaddr*>(&address),
                     static_cast<socklen_t>(sizeof(address)));

    assert(result.ok && "acquire must succeed");
    assert(!result.reused && "a fresh pool cannot reuse anything");
    assert(result.fd.get() >= 0);
    assert(result.error.empty());
    assert(wait_for_connect_result(result.fd.get(), 2000) == 0 &&
           "connection must reach the listener");
    assert(pool.idle_count() == 0 && "an acquired connection is not idle");

    std::cout << "  PASSED" << std::endl;
}

void test_release_and_reuse() {
    std::cout << "Testing release followed by reuse..." << std::endl;

    TestListener listener;
    UpstreamPool pool;
    const ProxyTarget target{"127.0.0.1", listener.port()};
    const struct sockaddr_in address = loopback_address(listener.port());
    const auto* raw_address = reinterpret_cast<const struct sockaddr*>(&address);
    const auto address_length = static_cast<socklen_t>(sizeof(address));

    UpstreamPool::AcquireResult first = pool.acquire(target, raw_address, address_length);
    assert(first.ok);
    assert(wait_for_connect_result(first.fd.get(), 2000) == 0);
    const int first_fd = first.fd.get();

    pool.release(target, std::move(first.fd));
    assert(pool.idle_count() == 1 && "released connection is pooled");
    assert(pool.target_count() == 1);

    UpstreamPool::AcquireResult second = pool.acquire(target, raw_address, address_length);
    assert(second.ok);
    assert(second.reused && "the pooled connection must be reused");
    assert(second.fd.get() == first_fd && "the same descriptor is handed back");
    assert(pool.idle_count() == 0);

    std::cout << "  PASSED" << std::endl;
}

void test_targets_are_isolated() {
    std::cout << "Testing per-target pooling..." << std::endl;

    TestListener first_listener;
    TestListener second_listener;

    UpstreamPool pool;
    const ProxyTarget first_target{"127.0.0.1", first_listener.port()};
    const ProxyTarget second_target{"127.0.0.1", second_listener.port()};

    const struct sockaddr_in first_address = loopback_address(first_listener.port());
    const struct sockaddr_in second_address = loopback_address(second_listener.port());

    UpstreamPool::AcquireResult first = pool.acquire(
        first_target, reinterpret_cast<const struct sockaddr*>(&first_address),
        static_cast<socklen_t>(sizeof(first_address)));
    assert(first.ok);
    assert(wait_for_connect_result(first.fd.get(), 2000) == 0);
    pool.release(first_target, std::move(first.fd));
    assert(pool.idle_count() == 1);

    // A different upstream must never receive another upstream's connection.
    UpstreamPool::AcquireResult second = pool.acquire(
        second_target, reinterpret_cast<const struct sockaddr*>(&second_address),
        static_cast<socklen_t>(sizeof(second_address)));
    assert(second.ok);
    assert(!second.reused && "pool entries are keyed by authority");

    // The first upstream's connection is still available for reuse.
    UpstreamPool::AcquireResult first_again = pool.acquire(
        first_target, reinterpret_cast<const struct sockaddr*>(&first_address),
        static_cast<socklen_t>(sizeof(first_address)));
    assert(first_again.ok);
    assert(first_again.reused);

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Bounds, expiry and cleanup
// =============================================================================

void test_pooling_can_be_disabled() {
    std::cout << "Testing zero-capacity pool..." << std::endl;

    TestListener listener;
    UpstreamPool pool;
    pool.set_max_idle_per_target(0);
    assert(pool.max_idle_per_target() == 0);

    const ProxyTarget target{"127.0.0.1", listener.port()};
    const struct sockaddr_in address = loopback_address(listener.port());
    const auto* raw_address = reinterpret_cast<const struct sockaddr*>(&address);
    const auto address_length = static_cast<socklen_t>(sizeof(address));

    UpstreamPool::AcquireResult result = pool.acquire(target, raw_address, address_length);
    assert(result.ok);

    pool.release(target, std::move(result.fd));
    assert(pool.idle_count() == 0 && "release must close when pooling is disabled");

    std::cout << "  PASSED" << std::endl;
}

void test_capacity_limit_is_enforced() {
    std::cout << "Testing idle capacity limit..." << std::endl;

    TestListener listener;
    UpstreamPool pool;
    pool.set_max_idle_per_target(2);

    const ProxyTarget target{"127.0.0.1", listener.port()};
    const struct sockaddr_in address = loopback_address(listener.port());
    const auto* raw_address = reinterpret_cast<const struct sockaddr*>(&address);
    const auto address_length = static_cast<socklen_t>(sizeof(address));

    std::vector<UpstreamPool::AcquireResult> held;

    // Hold every connection out at once, otherwise acquire() would simply reuse
    // the one already pooled and the cap would never be reached.
    for (int i = 0; i < 5; ++i) {
        UpstreamPool::AcquireResult result = pool.acquire(target, raw_address, address_length);
        assert(result.ok);
        assert(wait_for_connect_result(result.fd.get(), 2000) == 0);
        held.push_back(std::move(result));
    }

    for (UpstreamPool::AcquireResult& result : held) {
        pool.release(target, std::move(result.fd));
    }

    assert(pool.idle_count() == 2 && "descriptor usage must stay bounded");

    std::cout << "  PASSED" << std::endl;
}

void test_stale_connection_is_not_reused() {
    std::cout << "Testing stale connection eviction..." << std::endl;

    TestListener listener;
    UpstreamPool pool;
    pool.set_idle_timeout_ms(0);  // Everything is instantly expired
    assert(pool.idle_timeout_ms() == 0);

    const ProxyTarget target{"127.0.0.1", listener.port()};
    const struct sockaddr_in address = loopback_address(listener.port());
    const auto* raw_address = reinterpret_cast<const struct sockaddr*>(&address);
    const auto address_length = static_cast<socklen_t>(sizeof(address));

    UpstreamPool::AcquireResult result = pool.acquire(target, raw_address, address_length);
    assert(result.ok);
    pool.release(target, std::move(result.fd));
    assert(pool.idle_count() == 1);

    // The sweep runs on the maintenance tick and must close the expired entry.
    assert(pool.sweep_idle() == 1);
    assert(pool.idle_count() == 0);
    assert(pool.target_count() == 0 && "empty targets are pruned");

    UpstreamPool::AcquireResult again = pool.acquire(target, raw_address, address_length);
    assert(again.ok);
    assert(!again.reused && "the expired connection is gone");

    std::cout << "  PASSED" << std::endl;
}

void test_close_all_releases_descriptors() {
    std::cout << "Testing close_all..." << std::endl;

    TestListener listener;
    UpstreamPool pool;

    const ProxyTarget target{"127.0.0.1", listener.port()};
    const struct sockaddr_in address = loopback_address(listener.port());
    const auto* raw_address = reinterpret_cast<const struct sockaddr*>(&address);
    const auto address_length = static_cast<socklen_t>(sizeof(address));

    std::vector<UpstreamPool::AcquireResult> held;
    for (int i = 0; i < 3; ++i) {
        UpstreamPool::AcquireResult result = pool.acquire(target, raw_address, address_length);
        assert(result.ok);
        held.push_back(std::move(result));
    }
    for (UpstreamPool::AcquireResult& result : held) {
        pool.release(target, std::move(result.fd));
    }
    assert(pool.idle_count() == 3);

    pool.close_all();
    assert(pool.idle_count() == 0);
    assert(pool.target_count() == 0);

    std::cout << "  PASSED" << std::endl;
}

void test_connection_failure_is_reported() {
    std::cout << "Testing refused upstream connection..." << std::endl;

    UpstreamPool pool;
    const uint16_t unused_port = find_unused_port();
    const ProxyTarget target{"127.0.0.1", unused_port};
    const struct sockaddr_in address = loopback_address(unused_port);

    UpstreamPool::AcquireResult result =
        pool.acquire(target, reinterpret_cast<const struct sockaddr*>(&address),
                     static_cast<socklen_t>(sizeof(address)));

    // Either connect() fails immediately, or it reports the failure once the
    // socket becomes writable; both must be surfaced and nothing may be pooled.
    if (result.ok) {
        assert(wait_for_connect_result(result.fd.get(), 2000) != 0 &&
               "a refused connection must report a socket error");
    } else {
        assert(!result.error.empty() && "acquire must explain the failure");
    }
    assert(pool.idle_count() == 0);

    std::cout << "  PASSED" << std::endl;
}

void test_no_descriptor_leak() {
    std::cout << "Testing descriptor accounting..." << std::endl;

    // The listener closes accepted sockets immediately so that its own
    // descriptor usage stays flat and cannot mask a pool leak.
    TestListener listener(false);
    const ProxyTarget target{"127.0.0.1", listener.port()};
    const struct sockaddr_in address = loopback_address(listener.port());
    const auto* raw_address = reinterpret_cast<const struct sockaddr*>(&address);
    const auto address_length = static_cast<socklen_t>(sizeof(address));

    const size_t baseline = count_open_fds();

    {
        UpstreamPool pool;
        pool.set_max_idle_per_target(4);

        for (int round = 0; round < 3; ++round) {
            std::vector<UpstreamPool::AcquireResult> held;
            for (int i = 0; i < 4; ++i) {
                UpstreamPool::AcquireResult result =
                    pool.acquire(target, raw_address, address_length);
                assert(result.ok);
                held.push_back(std::move(result));
            }
            assert(pool.idle_count() == 0);

            for (UpstreamPool::AcquireResult& result : held) {
                pool.release(target, std::move(result.fd));
            }
            assert(pool.idle_count() == 4);

            // Churn: the next round reuses the pooled set instead of growing it.
            pool.sweep_idle();
        }

        pool.close_all();
    }

    // Wait for the listener to finish closing its side of every connection;
    // otherwise a descriptor still in its accept path could look like a leak.
    for (int attempt = 0; attempt < 200 && !listener.drained(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(listener.drained() && "listener must settle");

    // The pool is destroyed: every descriptor it owned must be closed again.
    assert(count_open_fds() == baseline && "pool must not leak sockets");

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "=== Upstream Pool Tests ===" << std::endl;
    std::cout << std::endl;

    test_acquire_creates_connectable_socket();
    test_release_and_reuse();
    test_targets_are_isolated();
    test_pooling_can_be_disabled();
    test_capacity_limit_is_enforced();
    test_stale_connection_is_not_reused();
    test_close_all_releases_descriptors();
    test_connection_failure_is_reported();
    test_no_descriptor_leak();

    std::cout << std::endl;
    std::cout << "=== All Tests Passed ===" << std::endl;

    return 0;
}
