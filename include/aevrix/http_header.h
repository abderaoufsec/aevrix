// =============================================================================
// Aevrix - HTTP Header Class
// =============================================================================
// This header provides a class for managing HTTP headers.
// HTTP headers are key-value pairs that provide metadata about HTTP requests
// and responses.
//
// Key Features:
// - Case-insensitive header name handling (HTTP header names are case-insensitive)
// - Support for multiple values per header (as per HTTP spec)
// - Efficient storage and lookup
// - Validation of header name and value syntax
//
// HTTP Header Format:
// Field-Name: Field-Value CRLF
// Example: Content-Type: text/plain
//
// Header Name Rules (RFC 9110):
// - Token characters: alphanumeric or !#$%&'*+-.^_`|~
// - Case-insensitive (Content-Type == content-type == CONTENT-TYPE)
// - No whitespace allowed
//
// Header Value Rules:
// - Can contain most ASCII characters
// - Leading/trailing whitespace should be trimmed
// - Long values can be folded across multiple lines (obsolete, not supported in Phase 3)
//
// Phase 3 Implementation:
// - Basic header storage with case-insensitive names
// - Single value per header (simplified for Phase 3)
// - Basic validation
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace aevrix {
namespace http {

// =============================================================================
// HttpHeader Class
// =============================================================================
// Represents a single HTTP header with name and value.
// Handles case-insensitive header names and basic validation.
// =============================================================================
class HttpHeader {
public:
    // =========================================================================
    // Constructors
    // =========================================================================

    /**
     * @brief Default constructor - creates an empty header
     */
    HttpHeader() = default;

    /**
     * @brief Constructor with name and value
     * 
     * Creates a header with the specified name and value.
     * The name will be normalized to lowercase for case-insensitive comparison.
     * 
     * @param name The header name (e.g., "Content-Type")
     * @param value The header value (e.g., "text/plain")
     */
    HttpHeader(const std::string& name, const std::string& value);

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Get the header name
     * 
     * Returns the header name in its original form (preserving case).
     * 
     * @return const std::string& The header name
     */
    const std::string& name() const { return name_; }

    /**
     * @brief Get the header value
     * 
     * Returns the header value.
     * 
     * @return const std::string& The header value
     */
    const std::string& value() const { return value_; }

    /**
     * @brief Get the normalized (lowercase) header name
     * 
     * Returns the header name in lowercase for case-insensitive comparison.
     * 
     * @return const std::string& The normalized header name
     */
    const std::string& normalized_name() const { return normalized_name_; }

    // =========================================================================
    // Validation
    // =========================================================================

    /**
     * @brief Check if the header name is valid
     * 
     * Validates that the header name contains only valid token characters.
     * Valid characters: alphanumeric or !#$%&'*+-.^_`|~
     * 
     * @return true if the header name is valid, false otherwise
     */
    bool is_valid_name() const;

    /**
     * @brief Check if the header value is valid
     * 
     * Basic validation for header value.
     * In Phase 3, we check for null bytes and control characters.
     * 
     * @return true if the header value is valid, false otherwise
     */
    bool is_valid_value() const;

    /**
     * @brief Check if the entire header is valid
     * 
     * @return true if both name and value are valid
     */
    bool is_valid() const {
        return is_valid_name() && is_valid_value();
    }

    // =========================================================================
    // Comparison Operators
    // =========================================================================

    /**
     * @brief Compare headers by normalized name (case-insensitive)
     * 
     * @param other The header to compare with
     * @return true if this header's normalized name is less than the other's
     */
    bool operator<(const HttpHeader& other) const {
        return normalized_name_ < other.normalized_name_;
    }

    /**
     * @brief Check if two headers have the same normalized name
     * 
     * @param other The header to compare with
     * @return true if the normalized names are equal
     */
    bool operator==(const HttpHeader& other) const {
        return normalized_name_ == other.normalized_name_;
    }

    /**
     * @brief Check if two headers have different normalized names
     * 
     * @param other The header to compare with
     * @return true if the normalized names are different
     */
    bool operator!=(const HttpHeader& other) const {
        return !(*this == other);
    }

    // =========================================================================
    // Public Static Helper Methods
    // =========================================================================

    /**
     * @brief Normalize a header name to lowercase
     * 
     * HTTP header names are case-insensitive, so we normalize to lowercase
     * for consistent comparison and storage.
     * 
     * @param name The header name to normalize
     * @return std::string The normalized (lowercase) header name
     */
    static std::string normalize_name(const std::string& name);

private:
    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /**
     * @brief Trim leading and trailing whitespace from a string
     * 
     * @param str The string to trim
     * @return std::string The trimmed string
     */
    static std::string trim_whitespace(const std::string& str);

    // =========================================================================
    // Member Variables
    // =========================================================================

    std::string name_;              ///< Original header name (preserving case)
    std::string value_;             ///< Header value
    std::string normalized_name_;   ///< Lowercase header name for comparison
};

// =============================================================================
// HttpHeaders Class
// =============================================================================
// A collection of HTTP headers with efficient lookup by name.
// Provides case-insensitive header access.
// =============================================================================
class HttpHeaders {
public:
    // =========================================================================
    // Constructors and Destructor
    // =========================================================================

    /**
     * @brief Default constructor - creates an empty header collection
     */
    HttpHeaders() = default;

    /**
     * @brief Destructor
     */
    ~HttpHeaders() = default;

    // =========================================================================
    // Header Management
    // =========================================================================

    /**
     * @brief Add or replace a header
     * 
     * If a header with the same name (case-insensitive) already exists,
     // it will be replaced with the new value.
     * 
     * @param name The header name
     * @param value The header value
     */
    void set(const std::string& name, const std::string& value);

    /**
     * @brief Get a header value by name
     * 
     * @param name The header name (case-insensitive)
     * @return std::string The header value, or empty string if not found
     */
    std::string get(const std::string& name) const;

    /**
     * @brief Check if a header exists
     * 
     * @param name The header name (case-insensitive)
     * @return true if the header exists, false otherwise
     */
    bool has(const std::string& name) const;

    /**
     * @brief Remove a header
     * 
     * @param name The header name (case-insensitive)
     * @return true if the header was removed, false if it didn't exist
     */
    bool remove(const std::string& name);

    /**
     * @brief Clear all headers
     */
    void clear() {
        headers_.clear();
    }

    /**
     * @brief Get the number of headers
     * 
     * @return size_t The number of headers
     */
    size_t size() const {
        return headers_.size();
    }

    /**
     * @brief Check if the collection is empty
     * 
     * @return true if there are no headers, false otherwise
     */
    bool empty() const {
        return headers_.empty();
    }

    // =========================================================================
    // Iteration
    // =========================================================================

    /**
     * @brief Get iterator to the beginning of headers
     * 
     * @return auto Iterator to the first header
     */
    auto begin() const {
        return headers_.begin();
    }

    /**
     * @brief Get iterator to the end of headers
     * 
     * @return auto Iterator to one past the last header
     */
    auto end() const {
        return headers_.end();
    }

private:
    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /**
     * @brief Find a header by normalized name
     * 
     * @param normalized_name The lowercase header name
     * @return auto Iterator to the header, or end() if not found
     */
    auto find_by_normalized_name(const std::string& normalized_name) const {
        return std::find_if(headers_.begin(), headers_.end(),
            [&normalized_name](const HttpHeader& header) {
                return header.normalized_name() == normalized_name;
            });
    }

    /**
     * @brief Find a header by normalized name (non-const version)
     * 
     * @param normalized_name The lowercase header name
     * @return auto Iterator to the header, or end() if not found
     */
    auto find_by_normalized_name(const std::string& normalized_name) {
        return std::find_if(headers_.begin(), headers_.end(),
            [&normalized_name](const HttpHeader& header) {
                return header.normalized_name() == normalized_name;
            });
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    std::vector<HttpHeader> headers_;  ///< Collection of headers
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline HttpHeader::HttpHeader(const std::string& name, const std::string& value)
    : name_(name), value_(trim_whitespace(value)), 
      normalized_name_(normalize_name(name)) {
}

inline std::string HttpHeader::normalize_name(const std::string& name) {
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), 
                   normalized.begin(), 
                   [](unsigned char c) { return std::tolower(c); });
    return normalized;
}

inline std::string HttpHeader::trim_whitespace(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";  // String is all whitespace
    }
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

inline bool HttpHeader::is_valid_name() const {
    if (name_.empty()) {
        return false;
    }

    // Check that all characters are valid token characters
    // Valid: alphanumeric or !#$%&'*+-.^_`|~
    for (char c : name_) {
        bool is_valid = std::isalnum(static_cast<unsigned char>(c)) ||
                       c == '!' || c == '#' || c == '$' || c == '%' ||
                       c == '&' || c == '\'' || c == '*' || c == '+' ||
                       c == '-' || c == '.' || c == '^' || c == '_' ||
                       c == '`' || c == '|' || c == '~';
        if (!is_valid) {
            return false;
        }
    }

    return true;
}

inline bool HttpHeader::is_valid_value() const {
    // Basic validation: no null bytes or control characters (except space/tab)
    for (char c : value_) {
        if (c == '\0' || (c < ' ' && c != ' ' && c != '\t')) {
            return false;
        }
    }
    return true;
}

inline void HttpHeaders::set(const std::string& name, const std::string& value) {
    std::string normalized = HttpHeader::normalize_name(name);
    auto it = find_by_normalized_name(normalized);
    
    if (it != headers_.end()) {
        // Replace existing header
        *it = HttpHeader(name, value);
    } else {
        // Add new header
        headers_.emplace_back(name, value);
    }
}

inline std::string HttpHeaders::get(const std::string& name) const {
    std::string normalized = HttpHeader::normalize_name(name);
    auto it = find_by_normalized_name(normalized);
    
    if (it != headers_.end()) {
        return it->value();
    }
    return "";
}

inline bool HttpHeaders::has(const std::string& name) const {
    std::string normalized = HttpHeader::normalize_name(name);
    return find_by_normalized_name(normalized) != headers_.end();
}

inline bool HttpHeaders::remove(const std::string& name) {
    std::string normalized = HttpHeader::normalize_name(name);
    auto it = find_by_normalized_name(normalized);
    
    if (it != headers_.end()) {
        headers_.erase(it);
        return true;
    }
    return false;
}

} // namespace http
} // namespace aevrix
