// =============================================================================
// Aevrix - Event Loop Implementation
// =============================================================================
// This file implements the event loop with epoll on Linux and select fallback
// on Windows for development purposes.
// =============================================================================

#include "aevrix/event_loop.h"
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
    
    std::cout << "Event loop initialized with epoll (Linux)\n";
    
#elif defined(AEVRIX_USE_SELECT)
    // Initialize select-based event loop for Windows/Unix
    FD_ZERO(&read_fds_);
    FD_ZERO(&write_fds_);
    FD_ZERO(&error_fds_);
    max_fd_ = 0;
    
    std::cout << "Event loop initialized with select (Windows/Unix fallback)\n";
#endif
}

EventLoop::~EventLoop() {
#ifdef AEVRIX_USE_EPOLL
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
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
            close(epoll_fd_);
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
    (void)events;  // Unused in select implementation
#ifdef AEVRIX_USE_EPOLL
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    
    // Store callback (in a real implementation, we'd use a map)
    // For Phase 8, we'll use a simple approach
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        std::cerr << "Failed to add fd to epoll: " << strerror(errno) << "\n";
        return false;
    }
    
    std::cout << "Added fd " << fd << " to epoll with events: " << events << "\n";
    return true;
    
#elif defined(AEVRIX_USE_SELECT)
    // Add to select-based tracking
    fd_callbacks_.push_back({fd, callback});
    
    if (fd > max_fd_) {
        max_fd_ = fd;
    }
    
    std::cout << "Added fd " << fd << " to select event loop\n";
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
        std::cerr << "Failed to modify fd in epoll: " << strerror(errno) << "\n";
        return false;
    }
    
    std::cout << "Modified fd " << fd << " in epoll with events: " << events << "\n";
    return true;
    
#elif defined(AEVRIX_USE_SELECT)
    // For select, we don't need to modify - we check on each iteration
    std::cout << "Modified fd " << fd << " in select event loop\n";
    return true;
#endif
}

bool EventLoop::remove_fd(int fd) {
#ifdef AEVRIX_USE_EPOLL
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        std::cerr << "Failed to remove fd from epoll: " << strerror(errno) << "\n";
        return false;
    }
    
    std::cout << "Removed fd " << fd << " from epoll\n";
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
    
    std::cout << "Removed fd " << fd << " from select event loop\n";
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
                // Interrupted by signal, continue
                std::cout << "epoll_wait interrupted by signal, continuing\n";
                continue;
            }
            std::cerr << "epoll_wait failed: " << strerror(errno) << "\n";
            return false;
        }
        
        if (nfds == 0) {
            // Timeout occurred
            std::cout << "epoll_wait timeout\n";
            continue;
        }
        
        std::cout << "epoll_wait returned " << nfds << " events\n";
        
        // Process each event
        for (int i = 0; i < nfds; ++i) {
            int fd = events_[i].data.fd;
            uint32_t revents = events_[i].events;
            
            // Determine event type
            if (revents & EPOLLIN) {
                std::cout << "fd " << fd << " is readable\n";
                // In a real implementation, we'd invoke the callback here
            }
            if (revents & EPOLLOUT) {
                std::cout << "fd " << fd << " is writable\n";
            }
            if (revents & EPOLLERR) {
                std::cout << "fd " << fd << " has error\n";
            }
            if (revents & EPOLLHUP) {
                std::cout << "fd " << fd << " hangup\n";
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
                std::cout << "select interrupted, continuing\n";
                continue;
            }
            std::cerr << "select failed: " << error << "\n";
#else
            if (errno == EINTR) {
                std::cout << "select interrupted by signal, continuing\n";
                continue;
            }
            std::cerr << "select failed: " << strerror(errno) << "\n";
#endif
            return false;
        }
        
        if (result == 0) {
            std::cout << "select timeout\n";
            continue;
        }
        
        std::cout << "select returned " << result << " ready descriptors\n";
        
        // Check each registered fd
        for (const auto& [fd, callback] : fd_callbacks_) {
            if (FD_ISSET(fd, &temp_read_fds)) {
                std::cout << "fd " << fd << " is readable\n";
                callback(fd, EventType::Readable);
            }
            if (FD_ISSET(fd, &temp_write_fds)) {
                std::cout << "fd " << fd << " is writable\n";
                callback(fd, EventType::Writable);
            }
            if (FD_ISSET(fd, &temp_error_fds)) {
                std::cout << "fd " << fd << " has error\n";
                callback(fd, EventType::Error);
            }
        }
#endif
    }
    
    return true;
}

void EventLoop::stop() {
    running_ = false;
    std::cout << "Event loop stop requested\n";
}

} // namespace aevrix
