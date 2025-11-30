#include "simple-nfsd/core/filesystem.hpp"
#include "simple-nfsd/core/vfs_interface.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <functional>
#include <fcntl.h>
#include <sys/xattr.h>
#include <algorithm>
#include <sstream>
#include <thread>
#include <atomic>
#ifdef __linux__
#include <sys/inotify.h>
#include <sys/select.h>
#elif __APPLE__
#include <CoreServices/CoreServices.h>
#endif
#include <thread>
#include <atomic>
#ifdef __linux__
#include <sys/inotify.h>
#include <sys/select.h>
#elif __APPLE__
#include <CoreServices/CoreServices.h>
#endif

namespace SimpleNfsd {

FilesystemManager::FilesystemManager() 
    : next_handle_id_(1), initialized_(false), monitoring_enabled_(false), stop_monitoring_(false) {
    lock_manager_ = std::make_unique<LockManager>();
    // VFS will be initialized in initialize() with the root path
}

FilesystemManager::~FilesystemManager() {
    shutdown();
}

bool FilesystemManager::initialize(const NfsServerConfig& config) {
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    // Create root directory if it doesn't exist
    try {
        std::filesystem::create_directories(config_.root_path);
        std::cout << "FilesystemManager initialized with root path: " << config_.root_path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create root directory: " << e.what() << std::endl;
        return false;
    }
    
    initialized_ = true;
    return true;
}

void FilesystemManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(file_handles_mutex_);
    file_handles_.clear();
    
    std::cout << "FilesystemManager shutdown" << std::endl;
    initialized_ = false;
}

uint32_t FilesystemManager::createFileHandle(const std::string& path) {
    std::lock_guard<std::mutex> lock(file_handles_mutex_);
    
    FileHandle handle;
    handle.handle_id = next_handle_id_++;
    handle.path = sanitizePath(path);
    
    try {
        if (std::filesystem::exists(handle.path)) {
            handle.type = std::filesystem::is_directory(handle.path) ? 
                         std::filesystem::file_type::directory : 
                         std::filesystem::file_type::regular;
        } else {
            handle.type = std::filesystem::file_type::not_found;
        }
    } catch (const std::exception& e) {
        handle.type = std::filesystem::file_type::not_found;
    }
    
    handle.created_at = std::chrono::steady_clock::now();
    handle.last_accessed = handle.created_at;
    
    file_handles_[handle.handle_id] = handle;
    return handle.handle_id;
}

bool FilesystemManager::getFileHandle(uint32_t handle_id, FileHandle& handle) {
    std::lock_guard<std::mutex> lock(file_handles_mutex_);
    
    auto it = file_handles_.find(handle_id);
    if (it != file_handles_.end()) {
        handle = it->second;
        updateFileHandleAccess(handle_id);
        return true;
    }
    return false;
}

bool FilesystemManager::validateFileHandle(uint32_t handle_id) {
    std::lock_guard<std::mutex> lock(file_handles_mutex_);
    
    auto it = file_handles_.find(handle_id);
    if (it != file_handles_.end()) {
        // Check if the file still exists
        if (std::filesystem::exists(it->second.path)) {
            updateFileHandleAccess(handle_id);
            return true;
        } else {
            // File no longer exists, remove handle
            file_handles_.erase(it);
        }
    }
    return false;
}

void FilesystemManager::releaseFileHandle(uint32_t handle_id) {
    std::lock_guard<std::mutex> lock(file_handles_mutex_);
    file_handles_.erase(handle_id);
}

bool FilesystemManager::fileExists(const std::string& path) {
    std::string sanitized_path = sanitizePath(path);
    return std::filesystem::exists(sanitized_path);
}

bool FilesystemManager::isDirectory(const std::string& path) {
    std::string sanitized_path = sanitizePath(path);
    return std::filesystem::is_directory(sanitized_path);
}

bool FilesystemManager::isFile(const std::string& path) {
    std::string sanitized_path = sanitizePath(path);
    return std::filesystem::is_regular_file(sanitized_path);
}

bool FilesystemManager::isSymlink(const std::string& path) {
    std::string sanitized_path = sanitizePath(path);
    return std::filesystem::is_symlink(sanitized_path);
}

