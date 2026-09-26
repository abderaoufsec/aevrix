// =============================================================================
// Aevrix - Signal Handler
// =============================================================================
// This file implements signal handling for graceful shutdown.
// In Phase 15, we add graceful shutdown to make shutdown safe and observable.
//
// The signal handler implements:
// - SIGINT (Ctrl+C) handling on Linux
// - SIGTERM handling on Linux
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
    ShutdownCallback shutdown_callback_;
};

/**
 * @brief Global signal handler instance
 */
extern SignalHandler g_signal_handler;

} // namespace aevrix
