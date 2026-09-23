// =============================================================================
// Aevrix - TcpListener Implementation
// =============================================================================
// This file implements the TcpListener class for TCP socket management.
// Implements cross-platform socket operations for Windows and Unix/Linux.
//
// Implementation Notes:
// - Windows requires WSAStartup/WSACleanup for socket initialization
// - Socket descriptors are different types on Windows (SOCKET) vs Unix (int)
// - Error handling uses errno on Unix and WSAGetLastError() on Windows
// - SO_REUSEADDR behavior differs slightly between platforms
// =============================================================================

#include "aevrix/tcp_listener.h"
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <errno.h>
#endif

namespace aevrix {

// =============================================================================
// Static Member Initialization
// =============================================================================

bool TcpListener::windows_initialized_ = false;

// =============================================================================
// Windows Socket Initialization
// =============================================================================

bool TcpListener::initialize_windows_sockets() {
#ifdef _WIN32
    if (!windows_initialized_) {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            std::cerr << "WSAStartup failed: " << result << "\n";
            return false;
        }
        windows_initialized_ = true;
    }
#endif
    return true;
}

void TcpListener::cleanup_windows_sockets() {
#ifdef _WIN32
    if (windows_initialized_) {
        WSACleanup();
        windows_initialized_ = false;
    }
#endif
}

// =============================================================================
// Socket Creation
// =============================================================================

socket_type TcpListener::create_socket() {
    // Initialize Windows sockets if needed
    if (!initialize_windows_sockets()) {
        last_error_ = -1;
        return INVALID_SOCKET_VALUE;
    }

#ifdef _WIN32
    // On Windows, create a socket using the Windows socket API
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        last_error_ = WSAGetLastError();
        std::cerr << "socket() failed: " << last_error_ << "\n";
        return INVALID_SOCKET_VALUE;
    }
    return sock;
#else
    // On Unix/Linux, create a socket using the POSIX socket API
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        last_error_ = errno;
        std::cerr << "socket() failed: " << strerror(last_error_) << "\n";
        return INVALID_SOCKET_VALUE;
    }
    return sock;
#endif
}

// =============================================================================
// Socket Options
// =============================================================================

bool TcpListener::set_socket_options(socket_type sock) {
    // Set SO_REUSEADDR to allow binding to addresses in TIME_WAIT state
    // This is useful for quick server restarts during development
    int opt = 1;
    
#ifdef _WIN32
    // Windows uses const char* for option values
    int result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
                          reinterpret_cast<const char*>(&opt), sizeof(opt));
    if (result == SOCKET_ERROR) {
        last_error_ = WSAGetLastError();
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << last_error_ << "\n";
        return false;
    }
#else
    // Unix uses const void* for option values
    int result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
                          &opt, sizeof(opt));
    if (result < 0) {
        last_error_ = errno;
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(last_error_) << "\n";
        return false;
    }
#endif

    return true;
}

// =============================================================================
// Address Resolution
// =============================================================================

bool TcpListener::resolve_address(const std::string& host, uint16_t port,
                                 struct sockaddr_in& addr_out) {
    // Initialize the address structure
    std::memset(&addr_out, 0, sizeof(addr_out));
    addr_out.sin_family = AF_INET;
    addr_out.sin_port = htons(port);  // Convert to network byte order

    // Convert hostname/IP to network address
    if (host.empty() || host == "0.0.0.0" || host == "*") {
        // Bind to all interfaces (INADDR_ANY)
        addr_out.sin_addr.s_addr = INADDR_ANY;
    } else {
        // Convert IP string to network address
#ifdef _WIN32
        // Windows uses inet_pton for IP conversion
        int result = inet_pton(AF_INET, host.c_str(), &addr_out.sin_addr);
        if (result <= 0) {
            last_error_ = WSAGetLastError();
            std::cerr << "inet_pton() failed for host: " << host << "\n";
            return false;
        }
#else
        // Unix uses inet_pton for IP conversion
        int result = inet_pton(AF_INET, host.c_str(), &addr_out.sin_addr);
        if (result <= 0) {
            last_error_ = errno;
            std::cerr << "inet_pton() failed for host: " << host << "\n";
            return false;
        }
#endif
    }

    return true;
}

// =============================================================================
// Socket Binding
// =============================================================================