bool FilesystemManager::getFileAttributes(const std::string& path, FileAttributes& attrs) {
    std::string sanitized_path = sanitizePath(path);
    
    if (!std::filesystem::exists(sanitized_path)) {
        return false;
    }
    
    try {
        auto status = std::filesystem::status(sanitized_path);
        auto perms = status.permissions();
        
        // Get file stats
        struct stat st;
        if (stat(sanitized_path.c_str(), &st) != 0) {
            return false;
        }
        
        // Fill attributes
        attrs.type = static_cast<uint32_t>(status.type());
        attrs.mode = static_cast<uint32_t>(perms);
        attrs.nlink = static_cast<uint32_t>(st.st_nlink);
        attrs.uid = static_cast<uint32_t>(st.st_uid);
        attrs.gid = static_cast<uint32_t>(st.st_gid);
        attrs.size = static_cast<uint64_t>(st.st_size);
        attrs.blocks = static_cast<uint64_t>(st.st_blocks);
        attrs.rdev = static_cast<uint32_t>(st.st_rdev);
        attrs.blocksize = static_cast<uint64_t>(st.st_blksize);
        attrs.fsid = static_cast<uint64_t>(st.st_dev);
        attrs.fileid = generateFileId(sanitized_path);
        attrs.atime = static_cast<uint64_t>(st.st_atime);
        attrs.mtime = static_cast<uint64_t>(st.st_mtime);
        attrs.ctime = static_cast<uint64_t>(st.st_ctime);
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error getting file attributes for " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::setFileAttributes(const std::string& path, const FileAttributes& attrs) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        // Set file permissions
        if (attrs.mode != 0) {
            std::filesystem::permissions(sanitized_path, static_cast<std::filesystem::perms>(attrs.mode));
        }
        
        // Set ownership
        if (attrs.uid != 0 || attrs.gid != 0) {
            if (chown(sanitized_path.c_str(), attrs.uid, attrs.gid) != 0) {
                return false;
            }
        }
        
        // Set timestamps
        if (attrs.atime != 0 || attrs.mtime != 0) {
            struct timespec times[2];
            times[0].tv_sec = attrs.atime;
            times[0].tv_nsec = 0;
            times[1].tv_sec = attrs.mtime;
            times[1].tv_nsec = 0;
            
            if (utimensat(0, sanitized_path.c_str(), times, 0) != 0) {
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error setting file attributes for " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::readFile(const std::string& path, uint64_t offset, uint32_t count, std::vector<uint8_t>& data) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        std::ifstream file(sanitized_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        file.seekg(offset);
        data.resize(count);
        file.read(reinterpret_cast<char*>(data.data()), count);
        
        if (file.gcount() < count) {
            data.resize(file.gcount());
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error reading file " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::writeFile(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        std::ofstream file(sanitized_path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            // File doesn't exist, create it
            file.open(sanitized_path, std::ios::binary | std::ios::out);
            if (!file.is_open()) {
                return false;
            }
        }
        
        file.seekp(offset);
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        
        return file.good();
    } catch (const std::exception& e) {
        std::cerr << "Error writing file " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::truncateFile(const std::string& path, uint64_t size) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        std::filesystem::resize_file(sanitized_path, size);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error truncating file " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::createDirectory(const std::string& path) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        return std::filesystem::create_directories(sanitized_path);
    } catch (const std::exception& e) {
        std::cerr << "Error creating directory " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::removeDirectory(const std::string& path) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        return std::filesystem::remove(sanitized_path);
    } catch (const std::exception& e) {
        std::cerr << "Error removing directory " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::readDirectory(const std::string& path, std::vector<DirectoryEntry>& entries) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        entries.clear();
        
        for (const auto& entry : std::filesystem::directory_iterator(sanitized_path)) {
            DirectoryEntry dir_entry;
            dir_entry.name = entry.path().filename().string();
            
            if (getFileAttributes(entry.path().string(), dir_entry.attrs)) {
                entries.push_back(dir_entry);
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error reading directory " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::createFile(const std::string& path) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        std::ofstream file(sanitized_path);
        return file.is_open();
    } catch (const std::exception& e) {
        std::cerr << "Error creating file " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::removeFile(const std::string& path) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        return std::filesystem::remove(sanitized_path);
    } catch (const std::exception& e) {
        std::cerr << "Error removing file " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::renameFile(const std::string& old_path, const std::string& new_path) {
    std::string sanitized_old = sanitizePath(old_path);
    std::string sanitized_new = sanitizePath(new_path);
    
    try {
        std::filesystem::rename(sanitized_old, sanitized_new);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error renaming file from " << old_path << " to " << new_path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::createSymlink(const std::string& target, const std::string& link_path) {
    std::string sanitized_target = sanitizePath(target);
    std::string sanitized_link = sanitizePath(link_path);
    
    try {
        std::filesystem::create_symlink(sanitized_target, sanitized_link);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error creating symlink from " << target << " to " << link_path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::readSymlink(const std::string& path, std::string& target) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        target = std::filesystem::read_symlink(sanitized_path).string();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error reading symlink " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FilesystemManager::isPathExported(const std::string& path) {
    // Check if path is within any configured export
    std::string sanitized_path = sanitizePath(path);
    
    // If no exports configured, default to root_path
    if (config_.exports.empty()) {
        return sanitized_path.find(config_.root_path) == 0;
    }
    
    // Check against configured exports
    for (const auto& export_config : config_.exports) {
        std::string export_path = sanitizePath(export_config.path);
        if (sanitized_path.find(export_path) == 0) {
            return true;
        }
    }
    
    return false;
}

bool FilesystemManager::validateExportPath(const std::string& path) {
    return isPathWithinExport(path);
}

std::string FilesystemManager::resolveExportPath(const std::string& nfs_path) {
    // Convert NFS path to local filesystem path
    std::string local_path = config_.root_path + nfs_path;
    return sanitizePath(local_path);
}

bool FilesystemManager::getFileSystemStats(const std::string& path, uint64_t& total_blocks, uint64_t& free_blocks, uint64_t& available_blocks) {
    std::string sanitized_path = sanitizePath(path);
    
    try {
        auto space = std::filesystem::space(sanitized_path);
        total_blocks = space.capacity;
        free_blocks = space.free;
        available_blocks = space.available;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error getting filesystem stats for " << path << ": " << e.what() << std::endl;
        return false;
    }
}

std::string FilesystemManager::sanitizePath(const std::string& path) const {
    // Remove any ".." components and normalize the path
    try {
        std::filesystem::path normalized_path = std::filesystem::canonical(path);
        return normalized_path.string();
    } catch (const std::exception& e) {
        // If canonical fails, try to clean the path manually
        std::filesystem::path clean_path = std::filesystem::path(path).lexically_normal();
        return clean_path.string();
    }
}

bool FilesystemManager::isPathWithinExport(const std::string& path) {
    // Check if path is within any configured export
    std::string sanitized_path = sanitizePath(path);
    
    // If no exports configured, default to root_path
    if (config_.exports.empty()) {
        return sanitized_path.find(config_.root_path) == 0;
    }
    
    // Check against configured exports
    for (const auto& export_config : config_.exports) {
        std::string export_path = sanitizePath(export_config.path);
        if (sanitized_path.find(export_path) == 0) {
            return true;
        }
    }
    
    return false;
}

uint32_t FilesystemManager::generateFileId(const std::string& path) {
    // Generate a unique file ID based on the path
    std::hash<std::string> hasher;
    return static_cast<uint32_t>(hasher(path));
}

void FilesystemManager::updateFileHandleAccess(uint32_t handle_id) {
    auto it = file_handles_.find(handle_id);
    if (it != file_handles_.end()) {
        it->second.last_accessed = std::chrono::steady_clock::now();
    }
}

// Extended attributes (xattrs) implementation
bool FilesystemManager::getExtendedAttribute(const std::string& path, const std::string& name, std::vector<uint8_t>& value) {
    std::string sanitized_path = sanitizePath(path);
    
    #ifdef __APPLE__
    // macOS uses getxattr
    ssize_t size = getxattr(sanitized_path.c_str(), name.c_str(), nullptr, 0, 0, 0);
    if (size < 0) {
        return false;
    }
    
    value.resize(size);
    ssize_t result = getxattr(sanitized_path.c_str(), name.c_str(), value.data(), size, 0, 0);
    return result == size;
    #elif __linux__
    // Linux uses getxattr
    ssize_t size = getxattr(sanitized_path.c_str(), name.c_str(), nullptr, 0);
    if (size < 0) {
        return false;
    }
    
    value.resize(size);
    ssize_t result = getxattr(sanitized_path.c_str(), name.c_str(), value.data(), size);
    return result == size;
    #else
    // Windows/other platforms - not supported
    (void)path; (void)name; (void)value;
    return false;
    #endif
}

bool FilesystemManager::setExtendedAttribute(const std::string& path, const std::string& name, const std::vector<uint8_t>& value) {
    std::string sanitized_path = sanitizePath(path);
    
    #ifdef __APPLE__
    int result = setxattr(sanitized_path.c_str(), name.c_str(), value.data(), value.size(), 0, 0);
    return result == 0;
    #elif __linux__
    int result = setxattr(sanitized_path.c_str(), name.c_str(), value.data(), value.size(), 0);
    return result == 0;
    #else
    (void)path; (void)name; (void)value;
    return false;
    #endif
}

bool FilesystemManager::removeExtendedAttribute(const std::string& path, const std::string& name) {
    std::string sanitized_path = sanitizePath(path);
    
    #ifdef __APPLE__
    int result = removexattr(sanitized_path.c_str(), name.c_str(), 0);
    return result == 0;
    #elif __linux__
    int result = removexattr(sanitized_path.c_str(), name.c_str());
    return result == 0;
    #else
    (void)path; (void)name;
    return false;
    #endif
}

bool FilesystemManager::listExtendedAttributes(const std::string& path, std::vector<std::string>& names) {
    std::string sanitized_path = sanitizePath(path);
    names.clear();
    
    #ifdef __APPLE__
    ssize_t size = listxattr(sanitized_path.c_str(), nullptr, 0, 0);
    if (size < 0) {
        return false;
    }
    
    std::vector<char> buffer(size);
    ssize_t result = listxattr(sanitized_path.c_str(), buffer.data(), size, 0);
    if (result < 0) {
        return false;
    }
    
    // Parse null-separated string list
    const char* ptr = buffer.data();
    const char* end = buffer.data() + result;
    while (ptr < end) {
        names.push_back(std::string(ptr));
        ptr += names.back().length() + 1;
    }
    return true;
    #elif __linux__
    ssize_t size = listxattr(sanitized_path.c_str(), nullptr, 0);
    if (size < 0) {
        return false;
    }
    
    std::vector<char> buffer(size);
    ssize_t result = listxattr(sanitized_path.c_str(), buffer.data(), size);
    if (result < 0) {
        return false;
    }
    
    // Parse null-separated string list
    const char* ptr = buffer.data();
    const char* end = buffer.data() + result;
    while (ptr < end) {
        names.push_back(std::string(ptr));
        ptr += names.back().length() + 1;
    }
    return true;
    #else
    (void)path; (void)names;
    return false;
    #endif
}

// Attribute caching implementation
bool FilesystemManager::getCachedFileAttributes(const std::string& path, FileAttributes& attrs) {
    std::lock_guard<std::mutex> lock(attribute_cache_mutex_);
    
    auto it = attribute_cache_.find(path);
    if (it != attribute_cache_.end() && isCacheValid(it->second)) {
        attrs = it->second.attrs;
        return true;
    }
    
    // Cache miss or expired - fetch from filesystem
    if (getFileAttributes(path, attrs)) {
        CachedAttributes cached;
        cached.attrs = attrs;
        cached.cached_at = std::chrono::steady_clock::now();
        attribute_cache_[path] = cached;
        return true;
    }
    
    return false;
}

void FilesystemManager::invalidateAttributeCache(const std::string& path) {
    std::lock_guard<std::mutex> lock(attribute_cache_mutex_);
    attribute_cache_.erase(path);
}

void FilesystemManager::clearAttributeCache() {
    std::lock_guard<std::mutex> lock(attribute_cache_mutex_);
    attribute_cache_.clear();
}

bool FilesystemManager::isCacheValid(const CachedAttributes& cached) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cached.cached_at);
    return elapsed < CachedAttributes::CACHE_TTL;
}

// Subtree checking implementation
bool FilesystemManager::isSubtreeCheckEnabled(const std::string& path) {
    // Check if subtree_check is enabled for the export containing this path
    for (const auto& export_config : config_.exports) {
        std::string export_path = sanitizePath(export_config.path);
        std::string sanitized_path = sanitizePath(path);
        
        if (sanitized_path.find(export_path) == 0) {
            // Check if subtree_check is in options
            std::string options = export_config.options;
            return options.find("subtree_check") != std::string::npos;
        }
    }
    return false;
}

bool FilesystemManager::validateSubtreeAccess(const std::string& path, const std::string& client_ip) {
    if (!isSubtreeCheckEnabled(path)) {
        return true; // Subtree checking disabled
    }
    
    // With subtree_check, verify the client has access to the exact path
    // and all parent directories
    std::string sanitized_path = sanitizePath(path);
    std::filesystem::path current_path(sanitized_path);
    
    while (!current_path.empty() && current_path != current_path.root_path()) {
        std::string check_path = current_path.string();
        
        // Check if this path is exported and client has access
        bool found_export = false;
        for (const auto& export_config : config_.exports) {
            std::string export_path = sanitizePath(export_config.path);
            if (check_path == export_path || check_path.find(export_path) == 0) {
                // Check client access
                std::string clients_str = export_config.clients;
                if (clients_str.empty()) {
                    found_export = true;
                    break; // No client restrictions
                }
                
                // Check if client IP matches (clients is a comma-separated string)
                if (clients_str.find(client_ip) != std::string::npos) {
                    found_export = true;
                    break;
                }
                if (found_export) break;
            }
        }
        
        if (!found_export) {
            return false; // Path not accessible
        }
        
        current_path = current_path.parent_path();
    }
    
    return true;
}

// Export enumeration implementation
std::vector<NfsServerConfig::Export> FilesystemManager::getExports() const {
    return config_.exports;
}

bool FilesystemManager::getExportInfo(const std::string& path, NfsServerConfig::Export& export_info) const {
    std::string sanitized_path = sanitizePath(path);
    
    for (const auto& export_config : config_.exports) {
        std::string export_path = sanitizePath(export_config.path);
        if (sanitized_path.find(export_path) == 0) {
            export_info = export_config;
            return true;
        }
    }
    
    return false;
}

std::vector<std::string> FilesystemManager::listExportedPaths() const {
    std::vector<std::string> paths;
    
    if (config_.exports.empty()) {
        // If no exports configured, return root_path
        paths.push_back(config_.root_path);
        return paths;
    }
    
    for (const auto& export_config : config_.exports) {
        paths.push_back(export_config.path);
    }
    
    return paths;
}

// File system caching implementation
bool FilesystemManager::getCachedFileContent(const std::string& path, std::vector<uint8_t>& content) {
    std::lock_guard<std::mutex> lock(content_cache_mutex_);
    
    auto it = content_cache_.find(path);
    if (it != content_cache_.end()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.cached_at);
        if (elapsed < CachedContent::CACHE_TTL) {
            content = it->second.content;
            return true;
        } else {
            // Cache expired
            content_cache_.erase(it);
        }
    }
    
    return false;
}

void FilesystemManager::cacheFileContent(const std::string& path, const std::vector<uint8_t>& content) {
    std::lock_guard<std::mutex> lock(content_cache_mutex_);
    
    CachedContent cached;
    cached.content = content;
    cached.cached_at = std::chrono::steady_clock::now();
    content_cache_[path] = cached;
}

void FilesystemManager::invalidateContentCache(const std::string& path) {
    std::lock_guard<std::mutex> lock(content_cache_mutex_);
    content_cache_.erase(path);
}

void FilesystemManager::clearContentCache() {
    std::lock_guard<std::mutex> lock(content_cache_mutex_);
    content_cache_.clear();
}

// File system monitoring implementation
bool FilesystemManager::startMonitoring(const std::string& path) {
    if (monitoring_enabled_) {
        return true; // Already monitoring
    }
    
    std::lock_guard<std::mutex> lock(monitoring_mutex_);
    monitoring_enabled_ = true;
    stop_monitoring_ = false;
    recent_changes_.clear();
    
    // Start monitoring thread
    monitoring_thread_ = std::thread([this, path]() {
        #ifdef __linux__
        int fd = inotify_init();
        if (fd < 0) {
            std::cerr << "Failed to initialize inotify" << std::endl;
            return;
        }
        
        int wd = inotify_add_watch(fd, path.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE);
        if (wd < 0) {
            std::cerr << "Failed to add watch for: " << path << std::endl;
            close(fd);
            return;
        }
        
        char buffer[4096];
        while (!stop_monitoring_) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(fd, &read_fds);
            
            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            int result = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
            if (result > 0 && FD_ISSET(fd, &read_fds)) {
                ssize_t length = read(fd, buffer, sizeof(buffer));
                if (length > 0) {
                    struct inotify_event* event = (struct inotify_event*)buffer;
                    if (event->len > 0) {
                        std::lock_guard<std::mutex> lock(monitoring_mutex_);
                        recent_changes_.push_back(std::string(event->name));
                        if (recent_changes_.size() > 100) {
                            recent_changes_.erase(recent_changes_.begin());
                        }
                    }
                }
            }
        }
        
        inotify_rm_watch(fd, wd);
        close(fd);
        #elif __APPLE__
        // FSEvents implementation would go here
        // For now, just a placeholder
        while (!stop_monitoring_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        #else
        // Other platforms - not supported
        (void)path;
        #endif
    });
    
    return true;
}

void FilesystemManager::stopMonitoring() {
    if (!monitoring_enabled_) {
        return;
    }
    
    stop_monitoring_ = true;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(monitoring_mutex_);
    monitoring_enabled_ = false;
}

bool FilesystemManager::isMonitoring() const {
    return monitoring_enabled_;
}

std::vector<std::string> FilesystemManager::getRecentChanges() const {
    std::lock_guard<std::mutex> lock(monitoring_mutex_);
    return recent_changes_;
}

// Quota management implementation
bool FilesystemManager::getQuotaInfo(const std::string& path, uint32_t uid, QuotaInfo& quota) {
    std::lock_guard<std::mutex> lock(quota_mutex_);
    
    auto key = std::make_pair(path, uid);
    auto it = quota_map_.find(key);
    if (it != quota_map_.end()) {
        quota.used_bytes = it->second.used_bytes;
        quota.soft_limit = it->second.soft_limit;
        quota.hard_limit = it->second.hard_limit;
        quota.available_bytes = (it->second.hard_limit > it->second.used_bytes) ? 
                                (it->second.hard_limit - it->second.used_bytes) : 0;
        return true;
    }
    
    // No quota set - return unlimited
    quota.used_bytes = 0;
    quota.soft_limit = 0; // 0 = unlimited
    quota.hard_limit = 0; // 0 = unlimited
    quota.available_bytes = UINT64_MAX;
    return true;
}

bool FilesystemManager::checkQuota(const std::string& path, uint32_t uid, uint64_t requested_bytes) {
    QuotaInfo quota;
    if (!getQuotaInfo(path, uid, quota)) {
        return false;
    }
    
    // If no limits set (0 = unlimited), allow
    if (quota.hard_limit == 0) {
        return true;
    }
    
    // Check if adding requested_bytes would exceed hard limit
    return (quota.used_bytes + requested_bytes) <= quota.hard_limit;
}

bool FilesystemManager::setQuota(const std::string& path, uint32_t uid, uint64_t soft_limit, uint64_t hard_limit) {
    std::lock_guard<std::mutex> lock(quota_mutex_);
    
    auto key = std::make_pair(path, uid);
    QuotaEntry entry;
    entry.uid = uid;
    entry.soft_limit = soft_limit;
    entry.hard_limit = hard_limit;
    
    // Calculate current usage (simplified - would need actual filesystem quota support)
    entry.used_bytes = 0; // Placeholder - would query filesystem
    
    quota_map_[key] = entry;
    return true;
}

// Export hot-reload implementation
bool FilesystemManager::reloadExports() {
    // This would reload exports from the current config
    // For now, just clear the export cache
    std::lock_guard<std::mutex> lock(export_cache_mutex_);
    export_cache_.clear();
    export_cache_time_ = std::chrono::steady_clock::now();
    return true;
}

bool FilesystemManager::reloadExportConfig(const std::string& config_file) {
    // This would reload the entire config from file
    // For now, just clear caches
    reloadExports();
    clearAttributeCache();
    clearContentCache();
    return true;
}

// File locking implementation
LockManager& FilesystemManager::getLockManager() {
    return *lock_manager_;
}

const LockManager& FilesystemManager::getLockManager() const {
    return *lock_manager_;
}

} // namespace SimpleNfsd