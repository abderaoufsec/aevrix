# HTTP Correctness Expansion (Phase 19)

## Overview

Phase 19 adds HTTP correctness features to the Aevrix server, including conditional requests, cache validation, and partial content support. These features improve bandwidth efficiency and enable more sophisticated HTTP interactions.

## Features Implemented

### 1. ETag Support

**Purpose**: Entity Tags (ETags) provide cache validation using unique identifiers for resource versions.

**Implementation**:
- `HttpResponse::set_etag()` - Sets the ETag header
- `HttpResponse::get_etag()` - Gets the ETag header value
- `generate_etag()` - Generates a strong ETag from content
- `generate_weak_etag()` - Generates a weak ETag from content
- `etag_matches()` - Compares ETags (accounting for weak validators)
- `is_weak_etag()` - Checks if an ETag is weak

**Usage**:
```cpp
// Generate ETag for a resource
std::string content = readFile("index.html");
std::string etag = http::generate_etag(content);

HttpResponse response;
response.set_etag(etag);
```

**HTTP/1.1 RFC Reference**: RFC 7232 - HTTP/1.1 ETag

### 2. Last-Modified Support

**Purpose**: Last-Modified provides cache validation using modification timestamps.

**Implementation**:
- `HttpResponse::set_last_modified()` - Sets the Last-Modified header
- `HttpResponse::get_last_modified()` - Gets the Last-Modified header value
- `format_http_date()` - Formats time_t as HTTP date string
- `current_http_date()` - Gets current time as HTTP date string
- `parse_http_date()` - Parses HTTP date string to time_t

**Usage**:
```cpp
// Get file modification time
time_t mod_time = getFileModificationTime("index.html");
std::string last_modified = http::format_http_date(mod_time);

HttpResponse response;
response.set_last_modified(last_modified);
```

**HTTP/1.1 RFC Reference**: RFC 7231 - HTTP/1.1 Date Formats

### 3. If-None-Match Support

**Purpose**: If-None-Match enables conditional requests using ETag validation.

**Implementation**:
- `HttpRequest::get_if_none_match()` - Gets the If-None-Match header value
- `should_return_not_modified_etag()` - Validates if 304 Not Modified should be returned

**Usage**:
```cpp
// Client sends: If-None-Match: "abc123"
std::string if_none_match = request.get_if_none_match();
std::string current_etag = generate_etag(content);

if (http::should_return_not_modified_etag(if_none_match, current_etag)) {
    return HttpResponse(StatusCode::NotModified);
}
```

**HTTP/1.1 RFC Reference**: RFC 7232 - HTTP/1.1 ETag

### 4. If-Modified-Since Support

**Purpose**: If-Modified-Since enables conditional requests using date validation.

**Implementation**:
- `HttpRequest::get_if_modified_since()` - Gets the If-Modified-Since header value
- `should_return_not_modified_date()` - Validates if 304 Not Modified should be returned

**Usage**:
```cpp
// Client sends: If-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT
std::string if_modified_since = request.get_if_modified_since();
time_t last_modified = getFileModificationTime("index.html");

if (http::should_return_not_modified_date(if_modified_since, last_modified)) {
    return HttpResponse(StatusCode::NotModified);
}
```

**HTTP/1.1 RFC Reference**: RFC 7232 - HTTP/1.1 ETag

### 5. Range Requests

**Purpose**: Range requests enable partial content retrieval for resumable downloads and seeking.

**Implementation**:
- `HttpRequest::get_range()` - Gets the Range header value
- `parse_range_header()` - Parses Range header to start/end offsets
- `format_content_range()` - Formats Content-Range header
- `StatusCode::PartialContent` - 206 status code for partial content

**Usage**:
```cpp
// Client sends: Range: bytes=0-1023
std::string range_header = request.get_range();
size_t start, end;

if (http::parse_range_header(range_header, file_size, start, end)) {
    // Serve partial content
    std::string partial_content = content.substr(start, end - start + 1);
    
    HttpResponse response(StatusCode::PartialContent);
    response.set_body(partial_content);
    response.set_header("Content-Range", http::format_content_range(start, end, file_size));
    return response;
}
```

