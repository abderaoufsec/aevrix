// =============================================================================
// Aevrix - TcpListener: TCP Socket Listener
// =============================================================================
// This header provides a TCP listener class for accepting incoming connections.
// TcpListener manages the lifecycle of a listening socket using RAII principles
// via the UniqueFd class, ensuring proper resource cleanup.
//
// Key Features:
// - RAII-based socket management using UniqueFd
// - Cross-platform support (Windows/Unix/Linux)
// - Configurable host and port binding
// - Non-blocking accept() support (for future epoll integration)
// - Error handling with descriptive messages
//
// Usage Example:
//   aevrix::TcpListener listener("127.0.0.1", 8080);
//   if (!listener.start()) {
//       // Handle error
//   }
//   while (true) {
//       auto client_fd = listener.accept();
//       if (client_fd.has_value()) {
//           // Handle connection
//       }
//   }
//
// TCP Server Basics:
// 1. socket() - Create a socket file descriptor
// 2. bind() - Associate the socket with a specific address and port
// 3. listen() - Mark the socket as passive, ready to accept incoming connections
// 4. accept() - Wait for and accept an incoming connection
//
// Why RAII for Sockets?
// Sockets are file descriptors that represent limited system resources.
// Like file descriptors, they must be properly closed to prevent resource leaks.
// RAII ensures deterministic cleanup without manual close() calls.
// =============================================================================

#pragma once

#include "aevrix/unique_fd.h"
#include <string>
#include <optional>
#include <system_error>
#include <cstdint>  // For uint16_t

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// Windows socket types
typedef SOCKET socket_type;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define SOCKET_ERROR_VALUE SOCKET_ERROR
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
// Unix socket types (file descriptors)
typedef int socket_type;
#define INVALID_SOCKET_VALUE -1
#define SOCKET_ERROR_VALUE -1
#endif

namespace aevrix {

// =============================================================================
// TcpListener Class
// =============================================================================
// A RAII wrapper for a TCP listening socket.
// Manages socket creation, binding, listening, and connection acceptance.
// =============================================================================
class TcpListener {
public:
    // =========================================================================
    // Constructors and Destructor
    // =========================================================================

    /**
     * @brief Default constructor - creates an unconfigured listener
     * 
     * Creates a TcpListener in an unconfigured state.
     * Must call start() with host and port before accepting connections.
     */
    TcpListener();

    /**
     * @brief Constructor with host and port
     * 
     * Creates a TcpListener and immediately starts listening on the specified
     * host and port. This is equivalent to default construction + start().
     * 
     * @param host The hostname or IP address to bind to (e.g., "127.0.0.1")
     * @param port The port number to listen on (e.g., 8080)
     * 
     * @throws std::system_error if socket creation, binding, or listening fails
     */
    TcpListener(const std::string& host, uint16_t port);

    /**
     * @brief Destructor - closes the listening socket
     * 
     * Automatically closes the listening socket via UniqueFd destructor.
     * Ensures proper resource cleanup.
     */
    ~TcpListener() = default;

    // =========================================================================
    // Move Semantics (No Copy Semantics)
    // =========================================================================

