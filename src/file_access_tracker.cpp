#include "simple_nfsd/file_access_tracker.hpp"
#include <algorithm>

namespace SimpleNfsd {

FileAccessTracker::FileAccessTracker() : next_open_id_(1) {}

FileAccessTracker::~FileAccessTracker() {
    clearAllOpens();
}

bool FileAccessTracker::openFile(const std::string& file_path, uint32_t client_id, uint32_t process_id,
                                 FileAccessMode access_mode, FileSharingMode sharing_mode, uint32_t& open_id) {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    // Clean up expired opens first
    removeExpiredOpens();
    
    // Check if file can be opened with requested modes
    if (!canOpenFile(file_path, access_mode, sharing_mode, client_id)) {
        return false;
    }
    
    // Create new open state
    open_id = next_open_id_++;
    FileOpenState open_state(file_path, client_id, process_id, access_mode, sharing_mode);
    open_files_[open_id] = open_state;
    
    return true;
}

bool FileAccessTracker::closeFile(uint32_t open_id, uint32_t client_id) {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    auto it = open_files_.find(open_id);
    if (it != open_files_.end()) {
        if (it->second.client_id == client_id) {
            open_files_.erase(it);
            return true;
        }
    }
    
    return false;
}

bool FileAccessTracker::closeFileForClient(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    bool closed = false;
    auto it = open_files_.begin();
    while (it != open_files_.end()) {
        if (it->second.client_id == client_id) {
            it = open_files_.erase(it);
            closed = true;
        } else {
            ++it;
        }
    }
    
    return closed;
}

bool FileAccessTracker::closeFileForPath(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    bool closed = false;
    auto it = open_files_.begin();
    while (it != open_files_.end()) {
        if (it->second.file_path == file_path) {
            it = open_files_.erase(it);
            closed = true;
        } else {
            ++it;
        }
    }
    
    return closed;
}

bool FileAccessTracker::isFileOpen(const std::string& file_path) const {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    for (const auto& pair : open_files_) {
        if (pair.second.file_path == file_path && pair.second.is_valid) {
            return true;
        }
    }
    
    return false;
}

bool FileAccessTracker::canOpenFile(const std::string& file_path, FileAccessMode access_mode,
                                    FileSharingMode sharing_mode, uint32_t client_id) const {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    // Check all existing opens for this file
    for (const auto& pair : open_files_) {
        const FileOpenState& existing = pair.second;
        
        // Skip expired or invalid opens
        if (!existing.is_valid) {
            continue;
        }
        
        // Same client can always open (for upgrade/downgrade)
        if (existing.client_id == client_id && existing.file_path == file_path) {
            continue;
        }
        
        // Check for conflicts
        if (existing.file_path == file_path) {
            if (checkSharingConflict(existing, access_mode, sharing_mode)) {
                return false; // Conflict found
            }
        }
    }
    
    return true; // No conflicts
}

std::vector<FileOpenState> FileAccessTracker::getOpenFiles(const std::string& file_path) const {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    std::vector<FileOpenState> result;
    for (const auto& pair : open_files_) {
        if (pair.second.file_path == file_path && pair.second.is_valid) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<FileOpenState> FileAccessTracker::getOpenFilesForClient(uint32_t client_id) const {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    std::vector<FileOpenState> result;
    for (const auto& pair : open_files_) {
        if (pair.second.client_id == client_id && pair.second.is_valid) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

bool FileAccessTracker::updateAccessMode(uint32_t open_id, FileAccessMode new_mode) {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    auto it = open_files_.find(open_id);
    if (it != open_files_.end() && it->second.is_valid) {
        it->second.access_mode = new_mode;
        it->second.last_accessed = std::chrono::steady_clock::now();
        return true;
    }
    
    return false;
}

bool FileAccessTracker::updateSharingMode(uint32_t open_id, FileSharingMode new_mode) {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    
    auto it = open_files_.find(open_id);
    if (it != open_files_.end() && it->second.is_valid) {
        it->second.sharing_mode = new_mode;
        it->second.last_accessed = std::chrono::steady_clock::now();
        return true;
    }
    
    return false;
}

void FileAccessTracker::cleanupExpiredOpens() {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    removeExpiredOpens();
}

void FileAccessTracker::clearAllOpens() {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    open_files_.clear();
}

size_t FileAccessTracker::getOpenFileCount() const {
    std::lock_guard<std::mutex> lock(opens_mutex_);
    return open_files_.size();
}

bool FileAccessTracker::checkSharingConflict(const FileOpenState& existing, FileAccessMode requested_mode,
                                             FileSharingMode requested_sharing) const {
    // Exclusive sharing mode conflicts with everything
    if (existing.sharing_mode == FileSharingMode::EXCLUSIVE ||
        requested_sharing == FileSharingMode::EXCLUSIVE) {
        return true; // Conflict
    }
    
    // Write access conflicts with other write access
    if ((existing.access_mode == FileAccessMode::WRITE_ONLY ||
         existing.access_mode == FileAccessMode::READ_WRITE ||
         existing.access_mode == FileAccessMode::APPEND) &&
        (requested_mode == FileAccessMode::WRITE_ONLY ||
         requested_mode == FileAccessMode::READ_WRITE ||
         requested_mode == FileAccessMode::APPEND)) {
        // Check if sharing allows it
        if (existing.sharing_mode != FileSharingMode::SHARED_WRITE &&
            existing.sharing_mode != FileSharingMode::SHARED_ALL &&
            requested_sharing != FileSharingMode::SHARED_WRITE &&
            requested_sharing != FileSharingMode::SHARED_ALL) {
            return true; // Conflict
        }
    }
    
    return false; // No conflict
}

void FileAccessTracker::removeExpiredOpens() {
    // Remove opens older than 1 hour (timeout)
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::hours(1);
    
    auto it = open_files_.begin();
    while (it != open_files_.end()) {
        auto elapsed = now - it->second.last_accessed;
        if (elapsed > timeout || !it->second.is_valid) {
            it = open_files_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace SimpleNfsd

