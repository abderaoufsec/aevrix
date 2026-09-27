// =============================================================================
// Aevrix - Event Loop Implementation
// =============================================================================
// This file implements the event loop with epoll on Linux and select fallback
// on Windows for development purposes.
// =============================================================================

#include "aevrix/event_loop.h"
#include "aevrix/signal_handler.h"
#include "aevrix/logger.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <algorithm>

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace aevrix {

EventLoop::EventLoop() : running_(false) {
#ifdef AEVRIX_USE_EPOLL
    // Create epoll instance for Linux
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create epoll instance: " + std::string(strerror(errno)));
    }
    
    // Allocate event array
    events_ = new struct epoll_event[MAX_EVENTS];
    
    aevrix::g_logger.info("Event loop initialized with epoll (Linux)");
    
#elif defined(AEVRIX_USE_SELECT)
    // Initialize select-based event loop for Windows/Unix
    FD_ZERO(&read_fds_);
    FD_ZERO(&write_fds_);
    FD_ZERO(&error_fds_);
    max_fd_ = 0;
    
    aevrix::g_logger.info("Event loop initialized with select (Windows/Unix fallback)");
#endif
}

EventLoop::~EventLoop() {
#ifdef AEVRIX_USE_EPOLL
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
    if (events_) {
        delete[] events_;
    }
#endif
}

EventLoop::EventLoop(EventLoop&& other) noexcept
    : running_(other.running_) {
#ifdef AEVRIX_USE_EPOLL
    epoll_fd_ = other.epoll_fd_;
    events_ = other.events_;
    other.epoll_fd_ = -1;
    other.events_ = nullptr;
#elif defined(AEVRIX_USE_SELECT)
    read_fds_ = other.read_fds_;
    write_fds_ = other.write_fds_;
    error_fds_ = other.error_fds_;
    max_fd_ = other.max_fd_;
    fd_callbacks_ = std::move(other.fd_callbacks_);
#endif
    other.running_ = false;
}

EventLoop& EventLoop::operator=(EventLoop&& other) noexcept {
    if (this != &other) {
        running_ = other.running_;
#ifdef AEVRIX_USE_EPOLL
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
        }
        if (events_) {
            delete[] events_;
        }
        epoll_fd_ = other.epoll_fd_;
        events_ = other.events_;
        other.epoll_fd_ = -1;
        other.events_ = nullptr;
#elif defined(AEVRIX_USE_SELECT)
        read_fds_ = other.read_fds_;
        write_fds_ = other.write_fds_;
        error_fds_ = other.error_fds_;
        max_fd_ = other.max_fd_;
        fd_callbacks_ = std::move(other.fd_callbacks_);
#endif
        other.running_ = false;
    }
    return *this;
}

bool EventLoop::add_fd(int fd, uint32_t events, EventCallback callback) {
#ifdef AEVRIX_USE_EPOLL
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    
    // Store callback for this fd
    fd_callbacks_[fd] = callback;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        aevrix::g_logger.error("Failed to add fd to epoll: " + std::string(strerror(errno)));
        return false;
    }
    
    aevrix::g_logger.debug("Added fd " + std::to_string(fd) + " to epoll with events: " + std::to_string(events));
    return true;
    
#elif defined(AEVRIX_USE_SELECT)
    // Add to select-based tracking
    (void)events;  // Select doesn't use event bitmask in add_fd
    fd_callbacks_.push_back({fd, callback});
    
    if (fd > max_fd_) {
        max_fd_ = fd;
    }
    
    aevrix::g_logger.debug("Added fd " + std::to_string(fd) + " to select event loop");
    return true;
#endif
}

bool EventLoop::modify_fd(int fd, uint32_t events) {
    (void)events;  // Unused in select implementation
#ifdef AEVRIX_USE_EPOLL
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        aevrix::g_logger.error("Failed to modify fd in epoll: " + std::string(strerror(errno)));
        return false;
    }
    
    aevrix::g_logger.debug("Modified fd " + std::to_string(fd) + " in epoll with events: " + std::to_string(events));
    return true;
    
#elif defined(AEVRIX_USE_SELECT)
    // For select, we don't need to modify - we check on each iteration
    aevrix::g_logger.debug("Modified fd " + std::to_string(fd) + " in select event loop");
    return true;
#endif
}

bool EventLoop::remove_fd(int fd) {
#ifdef AEVRIX_USE_EPOLL
    // First remove from epoll to prevent further events
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        // EBADF means fd is already closed or invalid, which is fine
        if (errno != EBADF) {
            aevrix::g_logger.error("Failed to remove fd from epoll: " + std::string(strerror(errno)));
        }
    }
    
    // Then remove the callback (after epoll removal to prevent new events)
    // Note: If remove_fd is called from within a callback, this is safe because
    // we make a copy of the callback before invoking it in the event loop
    fd_callbacks_.erase(fd);
    
    aevrix::g_logger.debug("Removed fd " + std::to_string(fd) + " from epoll");
    return true;
    
#elif defined(AEVRIX_USE_SELECT)
    // Remove from select-based tracking
    auto it = std::find_if(fd_callbacks_.begin(), fd_callbacks_.end(),
                          [fd](const auto& pair) { return pair.first == fd; });
    if (it != fd_callbacks_.end()) {
        fd_callbacks_.erase(it);
    }
    
    // Recalculate max_fd
    max_fd_ = 0;
    for (const auto& pair : fd_callbacks_) {
        if (pair.first > max_fd_) {
            max_fd_ = pair.first;
        }
    }
    
    aevrix::g_logger.debug("Removed fd " + std::to_string(fd) + " from select event loop");
    return true;
