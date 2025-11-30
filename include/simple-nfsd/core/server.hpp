/**
 * @file nfs_server.hpp
 * @brief NFS server implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#ifndef SIMPLE_NFSD_NFS_SERVER_HPP
#define SIMPLE_NFSD_NFS_SERVER_HPP

#include "simple_nfsd/nfs_protocol.hpp"
#include "simple_nfsd/network_server.hpp"
#include "simple-nfsd/config/config.hpp"
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>

namespace SimpleNfsd {

// NFS Server Statistics
struct NfsServerStats {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> successful_requests{0};
    std::atomic<uint64_t> failed_requests{0};
    std::atomic<uint64_t> bytes_read{0};
    std::atomic<uint64_t> bytes_written{0};
    std::atomic<uint64_t> files_created{0};
    std::atomic<uint64_t> files_deleted{0};
    std::atomic<uint64_t> directories_created{0};
    std::atomic<uint64_t> directories_deleted{0};
    std::atomic<uint64_t> lookups{0};
    std::atomic<uint64_t> reads{0};
    std::atomic<uint64_t> writes{0};
    std::atomic<uint64_t> getattrs{0};
    std::atomic<uint64_t> setattrs{0};
    std::atomic<uint64_t> readdirs{0};
    std::atomic<uint64_t> statfs{0};
};

// NFS Server Configuration
struct NfsServerConfig {
    std::string bind_address;
    uint16_t port;
    std::string root_path;
    uint32_t max_connections;
    uint32_t read_buffer_size;
    uint32_t write_buffer_size;
    bool enable_tcp;
    bool enable_udp;
    bool enable_nfsv2;
    bool enable_nfsv3;
    bool enable_nfsv4;
    std::vector<NfsExport> exports;
    
    NfsServerConfig() 
        : bind_address("0.0.0.0"), port(2049), root_path("/var/lib/simple-nfsd/shares"),
          max_connections(1000), read_buffer_size(8192), write_buffer_size(8192),
          enable_tcp(true), enable_udp(true), enable_nfsv2(true), 
          enable_nfsv3(true), enable_nfsv4(false) {}
};

// NFS Server Implementation
class NfsServerImpl : public NfsServer {
public:
    NfsServerImpl();
    virtual ~NfsServerImpl();
    
    // Initialize server
    bool initialize(const NfsServerConfig& config);
    bool loadConfiguration(const std::string& config_file);
    
    // Server lifecycle
    bool start();
    void stop();
    bool isRunning() const;
    
    // NFSv2 Operations
    virtual NfsError nfs_null() override;
    virtual NfsFileAttributes nfs_getattr(const NfsFileHandle& file) override;
    virtual NfsError nfs_setattr(const NfsFileHandle& file, const NfsFileAttributes& attrs) override;
    virtual NfsLookupResult nfs_lookup(const NfsLookupArgs& args) override;
    virtual NfsReadResult nfs_read(const NfsReadArgs& args) override;
    virtual NfsWriteResult nfs_write(const NfsWriteArgs& args) override;
    virtual NfsCreateResult nfs_create(const NfsCreateArgs& args) override;
    virtual NfsError nfs_remove(const NfsRemoveArgs& args) override;
    virtual NfsError nfs_rename(const NfsRenameArgs& args) override;
    virtual NfsMkdirResult nfs_mkdir(const NfsMkdirArgs& args) override;
    virtual NfsError nfs_rmdir(const NfsRmdirArgs& args) override;
    virtual NfsReadDirResult nfs_readdir(const NfsReadDirArgs& args) override;
    virtual NfsStatFSResult nfs_statfs(const NfsStatFSArgs& args) override;
    
    // Export Management
    virtual bool addExport(const NfsExport& export_info) override;
    virtual bool removeExport(const std::string& path) override;
    virtual std::vector<NfsExport> getExports() const override;
    virtual bool isPathExported(const std::string& path) const override;
    
    // File Handle Management
    virtual NfsFileHandle createFileHandle(const std::string& path) override;
    virtual std::string resolveFileHandle(const NfsFileHandle& handle) override;
    virtual bool isValidFileHandle(const NfsFileHandle& handle) const override;
    
    // Statistics
    NfsServerStats getStats() const;
    void resetStats();
    
    // Configuration
    NfsServerConfig getConfig() const;
    bool updateConfig(const NfsServerConfig& config);
    
private:
    // Server state
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    NfsServerConfig config_;
    NfsServerStats stats_;
    
    // Network servers
    std::unique_ptr<TcpServer> tcp_server_;
    std::unique_ptr<UdpServer> udp_server_;
    std::unique_ptr<NfsProtocolHandler> protocol_handler_;
    
    // Export management
    mutable std::mutex exports_mutex_;
    std::map<std::string, NfsExport> exports_;
    
    // File handle management
    mutable std::mutex handles_mutex_;
    std::map<NfsFileHandle, std::string> handle_to_path_;
    std::map<std::string, NfsFileHandle> path_to_handle_;
    uint64_t next_handle_id_;
    
    // Private methods
    bool setupExports();
    bool setupNetworkServers();
    bool validatePath(const std::string& path) const;
    bool checkPermissions(const std::string& path, NfsAccessMode mode) const;
    std::string getFullPath(const std::string& relative_path) const;
    std::string getRelativePath(const std::string& full_path) const;
    
    // File system operations
    bool fileExists(const std::string& path) const;
    bool isDirectory(const std::string& path) const;
    bool isFile(const std::string& path) const;
    bool createFile(const std::string& path, uint32_t mode) const;
    bool createDirectory(const std::string& path, uint32_t mode) const;
    bool removeFile(const std::string& path) const;
    bool removeDirectory(const std::string& path) const;
    bool renameFile(const std::string& from, const std::string& to) const;
    std::vector<uint8_t> readFile(const std::string& path, uint32_t offset, uint32_t count) const;
    bool writeFile(const std::string& path, uint32_t offset, const std::vector<uint8_t>& data) const;
    std::vector<std::string> readDirectory(const std::string& path) const;
    
    // File attributes
    NfsFileAttributes getFileAttributes(const std::string& path) const;
    bool setFileAttributes(const std::string& path, const NfsFileAttributes& attrs) const;
    
    // File handle utilities
    NfsFileHandle generateFileHandle(const std::string& path);
    std::string getPathFromHandle(const NfsFileHandle& handle) const;
    bool isValidPath(const std::string& path) const;
    
    // Export utilities
    bool isPathInExport(const std::string& path) const;
    bool checkExportAccess(const std::string& path, const std::string& client_ip) const;
    NfsExport getExportForPath(const std::string& path) const;
};

// NFS Server Manager
class NfsServerManager {
public:
    NfsServerManager();
    ~NfsServerManager();
    
    // Server management
    bool startServer(const NfsServerConfig& config);
    bool stopServer();
    bool restartServer();
    bool isServerRunning() const;
    
    // Configuration management
    bool loadConfiguration(const std::string& config_file);
    bool saveConfiguration(const std::string& config_file) const;
    NfsServerConfig getConfiguration() const;
    bool updateConfiguration(const NfsServerConfig& config);
    
    // Export management
    bool addExport(const NfsExport& export_info);
    bool removeExport(const std::string& path);
    std::vector<NfsExport> getExports() const;
    bool reloadExports();
    
    // Statistics and monitoring
    NfsServerStats getStatistics() const;
    void resetStatistics();
    
    // Health checks
    bool isHealthy() const;
    std::string getHealthStatus() const;
    
private:
    std::unique_ptr<NfsServerImpl> server_;
    mutable std::mutex server_mutex_;
    
    // Configuration
    NfsServerConfig config_;
    std::string config_file_;
    
    // Health monitoring
    std::chrono::steady_clock::time_point last_health_check_;
    mutable std::mutex health_mutex_;
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_NFS_SERVER_HPP
