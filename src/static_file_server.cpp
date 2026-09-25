// =============================================================================
// Aevrix - Static File Server Implementation
// =============================================================================
// This file implements the static file server with secure path validation,
// MIME type detection, and file serving capabilities.
// =============================================================================

#include "aevrix/static_file_server.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <iostream>

namespace aevrix {

StaticFileServer::StaticFileServer(const std::string& document_root)
    : document_root_(document_root) {
    
    // Initialize MIME type mapping
    initialize_mime_types();
    
    std::cout << "Document root string: " << document_root << "\n";
    
    // Convert document root to filesystem path
    try {
        document_root_path_ = std::filesystem::path(document_root);
        std::cout << "Document root path: " << document_root_path_.string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Failed to create path: " << e.what() << "\n";
        throw;
    }
    
    // Make path absolute for validation
    try {
        document_root_path_ = std::filesystem::absolute(document_root_path_);
        std::cout << "Document root absolute path: " << document_root_path_.string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Failed to make absolute path: " << e.what() << "\n";
        // Continue with relative path
    }
    
    // Validate that document root exists and is a directory
    if (!std::filesystem::exists(document_root_path_)) {
        throw std::runtime_error("Document root does not exist: " + document_root);
    }
    
    if (!std::filesystem::is_directory(document_root_path_)) {
        throw std::runtime_error("Document root is not a directory: " + document_root);
    }
    
    std::cout << "Static file server initialized with document root: " 
              << document_root_path_.string() << "\n";
}

std::tuple<std::string, std::string, int> StaticFileServer::serve_file(
    const std::string& request_target) {
    
    // Normalize the request target path
    std::string normalized = normalize_path(request_target);
    
    // Resolve to absolute path
    std::filesystem::path resolved_path = resolve_path(normalized);
    
    // Validate that the path stays within document root
    if (!validate_path(resolved_path)) {
        std::cerr << "Path validation failed: " << resolved_path.string() 
                  << " escapes document root\n";
        return {"", "text/plain", 403};  // Forbidden
    }
    
    // Check if path exists
    if (!std::filesystem::exists(resolved_path)) {
        std::cerr << "File not found: " << resolved_path.string() << "\n";
        return {"", "text/plain", 404};  // Not Found
    }
    
    // Check if path is a directory
    if (is_directory(resolved_path)) {
        std::cerr << "Attempted to access directory: " << resolved_path.string() << "\n";
        return {"", "text/plain", 403};  // Forbidden (no directory listing in Phase 6)
    }
    
    // Check if path is a regular file
    if (!std::filesystem::is_regular_file(resolved_path)) {
        std::cerr << "Not a regular file: " << resolved_path.string() << "\n";
        return {"", "text/plain", 403};  // Forbidden
    }
    
    // Detect MIME type
    std::string mime_type = detect_mime_type(resolved_path);
    
    // Read file content
    std::string content = read_file(resolved_path);
    
    if (content.empty()) {
        std::cerr << "Failed to read file: " << resolved_path.string() << "\n";
        return {"", "text/plain", 500};  // Internal Server Error
    }
    
    std::cout << "Serving file: " << resolved_path.string() 
              << " (" << content.length() << " bytes, MIME: " << mime_type << ")\n";
    
    return {content, mime_type, 200};  // OK
}

bool StaticFileServer::is_path_safe(const std::string& request_target) const {
    try {
        std::string normalized = normalize_path(request_target);
        std::filesystem::path resolved_path = resolve_path(normalized);
        return validate_path(resolved_path);
    } catch (...) {
        return false;
    }
}

std::string StaticFileServer::normalize_path(const std::string& path) const {
    // URL decode the path first
    std::string decoded = url_decode(path);
    
    // Remove leading slash if present (filesystem paths don't start with /)
    std::string normalized = decoded;
    if (!normalized.empty() && normalized[0] == '/') {
        normalized = normalized.substr(1);
    }
    
    // If path is empty after removing leading slash, use "index.html"
    if (normalized.empty()) {
        normalized = "index.html";
    }
    
    // Use filesystem to normalize the path (resolves ".." and ".")
    try {
        std::filesystem::path fs_path(normalized);
        std::filesystem::path absolute_path = std::filesystem::absolute(document_root_path_ / fs_path);
        
        // Get the relative path from document root
        std::filesystem::path relative_path = std::filesystem::relative(absolute_path, document_root_path_);
        
        // Convert back to string with forward slashes
        std::string result = relative_path.string();
        
        // Normalize path separators to forward slashes (URL-style)
        std::replace(result.begin(), result.end(), '\\', '/');
        
        return result;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Path normalization error: " << e.what() << "\n";
        throw std::runtime_error("Path normalization failed");
    }
}

std::string StaticFileServer::url_decode(const std::string& encoded) const {
    std::ostringstream decoded;
    
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            // Decode %XX sequence
            std::string hex_str = encoded.substr(i + 1, 2);
            try {
                int char_code = std::stoi(hex_str, nullptr, 16);
                decoded << static_cast<char>(char_code);
                i += 2;  // Skip the two hex characters
            } catch (...) {
                // Invalid hex sequence, keep the % as-is
                decoded << encoded[i];
            }
        } else if (encoded[i] == '+') {
            // Convert + to space (URL encoding for space)
            decoded << ' ';
        } else {
            decoded << encoded[i];
        }
    }
    
    return decoded.str();
}

