// =============================================================================
// Aevrix - Worker Pool
// =============================================================================
// This file implements a bounded worker pool for blocking operations.
// In Phase 11, we add a worker pool to keep blocking filesystem/application
// work out of the event loop, ensuring the event loop remains responsive.
//
// The worker pool includes:
// - Job abstraction with execute method
// - Bounded queue for job management
// - Worker threads that process jobs
// - Clean shutdown without detached threads
// - No unbounded task creation
//
// Previous Phases:
// - Phase 10: Timeouts and resource limits
//
// Future Phases Will Add:
// - Phase 12: Router
// =============================================================================

#pragma once

#include <functional>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include <future>
#include <stdexcept>
#include <iostream>

namespace aevrix {

/**
 * @brief Job result type
 * 
 * Represents the result of a job execution. Jobs can return any type,
// and this template class wraps the result for the worker pool.
 * 
 * @tparam T The type of the job result
 */
template<typename T>
class JobResult {
public:
    /**
     * @brief Construct a successful result
     * 
     * @param value The result value
     */
    explicit JobResult(T value) : value_(std::move(value)), has_value_(true) {}

    /**
     * @brief Construct a failed result
     * 
     * @param error The error message
     */
    explicit JobResult(const std::string& error) : error_(error), has_value_(false) {}

    /**
     * @brief Check if the result has a value
     * 
     * @return true if successful, false if failed
     */
    bool has_value() const { return has_value_; }

    /**
     * @brief Get the result value
     * 
     * @return const T& The result value
     * @throws std::runtime_error if result has no value
     */
    const T& value() const {
        if (!has_value_) {
            throw std::runtime_error("Job failed: " + error_);
        }
        return value_;
    }

    /**
     * @brief Get the error message
     * 
     * @return const std::string& The error message
     */
    const std::string& error() const { return error_; }

private:
    T value_;
    std::string error_;
    bool has_value_;
};

/**
 * @brief Job abstraction
 * 
 * A job represents a unit of work that can be executed by a worker thread.
// Jobs are callable objects that return a result.
 * 
 * This is a type alias for convenience - any callable that takes no arguments
// and returns a value can be used as a job.
 * 
 * @tparam T The type of the job result
 */
template<typename T>
using Job = std::function<T()>;

/**
 * @brief Bounded queue for job management
 * 
 * A thread-safe bounded queue that enforces a maximum size to prevent
// unbounded task creation. The queue blocks on push when full and blocks
// on pop when empty.
 * 
 * This ensures the worker pool does not create unbounded numbers of jobs,
// preventing memory exhaustion and ensuring backpressure.
 * 
 * @tparam T The type of elements in the queue
 */
template<typename T>
class BoundedQueue {
public:
    /**
     * @brief Construct a bounded queue
     * 
     * @param max_size Maximum number of elements in the queue
     */
    explicit BoundedQueue(size_t max_size) : max_size_(max_size), shutdown_(false) {}

    /**
     * @brief Destructor
     * 
     * Signals shutdown and notifies all waiting threads
     */
    ~BoundedQueue() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    /**
     * @brief Push an element into the queue
     * 
     * Blocks if the queue is full. Returns false if the queue is shut down.
     * 
     * @param value The value to push
     * @return true if pushed successfully, false if shut down
     */
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait until there's space or shutdown
        not_full_.wait(lock, [this] { 
            return queue_.size() < max_size_ || shutdown_; 
        });
        
        if (shutdown_) {
            return false;
        }
        
        queue_.push(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    /**
     * @brief Pop an element from the queue
     * 
     * Blocks if the queue is empty. Returns false if the queue is shut down.
     * 
     * @param value Output parameter for the popped value
     * @return true if popped successfully, false if shut down
     */
    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait until there's an element or shutdown
        not_empty_.wait(lock, [this] { 
            return !queue_.empty() || shutdown_; 
        });
        
        if (shutdown_ && queue_.empty()) {
            return false;
        }
        
        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return true;
    }

