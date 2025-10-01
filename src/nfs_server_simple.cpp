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
    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) {
        std::cerr << "Failed to create UDP socket: " << strerror(errno) << std::endl;
        return;
    }
    
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
            // Convert client address to vector
            std::vector<uint8_t> client_address_bytes(sizeof(client_addr));
            std::memcpy(client_address_bytes.data(), &client_addr, sizeof(client_addr));
            
            // Process the message
            std::vector<uint8_t> message_data(buffer, buffer + bytes_received);
            processRpcMessage(client_address_bytes, message_data);
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
        
        // Convert client address to vector (simplified for TCP)
        std::vector<uint8_t> client_address_bytes(4, 0); // Placeholder
        
        // Process the message
        std::vector<uint8_t> message_data(buffer, buffer + bytes_received);
        processRpcMessage(client_address_bytes, message_data);
    }
    
    close(client_socket);
    active_connections_--;
}

void NfsServerSimple::processRpcMessage(const std::vector<uint8_t>& client_address, const std::vector<uint8_t>& raw_message) {
    try {
        total_requests_++;
        
        // Parse RPC message
        RpcMessage message = RpcUtils::deserializeMessage(raw_message);
        
        // Validate message
        if (!RpcUtils::validateMessage(message)) {
            std::cerr << "Invalid RPC message received" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Handle RPC call
        if (message.header.type == RpcMessageType::CALL) {
            handleRpcCall(message);
        } else {
            std::cerr << "Unexpected RPC message type" << std::endl;
            failed_requests_++;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing RPC message: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleRpcCall(const RpcMessage& message) {
    try {
        // Route based on RPC program
        if (message.header.prog == static_cast<uint32_t>(RpcProgram::PORTMAP)) {
            handlePortmapperCall(message);
            return;
        }
        
        if (message.header.prog != static_cast<uint32_t>(RpcProgram::NFS)) {
            std::cerr << "Unsupported RPC program: " << message.header.prog << std::endl;
            failed_requests_++;
            return;
        }
        
        // Authenticate the request
        AuthContext auth_context;
        if (!authenticateRequest(message, auth_context)) {
            std::cerr << "Authentication failed for RPC call" << std::endl;
            failed_requests_++;
            return;
        }
        
        // Negotiate NFS version
        uint32_t negotiated_version = negotiateNfsVersion(message.header.vers);
        if (negotiated_version == 0) {
            std::cerr << "No compatible NFS version found for: " << message.header.vers << std::endl;
            failed_requests_++;
            return;
        }
        
        // Handle based on negotiated NFS version
        switch (negotiated_version) {
            case 2:
                handleNfsv2Call(message, auth_context);
                break;
            case 3:
                handleNfsv3Call(message, auth_context);
                break;
            case 4:
                handleNfsv4Call(message, auth_context);
                break;
            default:
                std::cerr << "Unsupported NFS version after negotiation: " << negotiated_version << std::endl;
                failed_requests_++;
                return;
        }
        
        successful_requests_++;
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling RPC call: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Call(const RpcMessage& message, const AuthContext& auth_context) {
    // Handle NFSv2 procedures
    switch (message.header.proc) {
        case 0:  // NULL
            handleNfsv2Null(message, auth_context);
            break;
        case 1:  // GETATTR
            handleNfsv2GetAttr(message, auth_context);
            break;
        case 2:  // SETATTR
            handleNfsv2SetAttr(message, auth_context);
            break;
        case 3:  // LOOKUP
            handleNfsv2Lookup(message, auth_context);
            break;
        case 5:  // READ
            handleNfsv2Read(message, auth_context);
            break;
        case 7:  // WRITE
            handleNfsv2Write(message, auth_context);
            break;
        case 8:  // CREATE
            handleNfsv2Create(message, auth_context);
            break;
        case 9:  // MKDIR
            handleNfsv2MkDir(message, auth_context);
            break;
        case 10: // RMDIR
            handleNfsv2RmDir(message, auth_context);
            break;
        case 11: // REMOVE
            handleNfsv2Remove(message, auth_context);
            break;
        case 12: // RENAME
            handleNfsv2Rename(message, auth_context);
            break;
        case 15: // READDIR
            handleNfsv2ReadDir(message, auth_context);
            break;
        case 16: // STATFS
            handleNfsv2StatFS(message, auth_context);
            break;
        default:
            std::cerr << "Unsupported NFSv2 procedure: " << message.header.proc << std::endl;
            failed_requests_++;
            break;
    }
}

void NfsServerSimple::handleNfsv3Call(const RpcMessage& message, const AuthContext& auth_context) {
    // Handle NFSv3 procedures
    switch (message.header.proc) {
        case 0:  // NULL
            handleNfsv3Null(message, auth_context);
            break;
        case 1:  // GETATTR
            handleNfsv3GetAttr(message, auth_context);
            break;
        case 2:  // SETATTR
            handleNfsv3SetAttr(message, auth_context);
            break;
        case 3:  // LOOKUP
            handleNfsv3Lookup(message, auth_context);
            break;
        case 4:  // ACCESS
            handleNfsv3Access(message, auth_context);
            break;
        case 5:  // READLINK
            handleNfsv3ReadLink(message, auth_context);
            break;
        case 6:  // READ
            handleNfsv3Read(message, auth_context);
            break;
        case 7:  // WRITE
            handleNfsv3Write(message, auth_context);
            break;
        case 8:  // CREATE
            handleNfsv3Create(message, auth_context);
            break;
        case 9:  // MKDIR
            handleNfsv3MkDir(message, auth_context);
            break;
        case 10: // SYMLINK
            handleNfsv3SymLink(message, auth_context);
            break;
        case 11: // MKNOD
            handleNfsv3MkNod(message, auth_context);
            break;
        case 12: // REMOVE
            handleNfsv3Remove(message, auth_context);
            break;
        case 13: // RMDIR
            handleNfsv3RmDir(message, auth_context);
            break;
        case 14: // RENAME
            handleNfsv3Rename(message, auth_context);
            break;
        case 15: // LINK
            handleNfsv3Link(message, auth_context);
            break;
        case 16: // READDIR
            handleNfsv3ReadDir(message, auth_context);
            break;
        case 17: // READDIRPLUS
            handleNfsv3ReadDirPlus(message, auth_context);
            break;
        case 18: // FSSTAT
            handleNfsv3FSStat(message, auth_context);
            break;
        case 19: // FSINFO
            handleNfsv3FSInfo(message, auth_context);
            break;
        case 20: // PATHCONF
            handleNfsv3PathConf(message, auth_context);
            break;
        case 21: // COMMIT
            handleNfsv3Commit(message, auth_context);
            break;
        default:
            std::cerr << "Unsupported NFSv3 procedure: " << message.header.proc << std::endl;
            failed_requests_++;
            break;
    }
}

void NfsServerSimple::handleNfsv4Call(const RpcMessage& message, const AuthContext& auth_context) {
    // Handle NFSv4 procedures
    switch (message.header.proc) {
        case 0:  // NULL
            handleNfsv4Null(message, auth_context);
            break;
        case 1:  // COMPOUND
            handleNfsv4Compound(message, auth_context);
            break;
        case 2:  // GETATTR
            handleNfsv4GetAttr(message, auth_context);
            break;
        case 3:  // SETATTR
            handleNfsv4SetAttr(message, auth_context);
            break;
        case 4:  // LOOKUP
            handleNfsv4Lookup(message, auth_context);
            break;
        case 5:  // ACCESS
            handleNfsv4Access(message, auth_context);
            break;
        case 6:  // READLINK
            handleNfsv4ReadLink(message, auth_context);
            break;
        case 7:  // READ
            handleNfsv4Read(message, auth_context);
            break;
        case 8:  // WRITE
            handleNfsv4Write(message, auth_context);
            break;
        case 9:  // CREATE
            handleNfsv4Create(message, auth_context);
            break;
        case 10: // MKDIR
            handleNfsv4MkDir(message, auth_context);
            break;
        case 11: // SYMLINK
            handleNfsv4SymLink(message, auth_context);
            break;
        case 12: // MKNOD
            handleNfsv4MkNod(message, auth_context);
            break;
        case 13: // REMOVE
            handleNfsv4Remove(message, auth_context);
            break;
        case 14: // RMDIR
            handleNfsv4RmDir(message, auth_context);
            break;
        case 15: // RENAME
            handleNfsv4Rename(message, auth_context);
            break;
        case 16: // LINK
            handleNfsv4Link(message, auth_context);
            break;
        case 17: // READDIR
            handleNfsv4ReadDir(message, auth_context);
            break;
        case 18: // READDIRPLUS
            handleNfsv4ReadDirPlus(message, auth_context);
            break;
        case 19: // FSSTAT
            handleNfsv4FSStat(message, auth_context);
            break;
        case 20: // FSINFO
            handleNfsv4FSInfo(message, auth_context);
            break;
        case 21: // PATHCONF
            handleNfsv4PathConf(message, auth_context);
            break;
        case 22: // COMMIT
            handleNfsv4Commit(message, auth_context);
            break;
        case 23: // DELEGRETURN
            handleNfsv4DelegReturn(message, auth_context);
            break;
        case 24: // GETACL
            handleNfsv4GetAcl(message, auth_context);
            break;
        case 25: // SETACL
            handleNfsv4SetAcl(message, auth_context);
            break;
        case 26: // FS_LOCATIONS
            handleNfsv4FSLocations(message, auth_context);
            break;
        case 27: // RELEASE_LOCKOWNER
            handleNfsv4ReleaseLockOwner(message, auth_context);
            break;
        case 28: // SECINFO
            handleNfsv4SecInfo(message, auth_context);
            break;
        case 29: // FSID_PRESENT
            handleNfsv4FSIDPresent(message, auth_context);
            break;
        case 30: // EXCHANGE_ID
            handleNfsv4ExchangeID(message, auth_context);
            break;
        case 31: // CREATE_SESSION
            handleNfsv4CreateSession(message, auth_context);
            break;
        case 32: // DESTROY_SESSION
            handleNfsv4DestroySession(message, auth_context);
            break;
        case 33: // SEQUENCE
            handleNfsv4Sequence(message, auth_context);
            break;
        case 34: // GET_DEVICE_INFO
            handleNfsv4GetDeviceInfo(message, auth_context);
            break;
        case 35: // BIND_CONN_TO_SESSION
            handleNfsv4BindConnToSession(message, auth_context);
            break;
        case 36: // DESTROY_CLIENTID
            handleNfsv4DestroyClientID(message, auth_context);
            break;
        case 37: // RECLAIM_COMPLETE
            handleNfsv4ReclaimComplete(message, auth_context);
            break;
        default:
            std::cerr << "Unsupported NFSv4 procedure: " << message.header.proc << std::endl;
            failed_requests_++;
            break;
    }
}

void NfsServerSimple::handlePortmapperCall(const RpcMessage& message) {
    if (!portmapper_) {
        std::cerr << "Portmapper not initialized" << std::endl;
        failed_requests_++;
        return;
    }
    
    portmapper_->handleRpcCall(message);
}

void NfsServerSimple::handleNfsv2Null(const RpcMessage& message, const AuthContext& auth_context) {
    // NULL procedure always succeeds (no authentication required)
    RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, {});
    // TODO: Send reply back to client
    std::cout << "Handled NFSv2 NULL procedure (user: " << auth_context.uid << ")" << std::endl;
}

void NfsServerSimple::handleNfsv2GetAttr(const RpcMessage& message, const AuthContext& auth_context) {
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
        
        // TODO: Send reply back to client
        std::cout << "Handled NFSv2 GETATTR for file: " << file_path << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in GETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Lookup(const RpcMessage& message, const AuthContext& auth_context) {
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
        
        // TODO: Send reply back to client
        std::cout << "Handled NFSv2 LOOKUP for: " << full_path << " (handle: " << file_handle << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in LOOKUP: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Read(const RpcMessage& message, const AuthContext& auth_context) {
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
        
        // TODO: Send reply back to client
        std::cout << "Handled NFSv2 READ for: " << file_path << " (offset: " << offset << ", count: " << count << ", read: " << file_data.size() << " bytes)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in READ: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Write(const RpcMessage& message, const AuthContext& auth_context) {
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
        
        // TODO: Send reply back to client
        std::cout << "Handled NFSv2 WRITE for: " << file_path << " (offset: " << offset << ", written: " << file_data.size() << " bytes)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in WRITE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2ReadDir(const RpcMessage& message, const AuthContext& auth_context) {
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
        
        // TODO: Send reply back to client
        std::cout << "Handled NFSv2 READDIR for: " << dir_path << " (cookie: " << cookie << ", entries: " << entries.size() << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in READDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2StatFS(const RpcMessage& message, const AuthContext& auth_context) {
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
        
        // TODO: Send reply back to client
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
void NfsServerSimple::handleNfsv2SetAttr(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement SETATTR procedure
        // Parse file handle and attributes from message
        // Check access permissions
        // Update file attributes
        std::cout << "Handled NFSv2 SETATTR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in SETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Create(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement CREATE procedure
        // Parse directory file handle and filename from message
        // Check access permissions
        // Create new file
        // Return file handle
        std::cout << "Handled NFSv2 CREATE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in CREATE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2MkDir(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement MKDIR procedure
        // Parse directory file handle and directory name from message
        // Check access permissions
        // Create new directory
        // Return directory file handle
        std::cout << "Handled NFSv2 MKDIR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in MKDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2RmDir(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement RMDIR procedure
        // Parse directory file handle from message
        // Check access permissions
        // Remove directory (must be empty)
        std::cout << "Handled NFSv2 RMDIR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in RMDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Remove(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement REMOVE procedure
        // Parse directory file handle and filename from message
        // Check access permissions
        // Remove file
        std::cout << "Handled NFSv2 REMOVE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in REMOVE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv2Rename(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement RENAME procedure
        // Parse source and destination file handles and names from message
        // Check access permissions
        // Rename file/directory
        std::cout << "Handled NFSv2 RENAME procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in RENAME: " << e.what() << std::endl;
        failed_requests_++;
    }
}

// NFSv3 procedure implementations
void NfsServerSimple::handleNfsv3Null(const RpcMessage& message, const AuthContext& auth_context) {
    // NULL procedure always succeeds
    RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, {});
    std::cout << "Handled NFSv3 NULL procedure (user: " << auth_context.uid << ")" << std::endl;
}

void NfsServerSimple::handleNfsv3GetAttr(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 GETATTR procedure
        std::cout << "Handled NFSv3 GETATTR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 GETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3SetAttr(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 SETATTR procedure
        std::cout << "Handled NFSv3 SETATTR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 SETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Lookup(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 LOOKUP procedure
        std::cout << "Handled NFSv3 LOOKUP procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 LOOKUP: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Access(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 ACCESS procedure
        std::cout << "Handled NFSv3 ACCESS procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 ACCESS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3ReadLink(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 READLINK procedure
        std::cout << "Handled NFSv3 READLINK procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 READLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Read(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 READ procedure
        std::cout << "Handled NFSv3 READ procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 READ: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Write(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 WRITE procedure
        std::cout << "Handled NFSv3 WRITE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 WRITE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Create(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 CREATE procedure
        std::cout << "Handled NFSv3 CREATE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 CREATE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3MkDir(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 MKDIR procedure
        std::cout << "Handled NFSv3 MKDIR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 MKDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3SymLink(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 SYMLINK procedure
        std::cout << "Handled NFSv3 SYMLINK procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 SYMLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3MkNod(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 MKNOD procedure
        std::cout << "Handled NFSv3 MKNOD procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 MKNOD: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Remove(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 REMOVE procedure
        std::cout << "Handled NFSv3 REMOVE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 REMOVE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3RmDir(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 RMDIR procedure
        std::cout << "Handled NFSv3 RMDIR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 RMDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Rename(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 RENAME procedure
        std::cout << "Handled NFSv3 RENAME procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 RENAME: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Link(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 LINK procedure
        std::cout << "Handled NFSv3 LINK procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 LINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3ReadDir(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 READDIR procedure
        std::cout << "Handled NFSv3 READDIR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 READDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 READDIRPLUS procedure
        std::cout << "Handled NFSv3 READDIRPLUS procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 READDIRPLUS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3FSStat(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 FSSTAT procedure
        std::cout << "Handled NFSv3 FSSTAT procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 FSSTAT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3FSInfo(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 FSINFO procedure
        std::cout << "Handled NFSv3 FSINFO procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 FSINFO: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3PathConf(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 PATHCONF procedure
        std::cout << "Handled NFSv3 PATHCONF procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 PATHCONF: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv3Commit(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv3 COMMIT procedure
        std::cout << "Handled NFSv3 COMMIT procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv3 COMMIT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

// NFSv4 procedure implementations
void NfsServerSimple::handleNfsv4Null(const RpcMessage& message, const AuthContext& auth_context) {
    // NULL procedure always succeeds
    RpcMessage reply = RpcUtils::createReply(message.header.xid, RpcAcceptState::SUCCESS, {});
    std::cout << "Handled NFSv4 NULL procedure (user: " << auth_context.uid << ")" << std::endl;
}

void NfsServerSimple::handleNfsv4Compound(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 COMPOUND procedure
        // This is the main NFSv4 procedure that handles multiple operations in a single call
        std::cout << "Handled NFSv4 COMPOUND procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 COMPOUND: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4GetAttr(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 GETATTR procedure
        std::cout << "Handled NFSv4 GETATTR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 GETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4SetAttr(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 SETATTR procedure
        std::cout << "Handled NFSv4 SETATTR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SETATTR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Lookup(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 LOOKUP procedure
        std::cout << "Handled NFSv4 LOOKUP procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 LOOKUP: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Access(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 ACCESS procedure
        std::cout << "Handled NFSv4 ACCESS procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 ACCESS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReadLink(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 READLINK procedure
        std::cout << "Handled NFSv4 READLINK procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 READLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Read(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 READ procedure
        std::cout << "Handled NFSv4 READ procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 READ: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Write(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 WRITE procedure
        std::cout << "Handled NFSv4 WRITE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 WRITE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Create(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 CREATE procedure
        std::cout << "Handled NFSv4 CREATE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 CREATE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4MkDir(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 MKDIR procedure
        std::cout << "Handled NFSv4 MKDIR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 MKDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4SymLink(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 SYMLINK procedure
        std::cout << "Handled NFSv4 SYMLINK procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SYMLINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4MkNod(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 MKNOD procedure
        std::cout << "Handled NFSv4 MKNOD procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 MKNOD: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Remove(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 REMOVE procedure
        std::cout << "Handled NFSv4 REMOVE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 REMOVE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4RmDir(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 RMDIR procedure
        std::cout << "Handled NFSv4 RMDIR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 RMDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Rename(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 RENAME procedure
        std::cout << "Handled NFSv4 RENAME procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 RENAME: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Link(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 LINK procedure
        std::cout << "Handled NFSv4 LINK procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 LINK: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReadDir(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 READDIR procedure
        std::cout << "Handled NFSv4 READDIR procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 READDIR: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 READDIRPLUS procedure
        std::cout << "Handled NFSv4 READDIRPLUS procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 READDIRPLUS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4FSStat(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 FSSTAT procedure
        std::cout << "Handled NFSv4 FSSTAT procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 FSSTAT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4FSInfo(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 FSINFO procedure
        std::cout << "Handled NFSv4 FSINFO procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 FSINFO: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4PathConf(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 PATHCONF procedure
        std::cout << "Handled NFSv4 PATHCONF procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 PATHCONF: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Commit(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 COMMIT procedure
        std::cout << "Handled NFSv4 COMMIT procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 COMMIT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4DelegReturn(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 DELEGRETURN procedure
        std::cout << "Handled NFSv4 DELEGRETURN procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 DELEGRETURN: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4GetAcl(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 GETACL procedure
        std::cout << "Handled NFSv4 GETACL procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 GETACL: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4SetAcl(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 SETACL procedure
        std::cout << "Handled NFSv4 SETACL procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SETACL: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4FSLocations(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 FS_LOCATIONS procedure
        std::cout << "Handled NFSv4 FS_LOCATIONS procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 FS_LOCATIONS: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReleaseLockOwner(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 RELEASE_LOCKOWNER procedure
        std::cout << "Handled NFSv4 RELEASE_LOCKOWNER procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 RELEASE_LOCKOWNER: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4SecInfo(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 SECINFO procedure
        std::cout << "Handled NFSv4 SECINFO procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SECINFO: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4FSIDPresent(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 FSID_PRESENT procedure
        std::cout << "Handled NFSv4 FSID_PRESENT procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 FSID_PRESENT: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ExchangeID(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 EXCHANGE_ID procedure
        std::cout << "Handled NFSv4 EXCHANGE_ID procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 EXCHANGE_ID: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4CreateSession(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 CREATE_SESSION procedure
        std::cout << "Handled NFSv4 CREATE_SESSION procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 CREATE_SESSION: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4DestroySession(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 DESTROY_SESSION procedure
        std::cout << "Handled NFSv4 DESTROY_SESSION procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 DESTROY_SESSION: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4Sequence(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 SEQUENCE procedure
        std::cout << "Handled NFSv4 SEQUENCE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 SEQUENCE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4GetDeviceInfo(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 GET_DEVICE_INFO procedure
        std::cout << "Handled NFSv4 GET_DEVICE_INFO procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 GET_DEVICE_INFO: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4BindConnToSession(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 BIND_CONN_TO_SESSION procedure
        std::cout << "Handled NFSv4 BIND_CONN_TO_SESSION procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 BIND_CONN_TO_SESSION: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4DestroyClientID(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 DESTROY_CLIENTID procedure
        std::cout << "Handled NFSv4 DESTROY_CLIENTID procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 DESTROY_CLIENTID: " << e.what() << std::endl;
        failed_requests_++;
    }
}

void NfsServerSimple::handleNfsv4ReclaimComplete(const RpcMessage& message, const AuthContext& auth_context) {
    try {
        // TODO: Implement NFSv4 RECLAIM_COMPLETE procedure
        std::cout << "Handled NFSv4 RECLAIM_COMPLETE procedure (user: " << auth_context.uid << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error in NFSv4 RECLAIM_COMPLETE: " << e.what() << std::endl;
        failed_requests_++;
    }
}

} // namespace SimpleNfsd

