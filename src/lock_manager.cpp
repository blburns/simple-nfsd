#include "simple_nfsd/lock_manager.hpp"
#include <iostream>
#include <algorithm>

namespace SimpleNfsd {

LockManager::LockManager() : next_lock_id_(1) {}

LockManager::~LockManager() {
    clearAllLocks();
}

bool LockManager::acquireLock(const std::string& file_path, LockType type, uint64_t offset,
                               uint64_t length, const LockOwner& owner, uint32_t& lock_id) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    // Clean up expired locks first
    removeExpiredLocks();
    
    // Create requested lock
    FileLock requested_lock(file_path, type, offset, length, owner);
    
    // Check for conflicts
    if (!canAcquireLock(requested_lock, owner)) {
        return false;
    }
    
    // Assign lock ID and store
    lock_id = next_lock_id_++;
    locks_[lock_id] = requested_lock;
    
    return true;
}

bool LockManager::releaseLock(uint32_t lock_id, const LockOwner& owner) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    auto it = locks_.find(lock_id);
    if (it != locks_.end()) {
        // Verify ownership
        if (it->second.owner.client_id == owner.client_id &&
            it->second.owner.process_id == owner.process_id) {
            locks_.erase(it);
            return true;
        }
    }
    
    return false;
}

bool LockManager::releaseLocksForOwner(const LockOwner& owner) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    bool released = false;
    auto it = locks_.begin();
    while (it != locks_.end()) {
        if (it->second.owner.client_id == owner.client_id &&
            it->second.owner.process_id == owner.process_id) {
            it = locks_.erase(it);
            released = true;
        } else {
            ++it;
        }
    }
    
    return released;
}

bool LockManager::releaseLocksForFile(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    bool released = false;
    auto it = locks_.begin();
    while (it != locks_.end()) {
        if (it->second.file_path == file_path) {
            it = locks_.erase(it);
            released = true;
        } else {
            ++it;
        }
    }
    
    return released;
}

bool LockManager::hasLock(uint32_t lock_id) const {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    return locks_.find(lock_id) != locks_.end();
}

bool LockManager::getLock(uint32_t lock_id, FileLock& file_lock) const {
    std::lock_guard<std::mutex> lock_guard(locks_mutex_);
    
    auto it = locks_.find(lock_id);
    if (it != locks_.end() && it->second.is_valid && !it->second.isExpired()) {
        file_lock = it->second;
        return true;
    }
    
    return false;
}

