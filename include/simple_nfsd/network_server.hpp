/**
 * @file network_server.hpp
 * @brief Network server for handling TCP/UDP connections
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#ifndef SIMPLE_NFSD_NETWORK_SERVER_HPP
#define SIMPLE_NFSD_NETWORK_SERVER_HPP

#include "simple_nfsd/rpc_protocol.hpp"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace SimpleNfsd {

// Connection types
enum class ConnectionType {
    TCP,
    UDP
};

// Connection state
enum class ConnectionState {
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    DISCONNECTED
};

// Network statistics
struct NetworkStats {
    std::atomic<uint64_t> total_connections{0};
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> packets_received{0};
    std::atomic<uint64_t> packets_sent{0};
    std::atomic<uint64_t> connection_errors{0};
    std::atomic<uint64_t> protocol_errors{0};
};

// Client connection information
struct ClientConnection {
    int socket_fd;
    sockaddr_in address;
    ConnectionType type;
    ConnectionState state;
    std::string client_ip;
    uint16_t client_port;
    std::chrono::steady_clock::time_point connect_time;
    std::chrono::steady_clock::time_point last_activity;
    std::vector<uint8_t> receive_buffer;
    std::vector<uint8_t> send_buffer;
    
    ClientConnection(int fd, const sockaddr_in& addr, ConnectionType t)
        : socket_fd(fd), address(addr), type(t), state(ConnectionState::CONNECTING) {
        client_ip = inet_ntoa(addr.sin_addr);
        client_port = ntohs(addr.sin_port);
        connect_time = std::chrono::steady_clock::now();
        last_activity = connect_time;
    }
    
    ~ClientConnection() {
        if (socket_fd >= 0) {
            close(socket_fd);
        }
    }
};

// Message handler callback
using MessageHandler = std::function<std::vector<uint8_t>(const RpcMessage&, ClientConnection&)>;

// Network server class
class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();
    
    // Server lifecycle
    bool start(const std::string& bind_address, uint16_t port, ConnectionType type);
    void stop();
    bool isRunning() const;
    
    // Connection management
    void setMaxConnections(uint32_t max_conn);
    void setConnectionTimeout(std::chrono::seconds timeout);
    void setReceiveBufferSize(size_t size);
    void setSendBufferSize(size_t size);
    
    // Message handling
    void setMessageHandler(MessageHandler handler);
    void setErrorHandler(std::function<void(const std::string&)> handler);
    
    // Statistics
    NetworkStats getStats() const;
    std::vector<ClientConnection> getActiveConnections() const;
    
    // Utility functions
    static std::string getLocalAddress();
    static std::vector<std::string> getAvailableAddresses();
    
private:
    // Server state
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;
    std::string bind_address_;
    uint16_t port_;
    ConnectionType connection_type_;
    int server_socket_;
    
    // Configuration
    uint32_t max_connections_;
    std::chrono::seconds connection_timeout_;
    size_t receive_buffer_size_;
    size_t send_buffer_size_;
    
    // Threading
    std::unique_ptr<std::thread> accept_thread_;
    std::vector<std::unique_ptr<std::thread>> worker_threads_;
    std::atomic<uint32_t> active_connections_;
    
    // Connection management
    mutable std::mutex connections_mutex_;
    std::map<int, std::unique_ptr<ClientConnection>> connections_;
    
    // Message handling
    MessageHandler message_handler_;
    std::function<void(const std::string&)> error_handler_;
    
    // Statistics
    mutable NetworkStats stats_;
    
    // Private methods
    void acceptLoop();
    void workerLoop();
    void handleConnection(std::unique_ptr<ClientConnection> connection);
    void cleanupConnections();
    void sendResponse(ClientConnection& connection, const std::vector<uint8_t>& data);
    bool receiveData(ClientConnection& connection);
    void processMessages(ClientConnection& connection);
    void handleError(const std::string& error);
    
    // Socket utilities
    bool createSocket();
    bool bindSocket();
    bool listenSocket();
    void closeSocket();
    
    // Connection utilities
    bool isValidConnection(const ClientConnection& connection) const;
    void updateConnectionActivity(ClientConnection& connection);
    void removeConnection(int socket_fd);
};

// TCP-specific server
class TcpServer : public NetworkServer {
public:
    TcpServer() : NetworkServer() {}
    
    bool start(const std::string& bind_address, uint16_t port) {
        return NetworkServer::start(bind_address, port, ConnectionType::TCP);
    }
};

// UDP-specific server
class UdpServer : public NetworkServer {
public:
    UdpServer() : NetworkServer() {}
    
    bool start(const std::string& bind_address, uint16_t port) {
        return NetworkServer::start(bind_address, port, ConnectionType::UDP);
    }
};

// Network utilities
class NetworkUtils {
public:
    // Address utilities
    static bool isValidAddress(const std::string& address);
    static bool isPortAvailable(uint16_t port);
    static std::vector<uint16_t> getAvailablePorts(uint16_t start_port, uint16_t end_port);
    
    // Socket utilities
    static int createSocket(ConnectionType type);
    static bool bindSocket(int socket_fd, const std::string& address, uint16_t port);
    static bool setSocketOptions(int socket_fd, ConnectionType type);
    static void closeSocket(int socket_fd);
    
    // Data utilities
    static bool sendData(int socket_fd, const std::vector<uint8_t>& data);
    static std::vector<uint8_t> receiveData(int socket_fd, size_t max_size = 65536);
    static bool sendUdpData(int socket_fd, const sockaddr_in& address, 
                           const std::vector<uint8_t>& data);
    static std::vector<uint8_t> receiveUdpData(int socket_fd, sockaddr_in& address,
                                              size_t max_size = 65536);
    
    // Network information
    static std::string getHostname();
    static std::vector<std::string> getInterfaces();
    static std::string getInterfaceAddress(const std::string& interface);
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_NETWORK_SERVER_HPP
