// =============================================================================
// Aevrix - Filesystem Worker Function (Stage 5 - WorkerPool Integration)
// =============================================================================
// This file declares the worker function for blocking filesystem operations.
// =============================================================================

#pragma once

#include "aevrix/worker_task.h"

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
WorkerResult execute_filesystem_task(const WorkerTask& task);

} // namespace aevrix
