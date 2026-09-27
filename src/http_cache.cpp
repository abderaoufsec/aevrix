// =============================================================================
// Aevrix - HTTP Cache Control Implementation
// =============================================================================
// This file implements HTTP cache control utilities including ETag generation,
// HTTP date formatting, conditional request validation, and range request parsing.
// =============================================================================

#include "aevrix/http_cache.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

// For MD5 hash generation (simplified implementation)
// In production, use a proper cryptographic library like OpenSSL
namespace {
    // Simple hash function for demonstration (NOT cryptographically secure)
    // In production, replace with proper MD5 implementation
    std::string simple_hash(const std::string& str) {
        unsigned int hash = 5381;
        for (char c : str) {
            hash = ((hash << 5) + hash) + static_cast<unsigned int>(c);
        }
        std::stringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
}

namespace aevrix {
namespace http {

// =============================================================================
// HTTP Date Utilities Implementation
// =============================================================================

std::string format_http_date(time_t time) {
    // Format according to RFC 7231 IMF-fixdate: "Wed, 21 Oct 2015 07:28:00 GMT"
    std::tm* tm_info = gmtime(&time);
    if (!tm_info) {
        return "";
    }
    
    char buffer[128];
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    return std::string(buffer);
}

std::string current_http_date() {
    return format_http_date(std::time(nullptr));
}

time_t parse_http_date(const std::string& date_str) {
    // Parse RFC 7231 IMF-fixdate format: "Wed, 21 Oct 2015 07:28:00 GMT"
    std::tm tm_info = {};
    // This is a simplified parser - in production, use a proper date parsing library
    if (date_str.empty()) {
        return -1;
    }
    
    // Windows/MinGW doesn't have strptime, so we implement a simple parser
    // Format: "Wed, 21 Oct 2015 07:28:00 GMT"
    //         0123456789012345678901234567890123456789
    
    if (date_str.length() < 29) {
        return -1;
    }
    
    // Parse day of month (positions 5-6)
    try {
        tm_info.tm_mday = std::stoi(date_str.substr(5, 2));
        
        // Parse month name (positions 8-10)
        std::string month = date_str.substr(8, 3);
        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        tm_info.tm_mon = 0;
        for (int i = 0; i < 12; i++) {
            if (month == months[i]) {
                tm_info.tm_mon = i;
                break;
            }
        }
        
        // Parse year (positions 12-15)
        tm_info.tm_year = std::stoi(date_str.substr(12, 4)) - 1900;
        
        // Parse hour (positions 17-18)
        tm_info.tm_hour = std::stoi(date_str.substr(17, 2));
        
        // Parse minute (positions 20-21)
        tm_info.tm_min = std::stoi(date_str.substr(20, 2));
        
        // Parse second (positions 23-24)
        tm_info.tm_sec = std::stoi(date_str.substr(23, 2));
        
        // Assume GMT (UTC)
        tm_info.tm_isdst = 0;
        
        return mktime(&tm_info);
    } catch (...) {
        return -1;
    }
}

// =============================================================================
// ETag Utilities Implementation
// =============================================================================

std::string generate_etag(const std::string& content) {
    // Generate hash-based ETag (using simple hash for demonstration)
    // In production, use proper MD5 or SHA-256
    std::string hash = simple_hash(content);
    return hash;
}

std::string generate_weak_etag(const std::string& content) {
    // Generate weak ETag (prefixed with W/)
    std::string hash = simple_hash(content);
    return "W/" + hash;
}

bool etag_matches(const std::string& etag1, const std::string& etag2) {
    // Strip W/ prefix if present for comparison
    auto strip_weak = [](const std::string& etag) {
        if (etag.size() >= 2 && etag[0] == 'W' && etag[1] == '/') {
            return etag.substr(2);
        }
        return etag;
    };
    
    return strip_weak(etag1) == strip_weak(etag2);
}

bool is_weak_etag(const std::string& etag) {
    return etag.size() >= 2 && etag[0] == 'W' && etag[1] == '/';
}

// =============================================================================
// Conditional Request Validation Implementation
// =============================================================================

bool should_return_not_modified_etag(const std::string& if_none_match, 
                                      const std::string& current_etag) {
    if (if_none_match.empty() || current_etag.empty()) {
        return false;
    }
    
    // If-None-Match can contain multiple ETags separated by commas
    // For simplicity, we only check the first one
    // In production, parse the full list
    std::string client_etag = if_none_match;
    
    // Remove quotes if present
    if (client_etag.size() >= 2 && client_etag.front() == '"' && client_etag.back() == '"') {
        client_etag = client_etag.substr(1, client_etag.length() - 2);
    }
    
    return etag_matches(client_etag, current_etag);
}

bool should_return_not_modified_date(const std::string& if_modified_since,
                                       time_t last_modified) {
    if (if_modified_since.empty() || last_modified == -1) {
        return false;
    }
    
    // Parse the If-Modified-Since date
    time_t client_time = parse_http_date(if_modified_since);
    if (client_time == -1) {
        return false;
    }
    
    // Resource hasn't been modified if last_modified <= client_time
    return last_modified <= client_time;
}

// =============================================================================
// Range Request Utilities Implementation
// =============================================================================

bool parse_range_header(const std::string& range_header, size_t file_size,
                       size_t& start_out, size_t& end_out) {
    if (range_header.empty()) {
        return false;
    }
    
    // Range format: "bytes=start-end"
    // Simplified parser - in production, handle multiple ranges and edge cases
    if (range_header.substr(0, 6) != "bytes=") {
        return false;
    }
    
    std::string range_spec = range_header.substr(6);
    size_t dash_pos = range_spec.find('-');
    
    if (dash_pos == std::string::npos) {
        return false;
    }
    
    try {
        std::string start_str = range_spec.substr(0, dash_pos);
        std::string end_str = range_spec.substr(dash_pos + 1);
        
        size_t start = start_str.empty() ? 0 : std::stoull(start_str);
        size_t end = end_str.empty() ? file_size - 1 : std::stoull(end_str);
        
        // Validate range
        if (start >= file_size || end >= file_size || start > end) {
            return false;
        }
        
        start_out = start;
        end_out = end;
        return true;
    } catch (...) {
        return false;
    }
}

std::string format_content_range(size_t start, size_t end, size_t total) {
    std::stringstream ss;
    ss << "bytes " << start << "-" << end << "/" << total;
    return ss.str();
}

} // namespace http
} // namespace aevrix
