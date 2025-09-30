/**
 * @file auth_manager.hpp
 * @brief Authentication manager for NFS server
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#ifndef SIMPLE_NFSD_AUTH_MANAGER_HPP
#define SIMPLE_NFSD_AUTH_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "simple_nfsd/rpc_protocol.hpp"

namespace SimpleNfsd {

// Forward declaration - AuthSysCredentials is defined in rpc_protocol.hpp

// Authentication result
enum class AuthResult {
    SUCCESS,
    FAILED,
    INVALID_CREDENTIALS,
    UNSUPPORTED_AUTH_TYPE
};

// Authentication context
struct AuthContext {
    bool authenticated;
    uint32_t uid;
    uint32_t gid;
    std::vector<uint32_t> gids;
    std::string machine_name;
    
    AuthContext() : authenticated(false), uid(0), gid(0) {}
};

// Authentication manager class
class AuthManager {
public:
    AuthManager();
    ~AuthManager() = default;
    
    // Initialize authentication manager
    bool initialize();
    
    // Authenticate RPC credentials
    AuthResult authenticate(const std::vector<uint8_t>& credentials, 
                           const std::vector<uint8_t>& verifier,
                           AuthContext& context);
    
    // Parse AUTH_SYS credentials
    bool parseAuthSysCredentials(const std::vector<uint8_t>& data, 
                                AuthSysCredentials& creds);
    
    // Validate AUTH_SYS credentials
    AuthResult validateAuthSysCredentials(const AuthSysCredentials& creds);
    
    // Create AUTH_SYS verifier
    std::vector<uint8_t> createAuthSysVerifier();
    
    // Check if user has access to path
    bool checkPathAccess(const std::string& path, const AuthContext& context, 
                        bool read_access, bool write_access);
    
    // Get user/group mapping
    bool getUserInfo(uint32_t uid, std::string& username);
    bool getGroupInfo(uint32_t gid, std::string& groupname);
    
    // Configuration
    void setRootSquash(bool enabled) { root_squash_ = enabled; }
    void setAllSquash(bool enabled) { all_squash_ = enabled; }
    void setAnonUid(uint32_t uid) { anon_uid_ = uid; }
    void setAnonGid(uint32_t gid) { anon_gid_ = gid; }
    
private:
    // Configuration
    bool root_squash_;
    bool all_squash_;
    uint32_t anon_uid_;
    uint32_t anon_gid_;
    
    // User/group mappings
    std::map<uint32_t, std::string> uid_to_name_;
    std::map<std::string, uint32_t> name_to_uid_;
    std::map<uint32_t, std::string> gid_to_name_;
    std::map<std::string, uint32_t> name_to_gid_;
    
    // Thread safety
    mutable std::mutex auth_mutex_;
    
    // Helper methods
    void loadUserDatabase();
    void loadGroupDatabase();
    bool isValidUid(uint32_t uid) const;
    bool isValidGid(uint32_t gid) const;
    std::string getMachineName() const;
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_AUTH_MANAGER_HPP
