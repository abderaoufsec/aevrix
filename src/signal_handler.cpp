// =============================================================================
// Aevrix - Signal Handler Implementation
// =============================================================================
// This file implements the signal handler for graceful shutdown.
// =============================================================================

#include "aevrix/signal_handler.h"
#include "aevrix/logger.h"
#include <csignal>
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
// Unix/Linux signal handler
static void SignalHandlerFunc(int signal) {
    (void)signal;  // Suppress unused parameter warning
    if (g_signal_handler_ptr) {
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
#endif
}

} // namespace aevrix