    /**
     * @brief Get the current size of the queue
     * 
     * @return size_t Current queue size
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief Check if the queue is empty
     * 
     * @return true if empty, false otherwise
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Signal shutdown
     * 
     * Unblocks all waiting threads and prevents further pushes/pops
     */
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::queue<T> queue_;
    size_t max_size_;
    bool shutdown_;
};

/**
 * @brief Worker pool for blocking operations
 * 
 * A pool of worker threads that execute jobs from a bounded queue.
// This keeps blocking filesystem/application work out of the event loop,
// ensuring the event loop remains responsive.
 * 
 * The worker pool enforces:
// - Bounded queue (prevents unbounded task creation)
// - Clean shutdown (no detached threads)
// - No unbounded task creation
 * 
 * Jobs are submitted as callable objects and return futures for their results.
 */
class WorkerPool {
public:
    /**
     * @brief Construct a worker pool
     * 
     * @param num_workers Number of worker threads
     * @param max_queue_size Maximum number of jobs in the queue
     */
    WorkerPool(size_t num_workers, size_t max_queue_size)
        : queue_(max_queue_size)
        , shutdown_(false) {
        
        for (size_t i = 0; i < num_workers; ++i) {
            workers_.emplace_back(&WorkerPool::worker_loop, this);
        }
        
        std::cout << "Worker pool created with " << num_workers << " workers"
                  << " and queue size " << max_queue_size << "\n";
    }

    /**
     * @brief Destructor
     * 
     * Shuts down the worker pool and joins all worker threads
     */
    ~WorkerPool() {
        shutdown();
    }

    // Delete copy operations
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    /**
     * @brief Submit a job to the worker pool
     * 
     * Submits a job to be executed by a worker thread. Returns a future
     // that can be used to get the result.
     * 
     * @tparam T The return type of the job
     * @param job The job to execute
     * @return std::future<T> Future for the job result
     * @throws std::runtime_error if the pool is shut down
     */
    template<typename T>
    std::future<T> submit(Job<T> job) {
        auto promise = std::make_shared<std::promise<T>>();
        auto future = promise->get_future();
        
        // Wrap the job with the promise
        auto wrapped_job = [job = std::move(job), promise]() mutable {
            try {
                T result = job();
                promise->set_value(std::move(result));
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        };
        
        if (!queue_.push(std::move(wrapped_job))) {
            throw std::runtime_error("Worker pool is shut down");
        }
        
        return future;
    }

    /**
     * @brief Shutdown the worker pool
     * 
     * Signals shutdown, waits for all jobs to complete, and joins all threads.
     * Clean shutdown without detached threads.
     */
    void shutdown() {
        if (shutdown_.exchange(true)) {
            return;  // Already shut down
        }
        
        std::cout << "Shutting down worker pool...\n";
        
        // Signal queue shutdown
        queue_.shutdown();
        
        // Join all worker threads
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        workers_.clear();
        std::cout << "Worker pool shut down successfully\n";
    }

    /**
     * @brief Get the number of pending jobs
     * 
     * @return size_t Number of jobs in the queue
     */
    size_t pending_jobs() const {
        return queue_.size();
    }

    /**
     * @brief Check if the pool is shut down
     * 
     * @return true if shut down, false otherwise
     */
    bool is_shutdown() const {
        return shutdown_;
    }

private:
    /**
     * @brief Worker thread loop
     * 
     * Each worker thread continuously pops jobs from the queue and executes them.
     * The loop exits when the queue is shut down and empty.
     */
    void worker_loop() {
        while (true) {
            std::function<void()> job;
            
            if (!queue_.pop(job)) {
                // Queue is shut down and empty
                break;
            }
            
            // Execute the job
            job();
        }
    }

    BoundedQueue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    std::atomic<bool> shutdown_;
};

} // namespace aevrix
