// =============================================================================
// Aevrix - Static File Server
// =============================================================================
// This file implements the static file server for serving files from a document
// root directory. It provides secure path validation, MIME type detection,
// and file serving capabilities with protection against path traversal attacks.
//
// Security Features:
// - Path normalization to prevent directory traversal
// - Validation that resolved path stays within document root
// - Support for encoded path segments (URL encoding)
// - MIME type detection based on file extension
// - Protection against absolute paths and relative navigation
//
// Current Implementation (Phase 6):
// - Document root configuration
// - Path normalization and validation
// - MIME type detection for common file types
// - File serving with proper HTTP responses
// - 404 Not Found for non-existent files
// - 403 Forbidden for directory access attempts
//
// Future Enhancements:
// - Range request support (206 Partial Content)
// - ETag generation for conditional requests
// - Last-Modified header support
// - Custom MIME type configuration
// - Directory listing support (optional)
// =============================================================================

#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <fstream>
#include <filesystem>

namespace aevrix {

/**
 * @brief Static file server for serving files from a document root
 * 
 * The StaticFileServer class provides secure file serving capabilities with:
 * - Path validation to prevent directory traversal attacks
 * - MIME type detection based on file extensions
 * - File reading and response generation
 * - Security checks for path normalization
 * 
 * Security is paramount - the server must never serve files outside the
// configured document root directory.
 */
class StaticFileServer {
public:
    /**
     * @brief Construct a static file server with a document root
     * 
     * @param document_root The root directory from which to serve files
     * @throws std::runtime_error if document_root is not a valid directory
     */
    explicit StaticFileServer(const std::string& document_root);

    /**
     * @brief Destructor
     */
    ~StaticFileServer() = default;

    /**
     * @brief Serve a file based on the request target path
     * 
     * This function:
     * 1. Normalizes the request target path
     * 2. Validates the path stays within document root
     * 3. Detects MIME type based on file extension
     * 4. Reads the file content
     * 5. Returns appropriate HTTP response
     * 
     * @param request_target The path from the HTTP request (e.g., "/index.html")
     * @return std::string The file content, or empty string on error
     * @return std::string The MIME type of the file
     * @return int HTTP status code (200, 404, or 403)
     */
    std::tuple<std::string, std::string, int> serve_file(const std::string& request_target);

    /**
     * @brief Get the document root directory
     * 
     * @return const std::string& The document root path
     */
    const std::string& document_root() const { return document_root_; }

    /**
     * @brief Check if a path is valid and safe
     * 
     * @param request_target The path to validate
     * @return true if the path is safe, false otherwise
     */
    bool is_path_safe(const std::string& request_target) const;

private:
    /**
     * @brief Normalize a request target path
     * 
     * Converts URL-encoded characters, removes redundant separators,
     * and resolves relative path components. This is critical for
     * security to prevent path traversal attacks.
     * 
     * @param path The path to normalize
     * @return std::string The normalized path
     */
    std::string normalize_path(const std::string& path) const;

    /**
     * @brief Decode URL-encoded characters in a path
     * 
     * Converts %XX sequences to their corresponding characters.
     * This is necessary because attackers may use URL encoding
     * to bypass path validation.
     * 
     * @param encoded The URL-encoded string
     * @return std::string The decoded string
     */
    std::string url_decode(const std::string& encoded) const;

    /**
     * @brief Resolve a request target to an absolute file path
     * 
     * Combines the document root with the normalized request target
     * to produce an absolute file system path.
     * 
     * @param request_target The normalized request target
     * @return std::filesystem::path The resolved absolute path
     */
    std::filesystem::path resolve_path(const std::string& request_target) const;

    /**
     * @brief Validate that a path stays within the document root
     * 
     * This is the critical security check. It ensures that after
     * normalization and resolution, the path does not escape the
     * document root directory.
     * 
     * @param resolved_path The path to validate
     * @return true if the path is within document root, false otherwise
     */
    bool validate_path(const std::filesystem::path& resolved_path) const;

    /**
     * @brief Detect MIME type based on file extension
     * 
     * Maps file extensions to MIME types. This is a simple
     * implementation based on common file types.
     * 
     * @param file_path The path to the file
     * @return std::string The MIME type (e.g., "text/html", "image/jpeg")
     */
    std::string detect_mime_type(const std::filesystem::path& file_path) const;

    /**
     * @brief Read file content into a string
     * 
     * @param file_path The path to the file
     * @return std::string The file content, or empty string on error
     */
    std::string read_file(const std::filesystem::path& file_path) const;

    /**
     * @brief Check if a path points to a directory
     * 
     * @param path The path to check
     * @return true if the path is a directory, false otherwise
     */
    bool is_directory(const std::filesystem::path& path) const;

    /**
     * @brief Initialize MIME type mapping
     * 
     * Populates the mime_types_ map with common file extensions
     * and their corresponding MIME types.
     */
    void initialize_mime_types();

private:
    std::string document_root_;  // The root directory for serving files
    std::filesystem::path document_root_path_;  // Document root as filesystem path
    std::map<std::string, std::string> mime_types_;  // Extension to MIME type mapping
};

} // namespace aevrix
