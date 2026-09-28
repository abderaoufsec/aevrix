// =============================================================================
// Aevrix - Filesystem Worker Function (Stage 5 - WorkerPool Integration)
// =============================================================================
// This file implements the worker function for blocking filesystem operations.
// All blocking filesystem work happens here, keeping the event loop responsive.
// =============================================================================

#include "aevrix/filesystem_worker.h"
#include "aevrix/worker_task.h"
#include "aevrix/static_file_server.h"
#include "aevrix/logger.h"
#include <memory>

namespace aevrix {

/**
 * @brief Worker function for filesystem operations
 * 
 * This function is executed by worker threads. It performs all blocking
 * filesystem work and returns an owned result that can be safely passed
 * back to the event loop.
 * 
 * @param task The worker task containing request information
 * @return WorkerResult The result of the filesystem operation
 */
WorkerResult execute_filesystem_task(const WorkerTask& task) {
    try {
        // Create a temporary StaticFileServer for this task
        // Note: This is thread-safe because each task gets its own instance
        aevrix::StaticFileServer file_server(task.document_root);
        
        // Perform the blocking filesystem operation
        auto [content, mime_type, status_code] = file_server.serve_file(task.request_target);
        
        // For HEAD requests, we don't need the content
        if (task.is_head_request && status_code == 200) {
            // Clear content for HEAD request
            content.clear();
        }
        
        if (status_code == 200) {
            // Success
            return WorkerResult(task.connection_id, std::move(content), 
                              std::move(mime_type), status_code);
        } else {
            // Error (404, 403, 500)
            std::string error_message;
            if (status_code == 404) {
                error_message = "File not found";
            } else if (status_code == 403) {
                error_message = "Forbidden";
            } else {
                error_message = "Internal server error";
            }
            return WorkerResult(task.connection_id, status_code, std::move(error_message));
        }
        
    } catch (const std::exception& e) {
        // Exception during filesystem operation
        std::cerr << "Filesystem task exception: " << e.what() << std::endl;
        return WorkerResult(task.connection_id, 500, 
                          std::string("Internal server error: ") + e.what());
    } catch (...) {
        // Unknown exception
        std::cerr << "Unknown filesystem task exception" << std::endl;
        return WorkerResult(task.connection_id, 500, "Internal server error");
    }
}

} // namespace aevrix
