/**
 * @file nfs_server_simple.cpp
 * @brief Simple NFS server implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include "simple_nfsd/nfs_server_simple.hpp"
#include "simple_nfsd/rpc_protocol.hpp"
#include "simple_nfsd/auth_manager.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>

// Helper function for 64-bit network byte order conversion
static uint64_t htonll_custom(uint64_t value) {
    static const int num = 42;
    if (*(char *)&num == 42) {
        // Little endian
        const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
        const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFF));
        return (static_cast<uint64_t>(low_part) << 32) | high_part;
    } else {
        // Big endian
        return value;
    }
}

static uint64_t ntohll_custom(uint64_t value) {
    // ntohll is the same as htonll (symmetric operation)
    return htonll_custom(value);
}

namespace SimpleNfsd {

NfsServerSimple::NfsServerSimple() 
    : running_(false), initialized_(false), next_handle_id_(1) {
    auth_manager_ = std::make_unique<AuthManager>();
    portmapper_ = std::make_unique<Portmapper>();
}

NfsServerSimple::~NfsServerSimple() {
    stop();
}

bool NfsServerSimple::initialize(const NfsServerConfig& config) {
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    // Initialize authentication
    if (!initializeAuthentication()) {
        std::cerr << "Failed to initialize authentication" << std::endl;
        return false;
    }
    
    // Initialize portmapper
    if (!portmapper_->initialize()) {
        std::cerr << "Failed to initialize portmapper" << std::endl;
        return false;
    }
    
    // Register NFS services with portmapper
    if (config.enable_tcp) {
        portmapper_->registerService(static_cast<uint32_t>(RpcProgram::NFS), 2, 6, config.port, "simple-nfsd");
        portmapper_->registerService(static_cast<uint32_t>(RpcProgram::NFS), 3, 6, config.port, "simple-nfsd");
        portmapper_->registerService(static_cast<uint32_t>(RpcProgram::NFS), 4, 6, config.port, "simple-nfsd");
    }
    if (config.enable_udp) {
        portmapper_->registerService(static_cast<uint32_t>(RpcProgram::NFS), 2, 17, config.port, "simple-nfsd");
        portmapper_->registerService(static_cast<uint32_t>(RpcProgram::NFS), 3, 17, config.port, "simple-nfsd");
        portmapper_->registerService(static_cast<uint32_t>(RpcProgram::NFS), 4, 17, config.port, "simple-nfsd");
    }
    
    // Create root directory if it doesn't exist
    std::filesystem::create_directories(config_.root_path);
    
    initialized_ = true;
    return true;
}

bool NfsServerSimple::loadConfiguration(const std::string& config_file) {
    ConfigManager config_manager;
    if (!config_manager.loadFromFile(config_file)) {
        return false;
    }
    
    const auto& nfsd_config = config_manager.getConfig();
    
    NfsServerConfig config;
    config.bind_address = nfsd_config.listen_address;
    config.port = static_cast<uint16_t>(nfsd_config.listen_port);
    config.root_path = "/var/lib/simple-nfsd/shares"; // Default path
    config.max_connections = static_cast<uint32_t>(nfsd_config.max_connections);
    config.enable_tcp = true; // Default to true
    config.enable_udp = true; // Default to true
    
    return initialize(config);
}

bool NfsServerSimple::start() {
    if (!initialized_) {
        std::cerr << "Server not initialized" << std::endl;
        return false;
    }
    
    if (running_) {
        return true;
    }
    
    // Start TCP listener thread
    if (config_.enable_tcp) {
        tcp_thread_ = std::make_unique<std::thread>(&NfsServerSimple::tcpListenerLoop, this);
    }
    
    // Start UDP listener thread
    if (config_.enable_udp) {
        udp_thread_ = std::make_unique<std::thread>(&NfsServerSimple::udpListenerLoop, this);
    }
    
    running_ = true;
    std::cout << "NFS server started on " << config_.bind_address << ":" << config_.port << std::endl;
    return true;
}

void NfsServerSimple::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Wait for threads to finish
    if (tcp_thread_ && tcp_thread_->joinable()) {
        tcp_thread_->join();
    }
    if (udp_thread_ && udp_thread_->joinable()) {
        udp_thread_->join();
    }
    
    tcp_thread_.reset();
    udp_thread_.reset();
    
    std::cout << "NFS server stopped" << std::endl;
}

bool NfsServerSimple::isRunning() const {
    return running_;
}

NfsServerConfig NfsServerSimple::getConfig() const {
    return config_;
}

bool NfsServerSimple::updateConfig(const NfsServerConfig& config) {
    if (running_) {
        return false; // Cannot update config while running
    }
    
    config_ = config;
    return true;
}

NfsServerStats NfsServerSimple::getStats() const {
    NfsServerStats result;
    result.total_requests = total_requests_.load();
    result.successful_requests = successful_requests_.load();
    result.failed_requests = failed_requests_.load();
    result.bytes_read = bytes_read_.load();
    result.bytes_written = bytes_written_.load();
    result.active_connections = active_connections_.load();
    return result;
}

void NfsServerSimple::resetStats() {
    total_requests_ = 0;
    successful_requests_ = 0;
    failed_requests_ = 0;
    bytes_read_ = 0;
    bytes_written_ = 0;
    active_connections_ = 0;
}

bool NfsServerSimple::isHealthy() const {
    return running_ && initialized_;
}

std::string NfsServerSimple::getHealthStatus() const {
    if (!initialized_) {
        return "Not initialized";
    }
    if (!running_) {
        return "Not running";
    }
    return "Healthy";
}

void NfsServerSimple::tcpListenerLoop() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Failed to create TCP socket: " << strerror(errno) << std::endl;
        return;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set TCP socket options: " << strerror(errno) << std::endl;
        close(server_socket);
        return;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config_.port);
    
    if (config_.bind_address == "0.0.0.0") {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, config_.bind_address.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid bind address: " << config_.bind_address << std::endl;
            close(server_socket);
            return;
        }
    }
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind TCP socket: " << strerror(errno) << std::endl;
        close(server_socket);
        return;
    }
    
    if (listen(server_socket, 128) < 0) {
        std::cerr << "Failed to listen on TCP socket: " << strerror(errno) << std::endl;
        close(server_socket);
        return;
    }
    
    std::cout << "TCP server listening on " << config_.bind_address << ":" << config_.port << std::endl;
    
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            if (running_) {
                std::cerr << "Failed to accept TCP connection: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        // Handle client connection in a separate thread
        std::thread client_thread(&NfsServerSimple::handleClientConnection, this, client_socket);
        client_thread.detach();
    }
    
    close(server_socket);
}

void NfsServerSimple::udpListenerLoop() {
    udp_server_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_server_socket_ < 0) {
        std::cerr << "Failed to create UDP socket: " << strerror(errno) << std::endl;
        return;
    }
    
    int server_socket = udp_server_socket_;  // Keep for compatibility
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set UDP socket options: " << strerror(errno) << std::endl;
        close(server_socket);
        return;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config_.port);
    
    if (config_.bind_address == "0.0.0.0") {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, config_.bind_address.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid bind address: " << config_.bind_address << std::endl;
            close(server_socket);
            return;
        }
    }
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind UDP socket: " << strerror(errno) << std::endl;
        close(server_socket);
        return;
    }
    
    std::cout << "UDP server listening on " << config_.bind_address << ":" << config_.port << std::endl;
    
    // Set socket to non-blocking
    int flags = fcntl(server_socket, F_GETFL, 0);
    fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);
    
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        char buffer[65536];
        ssize_t bytes_received = recvfrom(server_socket, buffer, sizeof(buffer), 0,
                                        (struct sockaddr*)&client_addr, &client_len);
        
        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (running_) {
                std::cerr << "Failed to receive UDP data: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        if (bytes_received > 0) {
            // Create client connection info for UDP
            ClientConnection client_conn;
            client_conn.is_tcp = false;
            client_conn.udp_socket = udp_server_socket_;
            client_conn.udp_addr = client_addr;
            
            // Process the message
            std::vector<uint8_t> message_data(buffer, buffer + bytes_received);
            processRpcMessage(client_conn, message_data);
        }
    }
    
    close(server_socket);
}

void NfsServerSimple::handleClientConnection(int client_socket) {
    total_requests_++;
    active_connections_++;
    
    char buffer[65536];
    
    while (running_) {
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            if (bytes_received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break;
        }
        
        // Create client connection info for TCP
        ClientConnection client_conn;
        client_conn.is_tcp = true;
        client_conn.tcp_socket = client_socket;
        
        // Process the message
        std::vector<uint8_t> message_data(buffer, buffer + bytes_received);
        processRpcMessage(client_conn, message_data);
    }
    
    close(client_socket);
    active_connections_--;
}

void NfsServerSimple::processRpcMessage(const ClientConnection& client_conn, const std::vector<uint8_t>& raw_message) {
    try {
        total_requests_++;
        
        // Parse RPC message
        RpcMessage message = RpcUtils::deserializeMessage(raw_message);
        
        // Validate message
        if (!RpcUtils::validateMessage(message)) {
            std::cerr << "Invalid RPC message received" << std::endl;
            failed_requests_++;
            // Send error reply
            RpcMessage error_reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::GARBAGE_ARGS, {});
            sendReply(error_reply, client_conn);
            return;
        }
        
        // Handle RPC call
        if (message.header.type == RpcMessageType::CALL) {
            handleRpcCall(message, client_conn);
        } else {
            std::cerr << "Unexpected RPC message type" << std::endl;
            failed_requests_++;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing RPC message: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleRpcCall(const RpcMessage& message, const ClientConnection& client_conn) {
    try {
        // Route based on RPC program
        if (message.header.prog == static_cast<uint32_t>(RpcProgram::PORTMAP)) {
            handlePortmapperCall(message, client_conn);
            return;
        }
        
        if (message.header.prog != static_cast<uint32_t>(RpcProgram::NFS)) {
            std::cerr << "Unsupported RPC program: " << message.header.prog << std::endl;
            failed_requests_++;
            RpcMessage error_reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::PROG_UNAVAIL, {});
            sendReply(error_reply, client_conn);
            return;
        }
        
        // Authenticate the request
        AuthContext auth_context;
        if (!authenticateRequest(message, auth_context)) {
            std::cerr << "Authentication failed for RPC call" << std::endl;
            failed_requests_++;
            RpcMessage error_reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SYSTEM_ERR, {});
            sendReply(error_reply, client_conn);
            return;
        }
        
        // Negotiate NFS version
        uint32_t negotiated_version = negotiateNfsVersion(message.header.vers);
        if (negotiated_version == 0) {
            std::cerr << "No compatible NFS version found for: " << message.header.vers << std::endl;
            failed_requests_++;
            RpcMessage error_reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::PROG_MISMATCH, {});
            sendReply(error_reply, client_conn);
            return;
        }
        
        // Handle based on negotiated NFS version
        switch (negotiated_version) {
            case 2:
                handleNfsv2Call(message, auth_context, client_conn);
                break;
            case 3:
                handleNfsv3Call(message, auth_context, client_conn);
                break;
            case 4:
                handleNfsv4Call(message, auth_context, client_conn);
                break;
            default:
                std::cerr << "Unsupported NFS version after negotiation: " << negotiated_version << std::endl;
                failed_requests_++;
                RpcMessage error_reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::PROG_MISMATCH, {});
                sendReply(error_reply, client_conn);
                return;
        }
        
        successful_requests_++;
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling RPC call: " << e.what() << std::endl;
        failed_requests_++;
    }
}

bool NfsServerSimple::sendReply(const RpcMessage& reply, const ClientConnection& client_conn) {
    try {
        // Serialize the reply message
        std::vector<uint8_t> reply_data = RpcUtils::serializeMessage(reply);
        
        if (client_conn.is_tcp) {
            // Send via TCP
            if (client_conn.tcp_socket < 0) {
                std::cerr << "Invalid TCP socket for reply" << std::endl;
                return false;
            }
            
            ssize_t bytes_sent = send(client_conn.tcp_socket, reply_data.data(), reply_data.size(), 0);
            if (bytes_sent < 0) {
                std::cerr << "Failed to send TCP reply: " << strerror(errno) << std::endl;
                return false;
            }
            
            if (static_cast<size_t>(bytes_sent) != reply_data.size()) {
                std::cerr << "Partial TCP reply sent: " << bytes_sent << " of " << reply_data.size() << std::endl;
                return false;
            }
        } else {
            // Send via UDP
            if (client_conn.udp_socket < 0) {
                std::cerr << "Invalid UDP socket for reply" << std::endl;
                return false;
            }
            
            ssize_t bytes_sent = sendto(client_conn.udp_socket, reply_data.data(), reply_data.size(), 0,
                                       (struct sockaddr*)&client_conn.udp_addr, sizeof(client_conn.udp_addr));
            if (bytes_sent < 0) {
                std::cerr << "Failed to send UDP reply: " << strerror(errno) << std::endl;
                return false;
            }
            
            if (static_cast<size_t>(bytes_sent) != reply_data.size()) {
                std::cerr << "Partial UDP reply sent: " << bytes_sent << " of " << reply_data.size() << std::endl;
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error sending reply: " << e.what() << std::endl;
        return false;
    }
}

void NfsServerSimple::handleNfsv2Call(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    // Handle NFSv2 procedures
    switch (message.header.proc) {
        case 0:  // NULL
            handleNfsv2Null(message, auth_context, client_conn);
            break;
        case 1:  // GETATTR
            handleNfsv2GetAttr(message, auth_context, client_conn);
            break;
        case 2:  // SETATTR
            handleNfsv2SetAttr(message, auth_context, client_conn);
            break;
        case 3:  // LOOKUP
            handleNfsv2Lookup(message, auth_context, client_conn);
            break;
        case 4:  // LINK
            handleNfsv2Link(message, auth_context, client_conn);
            break;
        case 5:  // READ
            handleNfsv2Read(message, auth_context, client_conn);
            break;
        case 6:  // SYMLINK
            handleNfsv2SymLink(message, auth_context, client_conn);
            break;
        case 7:  // WRITE
            handleNfsv2Write(message, auth_context, client_conn);
            break;
        case 8:  // CREATE
            handleNfsv2Create(message, auth_context, client_conn);
            break;
        case 9:  // MKDIR
            handleNfsv2MkDir(message, auth_context, client_conn);
            break;
        case 10: // RMDIR
            handleNfsv2RmDir(message, auth_context, client_conn);
            break;
        case 11: // REMOVE
            handleNfsv2Remove(message, auth_context, client_conn);
            break;
        case 12: // RENAME
            handleNfsv2Rename(message, auth_context, client_conn);
            break;
        case 15: // READDIR
            handleNfsv2ReadDir(message, auth_context, client_conn);
            break;
        case 16: // STATFS
            handleNfsv2StatFS(message, auth_context, client_conn);
            break;
        default:
            std::cerr << "Unsupported NFSv2 procedure: " << message.header.proc << std::endl;
            failed_requests_++;
            break;
    }
}

void NfsServerSimple::handleNfsv3Call(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    // Handle NFSv3 procedures
    switch (message.header.proc) {
        case 0:  // NULL
            handleNfsv3Null(message, auth_context, client_conn);
            break;
        case 1:  // GETATTR
            handleNfsv3GetAttr(message, auth_context, client_conn);
            break;
        case 2:  // SETATTR
            handleNfsv3SetAttr(message, auth_context, client_conn);
            break;
        case 3:  // LOOKUP
            handleNfsv3Lookup(message, auth_context, client_conn);
            break;
        case 4:  // ACCESS
            handleNfsv3Access(message, auth_context, client_conn);
            break;
        case 5:  // READLINK
            handleNfsv3ReadLink(message, auth_context, client_conn);
            break;
        case 6:  // READ
            handleNfsv3Read(message, auth_context, client_conn);
            break;
        case 7:  // WRITE
            handleNfsv3Write(message, auth_context, client_conn);
            break;
        case 8:  // CREATE
            handleNfsv3Create(message, auth_context, client_conn);
            break;
        case 9:  // MKDIR
            handleNfsv3MkDir(message, auth_context, client_conn);
            break;
        case 10: // SYMLINK
            handleNfsv3SymLink(message, auth_context, client_conn);
            break;
        case 11: // MKNOD
            handleNfsv3MkNod(message, auth_context, client_conn);
            break;
        case 12: // REMOVE
            handleNfsv3Remove(message, auth_context, client_conn);
            break;
        case 13: // RMDIR
            handleNfsv3RmDir(message, auth_context, client_conn);
            break;
        case 14: // RENAME
            handleNfsv3Rename(message, auth_context, client_conn);
            break;
        case 15: // LINK
            handleNfsv3Link(message, auth_context, client_conn);
            break;
        case 16: // READDIR
            handleNfsv3ReadDir(message, auth_context, client_conn);
            break;
        case 17: // READDIRPLUS
            handleNfsv3ReadDirPlus(message, auth_context, client_conn);
            break;
        case 18: // FSSTAT
            handleNfsv3FSStat(message, auth_context, client_conn);
            break;
        case 19: // FSINFO
            handleNfsv3FSInfo(message, auth_context, client_conn);
            break;
        case 20: // PATHCONF
            handleNfsv3PathConf(message, auth_context, client_conn);
            break;
        case 21: // COMMIT
            handleNfsv3Commit(message, auth_context, client_conn);
            break;
        default:
            std::cerr << "Unsupported NFSv3 procedure: " << message.header.proc << std::endl;
            failed_requests_++;
            break;
    }
}

void NfsServerSimple::handleNfsv4Call(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    // Handle NFSv4 procedures
    switch (message.header.proc) {
        case 0:  // NULL
            handleNfsv4Null(message, auth_context, client_conn);
            break;
        case 1:  // COMPOUND
            handleNfsv4Compound(message, auth_context, client_conn);
            break;
        case 2:  // GETATTR
            handleNfsv4GetAttr(message, auth_context, client_conn);
            break;
        case 3:  // SETATTR
            handleNfsv4SetAttr(message, auth_context, client_conn);
            break;
        case 4:  // LOOKUP
            handleNfsv4Lookup(message, auth_context, client_conn);
            break;
        case 5:  // ACCESS
            handleNfsv4Access(message, auth_context, client_conn);
            break;
        case 6:  // READLINK
            handleNfsv4ReadLink(message, auth_context, client_conn);
            break;
        case 7:  // READ
            handleNfsv4Read(message, auth_context, client_conn);
            break;
        case 8:  // WRITE
            handleNfsv4Write(message, auth_context, client_conn);
            break;
        case 9:  // CREATE
            handleNfsv4Create(message, auth_context, client_conn);
            break;
        case 10: // MKDIR
            handleNfsv4MkDir(message, auth_context, client_conn);
            break;
        case 11: // SYMLINK
            handleNfsv4SymLink(message, auth_context, client_conn);
            break;
        case 12: // MKNOD
            handleNfsv4MkNod(message, auth_context, client_conn);
            break;
        case 13: // REMOVE
            handleNfsv4Remove(message, auth_context, client_conn);
            break;
        case 14: // RMDIR
            handleNfsv4RmDir(message, auth_context, client_conn);
            break;
        case 15: // RENAME
            handleNfsv4Rename(message, auth_context, client_conn);
            break;
        case 16: // LINK
            handleNfsv4Link(message, auth_context, client_conn);
            break;
        case 17: // READDIR
            handleNfsv4ReadDir(message, auth_context, client_conn);
            break;
        case 18: // READDIRPLUS
            handleNfsv4ReadDirPlus(message, auth_context, client_conn);
            break;
        case 19: // FSSTAT
            handleNfsv4FSStat(message, auth_context, client_conn);
            break;
        case 20: // FSINFO
            handleNfsv4FSInfo(message, auth_context, client_conn);
            break;
        case 21: // PATHCONF
            handleNfsv4PathConf(message, auth_context, client_conn);
            break;
        case 22: // COMMIT
            handleNfsv4Commit(message, auth_context, client_conn);
            break;
        case 23: // DELEGRETURN
            handleNfsv4DelegReturn(message, auth_context, client_conn);
            break;
        case 24: // GETACL
            handleNfsv4GetAcl(message, auth_context, client_conn);
            break;
        case 25: // SETACL
            handleNfsv4SetAcl(message, auth_context, client_conn);
            break;
        case 26: // FS_LOCATIONS
            handleNfsv4FSLocations(message, auth_context, client_conn);
            break;
        case 27: // RELEASE_LOCKOWNER
            handleNfsv4ReleaseLockOwner(message, auth_context, client_conn);
            break;
        case 28: // SECINFO
            handleNfsv4SecInfo(message, auth_context, client_conn);
            break;
        case 29: // FSID_PRESENT
            handleNfsv4FSIDPresent(message, auth_context, client_conn);
            break;
        case 30: // EXCHANGE_ID
            handleNfsv4ExchangeID(message, auth_context, client_conn);
            break;
        case 31: // CREATE_SESSION
            handleNfsv4CreateSession(message, auth_context, client_conn);
            break;
        case 32: // DESTROY_SESSION
            handleNfsv4DestroySession(message, auth_context, client_conn);
            break;
        case 33: // SEQUENCE
            handleNfsv4Sequence(message, auth_context, client_conn);
            break;
        case 34: // GET_DEVICE_INFO
            handleNfsv4GetDeviceInfo(message, auth_context, client_conn);
            break;
        case 35: // BIND_CONN_TO_SESSION
            handleNfsv4BindConnToSession(message, auth_context, client_conn);
            break;
        case 36: // DESTROY_CLIENTID
            handleNfsv4DestroyClientID(message, auth_context, client_conn);
            break;
        case 37: // RECLAIM_COMPLETE
            handleNfsv4ReclaimComplete(message, auth_context, client_conn);
            break;
        default:
            std::cerr << "Unsupported NFSv4 procedure: " << message.header.proc << std::endl;
            failed_requests_++;
            break;
    }
}

void NfsServerSimple::handlePortmapperCall(const RpcMessage& message, const ClientConnection& client_conn) {
    if (!portmapper_) {
        std::cerr << "Portmapper not initialized" << std::endl;
        failed_requests_++;
        return;
    }
    
    portmapper_->handleRpcCall(message);
}

void NfsServerSimple::handleNfsv2Null(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    // NULL procedure always succeeds (no authentication required)
    RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, {});
    sendReply(reply, client_conn);
    std::cout << "Handled NFSv2 NULL procedure (user: " << auth_context.uid << ")" << std::endl;
}

void NfsServerSimple::handleNfsv2GetAttr(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse file handle from message data
        if (message.data.size() < 4) {
            std::cerr << "Invalid GETATTR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (first 4 bytes)
        uint32_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 4);
        file_handle = ntohl(file_handle);
        
        // Look up file path from handle
        std::string file_path = getPathFromHandle(file_handle);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << file_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for GETATTR on: " << file_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file attributes
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path)) {
            std::cerr << "File not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        
        // Create NFSv2 file attributes response
        std::vector<uint8_t> response_data;
        
        // File type (1 = regular file, 2 = directory)
        uint32_t file_type = std::filesystem::is_directory(full_path) ? 2 : 1;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        // File mode (permissions)
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF; // Last 9 bits
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        // Number of links
        uint32_t nlink = 1; // Simplified
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        // User ID
        uint32_t uid = 0; // Simplified
        uid = htonl(uid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        
        // Group ID
        uint32_t gid = 0; // Simplified
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        // File size
        uint32_t file_size = 0;
        if (std::filesystem::is_regular_file(full_path)) {
            file_size = static_cast<uint32_t>(std::filesystem::file_size(full_path));
        }
        file_size = htonl(file_size);
        response_data.insert(response_data.end(), (uint8_t*)&file_size, (uint8_t*)&file_size + 4);
        
        // Block size
        uint32_t block_size = 512; // Default
        block_size = htonl(block_size);
        response_data.insert(response_data.end(), (uint8_t*)&block_size, (uint8_t*)&block_size + 4);
        
        // Number of blocks
        uint32_t blocks = (file_size + block_size - 1) / block_size;
        blocks = htonl(blocks);
        response_data.insert(response_data.end(), (uint8_t*)&blocks, (uint8_t*)&blocks + 4);
        
        // Rdev (device number)
        uint32_t rdev = 0;
        rdev = htonl(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 4);
        
        // File size (64-bit)
        uint64_t file_size_64 = file_size;
        file_size_64 = htonll_custom(file_size_64);
        response_data.insert(response_data.end(), (uint8_t*)&file_size_64, (uint8_t*)&file_size_64 + 8);
        
        // File system ID
        uint32_t fsid = 1; // Simplified
        fsid = htonl(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 4);
        
        // File ID
        uint32_t fileid = file_handle; // Use handle as file ID
        fileid = htonl(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 4);
        
        // Last access time
        uint32_t atime = 0; // Simplified
        atime = htonl(atime);
        response_data.insert(response_data.end(), (uint8_t*)&atime, (uint8_t*)&atime + 4);
        
        // Last modification time
        uint32_t mtime = 0; // Simplified
        mtime = htonl(mtime);
        response_data.insert(response_data.end(), (uint8_t*)&mtime, (uint8_t*)&mtime + 4);
        
        // Last change time
        uint32_t ctime = 0; // Simplified
        ctime = htonl(ctime);
        response_data.insert(response_data.end(), (uint8_t*)&ctime, (uint8_t*)&ctime + 4);
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 GETATTR for file: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in GETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Lookup(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse directory handle and filename from message data
        if (message.data.size() < 8) {
            std::cerr << "Invalid LOOKUP request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (first 4 bytes)
        uint32_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 4);
        dir_handle = ntohl(dir_handle);
        
        // Extract filename length (next 4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 4, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 8 + name_len) {
            std::cerr << "Invalid LOOKUP request: insufficient data for filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract filename
        std::string filename(reinterpret_cast<const char*>(message.data.data() + 8), name_len);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions for directory
        if (!checkAccess(dir_path, auth_context, true, false)) {
            std::cerr << "Access denied for LOOKUP in: " << dir_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = dir_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += filename;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if file exists
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (!std::filesystem::exists(fs_path)) {
            std::cerr << "File not found: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create file handle
        uint32_t file_handle = getHandleForPath(full_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // File handle
        uint32_t handle_net = htonl(file_handle);
        response_data.insert(response_data.end(), (uint8_t*)&handle_net, (uint8_t*)&handle_net + 4);
        
        // File attributes (simplified)
        uint32_t file_type = std::filesystem::is_directory(fs_path) ? 2 : 1;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        // File mode
        uint32_t file_mode = 0644; // Default permissions
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        // Number of links
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        // User ID
        uint32_t uid = 0;
        uid = htonl(uid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        
        // Group ID
        uint32_t gid = 0;
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        // File size
        uint32_t file_size = 0;
        if (std::filesystem::is_regular_file(fs_path)) {
            file_size = static_cast<uint32_t>(std::filesystem::file_size(fs_path));
        }
        file_size = htonl(file_size);
        response_data.insert(response_data.end(), (uint8_t*)&file_size, (uint8_t*)&file_size + 4);
        
        // Block size
        uint32_t block_size = 512;
        block_size = htonl(block_size);
        response_data.insert(response_data.end(), (uint8_t*)&block_size, (uint8_t*)&block_size + 4);
        
        // Number of blocks
        uint32_t blocks = (file_size + block_size - 1) / block_size;
        blocks = htonl(blocks);
        response_data.insert(response_data.end(), (uint8_t*)&blocks, (uint8_t*)&blocks + 4);
        
        // Rdev
        uint32_t rdev = 0;
        rdev = htonl(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 4);
        
        // File size (64-bit)
        uint64_t file_size_64 = file_size;
        file_size_64 = htonll_custom(file_size_64);
        response_data.insert(response_data.end(), (uint8_t*)&file_size_64, (uint8_t*)&file_size_64 + 8);
        
        // File system ID
        uint32_t fsid = 1;
        fsid = htonl(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 4);
        
        // File ID
        uint32_t fileid = file_handle;
        fileid = htonl(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 4);
        
        // Times (simplified)
        uint32_t atime = 0, mtime = 0, ctime = 0;
        atime = htonl(atime);
        mtime = htonl(mtime);
        ctime = htonl(ctime);
        response_data.insert(response_data.end(), (uint8_t*)&atime, (uint8_t*)&atime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&mtime, (uint8_t*)&mtime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&ctime, (uint8_t*)&ctime + 4);
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 LOOKUP for: " << full_path << " (handle: " << file_handle << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in LOOKUP: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Read(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse file handle, offset, and count from message data
        if (message.data.size() < 12) {
            std::cerr << "Invalid READ request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (first 4 bytes)
        uint32_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 4);
        file_handle = ntohl(file_handle);
        
        // Extract offset (next 4 bytes)
        uint32_t offset = 0;
        memcpy(&offset, message.data.data() + 4, 4);
        offset = ntohl(offset);
        
        // Extract count (next 4 bytes)
        uint32_t count = 0;
        memcpy(&count, message.data.data() + 8, 4);
        count = ntohl(count);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(file_handle);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << file_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for READ on: " << file_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Validate path
        if (!validatePath(file_path)) {
            std::cerr << "Invalid path: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if file exists and is readable
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            std::cerr << "File not found or not a regular file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read file data
        std::vector<uint8_t> file_data = readFile(full_path.string(), offset, count);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // File attributes (simplified)
        uint32_t file_type = 1; // Regular file
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = 0644;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint32_t file_size = static_cast<uint32_t>(std::filesystem::file_size(full_path));
        file_size = htonl(file_size);
        response_data.insert(response_data.end(), (uint8_t*)&file_size, (uint8_t*)&file_size + 4);
        
        uint32_t block_size = 512;
        block_size = htonl(block_size);
        response_data.insert(response_data.end(), (uint8_t*)&block_size, (uint8_t*)&block_size + 4);
        
        uint32_t blocks = (file_size + block_size - 1) / block_size;
        blocks = htonl(blocks);
        response_data.insert(response_data.end(), (uint8_t*)&blocks, (uint8_t*)&blocks + 4);
        
        uint32_t rdev = 0;
        rdev = htonl(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 4);
        
        uint64_t file_size_64 = file_size;
        file_size_64 = htonll_custom(file_size_64);
        response_data.insert(response_data.end(), (uint8_t*)&file_size_64, (uint8_t*)&file_size_64 + 8);
        
        uint32_t fsid = 1;
        fsid = htonl(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 4);
        
        uint32_t fileid = file_handle;
        fileid = htonl(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 4);
        
        uint32_t atime = 0, mtime = 0, ctime = 0;
        atime = htonl(atime);
        mtime = htonl(mtime);
        ctime = htonl(ctime);
        response_data.insert(response_data.end(), (uint8_t*)&atime, (uint8_t*)&atime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&mtime, (uint8_t*)&mtime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&ctime, (uint8_t*)&ctime + 4);
        
        // Data count
        uint32_t data_count = static_cast<uint32_t>(file_data.size());
        data_count = htonl(data_count);
        response_data.insert(response_data.end(), (uint8_t*)&data_count, (uint8_t*)&data_count + 4);
        
        // EOF flag
        uint32_t eof = (offset + file_data.size() >= file_size) ? 1 : 0;
        eof = htonl(eof);
        response_data.insert(response_data.end(), (uint8_t*)&eof, (uint8_t*)&eof + 4);
        
        // File data
        response_data.insert(response_data.end(), file_data.begin(), file_data.end());
        
        // Update statistics
        bytes_read_ += file_data.size();
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 READ for: " << file_path << " (offset: " << offset << ", count: " << count << ", read: " << file_data.size() << " bytes)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in READ: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Write(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse file handle, offset, and data from message data
        if (message.data.size() < 12) {
            std::cerr << "Invalid WRITE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (first 4 bytes)
        uint32_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 4);
        file_handle = ntohl(file_handle);
        
        // Extract offset (next 4 bytes)
        uint32_t offset = 0;
        memcpy(&offset, message.data.data() + 4, 4);
        offset = ntohl(offset);
        
        // Extract data count (next 4 bytes)
        uint32_t data_count = 0;
        memcpy(&data_count, message.data.data() + 8, 4);
        data_count = ntohl(data_count);
        
        if (message.data.size() < 12 + data_count) {
            std::cerr << "Invalid WRITE request: insufficient data for file content" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file data
        std::vector<uint8_t> file_data(message.data.data() + 12, message.data.data() + 12 + data_count);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(file_handle);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << file_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true)) {
            std::cerr << "Access denied for WRITE on: " << file_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Validate path
        if (!validatePath(file_path)) {
            std::cerr << "Invalid path: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if file exists and is writable
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            std::cerr << "File not found or not a regular file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Write file data
        bool success = writeFile(full_path.string(), offset, file_data);
        if (!success) {
            std::cerr << "Failed to write to file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // File attributes (simplified)
        uint32_t file_type = 1; // Regular file
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = 0644;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint32_t file_size = static_cast<uint32_t>(std::filesystem::file_size(full_path));
        file_size = htonl(file_size);
        response_data.insert(response_data.end(), (uint8_t*)&file_size, (uint8_t*)&file_size + 4);
        
        uint32_t block_size = 512;
        block_size = htonl(block_size);
        response_data.insert(response_data.end(), (uint8_t*)&block_size, (uint8_t*)&block_size + 4);
        
        uint32_t blocks = (file_size + block_size - 1) / block_size;
        blocks = htonl(blocks);
        response_data.insert(response_data.end(), (uint8_t*)&blocks, (uint8_t*)&blocks + 4);
        
        uint32_t rdev = 0;
        rdev = htonl(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 4);
        
        uint64_t file_size_64 = file_size;
        file_size_64 = htonll_custom(file_size_64);
        response_data.insert(response_data.end(), (uint8_t*)&file_size_64, (uint8_t*)&file_size_64 + 8);
        
        uint32_t fsid = 1;
        fsid = htonl(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 4);
        
        uint32_t fileid = file_handle;
        fileid = htonl(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 4);
        
        uint32_t atime = 0, mtime = 0, ctime = 0;
        atime = htonl(atime);
        mtime = htonl(mtime);
        ctime = htonl(ctime);
        response_data.insert(response_data.end(), (uint8_t*)&atime, (uint8_t*)&atime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&mtime, (uint8_t*)&mtime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&ctime, (uint8_t*)&ctime + 4);
        
        // Bytes written count
        uint32_t bytes_written = static_cast<uint32_t>(file_data.size());
        bytes_written = htonl(bytes_written);
        response_data.insert(response_data.end(), (uint8_t*)&bytes_written, (uint8_t*)&bytes_written + 4);
        
        // Update statistics
        bytes_written_ += file_data.size();
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 WRITE for: " << file_path << " (offset: " << offset << ", written: " << file_data.size() << " bytes)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in WRITE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2ReadDir(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse directory handle and cookie from message data
        if (message.data.size() < 8) {
            std::cerr << "Invalid READDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (first 4 bytes)
        uint32_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 4);
        dir_handle = ntohl(dir_handle);
        
        // Extract cookie (next 4 bytes)
        uint32_t cookie = 0;
        memcpy(&cookie, message.data.data() + 4, 4);
        cookie = ntohl(cookie);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, true, false)) {
            std::cerr << "Access denied for READDIR on: " << dir_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Validate path
        if (!validatePath(dir_path)) {
            std::cerr << "Invalid path: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if directory exists
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / dir_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_directory(full_path)) {
            std::cerr << "Directory not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read directory entries
        std::vector<std::string> entries = readDirectory(dir_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Directory entries (simplified format)
        for (size_t i = cookie; i < entries.size() && i < cookie + 100; ++i) {
            const std::string& entry_name = entries[i];
            
            // File ID (use entry index as file ID)
            uint32_t file_id = static_cast<uint32_t>(i + 1);
            file_id = htonl(file_id);
            response_data.insert(response_data.end(), (uint8_t*)&file_id, (uint8_t*)&file_id + 4);
            
            // Filename length
            uint32_t name_len = static_cast<uint32_t>(entry_name.length());
            name_len = htonl(name_len);
            response_data.insert(response_data.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
            
            // Filename
            response_data.insert(response_data.end(), entry_name.begin(), entry_name.end());
            
            // Padding to 4-byte boundary
            while (response_data.size() % 4 != 0) {
                response_data.push_back(0);
            }
            
            // Cookie for next entry
            uint32_t next_cookie = static_cast<uint32_t>(i + 1);
            next_cookie = htonl(next_cookie);
            response_data.insert(response_data.end(), (uint8_t*)&next_cookie, (uint8_t*)&next_cookie + 4);
        }
        
        // EOF flag (1 if this is the last batch)
        uint32_t eof = (cookie + 100 >= entries.size()) ? 1 : 0;
        eof = htonl(eof);
        response_data.insert(response_data.end(), (uint8_t*)&eof, (uint8_t*)&eof + 4);
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 READDIR for: " << dir_path << " (cookie: " << cookie << ", entries: " << entries.size() << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in READDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2StatFS(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse file handle from message data
        if (message.data.size() < 4) {
            std::cerr << "Invalid STATFS request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (first 4 bytes)
        uint32_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 4);
        file_handle = ntohl(file_handle);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(file_handle);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << file_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for STATFS on: " << file_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Validate path
        if (!validatePath(file_path)) {
            std::cerr << "Invalid path: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get filesystem statistics
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        std::filesystem::space_info space = std::filesystem::space(full_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // File system type (simplified)
        uint32_t fstype = 1; // NFS
        fstype = htonl(fstype);
        response_data.insert(response_data.end(), (uint8_t*)&fstype, (uint8_t*)&fstype + 4);
        
        // Block size
        uint32_t bsize = 512;
        bsize = htonl(bsize);
        response_data.insert(response_data.end(), (uint8_t*)&bsize, (uint8_t*)&bsize + 4);
        
        // Total blocks
        uint32_t blocks = static_cast<uint32_t>(space.capacity / bsize);
        blocks = htonl(blocks);
        response_data.insert(response_data.end(), (uint8_t*)&blocks, (uint8_t*)&blocks + 4);
        
        // Free blocks
        uint32_t bfree = static_cast<uint32_t>(space.available / bsize);
        bfree = htonl(bfree);
        response_data.insert(response_data.end(), (uint8_t*)&bfree, (uint8_t*)&bfree + 4);
        
        // Available blocks
        uint32_t bavail = static_cast<uint32_t>(space.available / bsize);
        bavail = htonl(bavail);
        response_data.insert(response_data.end(), (uint8_t*)&bavail, (uint8_t*)&bavail + 4);
        
        // Total files
        uint32_t files = 1000; // Simplified
        files = htonl(files);
        response_data.insert(response_data.end(), (uint8_t*)&files, (uint8_t*)&files + 4);
        
        // Free files
        uint32_t ffree = 500; // Simplified
        ffree = htonl(ffree);
        response_data.insert(response_data.end(), (uint8_t*)&ffree, (uint8_t*)&ffree + 4);
        
        // File system ID
        uint32_t fsid = 1;
        fsid = htonl(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 4);
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 STATFS for: " << file_path << " (total: " << space.capacity << ", free: " << space.available << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in STATFS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

bool NfsServerSimple::fileExists(const std::string& path) const {
    return std::filesystem::exists(path);
}

bool NfsServerSimple::isDirectory(const std::string& path) const {
    return std::filesystem::is_directory(path);
}

bool NfsServerSimple::isFile(const std::string& path) const {
    return std::filesystem::is_regular_file(path);
}

uint32_t NfsServerSimple::getHandleForPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    
    // Check if we already have a handle for this path
    for (const auto& pair : file_handles_) {
        if (pair.second == path) {
            return pair.first;
        }
    }
    
    // Create new handle
    uint32_t handle = next_handle_id_++;
    file_handles_[handle] = path;
    return handle;
}

std::string NfsServerSimple::getPathFromHandle(uint32_t handle) const {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    
    auto it = file_handles_.find(handle);
    if (it != file_handles_.end()) {
        return it->second;
    }
    
    return "";
}

std::vector<std::string> NfsServerSimple::readDirectory(const std::string& path) const {
    std::vector<std::string> entries;
    
    try {
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / path;
        
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_directory(full_path)) {
            return entries;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
            std::string relative_path = entry.path().filename().string();
            entries.push_back(relative_path);
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error reading directory " << path << ": " << e.what() << std::endl;
    }
    
    return entries;
}

bool NfsServerSimple::validatePath(const std::string& path) const {
    try {
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / path;
        
        // Check if path is within root directory
        std::filesystem::path canonical_root = std::filesystem::canonical(config_.root_path);
        std::filesystem::path canonical_path = std::filesystem::canonical(full_path);
        
        return canonical_path.string().find(canonical_root.string()) == 0;
        
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<uint8_t> NfsServerSimple::readFile(const std::string& path, uint32_t offset, uint32_t count) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    
    file.seekg(offset);
    std::vector<uint8_t> data(count);
    file.read(reinterpret_cast<char*>(data.data()), count);
    data.resize(file.gcount());
    
    return data;
}

bool NfsServerSimple::writeFile(const std::string& path, uint32_t offset, const std::vector<uint8_t>& data) const {
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        return false;
    }
    
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    return file.good();
}

bool NfsServerSimple::initializeAuthentication() {
    if (!auth_manager_) {
        return false;
    }
    
    return auth_manager_->initialize();
}

bool NfsServerSimple::authenticateRequest(const RpcMessage& message, AuthContext& context) {
    if (!auth_manager_) {
        return false;
    }
    
    // Extract credentials and verifier from RPC message
    std::vector<uint8_t> credentials = message.header.cred.body;
    std::vector<uint8_t> verifier = message.header.verf.body;
    
    AuthResult result = auth_manager_->authenticate(credentials, verifier, context);
    return (result == AuthResult::SUCCESS);
}

bool NfsServerSimple::checkAccess(const std::string& path, const AuthContext& context, bool read, bool write) {
    if (!auth_manager_) {
        return false;
    }
    
    return auth_manager_->checkPathAccess(path, context, read, write);
}

uint32_t NfsServerSimple::negotiateNfsVersion(uint32_t client_version) {
    // Check if client's requested version is supported
    if (isNfsVersionSupported(client_version)) {
        return client_version;
    }
    
    // Find the highest supported version that's <= client version
    auto supported_versions = getSupportedNfsVersions();
    uint32_t best_version = 0;
    
    for (uint32_t version : supported_versions) {
        if (version <= client_version && version > best_version) {
            best_version = version;
        }
    }
    
    // If no compatible version found, return the highest supported version
    if (best_version == 0 && !supported_versions.empty()) {
        best_version = *std::max_element(supported_versions.begin(), supported_versions.end());
    }
    
    return best_version;
}

bool NfsServerSimple::isNfsVersionSupported(uint32_t version) {
    auto supported_versions = getSupportedNfsVersions();
    return std::find(supported_versions.begin(), supported_versions.end(), version) != supported_versions.end();
}

std::vector<uint32_t> NfsServerSimple::getSupportedNfsVersions() {
    // Return supported NFS versions in order of preference (highest first)
    return {4, 3, 2};
}

// Additional NFSv2 procedures implementation
void NfsServerSimple::handleNfsv2SetAttr(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse file handle and attributes from message data
        if (message.data.size() < 8) {
            std::cerr << "Invalid SETATTR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (first 4 bytes)
        uint32_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 4);
        file_handle = ntohl(file_handle);
        
        // Extract attributes (next 4 bytes - simplified)
        uint32_t attributes = 0;
        memcpy(&attributes, message.data.data() + 4, 4);
        attributes = ntohl(attributes);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(file_handle);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << file_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true)) {
            std::cerr << "Access denied for SETATTR on: " << file_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Validate path
        if (!validatePath(file_path)) {
            std::cerr << "Invalid path: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Update file attributes (simplified implementation)
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path)) {
            std::cerr << "File not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // For now, just update the modification time
        std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(full_path, now);
        
        // Create response data (file attributes after update)
        std::vector<uint8_t> response_data;
        
        // File type (1 = regular file, 2 = directory)
        uint32_t file_type = std::filesystem::is_directory(full_path) ? 2 : 1;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        // File mode (permissions)
        uint32_t file_mode = 0644; // Default permissions
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        // Number of links
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        // User ID
        uint32_t uid = 0;
        uid = htonl(uid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        
        // Group ID
        uint32_t gid = 0;
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        // File size
        uint32_t file_size = 0;
        if (std::filesystem::is_regular_file(full_path)) {
            file_size = static_cast<uint32_t>(std::filesystem::file_size(full_path));
        }
        file_size = htonl(file_size);
        response_data.insert(response_data.end(), (uint8_t*)&file_size, (uint8_t*)&file_size + 4);
        
        // Block size
        uint32_t block_size = 512;
        block_size = htonl(block_size);
        response_data.insert(response_data.end(), (uint8_t*)&block_size, (uint8_t*)&block_size + 4);
        
        // Number of blocks
        uint32_t blocks = (file_size + block_size - 1) / block_size;
        blocks = htonl(blocks);
        response_data.insert(response_data.end(), (uint8_t*)&blocks, (uint8_t*)&blocks + 4);
        
        // Rdev (device number)
        uint32_t rdev = 0;
        rdev = htonl(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 4);
        
        // File size (64-bit)
        uint64_t file_size_64 = file_size;
        file_size_64 = htonll_custom(file_size_64);
        response_data.insert(response_data.end(), (uint8_t*)&file_size_64, (uint8_t*)&file_size_64 + 8);
        
        // File system ID
        uint32_t fsid = 1;
        fsid = htonl(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 4);
        
        // File ID
        uint32_t fileid = file_handle;
        fileid = htonl(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 4);
        
        // Times (simplified)
        uint32_t atime = 0, mtime = 0, ctime = 0;
        atime = htonl(atime);
        mtime = htonl(mtime);
        ctime = htonl(ctime);
        response_data.insert(response_data.end(), (uint8_t*)&atime, (uint8_t*)&atime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&mtime, (uint8_t*)&mtime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&ctime, (uint8_t*)&ctime + 4);
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 SETATTR for file: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in SETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Create(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse directory file handle and filename from message data
        if (message.data.size() < 8) {
            std::cerr << "Invalid CREATE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (first 4 bytes)
        uint32_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 4);
        dir_handle = ntohl(dir_handle);
        
        // Extract filename length (next 4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 4, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 8 + name_len) {
            std::cerr << "Invalid CREATE request: insufficient data for filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract filename
        std::string filename(reinterpret_cast<const char*>(message.data.data() + 8), name_len);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions for directory
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for CREATE in: " << dir_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = dir_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += filename;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if file already exists
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (std::filesystem::exists(fs_path)) {
            std::cerr << "File already exists: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create the file
        try {
            std::ofstream file(fs_path);
            if (!file.is_open()) {
                std::cerr << "Failed to create file: " << fs_path << std::endl;
                failed_requests_++;
                return;
            }
            file.close();
        } catch (const std::exception& e) {
            std::cerr << "Error creating file: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create file handle
        uint32_t file_handle = getHandleForPath(full_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // File handle
        uint32_t handle_net = htonl(file_handle);
        response_data.insert(response_data.end(), (uint8_t*)&handle_net, (uint8_t*)&handle_net + 4);
        
        // File attributes (simplified)
        uint32_t file_type = 1; // Regular file
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        // File mode
        uint32_t file_mode = 0644; // Default permissions
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        // Number of links
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        // User ID
        uint32_t uid = 0;
        uid = htonl(uid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        
        // Group ID
        uint32_t gid = 0;
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        // File size (0 for new file)
        uint32_t file_size = 0;
        file_size = htonl(file_size);
        response_data.insert(response_data.end(), (uint8_t*)&file_size, (uint8_t*)&file_size + 4);
        
        // Block size
        uint32_t block_size = 512;
        block_size = htonl(block_size);
        response_data.insert(response_data.end(), (uint8_t*)&block_size, (uint8_t*)&block_size + 4);
        
        // Number of blocks
        uint32_t blocks = 0;
        blocks = htonl(blocks);
        response_data.insert(response_data.end(), (uint8_t*)&blocks, (uint8_t*)&blocks + 4);
        
        // Rdev
        uint32_t rdev = 0;
        rdev = htonl(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 4);
        
        // File size (64-bit)
        uint64_t file_size_64 = 0;
        file_size_64 = htonll_custom(file_size_64);
        response_data.insert(response_data.end(), (uint8_t*)&file_size_64, (uint8_t*)&file_size_64 + 8);
        
        // File system ID
        uint32_t fsid = 1;
        fsid = htonl(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 4);
        
        // File ID
        uint32_t fileid = file_handle;
        fileid = htonl(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 4);
        
        // Times (simplified)
        uint32_t atime = 0, mtime = 0, ctime = 0;
        atime = htonl(atime);
        mtime = htonl(mtime);
        ctime = htonl(ctime);
        response_data.insert(response_data.end(), (uint8_t*)&atime, (uint8_t*)&atime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&mtime, (uint8_t*)&mtime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&ctime, (uint8_t*)&ctime + 4);
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 CREATE for: " << full_path << " (handle: " << file_handle << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in CREATE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2MkDir(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse directory file handle and directory name from message data
        if (message.data.size() < 8) {
            std::cerr << "Invalid MKDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (first 4 bytes)
        uint32_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 4);
        dir_handle = ntohl(dir_handle);
        
        // Extract directory name length (next 4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 4, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 8 + name_len) {
            std::cerr << "Invalid MKDIR request: insufficient data for directory name" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory name
        std::string dirname(reinterpret_cast<const char*>(message.data.data() + 8), name_len);
        
        // Get parent directory path from handle
        std::string parent_path = getPathFromHandle(dir_handle);
        if (parent_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions for parent directory
        if (!checkAccess(parent_path, auth_context, false, true)) {
            std::cerr << "Access denied for MKDIR in: " << parent_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = parent_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += dirname;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if directory already exists
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (std::filesystem::exists(fs_path)) {
            std::cerr << "Directory already exists: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create the directory
        try {
            if (!std::filesystem::create_directory(fs_path)) {
                std::cerr << "Failed to create directory: " << fs_path << std::endl;
                failed_requests_++;
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error creating directory: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create directory handle
        uint32_t dir_handle_new = getHandleForPath(full_path);
        
        // Create response data (same as CREATE but with file_type = 2 for directory)
        std::vector<uint8_t> response_data;
        
        // Directory handle
        uint32_t handle_net = htonl(dir_handle_new);
        response_data.insert(response_data.end(), (uint8_t*)&handle_net, (uint8_t*)&handle_net + 4);
        
        // File attributes (directory)
        uint32_t file_type = 2; // Directory
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        // File mode
        uint32_t file_mode = 0755; // Directory permissions
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        // Number of links
        uint32_t nlink = 2; // Directory has 2 links (itself and parent)
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        // User ID
        uint32_t uid = 0;
        uid = htonl(uid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        
        // Group ID
        uint32_t gid = 0;
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        // File size (0 for directory)
        uint32_t file_size = 0;
        file_size = htonl(file_size);
        response_data.insert(response_data.end(), (uint8_t*)&file_size, (uint8_t*)&file_size + 4);
        
        // Block size
        uint32_t block_size = 512;
        block_size = htonl(block_size);
        response_data.insert(response_data.end(), (uint8_t*)&block_size, (uint8_t*)&block_size + 4);
        
        // Number of blocks
        uint32_t blocks = 0;
        blocks = htonl(blocks);
        response_data.insert(response_data.end(), (uint8_t*)&blocks, (uint8_t*)&blocks + 4);
        
        // Rdev
        uint32_t rdev = 0;
        rdev = htonl(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 4);
        
        // File size (64-bit)
        uint64_t file_size_64 = 0;
        file_size_64 = htonll_custom(file_size_64);
        response_data.insert(response_data.end(), (uint8_t*)&file_size_64, (uint8_t*)&file_size_64 + 8);
        
        // File system ID
        uint32_t fsid = 1;
        fsid = htonl(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 4);
        
        // File ID
        uint32_t fileid = dir_handle_new;
        fileid = htonl(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 4);
        
        // Times (simplified)
        uint32_t atime = 0, mtime = 0, ctime = 0;
        atime = htonl(atime);
        mtime = htonl(mtime);
        ctime = htonl(ctime);
        response_data.insert(response_data.end(), (uint8_t*)&atime, (uint8_t*)&atime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&mtime, (uint8_t*)&mtime + 4);
        response_data.insert(response_data.end(), (uint8_t*)&ctime, (uint8_t*)&ctime + 4);
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 MKDIR for: " << full_path << " (handle: " << dir_handle_new << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in MKDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2RmDir(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse directory file handle from message data
        if (message.data.size() < 4) {
            std::cerr << "Invalid RMDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (first 4 bytes)
        uint32_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 4);
        dir_handle = ntohl(dir_handle);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for RMDIR on: " << dir_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Validate path
        if (!validatePath(dir_path)) {
            std::cerr << "Invalid path: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if directory exists and is empty
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / dir_path;
        if (!std::filesystem::exists(fs_path)) {
            std::cerr << "Directory not found: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        if (!std::filesystem::is_directory(fs_path)) {
            std::cerr << "Path is not a directory: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if directory is empty
        bool is_empty = std::filesystem::is_empty(fs_path);
        if (!is_empty) {
            std::cerr << "Directory not empty: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Remove the directory
        try {
            if (!std::filesystem::remove(fs_path)) {
                std::cerr << "Failed to remove directory: " << fs_path << std::endl;
                failed_requests_++;
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error removing directory: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create RPC reply (RMDIR returns no data on success)
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 RMDIR for: " << dir_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in RMDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Remove(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse directory file handle and filename from message data
        if (message.data.size() < 8) {
            std::cerr << "Invalid REMOVE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (first 4 bytes)
        uint32_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 4);
        dir_handle = ntohl(dir_handle);
        
        // Extract filename length (next 4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 4, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 8 + name_len) {
            std::cerr << "Invalid REMOVE request: insufficient data for filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract filename
        std::string filename(reinterpret_cast<const char*>(message.data.data() + 8), name_len);
        
        // Get parent directory path from handle
        std::string parent_path = getPathFromHandle(dir_handle);
        if (parent_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions for parent directory
        if (!checkAccess(parent_path, auth_context, false, true)) {
            std::cerr << "Access denied for REMOVE in: " << parent_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = parent_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += filename;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if file exists
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (!std::filesystem::exists(fs_path)) {
            std::cerr << "File not found: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        if (std::filesystem::is_directory(fs_path)) {
            std::cerr << "Cannot remove directory with REMOVE: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Remove the file
        try {
            if (!std::filesystem::remove(fs_path)) {
                std::cerr << "Failed to remove file: " << fs_path << std::endl;
                failed_requests_++;
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error removing file: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create RPC reply (REMOVE returns no data on success)
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 REMOVE for: " << full_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in REMOVE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Rename(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse source and destination file handles and names from message data
        if (message.data.size() < 16) {
            std::cerr << "Invalid RENAME request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        
        // Extract source directory handle (first 4 bytes)
        uint32_t src_dir_handle = 0;
        memcpy(&src_dir_handle, message.data.data() + offset, 4);
        src_dir_handle = ntohl(src_dir_handle);
        offset += 4;
        
        // Extract source filename length (next 4 bytes)
        uint32_t src_name_len = 0;
        memcpy(&src_name_len, message.data.data() + offset, 4);
        src_name_len = ntohl(src_name_len);
        offset += 4;
        
        if (message.data.size() < offset + src_name_len + 8) {
            std::cerr << "Invalid RENAME request: insufficient data for source filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract source filename
        std::string src_filename(reinterpret_cast<const char*>(message.data.data() + offset), src_name_len);
        offset += src_name_len;
        
        // Extract destination directory handle (next 4 bytes)
        uint32_t dst_dir_handle = 0;
        memcpy(&dst_dir_handle, message.data.data() + offset, 4);
        dst_dir_handle = ntohl(dst_dir_handle);
        offset += 4;
        
        // Extract destination filename length (next 4 bytes)
        uint32_t dst_name_len = 0;
        memcpy(&dst_name_len, message.data.data() + offset, 4);
        dst_name_len = ntohl(dst_name_len);
        offset += 4;
        
        if (message.data.size() < offset + dst_name_len) {
            std::cerr << "Invalid RENAME request: insufficient data for destination filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract destination filename
        std::string dst_filename(reinterpret_cast<const char*>(message.data.data() + offset), dst_name_len);
        
        // Get source directory path from handle
        std::string src_parent_path = getPathFromHandle(src_dir_handle);
        if (src_parent_path.empty()) {
            std::cerr << "Invalid source directory handle: " << src_dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get destination directory path from handle
        std::string dst_parent_path = getPathFromHandle(dst_dir_handle);
        if (dst_parent_path.empty()) {
            std::cerr << "Invalid destination directory handle: " << dst_dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions for both directories
        if (!checkAccess(src_parent_path, auth_context, false, true)) {
            std::cerr << "Access denied for RENAME source: " << src_parent_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        if (!checkAccess(dst_parent_path, auth_context, false, true)) {
            std::cerr << "Access denied for RENAME destination: " << dst_parent_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct source and destination paths
        std::string src_full_path = src_parent_path;
        if (!src_full_path.empty() && src_full_path.back() != '/') {
            src_full_path += "/";
        }
        src_full_path += src_filename;
        
        std::string dst_full_path = dst_parent_path;
        if (!dst_full_path.empty() && dst_full_path.back() != '/') {
            dst_full_path += "/";
        }
        dst_full_path += dst_filename;
        
        // Validate paths
        if (!validatePath(src_full_path) || !validatePath(dst_full_path)) {
            std::cerr << "Invalid path in RENAME: " << src_full_path << " -> " << dst_full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if source exists
        std::filesystem::path src_fs_path = std::filesystem::path(config_.root_path) / src_full_path;
        if (!std::filesystem::exists(src_fs_path)) {
            std::cerr << "Source not found: " << src_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if destination already exists
        std::filesystem::path dst_fs_path = std::filesystem::path(config_.root_path) / dst_full_path;
        if (std::filesystem::exists(dst_fs_path)) {
            std::cerr << "Destination already exists: " << dst_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Rename the file/directory
        try {
            std::filesystem::rename(src_fs_path, dst_fs_path);
        } catch (const std::exception& e) {
            std::cerr << "Error renaming: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create RPC reply (RENAME returns no data on success)
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 RENAME: " << src_full_path << " -> " << dst_full_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in RENAME: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Link(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse source file handle and destination directory handle and name from message data
        if (message.data.size() < 16) {
            std::cerr << "Invalid LINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        
        // Extract source file handle (first 4 bytes)
        uint32_t src_file_handle = 0;
        memcpy(&src_file_handle, message.data.data() + offset, 4);
        src_file_handle = ntohl(src_file_handle);
        offset += 4;
        
        // Extract destination directory handle (next 4 bytes)
        uint32_t dst_dir_handle = 0;
        memcpy(&dst_dir_handle, message.data.data() + offset, 4);
        dst_dir_handle = ntohl(dst_dir_handle);
        offset += 4;
        
        // Extract destination filename length (next 4 bytes)
        uint32_t dst_name_len = 0;
        memcpy(&dst_name_len, message.data.data() + offset, 4);
        dst_name_len = ntohl(dst_name_len);
        offset += 4;
        
        if (message.data.size() < offset + dst_name_len) {
            std::cerr << "Invalid LINK request: insufficient data for destination filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract destination filename
        std::string dst_filename(reinterpret_cast<const char*>(message.data.data() + offset), dst_name_len);
        
        // Get source file path from handle
        std::string src_file_path = getPathFromHandle(src_file_handle);
        if (src_file_path.empty()) {
            std::cerr << "Invalid source file handle: " << src_file_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get destination directory path from handle
        std::string dst_dir_path = getPathFromHandle(dst_dir_handle);
        if (dst_dir_path.empty()) {
            std::cerr << "Invalid destination directory handle: " << dst_dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions for source file
        if (!checkAccess(src_file_path, auth_context, true, false)) {
            std::cerr << "Access denied for LINK source: " << src_file_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions for destination directory
        if (!checkAccess(dst_dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for LINK destination: " << dst_dir_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct destination path
        std::string dst_full_path = dst_dir_path;
        if (!dst_full_path.empty() && dst_full_path.back() != '/') {
            dst_full_path += "/";
        }
        dst_full_path += dst_filename;
        
        // Validate paths
        if (!validatePath(src_file_path) || !validatePath(dst_full_path)) {
            std::cerr << "Invalid path in LINK: " << src_file_path << " -> " << dst_full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if source exists and is a file
        std::filesystem::path src_fs_path = std::filesystem::path(config_.root_path) / src_file_path;
        if (!std::filesystem::exists(src_fs_path)) {
            std::cerr << "Source file not found: " << src_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        if (std::filesystem::is_directory(src_fs_path)) {
            std::cerr << "Cannot create hard link to directory: " << src_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if destination already exists
        std::filesystem::path dst_fs_path = std::filesystem::path(config_.root_path) / dst_full_path;
        if (std::filesystem::exists(dst_fs_path)) {
            std::cerr << "Destination already exists: " << dst_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create the hard link
        try {
            std::filesystem::create_hard_link(src_fs_path, dst_fs_path);
        } catch (const std::exception& e) {
            std::cerr << "Error creating hard link: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create RPC reply (LINK returns no data on success)
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 LINK: " << src_file_path << " -> " << dst_full_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in LINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2SymLink(const RpcMessage& message, const AuthContext& auth_context, const ClientConnection& client_conn) {
    try {
        // Parse destination directory handle, symlink name, and target path from message data
        if (message.data.size() < 16) {
            std::cerr << "Invalid SYMLINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        
        // Extract destination directory handle (first 4 bytes)
        uint32_t dst_dir_handle = 0;
        memcpy(&dst_dir_handle, message.data.data() + offset, 4);
        dst_dir_handle = ntohl(dst_dir_handle);
        offset += 4;
        
        // Extract symlink name length (next 4 bytes)
        uint32_t link_name_len = 0;
        memcpy(&link_name_len, message.data.data() + offset, 4);
        link_name_len = ntohl(link_name_len);
        offset += 4;
        
        if (message.data.size() < offset + link_name_len + 4) {
            std::cerr << "Invalid SYMLINK request: insufficient data for symlink name" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract symlink name
        std::string link_name(reinterpret_cast<const char*>(message.data.data() + offset), link_name_len);
        offset += link_name_len;
        
        // Extract target path length (next 4 bytes)
        uint32_t target_path_len = 0;
        memcpy(&target_path_len, message.data.data() + offset, 4);
        target_path_len = ntohl(target_path_len);
        offset += 4;
        
        if (message.data.size() < offset + target_path_len) {
            std::cerr << "Invalid SYMLINK request: insufficient data for target path" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract target path
        std::string target_path(reinterpret_cast<const char*>(message.data.data() + offset), target_path_len);
        
        // Get destination directory path from handle
        std::string dst_dir_path = getPathFromHandle(dst_dir_handle);
        if (dst_dir_path.empty()) {
            std::cerr << "Invalid destination directory handle: " << dst_dir_handle << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions for destination directory
        if (!checkAccess(dst_dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for SYMLINK destination: " << dst_dir_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct symlink path
        std::string symlink_full_path = dst_dir_path;
        if (!symlink_full_path.empty() && symlink_full_path.back() != '/') {
            symlink_full_path += "/";
        }
        symlink_full_path += link_name;
        
        // Validate symlink path
        if (!validatePath(symlink_full_path)) {
            std::cerr << "Invalid symlink path: " << symlink_full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if symlink already exists
        std::filesystem::path symlink_fs_path = std::filesystem::path(config_.root_path) / symlink_full_path;
        if (std::filesystem::exists(symlink_fs_path)) {
            std::cerr << "Symlink already exists: " << symlink_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create the symbolic link
        try {
            std::filesystem::create_symlink(target_path, symlink_fs_path);
        } catch (const std::exception& e) {
            std::cerr << "Error creating symbolic link: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create RPC reply (SYMLINK returns no data on success)
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        sendReply(reply, client_conn);
        std::cout << "Handled NFSv2 SYMLINK: " << symlink_full_path << " -> " << target_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in SYMLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

// NFSv3 procedure implementations
void NfsServerSimple::handleNfsv3Null(const RpcMessagevoid NfsServerSimple::handleNfsv3Null(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Null(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Null(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    // NULL procedure always succeeds
    RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, {});
    std::cout << "Handled NFSv3 NULL procedure (user: " << auth_context.uid << ")" << std::endl;
}

void NfsServerSimple::handleNfsv3GetAttr(const RpcMessagevoid NfsServerSimple::handleNfsv3GetAttr(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3GetAttr(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3GetAttr(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 GETATTR uses 64-bit file handles (8 bytes)
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv3 GETATTR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes for NFSv3)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Look up file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 GETATTR on: " << file_path << " (user: " << auth_context.uid << ")" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file attributes
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path)) {
            std::cerr << "File not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::is_regular_file(full_path) ? std::filesystem::file_size(full_path) : 0;
        
        // Create NFSv3 file attributes response (fattr3 structure)
        std::vector<uint8_t> response_data;
        
        // File type (NF3REG=1, NF3DIR=2, NF3BLK=3, NF3CHR=4, NF3LNK=5, NF3SOCK=6, NF3FIFO=7)
        uint32_t file_type = 1;
        if (std::filesystem::is_directory(full_path)) file_type = 2;
        else if (std::filesystem::is_symlink(full_path)) file_type = 5;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        // File mode (permissions)
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        // Number of links
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        // User ID
        uint32_t uid = 0;
        uid = htonl(uid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        
        // Group ID
        uint32_t gid = 0;
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        // File size (64-bit)
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        // Used size (64-bit)
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        // Rdev (device number)
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        // File system ID (64-bit)
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        // File ID (64-bit)
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        // Last access time (nfsv3_time)
        uint64_t atime_sec = 0, atime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        
        // Last modification time
        uint64_t mtime_sec = 0, mtime_nsec = 0;
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        
        // Last change time
        uint64_t ctime_sec = 0, ctime_nsec = 0;
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Create RPC reply
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        
        successful_requests_++;
        std::cout << "Handled NFSv3 GETATTR for file: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 GETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3SetAttr(const RpcMessagevoid NfsServerSimple::handleNfsv3SetAttr(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3SetAttr(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3SetAttr(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 SETATTR uses 64-bit file handle
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv3 SETATTR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 SETATTR on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Update file attributes (simplified - parse sattr3 structure)
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path)) {
            std::cerr << "File not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response with updated attributes (similar to GETATTR)
        std::vector<uint8_t> response_data;
        // Return attributes similar to GETATTR
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::is_regular_file(full_path) ? std::filesystem::file_size(full_path) : 0;
        
        uint32_t file_type = 1;
        if (std::filesystem::is_directory(full_path)) file_type = 2;
        else if (std::filesystem::is_symlink(full_path)) file_type = 5;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 SETATTR for file: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 SETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Lookup(const RpcMessagevoid NfsServerSimple::handleNfsv3Lookup(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Lookup(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Lookup(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 LOOKUP uses 64-bit directory handle
        if (message.data.size() < 16) {
            std::cerr << "Invalid NFSv3 LOOKUP request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (8 bytes for NFSv3)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        
        // Extract filename length (next 4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 8, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 12 + name_len) {
            std::cerr << "Invalid NFSv3 LOOKUP request: insufficient data for filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract filename
        std::string filename(reinterpret_cast<const char*>(message.data.data() + 12), name_len);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 LOOKUP in: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = dir_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += filename;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if file exists
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (!std::filesystem::exists(fs_path)) {
            std::cerr << "File not found: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create file handle
        uint32_t file_handle_id = getHandleForPath(full_path);
        uint64_t file_handle = file_handle_id;
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // File handle (64-bit)
        file_handle = htonll_custom(file_handle);
        response_data.insert(response_data.end(), (uint8_t*)&file_handle, (uint8_t*)&file_handle + 8);
        
        // Post-op attributes (fattr3 structure) - similar to GETATTR
        auto status = std::filesystem::status(fs_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::is_regular_file(fs_path) ? std::filesystem::file_size(fs_path) : 0;
        
        uint32_t file_type = 1;
        if (std::filesystem::is_directory(fs_path)) file_type = 2;
        else if (std::filesystem::is_symlink(fs_path)) file_type = 5;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = file_handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Directory attributes (pre-op)
        // For LOOKUP, we return the same attributes for pre-op
        response_data.insert(response_data.end(), response_data.begin() + 8, response_data.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 LOOKUP for: " << full_path << " (handle: " << file_handle_id << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 LOOKUP: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Access(const RpcMessagevoid NfsServerSimple::handleNfsv3Access(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Access(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Access(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 ACCESS uses 64-bit file handle
        if (message.data.size() < 12) {
            std::cerr << "Invalid NFSv3 ACCESS request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Extract access request (4 bytes)
        uint32_t access_request = 0;
        memcpy(&access_request, message.data.data() + 8, 4);
        access_request = ntohl(access_request);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path)) {
            std::cerr << "File not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        bool can_read = checkAccess(file_path, auth_context, true, false);
        bool can_write = checkAccess(file_path, auth_context, false, true);
        bool can_execute = false; // Simplified
        
        // Build access mask
        uint32_t access_mask = 0;
        if (can_read) access_mask |= 0x01;   // ACCESS3_READ
        if (can_write) access_mask |= 0x02;  // ACCESS3_LOOKUP
        if (can_execute) access_mask |= 0x04; // ACCESS3_MODIFY
        if (can_write) access_mask |= 0x08;  // ACCESS3_EXTEND
        if (can_write) access_mask |= 0x10;  // ACCESS3_DELETE
        if (can_execute) access_mask |= 0x20; // ACCESS3_EXECUTE
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Post-op attributes (fattr3)
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::is_regular_file(full_path) ? std::filesystem::file_size(full_path) : 0;
        
        uint32_t file_type = 1;
        if (std::filesystem::is_directory(full_path)) file_type = 2;
        else if (std::filesystem::is_symlink(full_path)) file_type = 5;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Access mask
        access_mask = htonl(access_mask);
        response_data.insert(response_data.end(), (uint8_t*)&access_mask, (uint8_t*)&access_mask + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 ACCESS for: " << file_path << " (mask: 0x" << std::hex << access_mask << std::dec << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 ACCESS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3ReadLink(const RpcMessagevoid NfsServerSimple::handleNfsv3ReadLink(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3ReadLink(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3ReadLink(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 READLINK uses 64-bit file handle
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv3 READLINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 READLINK on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_symlink(full_path)) {
            std::cerr << "File not found or not a symlink: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read symlink target
        std::string target = std::filesystem::read_symlink(full_path).string();
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Post-op attributes (fattr3)
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        
        uint32_t file_type = 5; // NF3LNK
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = target.length();
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = target.length();
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Symlink data (nfs3path)
        uint32_t path_len = static_cast<uint32_t>(target.length());
        path_len = htonl(path_len);
        response_data.insert(response_data.end(), (uint8_t*)&path_len, (uint8_t*)&path_len + 4);
        response_data.insert(response_data.end(), target.begin(), target.end());
        // Pad to 4-byte boundary
        while (response_data.size() % 4 != 0) {
            response_data.push_back(0);
        }
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 READLINK for: " << file_path << " -> " << target << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 READLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Read(const RpcMessagevoid NfsServerSimple::handleNfsv3Read(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Read(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Read(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 READ uses 64-bit file handle and offset
        if (message.data.size() < 24) {
            std::cerr << "Invalid NFSv3 READ request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Extract offset (8 bytes, 64-bit)
        uint64_t offset = 0;
        memcpy(&offset, message.data.data() + 8, 8);
        offset = ntohll_custom(offset);
        
        // Extract count (4 bytes)
        uint32_t count = 0;
        memcpy(&count, message.data.data() + 16, 4);
        count = ntohl(count);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 READ on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            std::cerr << "File not found or not a regular file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read file data
        uint64_t file_size = std::filesystem::file_size(full_path);
        std::vector<uint8_t> file_data = readFile(full_path.string(), static_cast<uint32_t>(offset), count);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Post-op attributes (fattr3)
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        
        uint32_t file_type = 1; // NF3REG
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Count of bytes read
        uint32_t data_count = static_cast<uint32_t>(file_data.size());
        data_count = htonl(data_count);
        response_data.insert(response_data.end(), (uint8_t*)&data_count, (uint8_t*)&data_count + 4);
        
        // EOF flag
        uint32_t eof = (offset + file_data.size() >= file_size) ? 1 : 0;
        eof = htonl(eof);
        response_data.insert(response_data.end(), (uint8_t*)&eof, (uint8_t*)&eof + 4);
        
        // File data
        response_data.insert(response_data.end(), file_data.begin(), file_data.end());
        // Pad to 4-byte boundary
        while (response_data.size() % 4 != 0) {
            response_data.push_back(0);
        }
        
        bytes_read_ += file_data.size();
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 READ for: " << file_path << " (offset: " << offset << ", count: " << count << ", read: " << file_data.size() << " bytes)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 READ: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Write(const RpcMessagevoid NfsServerSimple::handleNfsv3Write(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Write(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Write(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 WRITE uses 64-bit file handle and offset
        if (message.data.size() < 28) {
            std::cerr << "Invalid NFSv3 WRITE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Extract offset (8 bytes, 64-bit)
        uint64_t offset = 0;
        memcpy(&offset, message.data.data() + 8, 8);
        offset = ntohll_custom(offset);
        
        // Extract count (4 bytes)
        uint32_t count = 0;
        memcpy(&count, message.data.data() + 16, 4);
        count = ntohl(count);
        
        // Extract stable flag (4 bytes)
        uint32_t stable = 0;
        memcpy(&stable, message.data.data() + 20, 4);
        stable = ntohl(stable);
        
        // Extract data (count bytes, padded to 4-byte boundary)
        uint32_t data_offset = 24;
        // Skip padding if any
        while (data_offset % 4 != 0) data_offset++;
        
        if (message.data.size() < data_offset + count) {
            std::cerr << "Invalid NFSv3 WRITE request: insufficient data for file content" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::vector<uint8_t> file_data(message.data.data() + data_offset, message.data.data() + data_offset + count);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 WRITE on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            std::cerr << "File not found or not a regular file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Write file data
        bool success = writeFile(full_path.string(), static_cast<uint32_t>(offset), file_data);
        if (!success) {
            std::cerr << "Failed to write to file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Pre-op attributes (fattr3)
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::file_size(full_path);
        
        uint32_t file_type = 1;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Count of bytes written
        uint32_t data_count = static_cast<uint32_t>(file_data.size());
        data_count = htonl(data_count);
        response_data.insert(response_data.end(), (uint8_t*)&data_count, (uint8_t*)&data_count + 4);
        
        // Committed flag (FILE_SYNC if stable, UNSTABLE otherwise)
        uint32_t committed = (stable != 0) ? 0 : 1; // FILE_SYNC=0, UNSTABLE=1
        committed = htonl(committed);
        response_data.insert(response_data.end(), (uint8_t*)&committed, (uint8_t*)&committed + 4);
        
        // Write verifier (8 bytes, all zeros for now)
        uint64_t verifier = 0;
        verifier = htonll_custom(verifier);
        response_data.insert(response_data.end(), (uint8_t*)&verifier, (uint8_t*)&verifier + 8);
        
        bytes_written_ += file_data.size();
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 WRITE for: " << file_path << " (offset: " << offset << ", written: " << file_data.size() << " bytes)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 WRITE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Create(const RpcMessagevoid NfsServerSimple::handleNfsv3Create(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Create(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Create(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 CREATE uses 64-bit directory handle
        if (message.data.size() < 16) {
            std::cerr << "Invalid NFSv3 CREATE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (8 bytes)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        
        // Extract filename length (4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 8, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 12 + name_len) {
            std::cerr << "Invalid NFSv3 CREATE request: insufficient data for filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract filename
        std::string filename(reinterpret_cast<const char*>(message.data.data() + 12), name_len);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 CREATE in: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = dir_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += filename;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if file already exists
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (std::filesystem::exists(fs_path)) {
            std::cerr << "File already exists: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create the file
        try {
            std::ofstream file(fs_path);
            if (!file.is_open()) {
                std::cerr << "Failed to create file: " << fs_path << std::endl;
                failed_requests_++;
                return;
            }
            file.close();
        } catch (const std::exception& e) {
            std::cerr << "Error creating file: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create file handle
        uint32_t file_handle_id = getHandleForPath(full_path);
        uint64_t file_handle = file_handle_id;
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // File handle (64-bit)
        file_handle = htonll_custom(file_handle);
        response_data.insert(response_data.end(), (uint8_t*)&file_handle, (uint8_t*)&file_handle + 8);
        
        // Post-op attributes (fattr3)
        auto status = std::filesystem::status(fs_path);
        auto perms = status.permissions();
        
        uint32_t file_type = 1; // NF3REG
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = 0;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = 0;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = file_handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Wcc data (pre-op attributes) - same as post-op for new file
        response_data.insert(response_data.end(), response_data.begin() + 8, response_data.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 CREATE for: " << full_path << " (handle: " << file_handle_id << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 CREATE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3MkDir(const RpcMessagevoid NfsServerSimple::handleNfsv3MkDir(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3MkDir(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3MkDir(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 MKDIR uses 64-bit directory handle
        if (message.data.size() < 16) {
            std::cerr << "Invalid NFSv3 MKDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (8 bytes)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        
        // Extract directory name length (4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 8, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 12 + name_len) {
            std::cerr << "Invalid NFSv3 MKDIR request: insufficient data for directory name" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory name
        std::string dirname(reinterpret_cast<const char*>(message.data.data() + 12), name_len);
        
        // Get parent directory path from handle
        std::string parent_path = getPathFromHandle(dir_handle_id);
        if (parent_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(parent_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 MKDIR in: " << parent_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = parent_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += dirname;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if directory already exists
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (std::filesystem::exists(fs_path)) {
            std::cerr << "Directory already exists: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create the directory
        try {
            if (!std::filesystem::create_directory(fs_path)) {
                std::cerr << "Failed to create directory: " << fs_path << std::endl;
                failed_requests_++;
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error creating directory: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create directory handle
        uint32_t dir_handle_new = getHandleForPath(full_path);
        uint64_t new_handle = dir_handle_new;
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Directory handle (64-bit)
        new_handle = htonll_custom(new_handle);
        response_data.insert(response_data.end(), (uint8_t*)&new_handle, (uint8_t*)&new_handle + 8);
        
        // Post-op attributes (fattr3) - directory type
        auto status = std::filesystem::status(fs_path);
        auto perms = status.permissions();
        
        uint32_t file_type = 2; // NF3DIR
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 2;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = 0;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = 0;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = dir_handle_new;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Wcc data (pre-op attributes)
        response_data.insert(response_data.end(), response_data.begin() + 8, response_data.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 MKDIR for: " << full_path << " (handle: " << dir_handle_new << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 MKDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3SymLink(const RpcMessagevoid NfsServerSimple::handleNfsv3SymLink(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3SymLink(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3SymLink(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 SYMLINK uses 64-bit directory handle
        if (message.data.size() < 20) {
            std::cerr << "Invalid NFSv3 SYMLINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        
        // Extract directory handle (8 bytes)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data() + offset, 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        offset += 8;
        
        // Extract symlink name length (4 bytes)
        uint32_t link_name_len = 0;
        memcpy(&link_name_len, message.data.data() + offset, 4);
        link_name_len = ntohl(link_name_len);
        offset += 4;
        
        if (message.data.size() < offset + link_name_len + 4) {
            std::cerr << "Invalid NFSv3 SYMLINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract symlink name
        std::string link_name(reinterpret_cast<const char*>(message.data.data() + offset), link_name_len);
        offset += link_name_len;
        // Align to 4-byte boundary
        while (offset % 4 != 0) offset++;
        
        // Extract target path length (4 bytes)
        uint32_t target_path_len = 0;
        memcpy(&target_path_len, message.data.data() + offset, 4);
        target_path_len = ntohl(target_path_len);
        offset += 4;
        
        if (message.data.size() < offset + target_path_len) {
            std::cerr << "Invalid NFSv3 SYMLINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract target path
        std::string target_path(reinterpret_cast<const char*>(message.data.data() + offset), target_path_len);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 SYMLINK in: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct symlink path
        std::string symlink_full_path = dir_path;
        if (!symlink_full_path.empty() && symlink_full_path.back() != '/') {
            symlink_full_path += "/";
        }
        symlink_full_path += link_name;
        
        // Validate path
        if (!validatePath(symlink_full_path)) {
            std::cerr << "Invalid path: " << symlink_full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if symlink already exists
        std::filesystem::path symlink_fs_path = std::filesystem::path(config_.root_path) / symlink_full_path;
        if (std::filesystem::exists(symlink_fs_path)) {
            std::cerr << "Symlink already exists: " << symlink_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create the symbolic link
        try {
            std::filesystem::create_symlink(target_path, symlink_fs_path);
        } catch (const std::exception& e) {
            std::cerr << "Error creating symbolic link: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create symlink handle
        uint32_t symlink_handle_id = getHandleForPath(symlink_full_path);
        uint64_t symlink_handle = symlink_handle_id;
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // File handle (64-bit)
        symlink_handle = htonll_custom(symlink_handle);
        response_data.insert(response_data.end(), (uint8_t*)&symlink_handle, (uint8_t*)&symlink_handle + 8);
        
        // Post-op attributes (fattr3) - symlink type
        auto status = std::filesystem::status(symlink_fs_path);
        auto perms = status.permissions();
        
        uint32_t file_type = 5; // NF3LNK
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = target_path.length();
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = target_path.length();
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = symlink_handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // Wcc data (pre-op attributes)
        response_data.insert(response_data.end(), response_data.begin() + 8, response_data.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 SYMLINK: " << symlink_full_path << " -> " << target_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 SYMLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3MkNod(const RpcMessagevoid NfsServerSimple::handleNfsv3MkNod(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3MkNod(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3MkNod(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 MKNOD - create special file (simplified implementation)
        // This is a complex procedure that creates device files, FIFOs, etc.
        // For now, return success but don't actually create special files
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 MKNOD procedure (user: " << auth_context.uid << ") - simplified" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 MKNOD: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Remove(const RpcMessagevoid NfsServerSimple::handleNfsv3Remove(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Remove(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Remove(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 REMOVE uses 64-bit directory handle
        if (message.data.size() < 16) {
            std::cerr << "Invalid NFSv3 REMOVE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (8 bytes)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        
        // Extract filename length (4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 8, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 12 + name_len) {
            std::cerr << "Invalid NFSv3 REMOVE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract filename
        std::string filename(reinterpret_cast<const char*>(message.data.data() + 12), name_len);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 REMOVE in: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = dir_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += filename;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Remove the file
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (!std::filesystem::exists(fs_path) || !std::filesystem::is_regular_file(fs_path)) {
            std::cerr << "File not found or not a regular file: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        try {
            if (!std::filesystem::remove(fs_path)) {
                std::cerr << "Failed to remove file: " << fs_path << std::endl;
                failed_requests_++;
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error removing file: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data (WCC data only)
        std::vector<uint8_t> response_data;
        // WCC data (pre-op and post-op attributes) - simplified
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 REMOVE for: " << full_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 REMOVE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3RmDir(const RpcMessagevoid NfsServerSimple::handleNfsv3RmDir(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3RmDir(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3RmDir(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 RMDIR uses 64-bit directory handle
        if (message.data.size() < 16) {
            std::cerr << "Invalid NFSv3 RMDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (8 bytes)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        
        // Extract directory name length (4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 8, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 12 + name_len) {
            std::cerr << "Invalid NFSv3 RMDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory name
        std::string dirname(reinterpret_cast<const char*>(message.data.data() + 12), name_len);
        
        // Get parent directory path from handle
        std::string parent_path = getPathFromHandle(dir_handle_id);
        if (parent_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(parent_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 RMDIR in: " << parent_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full path
        std::string full_path = parent_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += "/";
        }
        full_path += dirname;
        
        // Validate path
        if (!validatePath(full_path)) {
            std::cerr << "Invalid path: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check if directory exists and is empty
        std::filesystem::path fs_path = std::filesystem::path(config_.root_path) / full_path;
        if (!std::filesystem::exists(fs_path) || !std::filesystem::is_directory(fs_path)) {
            std::cerr << "Directory not found: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        if (!std::filesystem::is_empty(fs_path)) {
            std::cerr << "Directory not empty: " << fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Remove the directory
        try {
            if (!std::filesystem::remove(fs_path)) {
                std::cerr << "Failed to remove directory: " << fs_path << std::endl;
                failed_requests_++;
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error removing directory: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data (WCC data only)
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 RMDIR for: " << full_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 RMDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Rename(const RpcMessagevoid NfsServerSimple::handleNfsv3Rename(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Rename(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Rename(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 RENAME uses 64-bit handles for both source and destination
        if (message.data.size() < 24) {
            std::cerr << "Invalid NFSv3 RENAME request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        
        // Extract source directory handle (8 bytes)
        uint64_t src_dir_handle = 0;
        memcpy(&src_dir_handle, message.data.data() + offset, 8);
        src_dir_handle = ntohll_custom(src_dir_handle);
        uint32_t src_dir_handle_id = static_cast<uint32_t>(src_dir_handle);
        offset += 8;
        
        // Extract source filename length (4 bytes)
        uint32_t src_name_len = 0;
        memcpy(&src_name_len, message.data.data() + offset, 4);
        src_name_len = ntohl(src_name_len);
        offset += 4;
        
        if (message.data.size() < offset + src_name_len + 12) {
            std::cerr << "Invalid NFSv3 RENAME request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract source filename
        std::string src_filename(reinterpret_cast<const char*>(message.data.data() + offset), src_name_len);
        offset += src_name_len;
        // Align to 4-byte boundary
        while (offset % 4 != 0) offset++;
        
        // Extract destination directory handle (8 bytes)
        uint64_t dst_dir_handle = 0;
        memcpy(&dst_dir_handle, message.data.data() + offset, 8);
        dst_dir_handle = ntohll_custom(dst_dir_handle);
        uint32_t dst_dir_handle_id = static_cast<uint32_t>(dst_dir_handle);
        offset += 8;
        
        // Extract destination filename length (4 bytes)
        uint32_t dst_name_len = 0;
        memcpy(&dst_name_len, message.data.data() + offset, 4);
        dst_name_len = ntohl(dst_name_len);
        offset += 4;
        
        if (message.data.size() < offset + dst_name_len) {
            std::cerr << "Invalid NFSv3 RENAME request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract destination filename
        std::string dst_filename(reinterpret_cast<const char*>(message.data.data() + offset), dst_name_len);
        
        // Get source and destination directory paths
        std::string src_dir_path = getPathFromHandle(src_dir_handle_id);
        std::string dst_dir_path = getPathFromHandle(dst_dir_handle_id);
        
        if (src_dir_path.empty() || dst_dir_path.empty()) {
            std::cerr << "Invalid directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(src_dir_path, auth_context, false, true) || 
            !checkAccess(dst_dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 RENAME" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full paths
        std::string src_full_path = src_dir_path;
        if (!src_full_path.empty() && src_full_path.back() != '/') {
            src_full_path += "/";
        }
        src_full_path += src_filename;
        
        std::string dst_full_path = dst_dir_path;
        if (!dst_full_path.empty() && dst_full_path.back() != '/') {
            dst_full_path += "/";
        }
        dst_full_path += dst_filename;
        
        // Validate paths
        if (!validatePath(src_full_path) || !validatePath(dst_full_path)) {
            std::cerr << "Invalid path" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Rename the file
        std::filesystem::path src_fs_path = std::filesystem::path(config_.root_path) / src_full_path;
        std::filesystem::path dst_fs_path = std::filesystem::path(config_.root_path) / dst_full_path;
        
        if (!std::filesystem::exists(src_fs_path)) {
            std::cerr << "Source file not found: " << src_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        try {
            std::filesystem::rename(src_fs_path, dst_fs_path);
        } catch (const std::exception& e) {
            std::cerr << "Error renaming file: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data (WCC data only)
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 RENAME: " << src_full_path << " -> " << dst_full_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 RENAME: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Link(const RpcMessagevoid NfsServerSimple::handleNfsv3Link(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Link(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Link(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 LINK uses 64-bit handles
        if (message.data.size() < 24) {
            std::cerr << "Invalid NFSv3 LINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t file_handle_id = static_cast<uint32_t>(file_handle);
        
        // Extract directory handle (8 bytes)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data() + 8, 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        
        // Extract link name length (4 bytes)
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + 16, 4);
        name_len = ntohl(name_len);
        
        if (message.data.size() < 20 + name_len) {
            std::cerr << "Invalid NFSv3 LINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract link name
        std::string link_name(reinterpret_cast<const char*>(message.data.data() + 20), name_len);
        
        // Get file and directory paths
        std::string file_path = getPathFromHandle(file_handle_id);
        std::string dir_path = getPathFromHandle(dir_handle_id);
        
        if (file_path.empty() || dir_path.empty()) {
            std::cerr << "Invalid handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true) || 
            !checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 LINK" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Construct full paths
        std::string src_file_path = file_path;
        std::string dst_full_path = dir_path;
        if (!dst_full_path.empty() && dst_full_path.back() != '/') {
            dst_full_path += "/";
        }
        dst_full_path += link_name;
        
        // Validate paths
        if (!validatePath(src_file_path) || !validatePath(dst_full_path)) {
            std::cerr << "Invalid path" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create hard link
        std::filesystem::path src_fs_path = std::filesystem::path(config_.root_path) / src_file_path;
        std::filesystem::path dst_fs_path = std::filesystem::path(config_.root_path) / dst_full_path;
        
        if (!std::filesystem::exists(src_fs_path) || !std::filesystem::is_regular_file(src_fs_path)) {
            std::cerr << "Source file not found: " << src_fs_path << std::endl;
            failed_requests_++;
            return;
        }
        
        try {
            std::filesystem::create_hard_link(src_fs_path, dst_fs_path);
        } catch (const std::exception& e) {
            std::cerr << "Error creating hard link: " << e.what() << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data (post-op attributes)
        std::vector<uint8_t> response_data;
        // Post-op attributes similar to GETATTR
        auto status = std::filesystem::status(src_fs_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::file_size(src_fs_path);
        
        uint32_t file_type = 1;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 2; // Hard link increases link count
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = file_handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // WCC data (pre-op attributes)
        response_data.insert(response_data.end(), response_data.begin(), response_data.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 LINK: " << src_file_path << " -> " << dst_full_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 LINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3ReadDir(const RpcMessagevoid NfsServerSimple::handleNfsv3ReadDir(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3ReadDir(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3ReadDir(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 READDIR uses 64-bit directory handle and cookie
        if (message.data.size() < 20) {
            std::cerr << "Invalid NFSv3 READDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (8 bytes)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        
        // Extract cookie (8 bytes, 64-bit)
        uint64_t cookie = 0;
        memcpy(&cookie, message.data.data() + 8, 8);
        cookie = ntohll_custom(cookie);
        
        // Extract count (4 bytes) - max bytes to return
        uint32_t count = 0;
        memcpy(&count, message.data.data() + 16, 4);
        count = ntohl(count);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 READDIR on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / dir_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_directory(full_path)) {
            std::cerr << "Directory not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read directory entries
        std::vector<std::string> entries = readDirectory(dir_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Directory entries
        size_t start_idx = static_cast<size_t>(cookie);
        size_t max_entries = 100; // Limit entries per response
        bool eof = false;
        
        for (size_t i = start_idx; i < entries.size() && i < start_idx + max_entries; ++i) {
            const std::string& entry_name = entries[i];
            
            // File ID (64-bit)
            uint64_t file_id = i + 1;
            file_id = htonll_custom(file_id);
            response_data.insert(response_data.end(), (uint8_t*)&file_id, (uint8_t*)&file_id + 8);
            
            // Filename length
            uint32_t name_len = static_cast<uint32_t>(entry_name.length());
            name_len = htonl(name_len);
            response_data.insert(response_data.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
            
            // Filename
            response_data.insert(response_data.end(), entry_name.begin(), entry_name.end());
            
            // Padding to 4-byte boundary
            while (response_data.size() % 4 != 0) {
                response_data.push_back(0);
            }
            
            // Cookie for next entry (64-bit)
            uint64_t next_cookie = i + 1;
            next_cookie = htonll_custom(next_cookie);
            response_data.insert(response_data.end(), (uint8_t*)&next_cookie, (uint8_t*)&next_cookie + 8);
            
            if (response_data.size() > count) {
                break;
            }
        }
        
        // EOF flag
        eof = (start_idx + max_entries >= entries.size());
        uint32_t eof_flag = eof ? 1 : 0;
        eof_flag = htonl(eof_flag);
        response_data.insert(response_data.end(), (uint8_t*)&eof_flag, (uint8_t*)&eof_flag + 8);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 READDIR for: " << dir_path << " (cookie: " << cookie << ", entries: " << entries.size() << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 READDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3ReadDirPlus(const RpcMessagevoid NfsServerSimple::handleNfsv3ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 READDIRPLUS uses 64-bit directory handle and cookie, returns attributes
        if (message.data.size() < 20) {
            std::cerr << "Invalid NFSv3 READDIRPLUS request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract directory handle (8 bytes)
        uint64_t dir_handle = 0;
        memcpy(&dir_handle, message.data.data(), 8);
        dir_handle = ntohll_custom(dir_handle);
        uint32_t dir_handle_id = static_cast<uint32_t>(dir_handle);
        
        // Extract cookie (8 bytes, 64-bit)
        uint64_t cookie = 0;
        memcpy(&cookie, message.data.data() + 8, 8);
        cookie = ntohll_custom(cookie);
        
        // Extract dircount (4 bytes) - max directory entries
        uint32_t dircount = 0;
        memcpy(&dircount, message.data.data() + 16, 4);
        dircount = ntohl(dircount);
        
        // Extract maxcount (4 bytes) - max bytes
        uint32_t maxcount = 0;
        memcpy(&maxcount, message.data.data() + 20, 4);
        maxcount = ntohl(maxcount);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 READDIRPLUS on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / dir_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_directory(full_path)) {
            std::cerr << "Directory not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read directory entries
        std::vector<std::string> entries = readDirectory(dir_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Directory entries with attributes
        size_t start_idx = static_cast<size_t>(cookie);
        size_t max_entries = (dircount > 0) ? dircount : 100;
        bool eof = false;
        
        for (size_t i = start_idx; i < entries.size() && i < start_idx + max_entries; ++i) {
            const std::string& entry_name = entries[i];
            std::string entry_path = dir_path;
            if (!entry_path.empty() && entry_path.back() != '/') {
                entry_path += "/";
            }
            entry_path += entry_name;
            
            std::filesystem::path entry_fs_path = std::filesystem::path(config_.root_path) / entry_path;
            
            // File ID (64-bit)
            uint32_t entry_handle_id = getHandleForPath(entry_path);
            uint64_t file_id = entry_handle_id;
            file_id = htonll_custom(file_id);
            response_data.insert(response_data.end(), (uint8_t*)&file_id, (uint8_t*)&file_id + 8);
            
            // Filename length
            uint32_t name_len = static_cast<uint32_t>(entry_name.length());
            name_len = htonl(name_len);
            response_data.insert(response_data.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
            
            // Filename
            response_data.insert(response_data.end(), entry_name.begin(), entry_name.end());
            
            // Padding to 4-byte boundary
            while (response_data.size() % 4 != 0) {
                response_data.push_back(0);
            }
            
            // Cookie for next entry (64-bit)
            uint64_t next_cookie = i + 1;
            next_cookie = htonll_custom(next_cookie);
            response_data.insert(response_data.end(), (uint8_t*)&next_cookie, (uint8_t*)&next_cookie + 8);
            
            // Post-op attributes (fattr3) - similar to GETATTR
            if (std::filesystem::exists(entry_fs_path)) {
                auto status = std::filesystem::status(entry_fs_path);
                auto perms = status.permissions();
                auto file_size = std::filesystem::is_regular_file(entry_fs_path) ? std::filesystem::file_size(entry_fs_path) : 0;
                
                uint32_t file_type = 1;
                if (std::filesystem::is_directory(entry_fs_path)) file_type = 2;
                else if (std::filesystem::is_symlink(entry_fs_path)) file_type = 5;
                file_type = htonl(file_type);
                response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
                
                uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
                file_mode = htonl(file_mode);
                response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
                
                uint32_t nlink = 1;
                nlink = htonl(nlink);
                response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
                
                uint32_t uid = 0, gid = 0;
                uid = htonl(uid);
                gid = htonl(gid);
                response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
                response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
                
                uint64_t size_64 = file_size;
                size_64 = htonll_custom(size_64);
                response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
                
                uint64_t used_64 = file_size;
                used_64 = htonll_custom(used_64);
                response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
                
                uint64_t rdev = 0;
                rdev = htonll_custom(rdev);
                response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
                
                uint64_t fsid = 1;
                fsid = htonll_custom(fsid);
                response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
                
                uint64_t fileid_attr = entry_handle_id;
                fileid_attr = htonll_custom(fileid_attr);
                response_data.insert(response_data.end(), (uint8_t*)&fileid_attr, (uint8_t*)&fileid_attr + 8);
                
                uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
                atime_sec = htonll_custom(atime_sec);
                atime_nsec = htonll_custom(atime_nsec);
                mtime_sec = htonll_custom(mtime_sec);
                mtime_nsec = htonll_custom(mtime_nsec);
                ctime_sec = htonll_custom(ctime_sec);
                ctime_nsec = htonll_custom(ctime_nsec);
                response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
                response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
                response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
                response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
                response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
                response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
            }
            
            if (response_data.size() > maxcount) {
                break;
            }
        }
        
        // EOF flag
        eof = (start_idx + max_entries >= entries.size());
        uint32_t eof_flag = eof ? 1 : 0;
        eof_flag = htonl(eof_flag);
        response_data.insert(response_data.end(), (uint8_t*)&eof_flag, (uint8_t*)&eof_flag + 8);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 READDIRPLUS for: " << dir_path << " (cookie: " << cookie << ", entries: " << entries.size() << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 READDIRPLUS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3FSStat(const RpcMessagevoid NfsServerSimple::handleNfsv3FSStat(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3FSStat(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3FSStat(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 FSSTAT uses 64-bit file handle
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv3 FSSTAT request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 FSSTAT on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        std::filesystem::space_info space = std::filesystem::space(full_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Post-op attributes (fattr3)
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::is_regular_file(full_path) ? std::filesystem::file_size(full_path) : 0;
        
        uint32_t file_type = 1;
        if (std::filesystem::is_directory(full_path)) file_type = 2;
        else if (std::filesystem::is_symlink(full_path)) file_type = 5;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // FSSTAT data (statvfs structure)
        uint64_t tbytes = space.capacity;
        uint64_t fbytes = space.free;
        uint64_t abytes = space.available;
        uint64_t tfiles = 1000; // Simplified
        uint64_t ffiles = 500;  // Simplified
        uint64_t afiles = 500;  // Simplified
        uint32_t invarsec = 1;  // Invariant seconds
        
        tbytes = htonll_custom(tbytes);
        fbytes = htonll_custom(fbytes);
        abytes = htonll_custom(abytes);
        tfiles = htonll_custom(tfiles);
        ffiles = htonll_custom(ffiles);
        afiles = htonll_custom(afiles);
        invarsec = htonl(invarsec);
        
        response_data.insert(response_data.end(), (uint8_t*)&tbytes, (uint8_t*)&tbytes + 8);
        response_data.insert(response_data.end(), (uint8_t*)&fbytes, (uint8_t*)&fbytes + 8);
        response_data.insert(response_data.end(), (uint8_t*)&abytes, (uint8_t*)&abytes + 8);
        response_data.insert(response_data.end(), (uint8_t*)&tfiles, (uint8_t*)&tfiles + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ffiles, (uint8_t*)&ffiles + 8);
        response_data.insert(response_data.end(), (uint8_t*)&afiles, (uint8_t*)&afiles + 8);
        response_data.insert(response_data.end(), (uint8_t*)&invarsec, (uint8_t*)&invarsec + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 FSSTAT for: " << file_path << " (total: " << space.capacity << ", free: " << space.available << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 FSSTAT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3FSInfo(const RpcMessagevoid NfsServerSimple::handleNfsv3FSInfo(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3FSInfo(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3FSInfo(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 FSINFO uses 64-bit file handle
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv3 FSINFO request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 FSINFO on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Post-op attributes (fattr3) - similar to GETATTR
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::is_regular_file(full_path) ? std::filesystem::file_size(full_path) : 0;
        
        uint32_t file_type = 1;
        if (std::filesystem::is_directory(full_path)) file_type = 2;
        else if (std::filesystem::is_symlink(full_path)) file_type = 5;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // FSINFO data
        uint32_t rtmax = 1048576;  // Max read transfer size
        uint32_t rtpref = 1048576; // Preferred read transfer size
        uint32_t rtmult = 512;     // Read transfer size multiple
        uint32_t wtmax = 1048576;  // Max write transfer size
        uint32_t wtpref = 1048576; // Preferred write transfer size
        uint32_t wtmult = 512;     // Write transfer size multiple
        uint32_t dtpref = 1048576; // Preferred directory transfer size
        uint64_t maxfilesize = 0x7FFFFFFFFFFFFFFFULL; // Max file size
        uint64_t time_delta_sec = 0;   // Time delta seconds
        uint64_t time_delta_nsec = 1;  // Time delta nanoseconds
        uint32_t properties = 0x00000001; // FSF3_LINK, FSF3_SYMLINK, FSF3_HOMOGENEOUS, FSF3_CANSETTIME
        
        rtmax = htonl(rtmax);
        rtpref = htonl(rtpref);
        rtmult = htonl(rtmult);
        wtmax = htonl(wtmax);
        wtpref = htonl(wtpref);
        wtmult = htonl(wtmult);
        dtpref = htonl(dtpref);
        maxfilesize = htonll_custom(maxfilesize);
        time_delta_sec = htonll_custom(time_delta_sec);
        time_delta_nsec = htonll_custom(time_delta_nsec);
        properties = htonl(properties);
        
        response_data.insert(response_data.end(), (uint8_t*)&rtmax, (uint8_t*)&rtmax + 4);
        response_data.insert(response_data.end(), (uint8_t*)&rtpref, (uint8_t*)&rtpref + 4);
        response_data.insert(response_data.end(), (uint8_t*)&rtmult, (uint8_t*)&rtmult + 4);
        response_data.insert(response_data.end(), (uint8_t*)&wtmax, (uint8_t*)&wtmax + 4);
        response_data.insert(response_data.end(), (uint8_t*)&wtpref, (uint8_t*)&wtpref + 4);
        response_data.insert(response_data.end(), (uint8_t*)&wtmult, (uint8_t*)&wtmult + 4);
        response_data.insert(response_data.end(), (uint8_t*)&dtpref, (uint8_t*)&dtpref + 4);
        response_data.insert(response_data.end(), (uint8_t*)&maxfilesize, (uint8_t*)&maxfilesize + 8);
        response_data.insert(response_data.end(), (uint8_t*)&time_delta_sec, (uint8_t*)&time_delta_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&time_delta_nsec, (uint8_t*)&time_delta_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&properties, (uint8_t*)&properties + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 FSINFO for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 FSINFO: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3PathConf(const RpcMessagevoid NfsServerSimple::handleNfsv3PathConf(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3PathConf(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3PathConf(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 PATHCONF uses 64-bit file handle
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv3 PATHCONF request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv3 PATHCONF on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Post-op attributes (fattr3) - similar to GETATTR
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::is_regular_file(full_path) ? std::filesystem::file_size(full_path) : 0;
        
        uint32_t file_type = 1;
        if (std::filesystem::is_directory(full_path)) file_type = 2;
        else if (std::filesystem::is_symlink(full_path)) file_type = 5;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        uint32_t nlink = 1;
        nlink = htonl(nlink);
        response_data.insert(response_data.end(), (uint8_t*)&nlink, (uint8_t*)&nlink + 4);
        
        uint32_t uid = 0, gid = 0;
        uid = htonl(uid);
        gid = htonl(gid);
        response_data.insert(response_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
        response_data.insert(response_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
        
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        uint64_t used_64 = file_size;
        used_64 = htonll_custom(used_64);
        response_data.insert(response_data.end(), (uint8_t*)&used_64, (uint8_t*)&used_64 + 8);
        
        uint64_t rdev = 0;
        rdev = htonll_custom(rdev);
        response_data.insert(response_data.end(), (uint8_t*)&rdev, (uint8_t*)&rdev + 8);
        
        uint64_t fsid = 1;
        fsid = htonll_custom(fsid);
        response_data.insert(response_data.end(), (uint8_t*)&fsid, (uint8_t*)&fsid + 8);
        
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        uint64_t atime_sec = 0, atime_nsec = 0, mtime_sec = 0, mtime_nsec = 0, ctime_sec = 0, ctime_nsec = 0;
        atime_sec = htonll_custom(atime_sec);
        atime_nsec = htonll_custom(atime_nsec);
        mtime_sec = htonll_custom(mtime_sec);
        mtime_nsec = htonll_custom(mtime_nsec);
        ctime_sec = htonll_custom(ctime_sec);
        ctime_nsec = htonll_custom(ctime_nsec);
        response_data.insert(response_data.end(), (uint8_t*)&atime_sec, (uint8_t*)&atime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&atime_nsec, (uint8_t*)&atime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_sec, (uint8_t*)&mtime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&mtime_nsec, (uint8_t*)&mtime_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_sec, (uint8_t*)&ctime_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ctime_nsec, (uint8_t*)&ctime_nsec + 8);
        
        // PATHCONF data
        uint32_t linkmax = 32767;     // Max links
        uint32_t name_max = 255;      // Max filename length
        uint32_t no_trunc = 1;        // No truncation
        uint32_t chown_restricted = 0; // Chown not restricted
        uint32_t case_insensitive = 0; // Case sensitive
        uint32_t case_preserving = 1;  // Case preserving
        
        linkmax = htonl(linkmax);
        name_max = htonl(name_max);
        no_trunc = htonl(no_trunc);
        chown_restricted = htonl(chown_restricted);
        case_insensitive = htonl(case_insensitive);
        case_preserving = htonl(case_preserving);
        
        response_data.insert(response_data.end(), (uint8_t*)&linkmax, (uint8_t*)&linkmax + 4);
        response_data.insert(response_data.end(), (uint8_t*)&name_max, (uint8_t*)&name_max + 4);
        no_trunc = htonl(no_trunc);
        response_data.insert(response_data.end(), (uint8_t*)&no_trunc, (uint8_t*)&no_trunc + 4);
        response_data.insert(response_data.end(), (uint8_t*)&chown_restricted, (uint8_t*)&chown_restricted + 4);
        response_data.insert(response_data.end(), (uint8_t*)&case_insensitive, (uint8_t*)&case_insensitive + 4);
        response_data.insert(response_data.end(), (uint8_t*)&case_preserving, (uint8_t*)&case_preserving + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 PATHCONF for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 PATHCONF: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Commit(const RpcMessagevoid NfsServerSimple::handleNfsv3Commit(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv3Commit(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv3Commit(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv3 COMMIT uses 64-bit file handle and offset
        if (message.data.size() < 24) {
            std::cerr << "Invalid NFSv3 COMMIT request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Extract file handle (8 bytes)
        uint64_t file_handle = 0;
        memcpy(&file_handle, message.data.data(), 8);
        file_handle = ntohll_custom(file_handle);
        uint32_t handle_id = static_cast<uint32_t>(file_handle);
        
        // Extract offset (8 bytes, 64-bit)
        uint64_t offset = 0;
        memcpy(&offset, message.data.data() + 8, 8);
        offset = ntohll_custom(offset);
        
        // Extract count (4 bytes)
        uint32_t count = 0;
        memcpy(&count, message.data.data() + 16, 4);
        count = ntohl(count);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv3 COMMIT on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // COMMIT ensures data is written to stable storage
        // For now, we just sync the file (simplified)
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
            // In a real implementation, we would fsync the file here
            // For now, we just acknowledge the commit
        }
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Write verifier (8 bytes)
        uint64_t verifier = 0;
        verifier = htonll_custom(verifier);
        response_data.insert(response_data.end(), (uint8_t*)&verifier, (uint8_t*)&verifier + 8);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv3 COMMIT for: " << file_path << " (offset: " << offset << ", count: " << count << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 COMMIT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

// Helper functions for NFSv4 variable-length handles
static std::vector<uint8_t> encodeNfsv4Handle(uint32_t handle_id) {
    std::vector<uint8_t> handle;
    uint32_t length = 4;
    length = htonl(length);
    handle.insert(handle.end(), (uint8_t*)&length, (uint8_t*)&length + 4);
    uint32_t id = htonl(handle_id);
    handle.insert(handle.end(), (uint8_t*)&id, (uint8_t*)&id + 4);
    return handle;
}

static uint32_t decodeNfsv4Handle(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 4 > data.size()) {
        return 0;
    }
    uint32_t length = 0;
    memcpy(&length, data.data() + offset, 4);
    length = ntohl(length);
    offset += 4;
    
    if (offset + length > data.size() || length != 4) {
        return 0;
    }
    
    uint32_t handle_id = 0;
    memcpy(&handle_id, data.data() + offset, 4);
    handle_id = ntohl(handle_id);
    offset += length;
    
    // Align to 4-byte boundary
    while (offset % 4 != 0) {
        offset++;
    }
    
    return handle_id;
}

// NFSv4 procedure implementations
void NfsServerSimple::handleNfsv4Null(const RpcMessagevoid NfsServerSimple::handleNfsv4Null(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Null(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Null(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    // NULL procedure always succeeds
    RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, {});
    successful_requests_++;
    std::cout << "Handled NFSv4 NULL procedure (user: " << auth_context.uid << ")" << std::endl;
}

void NfsServerSimple::handleNfsv4Compound(const RpcMessagevoid NfsServerSimple::handleNfsv4Compound(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Compound(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Compound(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 COMPOUND is the main entry point that processes multiple operations
        // For now, this is a simplified implementation
        // In a full implementation, this would parse the compound request and execute each operation
        
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 COMPOUND request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse compound header (simplified)
        // In full implementation, would parse: tag, minorversion, numops, operations[]
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Compound response header (simplified)
        uint32_t status = 0; // NFS4_OK
        status = htonl(status);
        response_data.insert(response_data.end(), (uint8_t*)&status, (uint8_t*)&status + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 COMPOUND procedure (framework) (user: " << auth_context.uid << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 COMPOUND: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4GetAttr(const RpcMessagevoid NfsServerSimple::handleNfsv4GetAttr(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4GetAttr(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4GetAttr(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 GETATTR uses variable-length file handle
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 GETATTR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv4 GETATTR on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path)) {
            std::cerr << "File not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file attributes
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        auto file_size = std::filesystem::is_regular_file(full_path) ? std::filesystem::file_size(full_path) : 0;
        
        // Create response data (simplified NFSv4 fattr4 structure)
        std::vector<uint8_t> response_data;
        
        // Attribute bitmap (simplified - all attributes supported)
        uint32_t attr_mask = 0xFFFFFFFF;
        attr_mask = htonl(attr_mask);
        response_data.insert(response_data.end(), (uint8_t*)&attr_mask, (uint8_t*)&attr_mask + 4);
        
        // File type
        uint32_t file_type = 1; // NF4REG
        if (std::filesystem::is_directory(full_path)) file_type = 2; // NF4DIR
        else if (std::filesystem::is_symlink(full_path)) file_type = 4; // NF4LNK
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        // File mode
        uint32_t file_mode = static_cast<uint32_t>(perms) & 0x1FF;
        file_mode = htonl(file_mode);
        response_data.insert(response_data.end(), (uint8_t*)&file_mode, (uint8_t*)&file_mode + 4);
        
        // File size (64-bit)
        uint64_t size_64 = file_size;
        size_64 = htonll_custom(size_64);
        response_data.insert(response_data.end(), (uint8_t*)&size_64, (uint8_t*)&size_64 + 8);
        
        // File ID
        uint64_t fileid = handle_id;
        fileid = htonll_custom(fileid);
        response_data.insert(response_data.end(), (uint8_t*)&fileid, (uint8_t*)&fileid + 8);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 GETATTR for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 GETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4SetAttr(const RpcMessagevoid NfsServerSimple::handleNfsv4SetAttr(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4SetAttr(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4SetAttr(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 SETATTR uses variable-length file handle
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 SETATTR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 SETATTR on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse attribute bitmap and values (simplified)
        // In a full implementation, we would parse the full sattr4 structure
        
        // Create response data with post-operation attributes
        std::vector<uint8_t> response_data;
        
        // Attribute bitmap
        uint32_t attr_mask = 0xFFFFFFFF;
        attr_mask = htonl(attr_mask);
        response_data.insert(response_data.end(), (uint8_t*)&attr_mask, (uint8_t*)&attr_mask + 4);
        
        // File type
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        uint32_t file_type = 1;
        if (std::filesystem::is_directory(full_path)) file_type = 2;
        else if (std::filesystem::is_symlink(full_path)) file_type = 4;
        file_type = htonl(file_type);
        response_data.insert(response_data.end(), (uint8_t*)&file_type, (uint8_t*)&file_type + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 SETATTR for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Lookup(const RpcMessagevoid NfsServerSimple::handleNfsv4Lookup(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Lookup(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Lookup(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 LOOKUP uses variable-length directory handle and filename
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 LOOKUP request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse filename (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 LOOKUP: missing filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + offset, 4);
        name_len = ntohl(name_len);
        offset += 4;
        
        if (offset + name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 LOOKUP: filename too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string filename(message.data.begin() + offset, message.data.begin() + offset + name_len);
        offset += name_len;
        while (offset % 4 != 0) offset++; // Align
        
        // Construct full path
        std::string file_path = dir_path;
        if (!file_path.empty() && file_path.back() != '/') {
            file_path += "/";
        }
        file_path += filename;
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv4 LOOKUP on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::exists(full_path)) {
            std::cerr << "File not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create handle for the file
        uint32_t file_handle_id = getHandleForPath(file_path);
        
        // Create response data with file handle
        std::vector<uint8_t> response_data;
        std::vector<uint8_t> file_handle = encodeNfsv4Handle(file_handle_id);
        response_data.insert(response_data.end(), file_handle.begin(), file_handle.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 LOOKUP for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 LOOKUP: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Access(const RpcMessagevoid NfsServerSimple::handleNfsv4Access(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Access(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Access(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 ACCESS uses variable-length file handle
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 ACCESS request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse access mask (32-bit)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 ACCESS: missing access mask" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t access_mask = 0;
        memcpy(&access_mask, message.data.data() + offset, 4);
        access_mask = ntohl(access_mask);
        
        // Check access permissions
        bool can_read = checkAccess(file_path, auth_context, true, false);
        bool can_write = checkAccess(file_path, auth_context, false, true);
        bool can_execute = checkAccess(file_path, auth_context, true, false);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Supported access rights
        uint32_t supported = 0x1F; // READ, LOOKUP, MODIFY, EXTEND, DELETE, EXECUTE
        supported = htonl(supported);
        response_data.insert(response_data.end(), (uint8_t*)&supported, (uint8_t*)&supported + 4);
        
        // Access rights
        uint32_t access_rights = 0;
        if (can_read) access_rights |= 0x01; // READ
        if (can_read) access_rights |= 0x02; // LOOKUP
        if (can_write) access_rights |= 0x04; // MODIFY
        if (can_write) access_rights |= 0x08; // EXTEND
        if (can_write) access_rights |= 0x10; // DELETE
        if (can_execute) access_rights |= 0x20; // EXECUTE
        access_rights = htonl(access_rights);
        response_data.insert(response_data.end(), (uint8_t*)&access_rights, (uint8_t*)&access_rights + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 ACCESS for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 ACCESS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReadLink(const RpcMessagevoid NfsServerSimple::handleNfsv4ReadLink(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4ReadLink(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4ReadLink(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 READLINK uses variable-length file handle
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 READLINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv4 READLINK on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::is_symlink(full_path)) {
            std::cerr << "Not a symlink: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read symlink target
        std::string target = std::filesystem::read_symlink(full_path).string();
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Symlink data (component4)
        uint32_t target_len = static_cast<uint32_t>(target.length());
        target_len = htonl(target_len);
        response_data.insert(response_data.end(), (uint8_t*)&target_len, (uint8_t*)&target_len + 4);
        response_data.insert(response_data.end(), target.begin(), target.end());
        
        // Pad to 4-byte boundary
        while (response_data.size() % 4 != 0) {
            response_data.push_back(0);
        }
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 READLINK for: " << file_path << " -> " << target << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 READLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Read(const RpcMessagevoid NfsServerSimple::handleNfsv4Read(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Read(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Read(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 READ uses variable-length file handle, 64-bit offset, and count
        if (message.data.size() < 20) {
            std::cerr << "Invalid NFSv4 READ request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse offset (64-bit)
        if (offset + 8 > message.data.size()) {
            std::cerr << "Invalid NFSv4 READ: missing offset" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint64_t read_offset = 0;
        memcpy(&read_offset, message.data.data() + offset, 8);
        read_offset = ntohll_custom(read_offset);
        offset += 8;
        
        // Parse count (32-bit)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 READ: missing count" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t count = 0;
        memcpy(&count, message.data.data() + offset, 4);
        count = ntohl(count);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv4 READ on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::is_regular_file(full_path)) {
            std::cerr << "Not a regular file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read file data
        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        file.seekg(static_cast<std::streamoff>(read_offset));
        std::vector<uint8_t> file_data(count);
        file.read(reinterpret_cast<char*>(file_data.data()), count);
        size_t bytes_read = file.gcount();
        file.close();
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // EOF flag
        uint32_t eof = (read_offset + bytes_read >= std::filesystem::file_size(full_path)) ? 1 : 0;
        eof = htonl(eof);
        response_data.insert(response_data.end(), (uint8_t*)&eof, (uint8_t*)&eof + 4);
        
        // Data length
        uint32_t data_len = static_cast<uint32_t>(bytes_read);
        data_len = htonl(data_len);
        response_data.insert(response_data.end(), (uint8_t*)&data_len, (uint8_t*)&data_len + 4);
        
        // File data
        response_data.insert(response_data.end(), file_data.begin(), file_data.begin() + bytes_read);
        
        // Pad to 4-byte boundary
        while (response_data.size() % 4 != 0) {
            response_data.push_back(0);
        }
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 READ for: " << file_path << " (offset: " << read_offset << ", count: " << bytes_read << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 READ: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Write(const RpcMessagevoid NfsServerSimple::handleNfsv4Write(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Write(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Write(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 WRITE uses variable-length file handle, 64-bit offset, count, and data
        if (message.data.size() < 20) {
            std::cerr << "Invalid NFSv4 WRITE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse offset (64-bit)
        if (offset + 8 > message.data.size()) {
            std::cerr << "Invalid NFSv4 WRITE: missing offset" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint64_t write_offset = 0;
        memcpy(&write_offset, message.data.data() + offset, 8);
        write_offset = ntohll_custom(write_offset);
        offset += 8;
        
        // Parse count (32-bit)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 WRITE: missing count" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t count = 0;
        memcpy(&count, message.data.data() + offset, 4);
        count = ntohl(count);
        offset += 4;
        
        // Parse stable flag (32-bit)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 WRITE: missing stable flag" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t stable = 0;
        memcpy(&stable, message.data.data() + offset, 4);
        stable = ntohl(stable);
        offset += 4;
        
        // Align to 4-byte boundary
        while (offset % 4 != 0) {
            offset++;
        }
        
        // Parse data length and data
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 WRITE: missing data length" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t data_len = 0;
        memcpy(&data_len, message.data.data() + offset, 4);
        data_len = ntohl(data_len);
        offset += 4;
        
        if (offset + data_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 WRITE: data too short" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::vector<uint8_t> write_data(message.data.begin() + offset, message.data.begin() + offset + data_len);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 WRITE on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        
        // Write file data
        std::ofstream file(full_path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            // File doesn't exist, create it
            file.open(full_path, std::ios::binary | std::ios::out);
        }
        
        if (!file.is_open()) {
            std::cerr << "Failed to open file for writing: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        file.seekp(static_cast<std::streamoff>(write_offset));
        file.write(reinterpret_cast<const char*>(write_data.data()), write_data.size());
        file.close();
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Write verifier (8 bytes)
        uint64_t verifier = 0;
        verifier = htonll_custom(verifier);
        response_data.insert(response_data.end(), (uint8_t*)&verifier, (uint8_t*)&verifier + 8);
        
        // Count written
        uint32_t count_written = static_cast<uint32_t>(write_data.size());
        count_written = htonl(count_written);
        response_data.insert(response_data.end(), (uint8_t*)&count_written, (uint8_t*)&count_written + 4);
        
        // Stable flag
        stable = htonl(stable);
        response_data.insert(response_data.end(), (uint8_t*)&stable, (uint8_t*)&stable + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 WRITE for: " << file_path << " (offset: " << write_offset << ", count: " << write_data.size() << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 WRITE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Create(const RpcMessagevoid NfsServerSimple::handleNfsv4Create(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Create(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Create(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 CREATE uses variable-length directory handle and filename
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 CREATE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse filename (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 CREATE: missing filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + offset, 4);
        name_len = ntohl(name_len);
        offset += 4;
        
        if (offset + name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 CREATE: filename too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string filename(message.data.begin() + offset, message.data.begin() + offset + name_len);
        offset += name_len;
        while (offset % 4 != 0) offset++; // Align
        
        // Construct full path
        std::string file_path = dir_path;
        if (!file_path.empty() && file_path.back() != '/') {
            file_path += "/";
        }
        file_path += filename;
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 CREATE on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create file
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        std::ofstream file(full_path);
        if (!file.is_open()) {
            std::cerr << "Failed to create file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        file.close();
        
        // Get or create handle for the new file
        uint32_t file_handle_id = getHandleForPath(file_path);
        
        // Create response data with file handle
        std::vector<uint8_t> response_data;
        std::vector<uint8_t> file_handle = encodeNfsv4Handle(file_handle_id);
        response_data.insert(response_data.end(), file_handle.begin(), file_handle.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 CREATE for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 CREATE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4MkDir(const RpcMessagevoid NfsServerSimple::handleNfsv4MkDir(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4MkDir(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4MkDir(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 MKDIR uses variable-length directory handle and directory name
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 MKDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse directory name (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 MKDIR: missing directory name" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + offset, 4);
        name_len = ntohl(name_len);
        offset += 4;
        
        if (offset + name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 MKDIR: directory name too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string dirname(message.data.begin() + offset, message.data.begin() + offset + name_len);
        offset += name_len;
        while (offset % 4 != 0) offset++; // Align
        
        // Construct full path
        std::string new_dir_path = dir_path;
        if (!new_dir_path.empty() && new_dir_path.back() != '/') {
            new_dir_path += "/";
        }
        new_dir_path += dirname;
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 MKDIR on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create directory
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / new_dir_path;
        if (!std::filesystem::create_directory(full_path)) {
            std::cerr << "Failed to create directory: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get or create handle for the new directory
        uint32_t new_dir_handle_id = getHandleForPath(new_dir_path);
        
        // Create response data with directory handle
        std::vector<uint8_t> response_data;
        std::vector<uint8_t> new_dir_handle = encodeNfsv4Handle(new_dir_handle_id);
        response_data.insert(response_data.end(), new_dir_handle.begin(), new_dir_handle.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 MKDIR for: " << new_dir_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 MKDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4SymLink(const RpcMessagevoid NfsServerSimple::handleNfsv4SymLink(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4SymLink(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4SymLink(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 SYMLINK uses variable-length directory handle, symlink name, and target
        if (message.data.size() < 12) {
            std::cerr << "Invalid NFSv4 SYMLINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse symlink name (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 SYMLINK: missing symlink name" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + offset, 4);
        name_len = ntohl(name_len);
        offset += 4;
        
        if (offset + name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 SYMLINK: symlink name too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string symlink_name(message.data.begin() + offset, message.data.begin() + offset + name_len);
        offset += name_len;
        while (offset % 4 != 0) offset++; // Align
        
        // Parse target path (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 SYMLINK: missing target path" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t target_len = 0;
        memcpy(&target_len, message.data.data() + offset, 4);
        target_len = ntohl(target_len);
        offset += 4;
        
        if (offset + target_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 SYMLINK: target path too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string target_path(message.data.begin() + offset, message.data.begin() + offset + target_len);
        
        // Construct full path
        std::string symlink_path = dir_path;
        if (!symlink_path.empty() && symlink_path.back() != '/') {
            symlink_path += "/";
        }
        symlink_path += symlink_name;
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 SYMLINK on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create symbolic link
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / symlink_path;
        std::filesystem::create_symlink(target_path, full_path);
        
        // Get or create handle for the symlink
        uint32_t symlink_handle_id = getHandleForPath(symlink_path);
        
        // Create response data with symlink handle
        std::vector<uint8_t> response_data;
        std::vector<uint8_t> symlink_handle = encodeNfsv4Handle(symlink_handle_id);
        response_data.insert(response_data.end(), symlink_handle.begin(), symlink_handle.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 SYMLINK for: " << symlink_path << " -> " << target_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SYMLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4MkNod(const RpcMessagevoid NfsServerSimple::handleNfsv4MkNod(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4MkNod(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4MkNod(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 MKNOD uses variable-length directory handle and filename
        // For now, this is a stub as special files are complex
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 MKNOD request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // MKNOD is complex (creates special files like devices, FIFOs, etc.)
        // For now, return success but don't create anything
        std::vector<uint8_t> response_data;
        uint32_t handle_id = 0;
        std::vector<uint8_t> handle = encodeNfsv4Handle(handle_id);
        response_data.insert(response_data.end(), handle.begin(), handle.end());
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        sendReply(reply, client_conn);
        successful_requests_++;
        std::cout << "Handled NFSv4 MKNOD procedure (stub) (user: " << auth_context.uid << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 MKNOD: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Remove(const RpcMessagevoid NfsServerSimple::handleNfsv4Remove(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Remove(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Remove(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 REMOVE uses variable-length directory handle and filename
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 REMOVE request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse filename (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 REMOVE: missing filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + offset, 4);
        name_len = ntohl(name_len);
        offset += 4;
        
        if (offset + name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 REMOVE: filename too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string filename(message.data.begin() + offset, message.data.begin() + offset + name_len);
        
        // Construct full path
        std::string file_path = dir_path;
        if (!file_path.empty() && file_path.back() != '/') {
            file_path += "/";
        }
        file_path += filename;
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 REMOVE on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Remove file
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (!std::filesystem::remove(full_path)) {
            std::cerr << "Failed to remove file: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data (empty for REMOVE)
        std::vector<uint8_t> response_data;
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 REMOVE for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 REMOVE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4RmDir(const RpcMessagevoid NfsServerSimple::handleNfsv4RmDir(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4RmDir(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4RmDir(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 RMDIR uses variable-length directory handle and directory name
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 RMDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse directory name (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 RMDIR: missing directory name" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + offset, 4);
        name_len = ntohl(name_len);
        offset += 4;
        
        if (offset + name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 RMDIR: directory name too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string dirname(message.data.begin() + offset, message.data.begin() + offset + name_len);
        
        // Construct full path
        std::string target_dir_path = dir_path;
        if (!target_dir_path.empty() && target_dir_path.back() != '/') {
            target_dir_path += "/";
        }
        target_dir_path += dirname;
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 RMDIR on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Remove directory
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / target_dir_path;
        if (!std::filesystem::remove(full_path)) {
            std::cerr << "Failed to remove directory: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data (empty for RMDIR)
        std::vector<uint8_t> response_data;
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 RMDIR for: " << target_dir_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 RMDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Rename(const RpcMessagevoid NfsServerSimple::handleNfsv4Rename(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Rename(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Rename(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 RENAME uses variable-length old and new directory handles and names
        if (message.data.size() < 16) {
            std::cerr << "Invalid NFSv4 RENAME request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t old_dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (old_dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 old directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get old directory path from handle
        std::string old_dir_path = getPathFromHandle(old_dir_handle_id);
        if (old_dir_path.empty()) {
            std::cerr << "Invalid old directory handle: " << old_dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse old filename (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 RENAME: missing old filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t old_name_len = 0;
        memcpy(&old_name_len, message.data.data() + offset, 4);
        old_name_len = ntohl(old_name_len);
        offset += 4;
        
        if (offset + old_name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 RENAME: old filename too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string old_filename(message.data.begin() + offset, message.data.begin() + offset + old_name_len);
        offset += old_name_len;
        while (offset % 4 != 0) offset++; // Align
        
        // Parse new directory handle
        uint32_t new_dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (new_dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 new directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get new directory path from handle
        std::string new_dir_path = getPathFromHandle(new_dir_handle_id);
        if (new_dir_path.empty()) {
            std::cerr << "Invalid new directory handle: " << new_dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse new filename (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 RENAME: missing new filename" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t new_name_len = 0;
        memcpy(&new_name_len, message.data.data() + offset, 4);
        new_name_len = ntohl(new_name_len);
        offset += 4;
        
        if (offset + new_name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 RENAME: new filename too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string new_filename(message.data.begin() + offset, message.data.begin() + offset + new_name_len);
        
        // Construct full paths
        std::string old_file_path = old_dir_path;
        if (!old_file_path.empty() && old_file_path.back() != '/') {
            old_file_path += "/";
        }
        old_file_path += old_filename;
        
        std::string new_file_path = new_dir_path;
        if (!new_file_path.empty() && new_file_path.back() != '/') {
            new_file_path += "/";
        }
        new_file_path += new_filename;
        
        // Check access permissions
        if (!checkAccess(old_dir_path, auth_context, false, true) || 
            !checkAccess(new_dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 RENAME" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Rename file/directory
        std::filesystem::path old_full_path = std::filesystem::path(config_.root_path) / old_file_path;
        std::filesystem::path new_full_path = std::filesystem::path(config_.root_path) / new_file_path;
        std::filesystem::rename(old_full_path, new_full_path);
        
        // Create response data (empty for RENAME)
        std::vector<uint8_t> response_data;
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 RENAME from: " << old_file_path << " to: " << new_file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 RENAME: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Link(const RpcMessagevoid NfsServerSimple::handleNfsv4Link(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Link(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Link(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 LINK uses variable-length file handle and destination directory handle/name
        if (message.data.size() < 16) {
            std::cerr << "Invalid NFSv4 LINK request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t file_handle_id = decodeNfsv4Handle(message.data, offset);
        if (file_handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(file_handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << file_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse destination directory handle
        uint32_t dest_dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dest_dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 destination directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get destination directory path from handle
        std::string dest_dir_path = getPathFromHandle(dest_dir_handle_id);
        if (dest_dir_path.empty()) {
            std::cerr << "Invalid destination directory handle: " << dest_dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse link name (component4)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 LINK: missing link name" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t name_len = 0;
        memcpy(&name_len, message.data.data() + offset, 4);
        name_len = ntohl(name_len);
        offset += 4;
        
        if (offset + name_len > message.data.size()) {
            std::cerr << "Invalid NFSv4 LINK: link name too long" << std::endl;
            failed_requests_++;
            return;
        }
        
        std::string link_name(message.data.begin() + offset, message.data.begin() + offset + name_len);
        
        // Construct full paths
        std::filesystem::path source_path = std::filesystem::path(config_.root_path) / file_path;
        std::string link_path = dest_dir_path;
        if (!link_path.empty() && link_path.back() != '/') {
            link_path += "/";
        }
        link_path += link_name;
        std::filesystem::path dest_path = std::filesystem::path(config_.root_path) / link_path;
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false) || 
            !checkAccess(dest_dir_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 LINK" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create hard link
        std::filesystem::create_hard_link(source_path, dest_path);
        
        // Create response data (empty for LINK)
        std::vector<uint8_t> response_data;
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 LINK from: " << file_path << " to: " << link_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 LINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReadDir(const RpcMessagevoid NfsServerSimple::handleNfsv4ReadDir(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4ReadDir(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4ReadDir(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 READDIR uses variable-length directory handle, cookie, and count
        if (message.data.size() < 20) {
            std::cerr << "Invalid NFSv4 READDIR request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t dir_handle_id = decodeNfsv4Handle(message.data, offset);
        if (dir_handle_id == 0) {
            std::cerr << "Invalid NFSv4 directory handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse cookie (64-bit)
        if (offset + 8 > message.data.size()) {
            std::cerr << "Invalid NFSv4 READDIR: missing cookie" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint64_t cookie = 0;
        memcpy(&cookie, message.data.data() + offset, 8);
        cookie = ntohll_custom(cookie);
        offset += 8;
        
        // Parse count (32-bit)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 READDIR: missing count" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t count = 0;
        memcpy(&count, message.data.data() + offset, 4);
        count = ntohl(count);
        
        // Get directory path from handle
        std::string dir_path = getPathFromHandle(dir_handle_id);
        if (dir_path.empty()) {
            std::cerr << "Invalid directory handle: " << dir_handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(dir_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv4 READDIR on: " << dir_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / dir_path;
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_directory(full_path)) {
            std::cerr << "Directory not found: " << full_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Read directory entries
        std::vector<std::string> entries = readDirectory(dir_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Directory entries
        size_t start_idx = static_cast<size_t>(cookie);
        size_t max_entries = 100;
        bool eof = false;
        
        for (size_t i = start_idx; i < entries.size() && i < start_idx + max_entries; ++i) {
            const std::string& entry_name = entries[i];
            
            // File ID (64-bit)
            uint64_t file_id = i + 1;
            file_id = htonll_custom(file_id);
            response_data.insert(response_data.end(), (uint8_t*)&file_id, (uint8_t*)&file_id + 8);
            
            // Filename length
            uint32_t name_len = static_cast<uint32_t>(entry_name.length());
            name_len = htonl(name_len);
            response_data.insert(response_data.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
            
            // Filename
            response_data.insert(response_data.end(), entry_name.begin(), entry_name.end());
            
            // Padding to 4-byte boundary
            while (response_data.size() % 4 != 0) {
                response_data.push_back(0);
            }
            
            // Cookie for next entry (64-bit)
            uint64_t next_cookie = i + 1;
            next_cookie = htonll_custom(next_cookie);
            response_data.insert(response_data.end(), (uint8_t*)&next_cookie, (uint8_t*)&next_cookie + 8);
            
            if (response_data.size() > count) {
                break;
            }
        }
        
        // EOF flag
        eof = (start_idx + max_entries >= entries.size());
        uint32_t eof_flag = eof ? 1 : 0;
        eof_flag = htonl(eof_flag);
        response_data.insert(response_data.end(), (uint8_t*)&eof_flag, (uint8_t*)&eof_flag + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 READDIR for: " << dir_path << " (cookie: " << cookie << ", entries: " << entries.size() << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 READDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReadDirPlus(const RpcMessagevoid NfsServerSimple::handleNfsv4ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 READDIRPLUS is similar to READDIR but returns attributes
        // For now, implement as READDIR (attributes can be added later)
        handleNfsv4ReadDir(message, auth_context, client_conn);
        std::cout << "Handled NFSv4 READDIRPLUS (using READDIR implementation)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 READDIRPLUS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4FSStat(const RpcMessagevoid NfsServerSimple::handleNfsv4FSStat(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4FSStat(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4FSStat(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 FSSTAT uses variable-length file handle
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 FSSTAT request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv4 FSSTAT on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        std::filesystem::space_info space = std::filesystem::space(full_path);
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // FSSTAT data (simplified)
        uint64_t tbytes = space.capacity;
        uint64_t fbytes = space.free;
        uint64_t abytes = space.available;
        uint64_t tfiles = 1000;
        uint64_t ffiles = 500;
        uint64_t afiles = 500;
        uint32_t invarsec = 1;
        
        tbytes = htonll_custom(tbytes);
        fbytes = htonll_custom(fbytes);
        abytes = htonll_custom(abytes);
        tfiles = htonll_custom(tfiles);
        ffiles = htonll_custom(ffiles);
        afiles = htonll_custom(afiles);
        invarsec = htonl(invarsec);
        
        response_data.insert(response_data.end(), (uint8_t*)&tbytes, (uint8_t*)&tbytes + 8);
        response_data.insert(response_data.end(), (uint8_t*)&fbytes, (uint8_t*)&fbytes + 8);
        response_data.insert(response_data.end(), (uint8_t*)&abytes, (uint8_t*)&abytes + 8);
        response_data.insert(response_data.end(), (uint8_t*)&tfiles, (uint8_t*)&tfiles + 8);
        response_data.insert(response_data.end(), (uint8_t*)&ffiles, (uint8_t*)&ffiles + 8);
        response_data.insert(response_data.end(), (uint8_t*)&afiles, (uint8_t*)&afiles + 8);
        response_data.insert(response_data.end(), (uint8_t*)&invarsec, (uint8_t*)&invarsec + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 FSSTAT for: " << file_path << " (total: " << space.capacity << ", free: " << space.available << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 FSSTAT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4FSInfo(const RpcMessagevoid NfsServerSimple::handleNfsv4FSInfo(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4FSInfo(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4FSInfo(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 FSINFO uses variable-length file handle
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 FSINFO request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv4 FSINFO on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // FSINFO data (simplified)
        uint32_t rtmax = 1048576;
        uint32_t rtpref = 1048576;
        uint32_t rtmult = 512;
        uint32_t wtmax = 1048576;
        uint32_t wtpref = 1048576;
        uint32_t wtmult = 512;
        uint32_t dtpref = 1048576;
        uint64_t maxfilesize = 0x7FFFFFFFFFFFFFFFULL;
        uint64_t time_delta_sec = 0;
        uint64_t time_delta_nsec = 1;
        uint32_t properties = 0x00000001;
        
        rtmax = htonl(rtmax);
        rtpref = htonl(rtpref);
        rtmult = htonl(rtmult);
        wtmax = htonl(wtmax);
        wtpref = htonl(wtpref);
        wtmult = htonl(wtmult);
        dtpref = htonl(dtpref);
        maxfilesize = htonll_custom(maxfilesize);
        time_delta_sec = htonll_custom(time_delta_sec);
        time_delta_nsec = htonll_custom(time_delta_nsec);
        properties = htonl(properties);
        
        response_data.insert(response_data.end(), (uint8_t*)&rtmax, (uint8_t*)&rtmax + 4);
        response_data.insert(response_data.end(), (uint8_t*)&rtpref, (uint8_t*)&rtpref + 4);
        response_data.insert(response_data.end(), (uint8_t*)&rtmult, (uint8_t*)&rtmult + 4);
        response_data.insert(response_data.end(), (uint8_t*)&wtmax, (uint8_t*)&wtmax + 4);
        response_data.insert(response_data.end(), (uint8_t*)&wtpref, (uint8_t*)&wtpref + 4);
        response_data.insert(response_data.end(), (uint8_t*)&wtmult, (uint8_t*)&wtmult + 4);
        response_data.insert(response_data.end(), (uint8_t*)&dtpref, (uint8_t*)&dtpref + 4);
        response_data.insert(response_data.end(), (uint8_t*)&maxfilesize, (uint8_t*)&maxfilesize + 8);
        response_data.insert(response_data.end(), (uint8_t*)&time_delta_sec, (uint8_t*)&time_delta_sec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&time_delta_nsec, (uint8_t*)&time_delta_nsec + 8);
        response_data.insert(response_data.end(), (uint8_t*)&properties, (uint8_t*)&properties + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 FSINFO for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 FSINFO: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4PathConf(const RpcMessagevoid NfsServerSimple::handleNfsv4PathConf(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4PathConf(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4PathConf(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 PATHCONF uses variable-length file handle
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 PATHCONF request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, true, false)) {
            std::cerr << "Access denied for NFSv4 PATHCONF on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // PATHCONF data (simplified)
        uint32_t linkmax = 32767;
        uint32_t name_max = 255;
        uint32_t no_trunc = 1;
        uint32_t chown_restricted = 0;
        uint32_t case_insensitive = 0;
        uint32_t case_preserving = 1;
        
        linkmax = htonl(linkmax);
        name_max = htonl(name_max);
        no_trunc = htonl(no_trunc);
        chown_restricted = htonl(chown_restricted);
        case_insensitive = htonl(case_insensitive);
        case_preserving = htonl(case_preserving);
        
        response_data.insert(response_data.end(), (uint8_t*)&linkmax, (uint8_t*)&linkmax + 4);
        response_data.insert(response_data.end(), (uint8_t*)&name_max, (uint8_t*)&name_max + 4);
        response_data.insert(response_data.end(), (uint8_t*)&no_trunc, (uint8_t*)&no_trunc + 4);
        response_data.insert(response_data.end(), (uint8_t*)&chown_restricted, (uint8_t*)&chown_restricted + 4);
        response_data.insert(response_data.end(), (uint8_t*)&case_insensitive, (uint8_t*)&case_insensitive + 4);
        response_data.insert(response_data.end(), (uint8_t*)&case_preserving, (uint8_t*)&case_preserving + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 PATHCONF for: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 PATHCONF: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Commit(const RpcMessagevoid NfsServerSimple::handleNfsv4Commit(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Commit(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Commit(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 COMMIT uses variable-length file handle, 64-bit offset, and count
        if (message.data.size() < 20) {
            std::cerr << "Invalid NFSv4 COMMIT request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Parse offset (64-bit)
        if (offset + 8 > message.data.size()) {
            std::cerr << "Invalid NFSv4 COMMIT: missing offset" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint64_t commit_offset = 0;
        memcpy(&commit_offset, message.data.data() + offset, 8);
        commit_offset = ntohll_custom(commit_offset);
        offset += 8;
        
        // Parse count (32-bit)
        if (offset + 4 > message.data.size()) {
            std::cerr << "Invalid NFSv4 COMMIT: missing count" << std::endl;
            failed_requests_++;
            return;
        }
        
        uint32_t count = 0;
        memcpy(&count, message.data.data() + offset, 4);
        count = ntohl(count);
        
        // Get file path from handle
        std::string file_path = getPathFromHandle(handle_id);
        if (file_path.empty()) {
            std::cerr << "Invalid file handle: " << handle_id << std::endl;
            failed_requests_++;
            return;
        }
        
        // Check access permissions
        if (!checkAccess(file_path, auth_context, false, true)) {
            std::cerr << "Access denied for NFSv4 COMMIT on: " << file_path << std::endl;
            failed_requests_++;
            return;
        }
        
        // COMMIT ensures data is written to stable storage
        // For now, we just acknowledge the commit
        std::filesystem::path full_path = std::filesystem::path(config_.root_path) / file_path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
            // In a real implementation, we would fsync the file here
        }
        
        // Create response data
        std::vector<uint8_t> response_data;
        
        // Write verifier (8 bytes)
        uint64_t verifier = 0;
        verifier = htonll_custom(verifier);
        response_data.insert(response_data.end(), (uint8_t*)&verifier, (uint8_t*)&verifier + 8);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 COMMIT for: " << file_path << " (offset: " << commit_offset << ", count: " << count << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 COMMIT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4DelegReturn(const RpcMessagevoid NfsServerSimple::handleNfsv4DelegReturn(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4DelegReturn(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4DelegReturn(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 DELEGRETURN returns a delegation
        // For now, return success
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 DELEGRETURN procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 DELEGRETURN: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4GetAcl(const RpcMessagevoid NfsServerSimple::handleNfsv4GetAcl(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4GetAcl(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4GetAcl(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 GETACL retrieves ACL for a file
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 GETACL request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Return empty ACL for now
        std::vector<uint8_t> response_data;
        uint32_t acl_count = 0;
        acl_count = htonl(acl_count);
        response_data.insert(response_data.end(), (uint8_t*)&acl_count, (uint8_t*)&acl_count + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        sendReply(reply, client_conn);
        successful_requests_++;
        std::cout << "Handled NFSv4 GETACL procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 GETACL: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4SetAcl(const RpcMessagevoid NfsServerSimple::handleNfsv4SetAcl(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4SetAcl(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4SetAcl(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 SETACL sets ACL for a file
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 SETACL request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Return success (ACL setting stubbed)
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        sendReply(reply, client_conn);
        successful_requests_++;
        std::cout << "Handled NFSv4 SETACL procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SETACL: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4FSLocations(const RpcMessagevoid NfsServerSimple::handleNfsv4FSLocations(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4FSLocations(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4FSLocations(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 FS_LOCATIONS returns filesystem locations
        if (message.data.size() < 4) {
            std::cerr << "Invalid NFSv4 FS_LOCATIONS request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        size_t offset = 0;
        uint32_t handle_id = decodeNfsv4Handle(message.data, offset);
        if (handle_id == 0) {
            std::cerr << "Invalid NFSv4 file handle" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Return empty locations for now
        std::vector<uint8_t> response_data;
        uint32_t location_count = 0;
        location_count = htonl(location_count);
        response_data.insert(response_data.end(), (uint8_t*)&location_count, (uint8_t*)&location_count + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 FS_LOCATIONS procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 FS_LOCATIONS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReleaseLockOwner(const RpcMessagevoid NfsServerSimple::handleNfsv4ReleaseLockOwner(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4ReleaseLockOwner(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4ReleaseLockOwner(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 RELEASE_LOCKOWNER releases a lock owner
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 RELEASE_LOCKOWNER procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 RELEASE_LOCKOWNER: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4SecInfo(const RpcMessagevoid NfsServerSimple::handleNfsv4SecInfo(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4SecInfo(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4SecInfo(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 SECINFO returns security information
        if (message.data.size() < 8) {
            std::cerr << "Invalid NFSv4 SECINFO request: insufficient data" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Return AUTH_SYS as supported security flavor
        std::vector<uint8_t> response_data;
        uint32_t flavor_count = 1;
        flavor_count = htonl(flavor_count);
        response_data.insert(response_data.end(), (uint8_t*)&flavor_count, (uint8_t*)&flavor_count + 4);
        
        uint32_t flavor = 1; // AUTH_SYS
        flavor = htonl(flavor);
        response_data.insert(response_data.end(), (uint8_t*)&flavor, (uint8_t*)&flavor + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 SECINFO procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SECINFO: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4FSIDPresent(const RpcMessagevoid NfsServerSimple::handleNfsv4FSIDPresent(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4FSIDPresent(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4FSIDPresent(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 FSID_PRESENT checks if FSID is present
        std::vector<uint8_t> response_data;
        uint32_t present = 1; // FSID is present
        present = htonl(present);
        response_data.insert(response_data.end(), (uint8_t*)&present, (uint8_t*)&present + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 FSID_PRESENT procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 FSID_PRESENT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ExchangeID(const RpcMessagevoid NfsServerSimple::handleNfsv4ExchangeID(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4ExchangeID(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4ExchangeID(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 EXCHANGE_ID exchanges client and server IDs
        std::vector<uint8_t> response_data;
        uint64_t client_id = 1;
        client_id = htonll_custom(client_id);
        response_data.insert(response_data.end(), (uint8_t*)&client_id, (uint8_t*)&client_id + 8);
        
        uint64_t server_id = 1;
        server_id = htonll_custom(server_id);
        response_data.insert(response_data.end(), (uint8_t*)&server_id, (uint8_t*)&server_id + 8);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 EXCHANGE_ID procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 EXCHANGE_ID: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4CreateSession(const RpcMessagevoid NfsServerSimple::handleNfsv4CreateSession(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4CreateSession(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4CreateSession(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 CREATE_SESSION creates a new session
        std::vector<uint8_t> response_data;
        uint64_t session_id = 1;
        session_id = htonll_custom(session_id);
        response_data.insert(response_data.end(), (uint8_t*)&session_id, (uint8_t*)&session_id + 8);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 CREATE_SESSION procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 CREATE_SESSION: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4DestroySession(const RpcMessagevoid NfsServerSimple::handleNfsv4DestroySession(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4DestroySession(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4DestroySession(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 DESTROY_SESSION destroys a session
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 DESTROY_SESSION procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 DESTROY_SESSION: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Sequence(const RpcMessagevoid NfsServerSimple::handleNfsv4Sequence(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4Sequence(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4Sequence(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 SEQUENCE handles sequence numbers for session
        std::vector<uint8_t> response_data;
        uint32_t sequence_id = 1;
        sequence_id = htonl(sequence_id);
        response_data.insert(response_data.end(), (uint8_t*)&sequence_id, (uint8_t*)&sequence_id + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 SEQUENCE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SEQUENCE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4GetDeviceInfo(const RpcMessagevoid NfsServerSimple::handleNfsv4GetDeviceInfo(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4GetDeviceInfo(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4GetDeviceInfo(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 GET_DEVICE_INFO returns device information
        std::vector<uint8_t> response_data;
        uint32_t device_count = 0;
        device_count = htonl(device_count);
        response_data.insert(response_data.end(), (uint8_t*)&device_count, (uint8_t*)&device_count + 4);
        
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 GET_DEVICE_INFO procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 GET_DEVICE_INFO: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4BindConnToSession(const RpcMessagevoid NfsServerSimple::handleNfsv4BindConnToSession(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4BindConnToSession(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4BindConnToSession(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 BIND_CONN_TO_SESSION binds connection to session
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 BIND_CONN_TO_SESSION procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 BIND_CONN_TO_SESSION: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4DestroyClientID(const RpcMessagevoid NfsServerSimple::handleNfsv4DestroyClientID(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4DestroyClientID(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4DestroyClientID(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 DESTROY_CLIENTID destroys a client ID
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 DESTROY_CLIENTID procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 DESTROY_CLIENTID: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReclaimComplete(const RpcMessagevoid NfsServerSimple::handleNfsv4ReclaimComplete(const RpcMessage& message, const AuthContext& auth_context) { message, const AuthContextvoid NfsServerSimple::handleNfsv4ReclaimComplete(const RpcMessage& message, const AuthContext& auth_context) { auth_context, const ClientConnectionvoid NfsServerSimple::handleNfsv4ReclaimComplete(const RpcMessage& message, const AuthContext& auth_context) { client_conn) {
    try {
        // NFSv4 RECLAIM_COMPLETE indicates reclaim is complete
        std::vector<uint8_t> response_data;
        RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, response_data);
        successful_requests_++;
        std::cout << "Handled NFSv4 RECLAIM_COMPLETE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 RECLAIM_COMPLETE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

} // namespace SimpleNfsd

