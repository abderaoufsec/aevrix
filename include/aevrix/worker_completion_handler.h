// =============================================================================
// Aevrix - Worker Completion Handler (Stage 5 - WorkerPool Integration)
// =============================================================================
// This file handles worker completion notifications using eventfd on Linux.
// When a worker completes a task, it notifies the event loop via eventfd,
// allowing the event loop to process the result without blocking.
//
// Stage 5 adds:
// - eventfd-based completion notification (Linux)
// - Thread-safe result queue
// - Integration with event loop
// =============================================================================

#pragma once

#include <queue>
#include <mutex>
#include <memory>

#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstdint>
#endif

#include "aevrix/worker_task.h"

namespace aevrix {

/**
 * @brief Worker completion handler
 * 
 * Manages worker completion notifications and result delivery.
 * Uses eventfd on Linux to wake the event loop when results are available.
 */
class WorkerCompletionHandler {
public:
    /**
     * @brief Construct a completion handler
     * 
     * Creates an eventfd for Linux notification.
     */
    WorkerCompletionHandler();
    
    /**
     * @brief Destructor
     * 
     * Closes the eventfd.
     */
    ~WorkerCompletionHandler();
    
    // Delete copy operations
    WorkerCompletionHandler(const WorkerCompletionHandler&) = delete;
    WorkerCompletionHandler& operator=(const WorkerCompletionHandler&) = delete;
    
    /**
     * @brief Enqueue a worker result
     * 
     * Thread-safe. Notifies the event loop via eventfd.
     * 
     * @param result The worker result to enqueue
     */
    void enqueue_result(WorkerResult result);
    
    /**
     * @brief Dequeue a worker result
     * 
     * Called from the event loop thread.
     * 
     * @return std::optional<WorkerResult> A result if available, empty otherwise
     */
    std::optional<WorkerResult> dequeue_result();
    
    /**
     * @brief Get the eventfd file descriptor
     * 
     * @return int The eventfd, or -1 if not available
     */
    int event_fd() const { 
#ifdef __linux__
        return event_fd_; 
#else
        return -1;
#endif
    }
    
    /**
     * @brief Clear the eventfd
     * 
     * Reads the eventfd to clear the notification.
     */
    void clear_event();
    
private:
    std::queue<WorkerResult> result_queue_;
    std::mutex queue_mutex_;
    
#ifdef __linux__
    int event_fd_ = -1;  // eventfd for completion notification
#endif
};

} // namespace aevrix
