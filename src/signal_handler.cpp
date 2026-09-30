// =============================================================================
// Aevrix - Signal Handler Implementation
// =============================================================================
// This file implements the signal handler for graceful shutdown.
// =============================================================================

#include "aevrix/signal_handler.h"
#include "aevrix/logger.h"
#include <csignal>
#include <cstdint>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace aevrix {

// Global signal handler instance
SignalHandler g_signal_handler;

// Global pointer for signal handler (needed for C-style signal handlers)
static SignalHandler* g_signal_handler_ptr = nullptr;

#ifdef _WIN32
// Windows console control handler
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        if (g_signal_handler_ptr) {
            g_signal_handler_ptr->request_shutdown();
        }
        return TRUE;  // Handle the signal
    }
    return FALSE;  // Pass to default handler
}
#else
// Unix/Linux signal handler.
// Runs in signal context, so it may only touch async-signal-safe state: the
// two atomics below and write() (see request_reload()).
static void SignalHandlerFunc(int signal) {
    if (g_signal_handler_ptr == nullptr) {
        return;
    }
#ifdef SIGHUP
    if (signal == SIGHUP) {
        // Phase 24: configuration reload request. The event loop performs the
        // actual parse/validate/swap outside this signal context.
        g_signal_handler_ptr->request_reload();
        return;
    }
#endif
    if (signal == SIGINT || signal == SIGTERM) {
        g_signal_handler_ptr->request_shutdown();
    }
}
#endif

SignalHandler::SignalHandler()
    : shutdown_requested_(false)
    , shutdown_callback_(nullptr)
{
    g_signal_handler_ptr = this;
    setup_signals();
    aevrix::g_logger.info("Signal handler initialized");
}

SignalHandler::~SignalHandler() {
    cleanup_signals();
    g_signal_handler_ptr = nullptr;
}

void SignalHandler::set_shutdown_callback(ShutdownCallback callback) {
    shutdown_callback_ = callback;
}

bool SignalHandler::shutdown_requested() const {
    return shutdown_requested_.load(std::memory_order_acquire);
}

void SignalHandler::request_shutdown() {
    bool expected = false;
    if (shutdown_requested_.compare_exchange_strong(expected, true, 
                                                  std::memory_order_acq_rel)) {
        aevrix::g_logger.info("Shutdown signal received");
        if (shutdown_callback_) {
            shutdown_callback_();
        }
    }
}

// =============================================================================
// Configuration Reload (Phase 24)
// =============================================================================

bool SignalHandler::reload_requested() const {
    return reload_requested_.load(std::memory_order_acquire);
}

bool SignalHandler::consume_reload_request() {
    bool expected = true;
    return reload_requested_.compare_exchange_strong(expected, false,
                                                     std::memory_order_acq_rel);
}

void SignalHandler::request_reload() {
    // Async-signal-safe by construction: atomic store + write(). Everything
    // expensive (file I/O, parse, validate, swap) happens on the event loop.
    reload_requested_.store(true, std::memory_order_release);

    const int fd = reload_notify_fd_.load(std::memory_order_acquire);
    if (fd >= 0) {
#ifdef _WIN32
        // No eventfd on Windows: the flag above is polled by the loop instead.
        (void)fd;
#else
        const uint64_t wake = 1;
        const ssize_t ignored = ::write(fd, &wake, sizeof(wake));
        (void)ignored;  // Best effort: a full counter still leaves the flag set
#endif
    }
}

void SignalHandler::set_reload_notify_fd(int fd) {
    reload_notify_fd_.store(fd, std::memory_order_release);
}

int SignalHandler::reload_notify_fd() const {
    return reload_notify_fd_.load(std::memory_order_acquire);
}

void SignalHandler::setup_signals() {
#ifdef _WIN32
    // Windows: Set console control handler for Ctrl+C
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        aevrix::g_logger.error("Failed to set console control handler");
    }
#else
    // Linux/Unix: Set up signal handlers for SIGINT and SIGTERM
    struct sigaction sa;
    sa.sa_handler = SignalHandlerFunc;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, nullptr) < 0) {
        aevrix::g_logger.error("Failed to set SIGINT handler");
    }

    if (sigaction(SIGTERM, &sa, nullptr) < 0) {
        aevrix::g_logger.error("Failed to set SIGTERM handler");
    }

    // Phase 24: SIGHUP asks for a configuration reload (nginx-style).
    // SA_RESTART keeps unrelated blocking calls from failing with EINTR.
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGHUP, &sa, nullptr) < 0) {
        aevrix::g_logger.error("Failed to set SIGHUP handler");
    }
#endif
}

void SignalHandler::cleanup_signals() {
#ifdef _WIN32
    // Windows: Remove console control handler
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
#else
    // Linux/Unix: Reset signal handlers to default
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
#endif
}

} // namespace aevrix
