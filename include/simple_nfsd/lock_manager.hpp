#ifndef SIMPLE_NFSD_LOCK_MANAGER_HPP
#define SIMPLE_NFSD_LOCK_MANAGER_HPP

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>
#include <cstdint>

namespace SimpleNfsd {

// Lock types
enum class LockType {
    SHARED,      // Multiple readers allowed
    EXCLUSIVE    // Only one writer allowed
};

// Lock owner information
struct LockOwner {
    uint32_t client_id;
    uint32_t process_id;
    std::string client_address;
    std::chrono::steady_clock::time_point acquired_at;
    
    LockOwner() : client_id(0), process_id(0) {}
    LockOwner(uint32_t cid, uint32_t pid, const std::string& addr)
        : client_id(cid), process_id(pid), client_address(addr),
          acquired_at(std::chrono::steady_clock::now()) {}
};

// File lock structure
struct FileLock {
    std::string file_path;
    LockType type;
    uint64_t offset;        // Start offset
    uint64_t length;        // Length of lock (0 = entire file)
    LockOwner owner;
    std::chrono::steady_clock::time_point expires_at;
    bool is_valid;
    
    FileLock() : offset(0), length(0), is_valid(false) {}
    FileLock(const std::string& path, LockType t, uint64_t off, uint64_t len, const LockOwner& own)
        : file_path(path), type(t), offset(off), length(len), owner(own),
          expires_at(std::chrono::steady_clock::now() + std::chrono::hours(24)), is_valid(true) {}
    
    bool overlaps(const FileLock& other) const {
        if (file_path != other.file_path) {
            return false;
        }
        
        // Check if lock ranges overlap
        uint64_t this_end = (length == 0) ? UINT64_MAX : offset + length;
        uint64_t other_end = (other.length == 0) ? UINT64_MAX : other.offset + other.length;
        
        return !(this_end <= other.offset || offset >= other_end);
    }
    
    bool isExpired() const {
        return std::chrono::steady_clock::now() > expires_at;
    }
};

// Lock manager class
class LockManager {
public:
    LockManager();
    ~LockManager();
    
    // Lock operations
    bool acquireLock(const std::string& file_path, LockType type, uint64_t offset, 
                     uint64_t length, const LockOwner& owner, uint32_t& lock_id);
    bool releaseLock(uint32_t lock_id, const LockOwner& owner);
    bool releaseLocksForOwner(const LockOwner& owner);
    bool releaseLocksForFile(const std::string& file_path);
    
    // Lock queries
    bool hasLock(uint32_t lock_id) const;
    bool getLock(uint32_t lock_id, FileLock& lock) const;
    std::vector<FileLock> getLocksForFile(const std::string& file_path) const;
    std::vector<FileLock> getLocksForOwner(const LockOwner& owner) const;
    
    // Lock conflict detection
    bool checkLockConflict(const std::string& file_path, LockType type, 
                          uint64_t offset, uint64_t length, const LockOwner& owner) const;
    
    // Lock management
    void cleanupExpiredLocks();
    void clearAllLocks();
    size_t getLockCount() const;
    
    // NLM (Network Lock Manager) support
    struct NlmLock {
        uint32_t lock_id;
        std::string file_path;
        LockType type;
        uint64_t offset;
        uint64_t length;
        LockOwner owner;
        bool granted;
    };
    
    bool nlmTest(const std::string& file_path, LockType type, uint64_t offset, 
                 uint64_t length, const LockOwner& owner, NlmLock& conflicting_lock);
    bool nlmLock(const std::string& file_path, LockType type, uint64_t offset,
                 uint64_t length, const LockOwner& owner, uint32_t& lock_id);
    bool nlmUnlock(const std::string& file_path, uint64_t offset, uint64_t length,
                   const LockOwner& owner);
    
private:
    mutable std::mutex locks_mutex_;
    std::map<uint32_t, FileLock> locks_;
    uint32_t next_lock_id_;
    
    // Helper methods
    bool canAcquireLock(const FileLock& requested_lock, const LockOwner& owner) const;
    void removeExpiredLocks();
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_LOCK_MANAGER_HPP