    /**
     * @brief Move constructor
     * 
     * Transfers ownership of the listening socket from another TcpListener.
     * The source TcpListener is left in an unconfigured state.
     * 
     * @param other The TcpListener to move from
     */
    TcpListener(TcpListener&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     * 
     * Transfers ownership of the listening socket from another TcpListener.
     * The current socket (if any) is closed first.
     * 
     * @param other The TcpListener to move from
     * @return TcpListener& Reference to this object
     */
    TcpListener& operator=(TcpListener&& other) noexcept = default;

    // Delete copy operations - sockets cannot be safely copied
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // =========================================================================
    // Socket Management
    // =========================================================================

    /**
     * @brief Start listening on the specified host and port
     * 
     * Creates a socket, binds it to the specified address, and begins listening.
     * This method performs the complete socket initialization sequence:
     * 1. socket() - Create socket
     * 2. setsockopt() - Set SO_REUSEADDR to allow quick restart
     * 3. bind() - Bind to address/port
     * 4. listen() - Start listening for connections
     * 
     * @param host The hostname or IP address to bind to (e.g., "127.0.0.1")
     * @param port The port number to listen on (e.g., 8080)
     * @param non_blocking If true, sets the socket to non-blocking mode (for event loop)
     * @return true if successful, false on error
     */
    bool start(const std::string& host, uint16_t port, bool non_blocking = false);

    /**
     * @brief Accept an incoming connection
     * 
     * Blocks until a client connects, then returns the client's socket file descriptor.
     * The returned descriptor is owned by the caller and must be closed properly.
     * 
     * @return std::optional<int> The client socket descriptor, or std::nullopt on error
     * 
     * @note In Phase 2, this is a blocking call. Phase 8 will add non-blocking support.
     * @note The caller is responsible for closing the returned descriptor.
     */
    std::optional<int> accept();

    /**
     * @brief Stop listening and close the socket
     * 
     * Closes the listening socket and resets the TcpListener to an unconfigured state.
     * Can be called to explicitly stop listening before destruction.
     */
    void stop();

    // =========================================================================
    // Accessors
    // =========================================================================

    /**
     * @brief Check if the listener is currently active
     * 
     * @return true if the listener has a valid socket and is listening, false otherwise
     */
    bool is_listening() const;

    /**
     * @brief Get the bound port number
     * 
     * @return uint16_t The port number, or 0 if not bound
     */
    uint16_t port() const { return port_; }

    /**
     * @brief Get the bound host address
     * 
     * @return std::string The host address, or empty if not bound
     */
    const std::string& host() const { return host_; }

    /**
     * @brief Get the last error code
     * 
     * Returns the system error code from the last failed operation.
     * 
     * @return int The error code, or 0 if no error
     */
    int error_code() const { return last_error_; }

    /**
     * @brief Get the last error message
     * 
     * Returns a human-readable error message for the last failed operation.
     * 
     * @return std::string The error message, or empty if no error
     */
    std::string error_message() const;

    /**
     * @brief Get the raw socket descriptor
     * 
     * Returns the underlying socket descriptor without transferring ownership.
     * Useful for low-level operations, but use with caution.
     * 
     * @return socket_type The socket descriptor, or INVALID_SOCKET_VALUE if invalid
     */
    socket_type get_socket() const;

private:
    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /**
     * @brief Initialize Windows Sockets (Windows only)
     * 
     * On Windows, WSAStartup must be called before using socket functions.
     * This method handles that initialization.
     * 
     * @return true if successful, false on error
     */
    static bool initialize_windows_sockets();

    /**
     * @brief Cleanup Windows Sockets (Windows only)
     * 
     * On Windows, WSACleanup should be called when done with sockets.
     */
    static void cleanup_windows_sockets();

    /**
     * @brief Create a TCP socket
     * 
     * Creates a socket for IPv4 TCP communication.
     * 
     * @return socket_type The socket descriptor, or INVALID_SOCKET_VALUE on error
     */
    socket_type create_socket();

    /**
     * @brief Set socket options
     * 
     * Sets SO_REUSEADDR to allow the socket to be bound to an address that
     * is already in use (useful for quick server restarts).
     * 
     * @param sock The socket descriptor
     * @return true if successful, false on error
     */
    bool set_socket_options(socket_type sock);

    /**
     * @brief Bind the socket to an address and port
     * 
     * Associates the socket with a specific IP address and port.
     * 
     * @param sock The socket descriptor
     * @param host The hostname or IP address
     * @param port The port number
     * @return true if successful, false on error
     */
    bool bind_socket(socket_type sock, const std::string& host, uint16_t port);

    /**
     * @brief Start listening on the socket
     * 
     * Marks the socket as passive and ready to accept incoming connections.
     * 
     * @param sock The socket descriptor
     * @param backlog The maximum length of the pending connections queue
     * @return true if successful, false on error
     */
    bool listen_socket(socket_type sock, int backlog = 128);

    /**
     * @brief Set a socket to non-blocking mode (Phase 8)
     * 
     * Configures the socket to operate in non-blocking mode, which is required
     * for event-driven I/O with epoll/select. In non-blocking mode, operations
     * return immediately with EAGAIN/EWOULDBLOCK if they would block.
     * 
     * @param sock The socket to configure
     * @return true if successful, false on error
     */
    bool set_non_blocking(socket_type sock);

    /**
     * @brief Convert hostname to address structure
     * 
     * Converts a hostname or IP string to a sockaddr_in structure.
     * 
     * @param host The hostname or IP address
     * @param port The port number
     * @param addr_out Output parameter for the address structure
     * @return true if successful, false on error
     */
    bool resolve_address(const std::string& host, uint16_t port, 
                        struct sockaddr_in& addr_out);

    // =========================================================================
    // Member Variables
    // =========================================================================

    UniqueFd socket_fd_;           ///< RAII wrapper for the socket descriptor
    std::string host_;             ///< Bound host address
    uint16_t port_;                ///< Bound port number
    int last_error_;               ///< Last error code from socket operations
    static bool windows_initialized_; ///< Track Windows socket initialization
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline TcpListener::TcpListener() 
    : port_(0), last_error_(0) {
}

inline TcpListener::TcpListener(const std::string& host, uint16_t port)
    : port_(0), last_error_(0) {
    start(host, port);
}

inline bool TcpListener::is_listening() const {
    return socket_fd_.is_valid();
}

inline socket_type TcpListener::get_socket() const {
#ifdef _WIN32
    // On Windows, UniqueFd stores the SOCKET cast to int
    // We need to cast it back for Windows socket APIs
    return static_cast<SOCKET>(socket_fd_.get());
#else
    // On Unix, file descriptors are already int
    return static_cast<socket_type>(socket_fd_.get());
#endif
}

} // namespace aevrix