#endif
}

bool EventLoop::run(int timeout_ms) {
    running_ = true;
    
    while (running_) {
#ifdef AEVRIX_USE_EPOLL
        // Wait for events using epoll
        int nfds = epoll_wait(epoll_fd_, events_, MAX_EVENTS, timeout_ms);
        
        if (nfds < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, check for shutdown
                if (g_signal_handler.shutdown_requested()) {
                    aevrix::g_logger.info("Shutdown requested, stopping event loop");
                    running_ = false;
                    return false;
                }
                aevrix::g_logger.debug("epoll_wait interrupted by signal, continuing");
                continue;
            }
            aevrix::g_logger.error("epoll_wait failed: " + std::string(strerror(errno)));
            return false;
        }
        
        if (nfds == 0) {
            // Timeout occurred, check for shutdown
            if (g_signal_handler.shutdown_requested()) {
                aevrix::g_logger.info("Shutdown requested, stopping event loop");
                running_ = false;
                return false;
            }
            aevrix::g_logger.debug("epoll_wait timeout");
            continue;
        }
        
        aevrix::g_logger.debug("epoll_wait returned " + std::to_string(nfds) + " events");
        
        // Process each event
        for (int i = 0; i < nfds; ++i) {
            int fd = events_[i].data.fd;
            uint32_t revents = events_[i].events;
            
            // Find callback for this fd
            auto it = fd_callbacks_.find(fd);
            if (it == fd_callbacks_.end()) {
                aevrix::g_logger.warn("No callback registered for fd " + std::to_string(fd) + ", skipping event");
                continue;
            }
            
            // Check if callback is still valid (not null)
            if (!it->second) {
                aevrix::g_logger.warn("Callback is null for fd " + std::to_string(fd) + ", skipping event");
                fd_callbacks_.erase(it);
                continue;
            }
            
            // Make a copy of the callback to prevent use-after-free if the callback
            // calls remove_fd() and erases itself from the map during execution
            EventCallback callback_copy = it->second;
            
            // Determine event type and invoke callback
            if (revents & EPOLLIN) {
                aevrix::g_logger.debug("fd " + std::to_string(fd) + " is readable");
                callback_copy(fd, EventType::Readable);
            }
            if (revents & EPOLLOUT) {
                aevrix::g_logger.debug("fd " + std::to_string(fd) + " is writable");
                callback_copy(fd, EventType::Writable);
            }
            if (revents & EPOLLERR) {
                aevrix::g_logger.debug("fd " + std::to_string(fd) + " has error");
                callback_copy(fd, EventType::Error);
            }
            if (revents & EPOLLHUP) {
                aevrix::g_logger.debug("fd " + std::to_string(fd) + " hangup");
                callback_copy(fd, EventType::Hangup);
            }
            if (revents & EPOLLRDHUP) {
                aevrix::g_logger.debug("fd " + std::to_string(fd) + " peer closed write end");
                callback_copy(fd, EventType::Hangup);
            }
        }
        
#elif defined(AEVRIX_USE_SELECT)
        // Use select for Windows/Unix fallback
        fd_set temp_read_fds = read_fds_;
        fd_set temp_write_fds = write_fds_;
        fd_set temp_error_fds = error_fds_;
        
        struct timeval tv;
        if (timeout_ms >= 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
        }
        
        int result = select(max_fd_ + 1, &temp_read_fds, &temp_write_fds, &temp_error_fds,
                           (timeout_ms >= 0) ? &tv : nullptr);
        
        if (result < 0) {
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAEINTR) {
                if (g_signal_handler.shutdown_requested()) {
                    aevrix::g_logger.info("Shutdown requested, stopping event loop");
                    running_ = false;
                    return false;
                }
                aevrix::g_logger.debug("select interrupted, continuing");
                continue;
            }
            aevrix::g_logger.error("select failed: " + std::to_string(error));
#else
            if (errno == EINTR) {
                if (g_signal_handler.shutdown_requested()) {
                    aevrix::g_logger.info("Shutdown requested, stopping event loop");
                    running_ = false;
                    return false;
                }
                aevrix::g_logger.debug("select interrupted by signal, continuing");
                continue;
            }
            aevrix::g_logger.error("select failed: " + std::string(strerror(errno)));
#endif
            return false;
        }
        
        if (result == 0) {
            if (g_signal_handler.shutdown_requested()) {
                aevrix::g_logger.info("Shutdown requested, stopping event loop");
                running_ = false;
                return false;
            }
            aevrix::g_logger.debug("select timeout");
            continue;
        }
        
        aevrix::g_logger.debug("select returned " + std::to_string(result) + " ready descriptors");
        
        // Check each registered fd
        for (const auto& [fd, callback] : fd_callbacks_) {
            if (FD_ISSET(fd, &temp_read_fds)) {
                aevrix::g_logger.debug("fd " + std::to_string(fd) + " is readable");
                callback(fd, EventType::Readable);
            }
            if (FD_ISSET(fd, &temp_write_fds)) {
                aevrix::g_logger.debug("fd " + std::to_string(fd) + " is writable");
                callback(fd, EventType::Writable);
            }
            if (FD_ISSET(fd, &temp_error_fds)) {
                aevrix::g_logger.debug("fd " + std::to_string(fd) + " has error");
                callback(fd, EventType::Error);
            }
        }
#endif
    }
    
    return true;
}

bool EventLoop::shutdown_requested() const {
    return g_signal_handler.shutdown_requested();
}

void EventLoop::stop() {
    running_ = false;
    aevrix::g_logger.info("Event loop stop requested");
}

} // namespace aevrix