**HTTP/1.1 RFC Reference**: RFC 7233 - HTTP/1.1 Range Requests

### 6. 304 Not Modified

**Purpose**: 304 Not Modified avoids transferring unchanged data when the client has a cached version.

**Implementation**:
- `StatusCode::NotModified` - 304 status code (already defined in http_status.h)
- Conditional request validation functions
- Response with 304 includes only headers, no body

**Usage**:
```cpp
// Validate using ETag
if (http::should_return_not_modified_etag(if_none_match, current_etag)) {
    HttpResponse response(StatusCode::NotModified);
    response.set_etag(current_etag);
    response.set_last_modified(last_modified);
    return response;  // Body is empty
}

// Validate using date
if (http::should_return_not_modified_date(if_modified_since, last_modified)) {
    HttpResponse response(StatusCode::NotModified);
    response.set_etag(current_etag);
    response.set_last_modified(last_modified);
    return response;  // Body is empty
}
```

**HTTP/1.1 RFC Reference**: RFC 7232 - HTTP/1.1 ETag

## Conditional Request Flow

### ETag Validation Flow

```
1. Client: GET /index.html
2. Server: 200 OK, ETag: "abc123"
3. Client caches response with ETag

4. Client: GET /index.html, If-None-Match: "abc123"
5. Server checks ETag
   - If matches: 304 Not Modified (no body)
   - If differs: 200 OK with new content and new ETag
```

### Last-Modified Validation Flow

```
1. Client: GET /index.html
2. Server: 200 OK, Last-Modified: Wed, 21 Oct 2015 07:28:00 GMT
3. Client caches response with timestamp

4. Client: GET /index.html, If-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT
5. Server checks file modification time
   - If not modified: 304 Not Modified (no body)
   - If modified: 200 OK with new content and new Last-Modified
```

### Range Request Flow

```
1. Client: GET /video.mp4, Range: bytes=0-1023
2. Server parses range: start=0, end=1023
3. Server serves bytes 0-1023
4. Server: 206 Partial Content, Content-Range: bytes 0-1023/1048576
```

## Cache Control Best Practices

### ETag vs Last-Modified

**ETag advantages**:
- More precise: detects content changes even with same timestamp
- Works with dynamic content that changes frequently
- Weak validators for content that may change semantically

**Last-Modified advantages**:
- Simpler to implement (just file timestamps)
- Human-readable
- Works with existing HTTP infrastructure

**Recommendation**: Use both for maximum cache validation accuracy.

### Strong vs Weak ETags

**Strong ETags**:
- Content must be byte-for-byte identical
- Example: `"33a64df551425fcc55e4d42a148795d9f25f89d4"`
- Use for static files and cache keys

**Weak ETags**:
- Content may change semantically but represent the same resource
- Example: `W/"33a64df551425fcc55e4d42a148795d9f25f89d4"`
- Use for server-side rendered content with minor variations

### Range Request Considerations

**Security**: Always validate range bounds to prevent:
- Negative ranges
- Ranges beyond file size
- Start > end

**Performance**: Use memory-mapped files or efficient seeking for large files.

**Multiple Ranges**: Current implementation supports single ranges. Multiple ranges require multipart/byteranges.

## Testing

### Unit Tests

Unit tests cover:
- HTTP date formatting and parsing
- ETag generation and matching
- Conditional request validation (ETag and date)
- Range request parsing
- Content-Range formatting
- HTTP response cache headers
- HTTP request cache headers

Run unit tests:
```bash
ctest --test-dir build/debug --output-on-failure
```

### Test Coverage

- HTTP date utilities: 7 tests
- ETag utilities: 4 tests
- Conditional request (ETag): 3 tests
- Conditional request (date): 3 tests
- Range request parsing: 5 tests
- Content-Range formatting: 1 test
- HTTP response cache headers: 4 tests
- HTTP request cache headers: 4 tests

**Total: 31 unit tests**

## Implementation Notes

### ETag Generation

