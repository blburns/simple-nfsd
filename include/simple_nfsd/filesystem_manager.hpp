#ifndef SIMPLE_NFSD_FILESYSTEM_MANAGER_HPP
#define SIMPLE_NFSD_FILESYSTEM_MANAGER_HPP

#include "simple_nfsd/rpc_protocol.hpp"
#include "simple_nfsd/config_manager.hpp"
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

private:
    NfsServerConfig config_;
    std::map<uint32_t, FileHandle> file_handles_;
    std::mutex file_handles_mutex_;
    uint32_t next_handle_id_;
    bool initialized_;

    // Helper methods
    std::string sanitizePath(const std::string& path);
    bool isPathWithinExport(const std::string& path);
    uint32_t generateFileId(const std::string& path);
    void updateFileHandleAccess(uint32_t handle_id);
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_FILESYSTEM_MANAGER_HPP