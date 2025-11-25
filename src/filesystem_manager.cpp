#include "simple_nfsd/filesystem_manager.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <functional>
#include <fcntl.h>

namespace SimpleNfsd {

FilesystemManager::FilesystemManager() : next_handle_id_(1), initialized_(false) {}

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

std::string FilesystemManager::sanitizePath(const std::string& path) {
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

} // namespace SimpleNfsd