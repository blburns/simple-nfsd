#include "simple-nfsd/core/vfs_interface.hpp"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/xattr.h>
#include <iostream>

namespace SimpleNfsd {

PosixVfs::PosixVfs(const std::string& root_path) : root_path_(root_path) {
    // Ensure root path exists
    std::filesystem::create_directories(root_path_);
}

std::string PosixVfs::resolvePath(const std::string& path) const {
    if (path.empty() || path == "/") {
        return root_path_;
    }
    
    // Remove leading slash if present
    std::string clean_path = path;
    if (clean_path[0] == '/') {
        clean_path = clean_path.substr(1);
    }
    
    return (std::filesystem::path(root_path_) / clean_path).string();
}

bool PosixVfs::fileExists(const std::string& path) {
    return std::filesystem::exists(resolvePath(path));
}

bool PosixVfs::isDirectory(const std::string& path) {
    return std::filesystem::is_directory(resolvePath(path));
}

bool PosixVfs::isFile(const std::string& path) {
    return std::filesystem::is_regular_file(resolvePath(path));
}

bool PosixVfs::isSymlink(const std::string& path) {
    return std::filesystem::is_symlink(resolvePath(path));
}

bool PosixVfs::readFile(const std::string& path, uint64_t offset, uint32_t count, 
                        std::vector<uint8_t>& data) {
    std::string full_path = resolvePath(path);
    std::ifstream file(full_path, std::ios::binary);
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
}

bool PosixVfs::writeFile(const std::string& path, uint64_t offset, 
                         const std::vector<uint8_t>& data) {
    std::string full_path = resolvePath(path);
    std::ofstream file(full_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        file.open(full_path, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            return false;
        }
    }
    
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

bool PosixVfs::truncateFile(const std::string& path, uint64_t size) {
    try {
        std::filesystem::resize_file(resolvePath(path), size);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::createFile(const std::string& path) {
    std::ofstream file(resolvePath(path));
    return file.is_open();
}

bool PosixVfs::removeFile(const std::string& path) {
    try {
        return std::filesystem::remove(resolvePath(path));
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::renameFile(const std::string& old_path, const std::string& new_path) {
    try {
        std::filesystem::rename(resolvePath(old_path), resolvePath(new_path));
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::createDirectory(const std::string& path) {
    try {
        return std::filesystem::create_directories(resolvePath(path));
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::removeDirectory(const std::string& path) {
    try {
        return std::filesystem::remove(resolvePath(path));
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::readDirectory(const std::string& path, std::vector<VfsDirectoryEntry>& entries) {
    entries.clear();
    std::string full_path = resolvePath(path);
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
            VfsDirectoryEntry dir_entry;
            dir_entry.name = entry.path().filename().string();
            
            if (getAttributes(entry.path().string(), dir_entry.attrs)) {
                entries.push_back(dir_entry);
            }
        }
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::createSymlink(const std::string& target, const std::string& link_path) {
    try {
        std::filesystem::create_symlink(target, resolvePath(link_path));
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::readSymlink(const std::string& path, std::string& target) {
    try {
        target = std::filesystem::read_symlink(resolvePath(path)).string();
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::getAttributes(const std::string& path, VfsFileAttributes& attrs) {
    std::string full_path = resolvePath(path);
    
    if (!std::filesystem::exists(full_path)) {
        return false;
    }
    
    try {
        auto status = std::filesystem::status(full_path);
        auto perms = status.permissions();
        
        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) {
            return false;
        }
        
        attrs.mode = static_cast<uint32_t>(perms);
        attrs.uid = static_cast<uint32_t>(st.st_uid);
        attrs.gid = static_cast<uint32_t>(st.st_gid);
        attrs.size = static_cast<uint64_t>(st.st_size);
        attrs.atime = static_cast<uint64_t>(st.st_atime);
        attrs.mtime = static_cast<uint64_t>(st.st_mtime);
        attrs.ctime = static_cast<uint64_t>(st.st_ctime);
        attrs.is_directory = std::filesystem::is_directory(full_path);
        attrs.is_file = std::filesystem::is_regular_file(full_path);
        attrs.is_symlink = std::filesystem::is_symlink(full_path);
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::setAttributes(const std::string& path, const VfsFileAttributes& attrs) {
    std::string full_path = resolvePath(path);
    
    try {
        // Set permissions
        if (attrs.mode != 0) {
            std::filesystem::permissions(full_path, static_cast<std::filesystem::perms>(attrs.mode));
        }
        
        // Set ownership
        if (attrs.uid != 0 || attrs.gid != 0) {
            if (chown(full_path.c_str(), attrs.uid, attrs.gid) != 0) {
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
            
            if (utimensat(0, full_path.c_str(), times, 0) != 0) {
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool PosixVfs::getExtendedAttribute(const std::string& path, const std::string& name, 
                                    std::vector<uint8_t>& value) {
    std::string full_path = resolvePath(path);
    
    #ifdef __APPLE__
    ssize_t size = getxattr(full_path.c_str(), name.c_str(), nullptr, 0, 0, 0);
    if (size < 0) {
        return false;
    }
    value.resize(size);
    ssize_t result = getxattr(full_path.c_str(), name.c_str(), value.data(), size, 0, 0);
    return result == size;
    #elif __linux__
    ssize_t size = getxattr(full_path.c_str(), name.c_str(), nullptr, 0);
    if (size < 0) {
        return false;
    }
    value.resize(size);
    ssize_t result = getxattr(full_path.c_str(), name.c_str(), value.data(), size);
    return result == size;
    #else
    (void)path; (void)name; (void)value;
    return false;
    #endif
}

bool PosixVfs::setExtendedAttribute(const std::string& path, const std::string& name, 
                                    const std::vector<uint8_t>& value) {
    std::string full_path = resolvePath(path);
    
    #ifdef __APPLE__
    int result = setxattr(full_path.c_str(), name.c_str(), value.data(), value.size(), 0, 0);
    return result == 0;
    #elif __linux__
    int result = setxattr(full_path.c_str(), name.c_str(), value.data(), value.size(), 0);
    return result == 0;
    #else
    (void)path; (void)name; (void)value;
    return false;
    #endif
}

bool PosixVfs::getFileSystemStats(const std::string& path, uint64_t& total_blocks, 
                                  uint64_t& free_blocks, uint64_t& available_blocks) {
    std::string full_path = resolvePath(path);
    
    try {
        auto space = std::filesystem::space(full_path);
        total_blocks = space.capacity / 512; // Convert to 512-byte blocks
        free_blocks = space.free / 512;
        available_blocks = space.available / 512;
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

} // namespace SimpleNfsd

