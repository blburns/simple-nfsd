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
#include "simple_nfsd/auth_manager.hpp"
#include "simple_nfsd/portmapper.hpp"
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
    std::map<uint32_t, std::string> file_handles_;  // Handle ID to path mapping
    uint64_t next_handle_id_;
    
    // Authentication
    std::unique_ptr<AuthManager> auth_manager_;
    
    // Portmapper service
    std::unique_ptr<Portmapper> portmapper_;
    
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
    void handleNfsv2Call(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Call(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Call(const RpcMessage& message, const AuthContext& auth_context);
    
    // Portmapper integration
    void handlePortmapperCall(const RpcMessage& message);
    
    // NFSv2 procedures
    void handleNfsv2Null(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2GetAttr(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2SetAttr(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2Lookup(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2Link(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2Read(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2SymLink(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2Write(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2Create(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2MkDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2RmDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2Remove(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2Rename(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2ReadDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv2StatFS(const RpcMessage& message, const AuthContext& auth_context);
    
    // NFSv3 procedures
    void handleNfsv3Null(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3GetAttr(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3SetAttr(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Lookup(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Access(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3ReadLink(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Read(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Write(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Create(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3MkDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3SymLink(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3MkNod(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Remove(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3RmDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Rename(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Link(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3ReadDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3FSStat(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3FSInfo(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3PathConf(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv3Commit(const RpcMessage& message, const AuthContext& auth_context);
    
    // NFSv4 procedures
    void handleNfsv4Null(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Compound(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4GetAttr(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4SetAttr(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Lookup(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Access(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4ReadLink(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Read(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Write(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Create(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4MkDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4SymLink(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4MkNod(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Remove(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4RmDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Rename(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Link(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4ReadDir(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4ReadDirPlus(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4FSStat(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4FSInfo(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4PathConf(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Commit(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4DelegReturn(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4GetAcl(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4SetAcl(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4FSLocations(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4ReleaseLockOwner(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4SecInfo(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4FSIDPresent(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4ExchangeID(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4CreateSession(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4DestroySession(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4Sequence(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4GetDeviceInfo(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4BindConnToSession(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4DestroyClientID(const RpcMessage& message, const AuthContext& auth_context);
    void handleNfsv4ReclaimComplete(const RpcMessage& message, const AuthContext& auth_context);
    
    // File system operations
    bool fileExists(const std::string& path) const;
    bool isDirectory(const std::string& path) const;
    bool isFile(const std::string& path) const;
    std::vector<uint8_t> readFile(const std::string& path, uint32_t offset, uint32_t count) const;
    bool writeFile(const std::string& path, uint32_t offset, const std::vector<uint8_t>& data) const;
    
    // File handle management
    uint32_t getHandleForPath(const std::string& path);
    std::string getPathFromHandle(uint32_t handle) const;
    std::vector<std::string> readDirectory(const std::string& path) const;
    bool validatePath(const std::string& path) const;
    
    // Authentication
    bool initializeAuthentication();
    bool authenticateRequest(const RpcMessage& message, AuthContext& context);
    bool checkAccess(const std::string& path, const AuthContext& context, bool read, bool write);
    
    // Protocol negotiation
    uint32_t negotiateNfsVersion(uint32_t client_version);
    bool isNfsVersionSupported(uint32_t version);
    std::vector<uint32_t> getSupportedNfsVersions();
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_NFS_SERVER_SIMPLE_HPP
