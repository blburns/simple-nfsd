/**
 * @file security_manager.hpp
 * @brief Enhanced security and authentication management
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#ifndef SIMPLE_NFSD_SECURITY_MANAGER_HPP
#define SIMPLE_NFSD_SECURITY_MANAGER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include "simple-nfsd/core/rpc_protocol.hpp"

namespace SimpleNfsd {

// Enhanced authentication context
struct SecurityContext {
    bool authenticated = false;
    uint32_t uid = 0;
    uint32_t gid = 0;
    std::vector<uint32_t> gids;
    std::string username;
    std::string machine_name;
    std::string client_ip;
    RpcAuthFlavor auth_flavor = RpcAuthFlavor::AUTH_NONE;
    std::string session_id;
    std::chrono::time_point<std::chrono::system_clock> auth_time;
    std::map<std::string, std::string> attributes;
    
    SecurityContext() : auth_time(std::chrono::system_clock::now()) {}
};

// ACL entry
struct AclEntry {
    uint32_t type;        // ACL entry type (user, group, other, etc.)
    uint32_t id;          // User/group ID
    uint32_t permissions; // Permission bits
    std::string name;     // User/group name (optional)
    
    AclEntry(uint32_t t, uint32_t i, uint32_t p, const std::string& n = "")
        : type(t), id(i), permissions(p), name(n) {}
};

// File ACL
struct FileAcl {
    std::vector<AclEntry> entries;
    uint32_t default_mask = 0;
    bool is_directory = false;
    
    bool hasPermission(uint32_t uid, uint32_t gid, const std::vector<uint32_t>& gids, uint32_t requested_perms) const;
    void addEntry(const AclEntry& entry);
    void removeEntry(uint32_t type, uint32_t id);
    void clear();
};

// Security configuration
struct SecurityConfig {
    bool enable_auth_sys = true;
    bool enable_auth_dh = false;
    bool enable_kerberos = false;
    bool enable_acl = true;
    bool enable_encryption = false;
    bool enable_audit_logging = true;
    bool root_squash = true;
    bool anonymous_access = false;
    uint32_t session_timeout = 3600; // 1 hour
    std::string kerberos_realm;
    std::string kerberos_keytab;
    std::string encryption_cert;
    std::string encryption_key;
    std::string audit_log_file;
};

// Audit log entry
struct AuditEntry {
    std::chrono::time_point<std::chrono::system_clock> timestamp;
    std::string client_ip;
    std::string username;
    std::string operation;
    std::string resource;
    bool success;
    std::string details;
    
    AuditEntry(const std::string& ip, const std::string& user, const std::string& op, 
               const std::string& res, bool succ, const std::string& det = "")
        : timestamp(std::chrono::system_clock::now()), client_ip(ip), username(user),
          operation(op), resource(res), success(succ), details(det) {}
};

class SecurityManager {
public:
    SecurityManager();
    ~SecurityManager();

    // Initialization and configuration
    bool initialize(const SecurityConfig& config);
    void shutdown();
    void setConfig(const SecurityConfig& config);
    SecurityConfig getConfig() const;

    // Authentication methods
    bool authenticateAUTH_SYS(const RpcMessage& message, SecurityContext& context);
    bool authenticateAUTH_DH(const RpcMessage& message, SecurityContext& context);
    bool authenticateKerberos(const RpcMessage& message, SecurityContext& context);
    bool authenticate(const RpcMessage& message, SecurityContext& context);

    // Authorization and access control
    bool checkPathAccess(const SecurityContext& context, const std::string& path, uint32_t requested_perms);
    bool checkFileAccess(const SecurityContext& context, const std::string& file_path, uint32_t requested_perms);
    bool isPathAllowed(const SecurityContext& context, const std::string& path);

    // ACL management
    bool setFileAcl(const std::string& path, const FileAcl& acl);
    FileAcl getFileAcl(const std::string& path) const;
    bool removeFileAcl(const std::string& path);
    bool hasAcl(const std::string& path) const;

    // Session management
    std::string createSession(const SecurityContext& context);
    bool validateSession(const std::string& session_id, SecurityContext& context);
    void destroySession(const std::string& session_id);
    void cleanupExpiredSessions();

    // Audit logging
    void logAuditEvent(const AuditEntry& entry);
    void logAccess(const SecurityContext& context, const std::string& operation, 
                   const std::string& resource, bool success, const std::string& details = "");
    void logAuthentication(const SecurityContext& context, bool success, const std::string& details = "");
    void logAuthorization(const SecurityContext& context, const std::string& resource, 
                         bool success, const std::string& details = "");

    // Encryption support
    bool encryptData(const std::vector<uint8_t>& data, std::vector<uint8_t>& encrypted);
    bool decryptData(const std::vector<uint8_t>& encrypted, std::vector<uint8_t>& data);
    bool isEncryptionEnabled() const;

    // Statistics and monitoring
    struct SecurityStats {
        uint64_t total_authentications = 0;
        uint64_t successful_authentications = 0;
        uint64_t failed_authentications = 0;
        uint64_t total_authorizations = 0;
        uint64_t successful_authorizations = 0;
        uint64_t failed_authorizations = 0;
        uint64_t active_sessions = 0;
        uint64_t acl_operations = 0;
        uint64_t audit_events = 0;
    };
    
    SecurityStats getStats() const;
    void resetStats();

    // Health and status
    bool isHealthy() const;
    std::vector<std::string> getActiveSessions() const;

private:
    // Internal state
    bool initialized_;
    SecurityConfig config_;
    mutable std::mutex sessions_mutex_;
    std::map<std::string, SecurityContext> active_sessions_;
    mutable std::mutex acl_mutex_;
    std::map<std::string, FileAcl> file_acls_;
    mutable std::mutex audit_mutex_;
    std::vector<AuditEntry> audit_log_;
    mutable std::mutex stats_mutex_;
    SecurityStats stats_;

    // Helper methods
    bool parseAuthSysCredentials(const std::vector<uint8_t>& data, SecurityContext& context);
    bool parseAuthDhCredentials(const std::vector<uint8_t>& data, SecurityContext& context);
    bool parseKerberosCredentials(const std::vector<uint8_t>& data, SecurityContext& context);
    bool validateCredentials(const SecurityContext& context);
    uint32_t getFilePermissions(const std::string& path) const;
    bool checkUnixPermissions(const SecurityContext& context, const std::string& path, uint32_t requested_perms);
    void writeAuditLog(const AuditEntry& entry);
    std::string generateSessionId() const;
    bool isSessionExpired(const SecurityContext& context) const;
    void loadDefaultAcls();
    void saveAuditLog();
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_SECURITY_MANAGER_HPP
