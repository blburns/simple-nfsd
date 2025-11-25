#ifndef SIMPLE_NFSD_FILE_ACCESS_TRACKER_HPP
#define SIMPLE_NFSD_FILE_ACCESS_TRACKER_HPP

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <cstdint>
#include <chrono>

namespace SimpleNfsd {

// File access mode
enum class FileAccessMode {
    READ_ONLY,      // File opened for reading only
    WRITE_ONLY,     // File opened for writing only
    READ_WRITE,     // File opened for reading and writing
    APPEND          // File opened for appending
};

// File sharing mode
enum class FileSharingMode {
    EXCLUSIVE,      // Exclusive access (no other clients can access)
    SHARED_READ,    // Shared read access
    SHARED_WRITE,   // Shared write access
    SHARED_ALL      // Shared read and write access
};

// File open state
struct FileOpenState {
    std::string file_path;
    uint32_t client_id;
    uint32_t process_id;
    FileAccessMode access_mode;
    FileSharingMode sharing_mode;
    std::chrono::steady_clock::time_point opened_at;
    std::chrono::steady_clock::time_point last_accessed;
    bool is_valid;
    
    FileOpenState() : client_id(0), process_id(0), 
                     access_mode(FileAccessMode::READ_ONLY),
                     sharing_mode(FileSharingMode::SHARED_READ),
                     is_valid(false) {}
    
    FileOpenState(const std::string& path, uint32_t cid, uint32_t pid,
                  FileAccessMode amode, FileSharingMode smode)
        : file_path(path), client_id(cid), process_id(pid),
          access_mode(amode), sharing_mode(smode),
          opened_at(std::chrono::steady_clock::now()),
          last_accessed(std::chrono::steady_clock::now()),
          is_valid(true) {}
};

// File access tracker
class FileAccessTracker {
public:
    FileAccessTracker();
    ~FileAccessTracker();
    
    // File open/close tracking
    bool openFile(const std::string& file_path, uint32_t client_id, uint32_t process_id,
                  FileAccessMode access_mode, FileSharingMode sharing_mode, uint32_t& open_id);
    bool closeFile(uint32_t open_id, uint32_t client_id);
    bool closeFileForClient(uint32_t client_id);
    bool closeFileForPath(const std::string& file_path);
    
    // File access queries
    bool isFileOpen(const std::string& file_path) const;
    bool canOpenFile(const std::string& file_path, FileAccessMode access_mode, 
                     FileSharingMode sharing_mode, uint32_t client_id) const;
    std::vector<FileOpenState> getOpenFiles(const std::string& file_path) const;
    std::vector<FileOpenState> getOpenFilesForClient(uint32_t client_id) const;
    
    // Access mode management
    bool updateAccessMode(uint32_t open_id, FileAccessMode new_mode);
    bool updateSharingMode(uint32_t open_id, FileSharingMode new_mode);
    
    // Cleanup
    void cleanupExpiredOpens();
    void clearAllOpens();
    size_t getOpenFileCount() const;

private:
    mutable std::mutex opens_mutex_;
    std::map<uint32_t, FileOpenState> open_files_;
    uint32_t next_open_id_;
    
    // Helper methods
    bool checkSharingConflict(const FileOpenState& existing, FileAccessMode requested_mode,
                             FileSharingMode requested_sharing) const;
    void removeExpiredOpens();
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_FILE_ACCESS_TRACKER_HPP

