// =============================================================================
// Aevrix - UniqueFd: RAII File Descriptor Wrapper
// =============================================================================
// This header provides a RAII (Resource Acquisition Is Initialization) wrapper
// for Unix file descriptors. UniqueFd ensures that file descriptors are properly
// closed when they go out of scope, preventing resource leaks.
//
// Key Features:
// - Move-only type (cannot be copied, only moved)
// - Automatic closure on destruction
// - Explicit ownership semantics
// - Support for release() and reset() operations
// - Invalid descriptor handling (-1 or negative values)
//
// Usage Example:
//   {
//       aevrix::UniqueFd fd(socket(AF_INET, SOCK_STREAM, 0));
//       if (fd.get() == -1) {
//           // Handle error
//       }
//       // fd is automatically closed when scope ends
//   }
//
// Why RAII for File Descriptors?
// File descriptors are limited system resources. If not properly closed, they can
// lead to resource exhaustion, especially in long-running servers. RAII ensures
// deterministic cleanup without relying on manual close() calls scattered throughout
// the code.
// =============================================================================

#pragma once

#include <algorithm>  // For std::swap

#ifdef _WIN32
#include <io.h>       // For _close() on Windows
#define CLOSE_FD _close
#else
#include <unistd.h>   // For close() on Unix/Linux
#define CLOSE_FD close
#endif

namespace aevrix {

// =============================================================================
// UniqueFd Class
// =============================================================================
// A move-only RAII wrapper for Unix file descriptors.
// Ensures that the owned file descriptor is closed exactly once.
// =============================================================================
class UniqueFd {
public:
    // =========================================================================
    // Constructors and Destructor
    // =========================================================================

    /**
     * @brief Default constructor - creates an invalid file descriptor
     * 
     * Constructs a UniqueFd in an invalid state (fd_ = -1).
     * This is useful for default-initialized members that will be assigned later.
     */
    UniqueFd() noexcept : fd_(-1) {}

    /**
     * @brief Constructor from raw file descriptor
     * 
     * Takes ownership of a raw file descriptor.
     * The descriptor will be closed when this UniqueFd is destroyed.
     * 
     * @param fd The file descriptor to take ownership of. Can be -1 for invalid.
     * 
     * @note If fd is -1, this is equivalent to the default constructor.
     */
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    /**
     * @brief Destructor - closes the owned file descriptor
     * 
     * If the file descriptor is valid (>= 0), it will be closed.
     * Invalid descriptors (-1) are ignored.
     * 
     * @note close() is called without error checking because in destructor context,
     *       there's typically nothing useful we can do with a close() failure.
     */
    ~UniqueFd() {
        close();
    }

    // =========================================================================
    // Move Semantics (No Copy Semantics)
    // =========================================================================

    /**
     * @brief Move constructor
     * 
     * Transfers ownership of the file descriptor from another UniqueFd.
     * The source UniqueFd is left in an invalid state.
     * 
     * @param other The UniqueFd to move from. After move, other.get() returns -1.
     */
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;  // Leave source in invalid state
    }

    /**
     * @brief Move assignment operator
     * 
     * Transfers ownership of the file descriptor from another UniqueFd.
     * The current file descriptor (if valid) is closed first.
     * The source UniqueFd is left in an invalid state.
     * 
     * @param other The UniqueFd to move from. After move, other.get() returns -1.
     * @return UniqueFd& Reference to this object for chaining.
     */
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            close();        // Close current descriptor first
            fd_ = other.fd_;
            other.fd_ = -1; // Leave source in invalid state
        }
        return *this;
    }

    // Delete copy constructor and copy assignment
    // File descriptors cannot be safely copied (would lead to double-close)
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Get the raw file descriptor
     * 
     * Returns the underlying file descriptor without transferring ownership.
     * The caller must not close this descriptor manually.
     * 
     * @return int The file descriptor, or -1 if invalid.
     */
    int get() const noexcept {
        return fd_;
    }

    /**
     * @brief Check if the file descriptor is valid
     * 
     * @return true if the descriptor is valid (>= 0), false if invalid (-1).
     */
    bool is_valid() const noexcept {
        return fd_ >= 0;
    }

    /**
     * @brief Explicit conversion to bool
     * 
     * Allows using UniqueFd in boolean contexts (if statements, etc.)
     * 
     * @return true if the descriptor is valid, false otherwise.
     */
    explicit operator bool() const noexcept {
        return is_valid();
    }

    // =========================================================================
    // Modifiers
    // =========================================================================

    /**
     * @brief Release ownership of the file descriptor
     * 
     * Transfers ownership of the file descriptor to the caller.
     * The UniqueFd no longer owns the descriptor and will not close it.
     * The caller is now responsible for closing the descriptor.
     * 
     * @return int The file descriptor that was owned. May be -1 if invalid.
     * 
     * @note After release(), this UniqueFd is in an invalid state.
     */
    int release() noexcept {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

    /**
     * @brief Reset to a new file descriptor
     * 
     * Closes the current descriptor (if valid) and takes ownership of a new one.
     * 
     * @param fd The new file descriptor to own. Can be -1 to just close current.
     * 
     * @note If fd is the same as the current descriptor, this is a no-op.
     */
    void reset(int fd = -1) noexcept {
        if (fd_ != fd) {
            close();
            fd_ = fd;
        }
    }

    /**
     * @brief Swap with another UniqueFd
     * 
     * Exchanges the owned file descriptors with another UniqueFd.
     * 
     * @param other The UniqueFd to swap with.
     */
    void swap(UniqueFd& other) noexcept {
        std::swap(fd_, other.fd_);
    }

private:
    // =========================================================================
    // Private Helper
    // =========================================================================

    /**
     * @brief Close the file descriptor if valid
     * 
     * Internal helper that closes the descriptor if it's valid.
     * Sets fd_ to -1 after closing to mark as invalid.
     * 
     * @note close() errors are silently ignored in destructor context.
     */
    void close() noexcept {
        if (fd_ >= 0) {
            CLOSE_FD(fd_);
            fd_ = -1;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    int fd_;  ///< The owned file descriptor, or -1 if invalid
};

// =============================================================================
// Non-member Swap Function
// =============================================================================
// Enables ADL (Argument-Dependent Lookup) for swap operations
// Allows using std::swap() with UniqueFd objects
// =============================================================================
inline void swap(UniqueFd& a, UniqueFd& b) noexcept {
    a.swap(b);
}

// =============================================================================
// Comparison Operators
// =============================================================================
// Allow comparison of UniqueFd objects with each other and with raw descriptors
// =============================================================================

inline bool operator==(const UniqueFd& a, const UniqueFd& b) noexcept {
    return a.get() == b.get();
}

inline bool operator!=(const UniqueFd& a, const UniqueFd& b) noexcept {
    return a.get() != b.get();
}

inline bool operator<(const UniqueFd& a, const UniqueFd& b) noexcept {
    return a.get() < b.get();
}

inline bool operator==(const UniqueFd& a, int b) noexcept {
    return a.get() == b;
}

inline bool operator!=(const UniqueFd& a, int b) noexcept {
    return a.get() != b;
}

} // namespace aevrix
