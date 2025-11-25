#ifndef SIMPLE_NFSD_FILESYSTEM_MANAGER_HPP
#define SIMPLE_NFSD_FILESYSTEM_MANAGER_HPP

#include "simple_nfsd/rpc_protocol.hpp"
#include "simple_nfsd/config_manager.hpp"
#include "simple_nfsd/nfs_server_simple.hpp"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace SimpleNfsd {

// File handle structure
struct FileHandle {
    uint32_t handle_id;
    std::string path;
    std::filesystem::file_type type;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_accessed;
};

// File attributes structure
struct FileAttributes {
    uint32_t type;           // File type
    uint32_t mode;           // File mode/permissions
    uint32_t nlink;          // Number of hard links
    uint32_t uid;            // User ID
    uint32_t gid;            // Group ID
    uint64_t size;           // File size
    uint64_t blocks;         // Number of blocks
    uint32_t rdev;           // Device ID
    uint64_t blocksize;      // Block size
    uint64_t fsid;           // File system ID
    uint64_t fileid;         // File ID
    uint64_t atime;          // Access time
    uint64_t mtime;          // Modification time
    uint64_t ctime;          // Change time
};

// Directory entry structure
struct DirectoryEntry {
    std::string name;
    uint32_t fileid;
    FileAttributes attrs;
};

class FilesystemManager {
public:
    FilesystemManager();
    ~FilesystemManager();

    bool initialize(const NfsServerConfig& config);
    void shutdown();

    // File handle management
    uint32_t createFileHandle(const std::string& path);
    bool getFileHandle(uint32_t handle_id, FileHandle& handle);
    bool validateFileHandle(uint32_t handle_id);
    void releaseFileHandle(uint32_t handle_id);

    // File operations
    bool fileExists(const std::string& path);
    bool isDirectory(const std::string& path);
    bool isFile(const std::string& path);
    bool isSymlink(const std::string& path);

    // File attributes
    bool getFileAttributes(const std::string& path, FileAttributes& attrs);
    bool setFileAttributes(const std::string& path, const FileAttributes& attrs);

    // File I/O operations
    bool readFile(const std::string& path, uint64_t offset, uint32_t count, std::vector<uint8_t>& data);
    bool writeFile(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data);
    bool truncateFile(const std::string& path, uint64_t size);

    // Directory operations
    bool createDirectory(const std::string& path);
    bool removeDirectory(const std::string& path);
    bool readDirectory(const std::string& path, std::vector<DirectoryEntry>& entries);

    // File system operations
    bool createFile(const std::string& path);
    bool removeFile(const std::string& path);
    bool renameFile(const std::string& old_path, const std::string& new_path);
    bool createSymlink(const std::string& target, const std::string& link_path);
    bool readSymlink(const std::string& path, std::string& target);

    // Export management
    bool isPathExported(const std::string& path);
    bool validateExportPath(const std::string& path);
    std::string resolveExportPath(const std::string& nfs_path);

    // File system statistics
    bool getFileSystemStats(const std::string& path, uint64_t& total_blocks, uint64_t& free_blocks, uint64_t& available_blocks);

    // Extended attributes (xattrs)
    bool getExtendedAttribute(const std::string& path, const std::string& name, std::vector<uint8_t>& value);
    bool setExtendedAttribute(const std::string& path, const std::string& name, const std::vector<uint8_t>& value);
    bool removeExtendedAttribute(const std::string& path, const std::string& name);
    bool listExtendedAttributes(const std::string& path, std::vector<std::string>& names);

    // Attribute caching
    bool getCachedFileAttributes(const std::string& path, FileAttributes& attrs);
    void invalidateAttributeCache(const std::string& path);
    void clearAttributeCache();

    // Subtree checking
    bool isSubtreeCheckEnabled(const std::string& path);
    bool validateSubtreeAccess(const std::string& path, const std::string& client_ip);

    // Export enumeration
    std::vector<NfsServerConfig::Export> getExports() const;
    bool getExportInfo(const std::string& path, NfsServerConfig::Export& export_info) const;
    std::vector<std::string> listExportedPaths() const;

    // File system caching
    bool getCachedFileContent(const std::string& path, std::vector<uint8_t>& content);
    void cacheFileContent(const std::string& path, const std::vector<uint8_t>& content);
    void invalidateContentCache(const std::string& path);
    void clearContentCache();

    // File system monitoring
    bool startMonitoring(const std::string& path);
    void stopMonitoring();
    bool isMonitoring() const;
    std::vector<std::string> getRecentChanges() const;

    // Quota management
    struct QuotaInfo {
        uint64_t used_bytes;
        uint64_t soft_limit;
        uint64_t hard_limit;
        uint64_t available_bytes;
    };
    bool getQuotaInfo(const std::string& path, uint32_t uid, QuotaInfo& quota);
    bool checkQuota(const std::string& path, uint32_t uid, uint64_t requested_bytes);
    bool setQuota(const std::string& path, uint32_t uid, uint64_t soft_limit, uint64_t hard_limit);

    // Export hot-reload
    bool reloadExports();
    bool reloadExportConfig(const std::string& config_file);

private:
    NfsServerConfig config_;
    std::map<uint32_t, FileHandle> file_handles_;
    std::mutex file_handles_mutex_;
    uint32_t next_handle_id_;
    bool initialized_;

    // Attribute cache
    struct CachedAttributes {
        FileAttributes attrs;
        std::chrono::steady_clock::time_point cached_at;
        static constexpr std::chrono::seconds CACHE_TTL{30}; // 30 second TTL
    };
    std::map<std::string, CachedAttributes> attribute_cache_;
    std::mutex attribute_cache_mutex_;

    // Export cache
    mutable std::map<std::string, bool> export_cache_;
    mutable std::mutex export_cache_mutex_;
    std::chrono::steady_clock::time_point export_cache_time_;

    // Content cache
    struct CachedContent {
        std::vector<uint8_t> content;
        std::chrono::steady_clock::time_point cached_at;
        static constexpr std::chrono::seconds CACHE_TTL{60}; // 60 second TTL for content
    };
    std::map<std::string, CachedContent> content_cache_;
    std::mutex content_cache_mutex_;

    // File system monitoring
    bool monitoring_enabled_;
    std::vector<std::string> recent_changes_;
    mutable std::mutex monitoring_mutex_;
    std::thread monitoring_thread_;
    std::atomic<bool> stop_monitoring_;

    // Quota management
    struct QuotaEntry {
        uint32_t uid;
        uint64_t soft_limit;
        uint64_t hard_limit;
        uint64_t used_bytes;
    };
    std::map<std::pair<std::string, uint32_t>, QuotaEntry> quota_map_;
    std::mutex quota_mutex_;

    // Helper methods
    std::string sanitizePath(const std::string& path) const;
    bool isPathWithinExport(const std::string& path);
    uint32_t generateFileId(const std::string& path);
    void updateFileHandleAccess(uint32_t handle_id);
    bool isCacheValid(const CachedAttributes& cached) const;
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_FILESYSTEM_MANAGER_HPP