std::filesystem::path StaticFileServer::resolve_path(const std::string& request_target) const {
    // Combine document root with normalized request target
    std::filesystem::path resolved = document_root_path_ / request_target;
    
    // Make it absolute
    resolved = std::filesystem::absolute(resolved);
    
    // Normalize the path (remove redundant separators, resolve ".." and ".")
    resolved = resolved.lexically_normal();
    
    return resolved;
}

bool StaticFileServer::validate_path(const std::filesystem::path& resolved_path) const {
    try {
        // Make both paths absolute and normalized for comparison
        std::filesystem::path abs_resolved = std::filesystem::absolute(resolved_path).lexically_normal();
        std::filesystem::path abs_root = document_root_path_.lexically_normal();
        
        // Check if the resolved path starts with the document root
        // This ensures the path is within the document root
        auto resolved_it = abs_resolved.begin();
        auto root_it = abs_root.begin();
        
        // Compare each component
        for (; root_it != abs_root.end(); ++root_it, ++resolved_it) {
            if (resolved_it == abs_resolved.end()) {
                // Resolved path is shorter than root - invalid
                return false;
            }
            
            if (*resolved_it != *root_it) {
                // Path component differs - escape attempt
                return false;
            }
        }
        
        // If we've matched all root components, the path is within root
        return true;
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Path validation error: " << e.what() << "\n";
        return false;
    }
}

std::string StaticFileServer::detect_mime_type(const std::filesystem::path& file_path) const {
    // Get file extension
    std::string extension = file_path.extension().string();
    
    // Convert extension to lowercase for case-insensitive lookup
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    // Look up MIME type
    auto it = mime_types_.find(extension);
    if (it != mime_types_.end()) {
        return it->second;
    }
    
    // Default to application/octet-stream for unknown types
    return "application/octet-stream";
}

std::string StaticFileServer::read_file(const std::filesystem::path& file_path) const {
    try {
        // Open file in binary mode
        std::ifstream file(file_path, std::ios::binary);
        
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << file_path.string() << "\n";
            return "";
        }
        
        // Read file content
        std::string content;
        file.seekg(0, std::ios::end);
        content.resize(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(&content[0], content.size());
        
        if (!file) {
            std::cerr << "Failed to read file: " << file_path.string() << "\n";
            return "";
        }
        
        return content;
        
    } catch (const std::exception& e) {
        std::cerr << "File read error: " << e.what() << "\n";
        return "";
    }
}

bool StaticFileServer::is_directory(const std::filesystem::path& path) const {
    try {
        return std::filesystem::is_directory(path);
    } catch (...) {
        return false;
    }
}

void StaticFileServer::initialize_mime_types() {
    // Common MIME types for web content
    mime_types_[".html"] = "text/html";
    mime_types_[".htm"] = "text/html";
    mime_types_[".css"] = "text/css";
    mime_types_[".js"] = "application/javascript";
    mime_types_[".json"] = "application/json";
    mime_types_[".xml"] = "application/xml";
    
    // Image types
    mime_types_[".jpg"] = "image/jpeg";
    mime_types_[".jpeg"] = "image/jpeg";
    mime_types_[".png"] = "image/png";
    mime_types_[".gif"] = "image/gif";
    mime_types_[".svg"] = "image/svg+xml";
    mime_types_[".ico"] = "image/x-icon";
    mime_types_[".webp"] = "image/webp";
    
    // Font types
    mime_types_[".woff"] = "font/woff";
    mime_types_[".woff2"] = "font/woff2";
    mime_types_[".ttf"] = "font/ttf";
    mime_types_[".otf"] = "font/otf";
    mime_types_[".eot"] = "application/vnd.ms-fontobject";
    
    // Text types
    mime_types_[".txt"] = "text/plain";
    mime_types_[".md"] = "text/markdown";
    mime_types_[".csv"] = "text/csv";
    
    // Document types
    mime_types_[".pdf"] = "application/pdf";
    mime_types_[".zip"] = "application/zip";
    mime_types_[".tar"] = "application/x-tar";
    mime_types_[".gz"] = "application/gzip";
    
    // Audio types
    mime_types_[".mp3"] = "audio/mpeg";
    mime_types_[".wav"] = "audio/wav";
    mime_types_[".ogg"] = "audio/ogg";
    
    // Video types
    mime_types_[".mp4"] = "video/mp4";
    mime_types_[".webm"] = "video/webm";
    mime_types_[".avi"] = "video/x-msvideo";
    
    // Default binary type
    mime_types_[""] = "application/octet-stream";
}

} // namespace aevrix
