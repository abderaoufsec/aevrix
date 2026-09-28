// =============================================================================
// Aevrix - Worker Completion Handler Implementation
// =============================================================================

#include "aevrix/worker_completion_handler.h"
#include <iostream>
#include <cstring>
#include <cerrno>

namespace aevrix {

WorkerCompletionHandler::WorkerCompletionHandler() {
#ifdef __linux__
    // Create eventfd for completion notification
    event_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd_ < 0) {
        std::cerr << "Failed to create eventfd: " << strerror(errno) << std::endl;
        event_fd_ = -1;
    } else {
        std::cout << "Worker completion handler created with eventfd: " << event_fd_ << std::endl;
    }
#else
    std::cout << "Worker completion handler created (no eventfd on this platform)" << std::endl;
#endif
}

WorkerCompletionHandler::~WorkerCompletionHandler() {
#ifdef __linux__
    if (event_fd_ >= 0) {
        close(event_fd_);
        std::cout << "Worker completion handler destroyed, eventfd closed" << std::endl;
    }
#endif
}

void WorkerCompletionHandler::enqueue_result(WorkerResult result) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        result_queue_.push(std::move(result));
    }
    
#ifdef __linux__
    // Notify event loop via eventfd
    if (event_fd_ >= 0) {
        uint64_t value = 1;
        ssize_t written = write(event_fd_, &value, sizeof(value));
        if (written != sizeof(value)) {
            std::cerr << "Failed to write to eventfd: " << strerror(errno) << std::endl;
        }
    }
#endif
}

std::optional<WorkerResult> WorkerCompletionHandler::dequeue_result() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (result_queue_.empty()) {
        return std::nullopt;
    }
    
    WorkerResult result = std::move(result_queue_.front());
    result_queue_.pop();
    return result;
}

void WorkerCompletionHandler::clear_event() {
#ifdef __linux__
    if (event_fd_ >= 0) {
        uint64_t value;
        ssize_t read_bytes = read(event_fd_, &value, sizeof(value));
        if (read_bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "Failed to read from eventfd: " << strerror(errno) << std::endl;
        }
    }
#endif
}

} // namespace aevrix
