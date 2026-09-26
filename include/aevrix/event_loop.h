// =============================================================================
// Aevrix - Event Loop Abstraction
// =============================================================================
// This file provides an abstraction for the event loop, allowing the server
// to handle multiple connections efficiently without one thread per connection.
//
// Phase 8 moves from blocking I/O to an event-driven runtime using epoll on Linux
// and a select-based fallback on Windows for development purposes.
//
// Phase 15 adds graceful shutdown support to make shutdown safe and observable.
//
// Architecture:
// epoll_wait/select
//     │
//     ├── listener → accept loop
//     │
//     ├── readable → connection.on_readable()
//     │
//     ├── writable → connection.on_writable()
//     │
//     └── error → connection.close()
//
// The event loop handles:
// - listen fd (accept new connections)
// - connection readable (read data)
// - connection writable (write data)
// - hangup (connection closed by peer)
// - error (connection error)
//
// Important: With non-blocking sockets, we must handle:
// - EAGAIN (resource temporarily unavailable)
// - EWOULDBLOCK (operation would block)
// - EINTR (interrupted system call)
//
// These are not fatal errors and should be handled gracefully.
// =============================================================================

#pragma once

#include <functional>
#include <memory>
#include <cstdint>

#ifdef __linux__
#include <sys/epoll.h>
#define AEVRIX_USE_EPOLL
#elif defined(_WIN32)
#include <winsock2.h>
#include <vector>
#define AEVRIX_USE_SELECT
#else
#include <sys/select.h>
#define AEVRIX_USE_SELECT
#endif

namespace aevrix {

/**
 * @brief Event types for the event loop
 */
enum class EventType {
    Readable,   // File descriptor is readable
    Writable,   // File descriptor is writable
    Error,      // File descriptor has an error
    Hangup      // File descriptor was closed by peer
};

/**
 * @brief Callback function type for event handling
 * 
 * @param fd The file descriptor that triggered the event
 * @param event The type of event that occurred
 */
using EventCallback = std::function<void(int fd, EventType event)>;

/**
 * @brief Event loop abstraction
 * 
 * Provides a cross-platform event loop implementation:
// - Linux: Uses epoll for high-performance event handling
// - Windows: Uses select as a fallback for development
// - Other Unix: Uses select as a fallback
// 
// The event loop allows the server to handle thousands of connections
// without one thread per connection, following the Reactor pattern.
 */
class EventLoop {
public:
    /**
     * @brief Construct an event loop
     * 
     * @throws std::runtime_error if event loop initialization fails
     */
    EventLoop();

    /**
     * @brief Destructor
     */
    ~EventLoop();

    // Delete copy operations (event loop is not copyable)
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Allow move operations
    EventLoop(EventLoop&&) noexcept;
    EventLoop& operator=(EventLoop&&) noexcept;

    /**
     * @brief Add a file descriptor to the event loop
     * 
     * Registers a file descriptor for event monitoring. The callback
     * will be invoked when the specified events occur on the fd.
     * 
     * @param fd The file descriptor to monitor
     * @param events Bitmask of events to monitor (readable, writable, etc.)
     * @param callback The callback function to invoke when events occur
     * @return true if registration succeeded, false otherwise
     */
    bool add_fd(int fd, uint32_t events, EventCallback callback);

    /**
     * @brief Modify the events being monitored for a file descriptor
     * 
     * @param fd The file descriptor to modify
     * @param events New bitmask of events to monitor
     * @return true if modification succeeded, false otherwise
     */
    bool modify_fd(int fd, uint32_t events);

    /**
     * @brief Remove a file descriptor from the event loop
     * 
     * @param fd The file descriptor to remove
     * @return true if removal succeeded, false otherwise
     */
    bool remove_fd(int fd);

    /**
     * @brief Run the event loop
     * 
     * Blocks until events occur, then invokes the appropriate callbacks.
     * This function runs indefinitely until stop() is called.
     * 
     * @param timeout_ms Timeout in milliseconds (or -1 for infinite wait)
     * @return true if the loop is still running, false if stopped
     */
    bool run(int timeout_ms = -1);

    /**
     * @brief Stop the event loop
     * 
     * Causes run() to return on the next iteration.
     */
    void stop();

    /**
     * @brief Check if the event loop is running
     * 
     * @return true if the event loop is running, false otherwise
     */
    bool is_running() const { return running_; }

    // =========================================================================
    // Graceful Shutdown (Phase 15)
    // =========================================================================

    /**
     * @brief Check if shutdown was requested
     * 
     * @return true if shutdown was requested, false otherwise
     */
    bool shutdown_requested() const;

private:
#ifdef AEVRIX_USE_EPOLL
    // Linux-specific epoll implementation
    int epoll_fd_;  // epoll file descriptor
    struct epoll_event* events_;  // Array to store returned events
    static constexpr size_t MAX_EVENTS = 1024;  // Maximum events per epoll_wait
#elif defined(AEVRIX_USE_SELECT)
    // Windows/Unix select implementation
    fd_set read_fds_;
    fd_set write_fds_;
    fd_set error_fds_;
    int max_fd_;
    std::vector<std::pair<int, EventCallback>> fd_callbacks_;
#endif

    bool running_;  // Whether the event loop is running
};

} // namespace aevrix