bool TcpListener::bind_socket(socket_type sock, const std::string& host, uint16_t port) {
    // Resolve the address
    struct sockaddr_in addr;
    if (!resolve_address(host, port, addr)) {
        return false;
    }

    // Bind the socket to the address
#ifdef _WIN32
    int result = bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (result == SOCKET_ERROR) {
        last_error_ = WSAGetLastError();
        std::cerr << "bind() failed on " << host << ":" << port 
                  << " - Error: " << last_error_ << "\n";
        return false;
    }
#else
    int result = bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (result < 0) {
        last_error_ = errno;
        std::cerr << "bind() failed on " << host << ":" << port 
                  << " - Error: " << strerror(last_error_) << "\n";
        return false;
    }
#endif

    std::cout << "Successfully bound to " << host << ":" << port << "\n";
    return true;
}

// =============================================================================
// Socket Listening
// =============================================================================

bool TcpListener::listen_socket(socket_type sock, int backlog) {
#ifdef _WIN32
    int result = listen(sock, backlog);
    if (result == SOCKET_ERROR) {
        last_error_ = WSAGetLastError();
        std::cerr << "listen() failed: " << last_error_ << "\n";
        return false;
    }
#else
    int result = listen(sock, backlog);
    if (result < 0) {
        last_error_ = errno;
        std::cerr << "listen() failed: " << strerror(last_error_) << "\n";
        return false;
    }
#endif

    std::cout << "Listening for connections (backlog=" << backlog << ")\n";
    return true;
}

// =============================================================================
// Public Methods
// =============================================================================

bool TcpListener::start(const std::string& host, uint16_t port) {
    // Store the configuration
    host_ = host;
    port_ = port;
    last_error_ = 0;

    std::cout << "Starting TCP listener on " << host << ":" << port << "\n";

    // Create the socket
    socket_type sock = create_socket();
    if (sock == INVALID_SOCKET_VALUE) {
        return false;
    }

    // Set socket options
    if (!set_socket_options(sock)) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return false;
    }

    // Bind the socket
    if (!bind_socket(sock, host, port)) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return false;
    }

    // Start listening
    if (!listen_socket(sock)) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return false;
    }

    // Store the socket in our RAII wrapper
    // On Windows, we need to cast SOCKET to int for UniqueFd
#ifdef _WIN32
    socket_fd_.reset(static_cast<int>(sock));
#else
    socket_fd_.reset(sock);
#endif

    std::cout << "TCP listener started successfully\n";
    return true;
}

std::optional<int> TcpListener::accept() {
    if (!is_listening()) {
        std::cerr << "Cannot accept: listener is not active\n";
        return std::nullopt;
    }

    // Accept a connection (blocking call in Phase 2)
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

#ifdef _WIN32
    SOCKET client_sock = ::accept(get_socket(), 
                                 reinterpret_cast<struct sockaddr*>(&client_addr), 
                                 &client_len);
    if (client_sock == INVALID_SOCKET) {
        last_error_ = WSAGetLastError();
        std::cerr << "accept() failed: " << last_error_ << "\n";
        return std::nullopt;
    }
    
    // Convert Windows SOCKET to int for our API
    int client_fd = static_cast<int>(client_sock);
#else
    int client_fd = ::accept(get_socket(), 
                             reinterpret_cast<struct sockaddr*>(&client_addr), 
                             &client_len);
    if (client_fd < 0) {
        last_error_ = errno;
        std::cerr << "accept() failed: " << strerror(last_error_) << "\n";
        return std::nullopt;
    }
#endif

    // Get client IP address for logging
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    std::cout << "Accepted connection from " << client_ip 
              << ":" << ntohs(client_addr.sin_port) << "\n";

    return client_fd;
}

void TcpListener::stop() {
    if (is_listening()) {
        std::cout << "Stopping TCP listener\n";
        socket_fd_.reset(-1);  // UniqueFd will close the socket
        host_.clear();
        port_ = 0;
        last_error_ = 0;
    }
}

std::string TcpListener::error_message() const {
    if (last_error_ == 0) {
        return "No error";
    }

#ifdef _WIN32
    char message[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  nullptr, last_error_, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  message, sizeof(message), nullptr);
    return std::string(message);
#else
    return std::string(strerror(last_error_));
#endif
}

} // namespace aevrix
