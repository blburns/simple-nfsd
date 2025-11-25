#ifndef SIMPLE_NFSD_VFS_INTERFACE_HPP
#define SIMPLE_NFSD_VFS_INTERFACE_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <chrono>

namespace SimpleNfsd {

// VFS file attributes
struct VfsFileAttributes {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    bool is_directory;
    bool is_file;
    bool is_symlink;
};

// VFS directory entry
struct VfsDirectoryEntry {
    std::string name;
    VfsFileAttributes attrs;
};

// VFS operations interface
class VfsInterface {
public:
    virtual ~VfsInterface() = default;
    
    // File operations
    virtual bool fileExists(const std::string& path) = 0;
    virtual bool isDirectory(const std::string& path) = 0;
    virtual bool isFile(const std::string& path) = 0;
    virtual bool isSymlink(const std::string& path) = 0;
    
    // File I/O
    virtual bool readFile(const std::string& path, uint64_t offset, uint32_t count, 
                         std::vector<uint8_t>& data) = 0;
    virtual bool writeFile(const std::string& path, uint64_t offset, 
                          const std::vector<uint8_t>& data) = 0;
    virtual bool truncateFile(const std::string& path, uint64_t size) = 0;
    
    // File management
    virtual bool createFile(const std::string& path) = 0;
    virtual bool removeFile(const std::string& path) = 0;
    virtual bool renameFile(const std::string& old_path, const std::string& new_path) = 0;
    
    // Directory operations
    virtual bool createDirectory(const std::string& path) = 0;
    virtual bool removeDirectory(const std::string& path) = 0;
    virtual bool readDirectory(const std::string& path, std::vector<VfsDirectoryEntry>& entries) = 0;
    
    // Symlink operations
    virtual bool createSymlink(const std::string& target, const std::string& link_path) = 0;
    virtual bool readSymlink(const std::string& path, std::string& target) = 0;
    
    // Attributes
    virtual bool getAttributes(const std::string& path, VfsFileAttributes& attrs) = 0;
    virtual bool setAttributes(const std::string& path, const VfsFileAttributes& attrs) = 0;
    
    // Extended attributes
    virtual bool getExtendedAttribute(const std::string& path, const std::string& name, 
                                     std::vector<uint8_t>& value) = 0;
    virtual bool setExtendedAttribute(const std::string& path, const std::string& name, 
                                     const std::vector<uint8_t>& value) = 0;
    
    // File system statistics
    virtual bool getFileSystemStats(const std::string& path, uint64_t& total_blocks, 
                                   uint64_t& free_blocks, uint64_t& available_blocks) = 0;
};

// POSIX VFS implementation (wraps std::filesystem)
class PosixVfs : public VfsInterface {
public:
    PosixVfs(const std::string& root_path);
    virtual ~PosixVfs() = default;
    
    // File operations
    bool fileExists(const std::string& path) override;
    bool isDirectory(const std::string& path) override;
    bool isFile(const std::string& path) override;
    bool isSymlink(const std::string& path) override;
    
    // File I/O
    bool readFile(const std::string& path, uint64_t offset, uint32_t count, 
                 std::vector<uint8_t>& data) override;
    bool writeFile(const std::string& path, uint64_t offset, 
                  const std::vector<uint8_t>& data) override;
    bool truncateFile(const std::string& path, uint64_t size) override;
    
    // File management
    bool createFile(const std::string& path) override;
    bool removeFile(const std::string& path) override;
    bool renameFile(const std::string& old_path, const std::string& new_path) override;
    
    // Directory operations
    bool createDirectory(const std::string& path) override;
    bool removeDirectory(const std::string& path) override;
    bool readDirectory(const std::string& path, std::vector<VfsDirectoryEntry>& entries) override;
    
    // Symlink operations
    bool createSymlink(const std::string& target, const std::string& link_path) override;
    bool readSymlink(const std::string& path, std::string& target) override;
    
    // Attributes
    bool getAttributes(const std::string& path, VfsFileAttributes& attrs) override;
    bool setAttributes(const std::string& path, const VfsFileAttributes& attrs) override;
    
    // Extended attributes
    bool getExtendedAttribute(const std::string& path, const std::string& name, 
                             std::vector<uint8_t>& value) override;
    bool setExtendedAttribute(const std::string& path, const std::string& name, 
                             const std::vector<uint8_t>& value) override;
    
    // File system statistics
    bool getFileSystemStats(const std::string& path, uint64_t& total_blocks, 
                           uint64_t& free_blocks, uint64_t& available_blocks) override;

private:
    std::string root_path_;
    std::string resolvePath(const std::string& path) const;
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_VFS_INTERFACE_HPP