std::vector<FileLock> LockManager::getLocksForFile(const std::string& file_path) const {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    std::vector<FileLock> result;
    for (const auto& pair : locks_) {
        if (pair.second.file_path == file_path && pair.second.is_valid && !pair.second.isExpired()) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<FileLock> LockManager::getLocksForOwner(const LockOwner& owner) const {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    std::vector<FileLock> result;
    for (const auto& pair : locks_) {
        if (pair.second.owner.client_id == owner.client_id &&
            pair.second.owner.process_id == owner.process_id &&
            pair.second.is_valid && !pair.second.isExpired()) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

bool LockManager::checkLockConflict(const std::string& file_path, LockType type,
                                    uint64_t offset, uint64_t length, const LockOwner& owner) const {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    FileLock requested_lock(file_path, type, offset, length, owner);
    
    for (const auto& pair : locks_) {
        const FileLock& existing = pair.second;
        
        // Skip expired or invalid locks
        if (!existing.is_valid || existing.isExpired()) {
            continue;
        }
        
        // Skip locks from the same owner
        if (existing.owner.client_id == owner.client_id &&
            existing.owner.process_id == owner.process_id) {
            continue;
        }
        
        // Check for overlap
        if (requested_lock.overlaps(existing)) {
            // Conflict rules:
            // - Exclusive locks conflict with everything
            // - Shared locks conflict with exclusive locks
            if (type == LockType::EXCLUSIVE || existing.type == LockType::EXCLUSIVE) {
                return true; // Conflict found
            }
        }
    }
    
    return false; // No conflict
}

void LockManager::cleanupExpiredLocks() {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    removeExpiredLocks();
}

void LockManager::clearAllLocks() {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    locks_.clear();
}

size_t LockManager::getLockCount() const {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    return locks_.size();
}

bool LockManager::nlmTest(const std::string& file_path, LockType type, uint64_t offset,
                          uint64_t length, const LockOwner& owner, NlmLock& conflicting_lock) {
    std::lock_guard<std::mutex> lock(locks_mutex_);
    
    removeExpiredLocks();
    
    FileLock requested_lock(file_path, type, offset, length, owner);
    
    for (const auto& pair : locks_) {
        const FileLock& existing = pair.second;
        
        if (!existing.is_valid || existing.isExpired()) {
            continue;
        }
        
        if (existing.owner.client_id == owner.client_id &&
            existing.owner.process_id == owner.process_id) {
            continue; // Same owner
        }
        
        if (requested_lock.overlaps(existing)) {
            if (type == LockType::EXCLUSIVE || existing.type == LockType::EXCLUSIVE) {
                // Conflict found
                conflicting_lock.lock_id = pair.first;
                conflicting_lock.file_path = existing.file_path;
                conflicting_lock.type = existing.type;
                conflicting_lock.offset = existing.offset;
                conflicting_lock.length = existing.length;
                conflicting_lock.owner = existing.owner;
                conflicting_lock.granted = false;
                return true; // Conflict exists
            }
        }
    }
    
    conflicting_lock.granted = true;
    return false; // No conflict
}

bool LockManager::nlmLock(const std::string& file_path, LockType type, uint64_t offset,
                          uint64_t length, const LockOwner& owner, uint32_t& lock_id) {
    return acquireLock(file_path, type, offset, length, owner, lock_id);
}

bool LockManager::nlmUnlock(const std::string& file_path, uint64_t offset, uint64_t length,
                            const LockOwner& owner) {
    std::lock_guard<std::mutex> lock_guard(locks_mutex_);
    
    bool released = false;
    auto it = locks_.begin();
    while (it != locks_.end()) {
        const FileLock& file_lock = it->second;
        
        if (file_lock.file_path == file_path &&
            file_lock.owner.client_id == owner.client_id &&
            file_lock.owner.process_id == owner.process_id) {
            
            // Check if offset/length matches (0 means entire file)
            bool matches = false;
            if (offset == 0 && length == 0) {
                matches = true; // Unlock entire file
            } else {
                // Create a temporary lock to check overlap
                FileLock unlock_range(file_path, LockType::SHARED, offset, length, owner);
                if (file_lock.overlaps(unlock_range)) {
                    matches = true;
                }
            }
            
            if (matches) {
                it = locks_.erase(it);
                released = true;
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
    
    return released;
}

bool LockManager::canAcquireLock(const FileLock& requested_lock, const LockOwner& owner) const {
    for (const auto& pair : locks_) {
        const FileLock& existing = pair.second;
        
        // Skip expired or invalid locks
        if (!existing.is_valid || existing.isExpired()) {
            continue;
        }
        
        // Same owner can acquire overlapping locks (upgrade/downgrade)
        if (existing.owner.client_id == owner.client_id &&
            existing.owner.process_id == owner.process_id) {
            continue;
        }
        
        // Check for overlap
        if (requested_lock.overlaps(existing)) {
            // Conflict rules:
            // - Exclusive locks conflict with everything
            // - Shared locks conflict with exclusive locks
            if (requested_lock.type == LockType::EXCLUSIVE || existing.type == LockType::EXCLUSIVE) {
                return false; // Conflict
            }
        }
    }
    
    return true; // No conflict
}

void LockManager::removeExpiredLocks() {
    auto it = locks_.begin();
    while (it != locks_.end()) {
        if (it->second.isExpired() || !it->second.is_valid) {
            it = locks_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace SimpleNfsd

