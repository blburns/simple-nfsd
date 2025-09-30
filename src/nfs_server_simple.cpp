/**
 * @file nfs_server_simple.cpp
 * @brief Simple NFS server implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include "simple_nfsd/nfs_server_simple.hpp"
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

namespace SimpleNfsd {

NfsServerSimple::NfsServerSimple() 
    : running_(false), initialized_(false), next_handle_id_(1) {
}

NfsServerSimple::~NfsServerSimple() {
    stop();
}

bool NfsServerSimple::initialize(const NfsServerConfig& config) {
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
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
        // For now, just simulate processing
        total_requests_++;
        successful_requests_++;
        
        // TODO: Implement actual RPC message processing
        std::cout << "Received RPC message of " << raw_message.size() << " bytes" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing RPC message: " << e.what() << std::endl;
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

std::vector<std::string> NfsServerSimple::readDirectory(const std::string& path) const {
    std::vector<std::string> entries;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            entries.push_back(entry.path().filename().string());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading directory " << path << ": " << e.what() << std::endl;
    }
    
    return entries;
}

uint64_t NfsServerSimple::createFileHandle(const std::string& path) {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    
    auto it = path_to_handle_.find(path);
    if (it != path_to_handle_.end()) {
        return it->second;
    }
    
    uint64_t handle = next_handle_id_++;
    path_to_handle_[path] = handle;
    handle_to_path_[handle] = path;
    
    return handle;
}

std::string NfsServerSimple::resolveFileHandle(uint64_t handle) const {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    
    auto it = handle_to_path_.find(handle);
    if (it != handle_to_path_.end()) {
        return it->second;
    }
    
    return "";
}

bool NfsServerSimple::isValidFileHandle(uint64_t handle) const {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    return handle_to_path_.find(handle) != handle_to_path_.end();
}

std::string NfsServerSimple::getFullPath(const std::string& relative_path) const {
    if (relative_path.empty() || relative_path[0] == '/') {
        return relative_path;
    }
    
    return config_.root_path + "/" + relative_path;
}

std::string NfsServerSimple::getRelativePath(const std::string& full_path) const {
    if (full_path.find(config_.root_path) == 0) {
        return full_path.substr(config_.root_path.length());
    }
    
    return full_path;
}

bool NfsServerSimple::validatePath(const std::string& path) const {
    std::string full_path = getFullPath(path);
    return full_path.find(config_.root_path) == 0;
}

} // namespace SimpleNfsd
