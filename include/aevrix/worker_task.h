// =============================================================================
// Aevrix - Worker Task and Result Types (Stage 5 - WorkerPool Integration)
// =============================================================================
// This file defines the task and result types for WorkerPool integration.
// These types ensure safe ownership and prevent use-after-free when workers
// complete after a connection has been removed.
//
// Stage 5 adds:
// - WorkerTask: Immutable work item containing request information
// - WorkerResult: Owned result containing response data
// - Safe connection lifetime handling
// - Completion notification mechanism
// =============================================================================

#pragma once

#include <string>
#include <optional>
#include <cstdint>

namespace aevrix {

/**
 * @brief Worker task for filesystem operations
 * 
 * Contains immutable information needed to perform blocking filesystem work.
 * This is captured by value in worker tasks to prevent use-after-free.
 */
struct WorkerTask {
    uint64_t connection_id;      // Connection identifier for result routing
    std::string request_target; // The requested file path
    std::string document_root;  // Document root directory
    bool is_head_request;       // Whether this is a HEAD request (no body needed)
    
    WorkerTask(uint64_t id, std::string target, std::string root, bool head)
        : connection_id(id)
        , request_target(std::move(target))
        , document_root(std::move(root))
        , is_head_request(head) {}
};

/**
 * @brief Worker result from filesystem operations
 * 
 * Contains owned response data from worker completion.
 * The event loop looks up the connection by ID and applies the result
 * if the connection still exists.
 */
struct WorkerResult {
    uint64_t connection_id;      // Connection identifier for result routing
    std::string content;         // File content (empty on error or HEAD request)
    std::string mime_type;       // MIME type of the file
    int status_code;             // HTTP status code (200, 404, 403, 500)
    std::string error_message;   // Error message if status_code indicates error
    bool success;                // Whether the operation succeeded
    
    /**
     * @brief Construct a successful result
     */
    WorkerResult(uint64_t id, std::string content_data, std::string mime, int status)
        : connection_id(id)
        , content(std::move(content_data))
        , mime_type(std::move(mime))
        , status_code(status)
        , success(true) {}
    
    /**
     * @brief Construct an error result
     */
    WorkerResult(uint64_t id, int status, std::string error)
        : connection_id(id)
        , status_code(status)
        , error_message(std::move(error))
        , success(false) {}
};

} // namespace aevrix