Current implementation uses a simple hash function for demonstration. In production, replace with proper MD5 or SHA-256:

```cpp
// Production implementation (requires OpenSSL)
#include <openssl/md5.h>

std::string generate_etag(const std::string& content) {
    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(content.c_str()), content.length(), hash);
    
    std::stringstream ss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}
```

### Date Parsing

Current implementation is a simplified parser for Windows/MinGW compatibility (which lacks strptime). In production on Linux, use strptime:

```cpp
#include <ctime>

time_t parse_http_date(const std::string& date_str) {
    std::tm tm_info = {};
    char* result = strptime(date_str.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm_info);
    if (result == nullptr) {
        return -1;
    }
    return mktime(&tm_info);
}
```

### Range Request Limitations

Current implementation:
- Supports single range requests only
- Does not support multipart/byteranges
- Does not support suffix-byte-range-spec (e.g., "bytes=-500")

Future enhancements:
- Multiple range support
- Multipart/byteranges response
- Suffix-byte-range-spec support

## Integration with Static File Server

To integrate cache control with the static file server:

```cpp
// In StaticFileServer::serve_file()
std::string content = readFile(path);
time_t mod_time = getFileModificationTime(path);
std::string etag = http::generate_etag(content);
std::string last_modified = http::format_http_date(mod_time);

// Check conditional requests
std::string if_none_match = request.get_if_none_match();
std::string if_modified_since = request.get_if_modified_since();

if (http::should_return_not_modified_etag(if_none_match, etag) ||
    http::should_return_not_modified_date(if_modified_since, mod_time)) {
    HttpResponse response(StatusCode::NotModified);
    response.set_etag(etag);
    response.set_last_modified(last_modified);
    return response;
}

// Check range requests
std::string range_header = request.get_range();
if (!range_header.empty()) {
    size_t start, end;
    if (http::parse_range_header(range_header, content.length(), start, end)) {
        content = content.substr(start, end - start + 1);
        HttpResponse response(StatusCode::PartialContent);
        response.set_body(content);
        response.set_etag(etag);
        response.set_last_modified(last_modified);
        response.set_header("Content-Range", http::format_content_range(start, end, file_size));
        return response;
    }
}

// Normal response
HttpResponse response(StatusCode::OK);
response.set_body(content);
response.set_etag(etag);
response.set_last_modified(last_modified);
return response;
```

## Security Considerations

### ETag Security

- ETags should not contain sensitive information
- Use strong cryptographic hashes (MD5/SHA-256) in production
- Weak ETags should only be used when appropriate

### Last-Modified Security

- File modification times may leak information about server activity
- Consider privacy implications for sensitive files

### Range Request Security

- Always validate range bounds to prevent:
  - Out-of-bounds reads
  - Denial of service through extreme ranges
  - Information disclosure through precise byte ranges

## Performance Impact

### Benefits

- **Bandwidth savings**: 304 responses have no body
- **Reduced server load**: Fewer full content transfers
- **Faster client experience**: Cached content loads instantly

### Costs

- **ETag computation**: Hashing large files takes CPU time
- **File stat calls**: Last-Modified requires filesystem access
- **Range seeking**: Partial content may require file seeking

### Recommendations

- Cache ETag and Last-Modified values
- Use memory-mapped files for large files
- Consider ETag computation frequency vs change frequency

## Future Enhancements

1. **Multiple Range Support**: Support for multipart/byteranges
2. **Cache-Control Header**: Implement Cache-Control directives
3. **Vary Header**: Support for content negotiation
4. **If-Match and If-Unmodified-Since**: Additional conditional request headers
5. **ETag Weakness Selection**: Automatic weak vs strong ETag selection
6. **Precondition Failed (412)**: Support for failed preconditions
7. **Requested Range Not Satisfiable (416)**: Better error handling for invalid ranges

## References

- RFC 7232: HTTP/1.1 ETag
- RFC 7231: HTTP/1.1 Date Formats
- RFC 7233: HTTP/1.1 Range Requests
- RFC 9110: HTTP Semantics
- MDN Web Docs: HTTP caching
