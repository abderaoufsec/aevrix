// =============================================================================
// Aevrix - Signal Handler
// =============================================================================
// This file implements signal handling for graceful shutdown.
// In Phase 15, we add graceful shutdown to make shutdown safe and observable.
//
// The signal handler implements:
// - SIGINT (Ctrl+C) handling on Linux
// - SIGTERM handling on Linux
// - SIGHUP handling on Linux (Phase 24: configuration reload)
// - Ctrl+C handling on Windows (SetConsoleCtrlHandler)
//
// Shutdown sequence:
// 1. Stop accepting connections
// 2. Finish safe work
// 3. Close connections
// 4. Stop workers
// 5. Flush logs
// 6. Exit
//
// Previous Phases:
// - Phase 14: Structured logging
//
// Future Phases Will Add:
// - Phase 16: Test pyramid
// =============================================================================

#pragma once

#include <atomic>
#include <functional>

namespace aevrix {

/**
 * @brief Signal handler for graceful shutdown
 * 
 * Provides cross-platform signal handling for SIGINT and SIGTERM on Linux,
// and Ctrl+C on Windows. Uses atomic flag for thread-safe shutdown signaling.
 */
class SignalHandler {
public:
    /**
     * @brief Shutdown callback type
     */
    using ShutdownCallback = std::function<void()>;

    /**
     * @brief Constructor
     */
    SignalHandler();

    /**
     * @brief Destructor
     */
    ~SignalHandler();

    /**
     * @brief Set the shutdown callback
     * 
     * @param callback Function to call when shutdown signal is received
     */
    void set_shutdown_callback(ShutdownCallback callback);

    /**
     * @brief Check if shutdown was requested
     * 
     * @return true if shutdown was requested, false otherwise
     */
    bool shutdown_requested() const;

    /**
     * @brief Request shutdown programmatically
     */
    void request_shutdown();

    // =========================================================================
    // Configuration Reload (Phase 24)
    // =========================================================================

    /**
     * @brief Check whether a configuration reload has been requested
     *
     * @return true when a reload flag is pending (does not clear it)
     */
    bool reload_requested() const;

    /**
     * @brief Check for a pending reload and clear the flag atomically
     *
     * Lets the event loop consume exactly one reload per SIGHUP burst.
     *
     * @return true if a reload was pending (and is now cleared)
     */
    bool consume_reload_request();

    /**
     * @brief Signal that a configuration reload was requested
     *
     * Async-signal-safe: it only flips an atomic flag and writes a wake-up byte
     * to the descriptor registered with set_reload_notify_fd(). No logging and
     * no allocation, because it runs inside the signal handler.
     */
    void request_reload();

    /**
     * @brief Register a descriptor to write to when a reload is requested
     *
     * On Linux this is an eventfd already registered with the epoll loop, so the
     * event loop wakes up immediately instead of waiting for its next tick.
     *
     * @param fd Descriptor to write 8 bytes to, or -1 to disable
     */
    void set_reload_notify_fd(int fd);

    /**
     * @brief Descriptor currently registered for reload wake-ups (-1 if none)
     */
    int reload_notify_fd() const;

private:
    /**
     * @brief Platform-specific signal setup
     */
    void setup_signals();

    /**
     * @brief Platform-specific signal cleanup
     */
    void cleanup_signals();

    std::atomic<bool> shutdown_requested_;
    std::atomic<bool> reload_requested_{false};
    std::atomic<int> reload_notify_fd_{-1};
    ShutdownCallback shutdown_callback_;
};

/**
 * @brief Global signal handler instance
 */
extern SignalHandler g_signal_handler;

} // namespace aevrix
