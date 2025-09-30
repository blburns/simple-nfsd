/**
 * @file nfs_server_simple.hpp
 * @brief Simple NFS server implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#ifndef SIMPLE_NFSD_NFS_SERVER_SIMPLE_HPP
#define SIMPLE_NFSD_NFS_SERVER_SIMPLE_HPP

#include "simple_nfsd/config_manager.hpp"
#include "simple_nfsd/rpc_protocol.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>

namespace SimpleNfsd {

// Simple NFS Server Configuration
struct NfsServerConfig {
    std::string bind_address;
    uint16_t port;
    std::string root_path;
    uint32_t max_connections;
    bool enable_tcp;
    bool enable_udp;
    
    NfsServerConfig() 
        : bind_address("0.0.0.0"), port(2049), root_path("/var/lib/simple-nfsd/shares"),
          max_connections(1000), enable_tcp(true), enable_udp(true) {}
};

// Simple NFS Server Statistics
struct NfsServerStats {
    uint64_t total_requests{0};
    uint64_t successful_requests{0};
    uint64_t failed_requests{0};
    uint64_t bytes_read{0};
    uint64_t bytes_written{0};
    uint64_t active_connections{0};
};

// Simple NFS Server
class NfsServerSimple {
public:
    NfsServerSimple();
    ~NfsServerSimple();
    
    // Server lifecycle
    bool initialize(const NfsServerConfig& config);
    bool loadConfiguration(const std::string& config_file);
    bool start();
    void stop();
    bool isRunning() const;
    
    // Configuration
    NfsServerConfig getConfig() const;
    bool updateConfig(const NfsServerConfig& config);
    
    // Statistics
    NfsServerStats getStats() const;
    void resetStats();
    
    // Health checks
    bool isHealthy() const;
    std::string getHealthStatus() const;
    
private:
    // Server state
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    NfsServerConfig config_;
    
    // Network threads
    std::unique_ptr<std::thread> tcp_thread_;
    std::unique_ptr<std::thread> udp_thread_;
    
    // File handle management
    mutable std::mutex handles_mutex_;
    std::map<std::string, uint64_t> path_to_handle_;
    std::map<uint64_t, std::string> handle_to_path_;
    uint64_t next_handle_id_;
    
    // Statistics (atomic for thread safety)
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
    std::atomic<uint64_t> bytes_read_{0};
    std::atomic<uint64_t> bytes_written_{0};
    std::atomic<uint64_t> active_connections_{0};
    
    // Private methods
    void tcpListenerLoop();
    void udpListenerLoop();
    void handleClientConnection(int client_socket);
    void processRpcMessage(const std::vector<uint8_t>& client_address, const std::vector<uint8_t>& raw_message);
    void handleRpcCall(const RpcMessage& message);
    
    // NFS procedure handlers
    void handleNfsv2Call(const RpcMessage& message);
    void handleNfsv3Call(const RpcMessage& message);
    void handleNfsv4Call(const RpcMessage& message);
    
    // NFSv2 procedures
    void handleNfsv2Null(const RpcMessage& message);
    void handleNfsv2GetAttr(const RpcMessage& message);
    void handleNfsv2Lookup(const RpcMessage& message);
    void handleNfsv2Read(const RpcMessage& message);
    void handleNfsv2Write(const RpcMessage& message);
    void handleNfsv2ReadDir(const RpcMessage& message);
    void handleNfsv2StatFS(const RpcMessage& message);
    
    // File system operations
    bool fileExists(const std::string& path) const;
    bool isDirectory(const std::string& path) const;
    bool isFile(const std::string& path) const;
    std::vector<uint8_t> readFile(const std::string& path, uint32_t offset, uint32_t count) const;
    bool writeFile(const std::string& path, uint32_t offset, const std::vector<uint8_t>& data) const;
    std::vector<std::string> readDirectory(const std::string& path) const;
    
    // File handle utilities
    uint64_t createFileHandle(const std::string& path);
    std::string resolveFileHandle(uint64_t handle) const;
    bool isValidFileHandle(uint64_t handle) const;
    
    // Path utilities
    std::string getFullPath(const std::string& relative_path) const;
    std::string getRelativePath(const std::string& full_path) const;
    bool validatePath(const std::string& path) const;
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_NFS_SERVER_SIMPLE_HPP
