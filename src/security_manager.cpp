/**
 * @file security_manager.cpp
 * @brief Enhanced security and authentication management implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include "simple_nfsd/security_manager.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <filesystem>

namespace SimpleNfsd {

SecurityManager::SecurityManager() : initialized_(false) {
}

SecurityManager::~SecurityManager() {
    shutdown();
}

bool SecurityManager::initialize(const SecurityConfig& config) {
    if (initialized_) {
        return true;
    }

    config_ = config;
    
    // Initialize audit logging if enabled
    if (config_.enable_audit_logging && !config_.audit_log_file.empty()) {
        // Ensure audit log directory exists
        std::filesystem::path log_path(config_.audit_log_file);
        std::filesystem::create_directories(log_path.parent_path());
    }
    
    // Load default ACLs
    loadDefaultAcls();
    
    initialized_ = true;
    std::cout << "SecurityManager initialized successfully" << std::endl;
    return true;
}

void SecurityManager::shutdown() {
    if (!initialized_) {
        return;
    }

    // Save audit log
    if (config_.enable_audit_logging) {
        saveAuditLog();
    }
    
    // Clear all sessions
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        active_sessions_.clear();
    }
    
    initialized_ = false;
    std::cout << "SecurityManager shutdown" << std::endl;
}

void SecurityManager::setConfig(const SecurityConfig& config) {
    config_ = config;
}

SecurityConfig SecurityManager::getConfig() const {
    return config_;
}

bool SecurityManager::authenticateAUTH_SYS(const RpcMessage& message, SecurityContext& context) {
    try {
        // Parse AUTH_SYS credentials
        if (message.header.cred.flavor != RpcAuthFlavor::AUTH_SYS) {
            return false;
        }
        
        if (!parseAuthSysCredentials(message.header.cred.body, context)) {
            return false;
        }
        
        context.auth_flavor = RpcAuthFlavor::AUTH_SYS;
        context.authenticated = true;
        
        // Log authentication
        logAuthentication(context, true, "AUTH_SYS authentication successful");
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_authentications++;
            stats_.successful_authentications++;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "AUTH_SYS authentication error: " << e.what() << std::endl;
        logAuthentication(context, false, "AUTH_SYS authentication failed: " + std::string(e.what()));
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_authentications++;
            stats_.failed_authentications++;
        }
        
        return false;
    }
}

bool SecurityManager::authenticateAUTH_DH(const RpcMessage& message, SecurityContext& context) {
    try {
        // TODO: Implement AUTH_DH authentication
        // This would involve Diffie-Hellman key exchange
        std::cout << "AUTH_DH authentication not yet implemented" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "AUTH_DH authentication error: " << e.what() << std::endl;
        return false;
    }
}

bool SecurityManager::authenticateKerberos(const RpcMessage& message, SecurityContext& context) {
    try {
        // TODO: Implement Kerberos authentication
        // This would involve GSS-API integration
        std::cout << "Kerberos authentication not yet implemented" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Kerberos authentication error: " << e.what() << std::endl;
        return false;
    }
}

bool SecurityManager::authenticate(const RpcMessage& message, SecurityContext& context) {
    if (!initialized_) {
        return false;
    }

    // Try different authentication methods based on configuration
    if (config_.enable_auth_sys && message.header.cred.flavor == RpcAuthFlavor::AUTH_SYS) {
        return authenticateAUTH_SYS(message, context);
    }
    
    if (config_.enable_auth_dh && message.header.cred.flavor == RpcAuthFlavor::AUTH_DH) {
        return authenticateAUTH_DH(message, context);
    }
    
    if (config_.enable_kerberos && message.header.cred.flavor == RpcAuthFlavor::RPCSEC_GSS) {
        return authenticateKerberos(message, context);
    }
    
    // If no authentication is required
    if (config_.anonymous_access) {
        context.authenticated = true;
        context.auth_flavor = RpcAuthFlavor::AUTH_NONE;
        context.uid = 65534; // nobody
        context.gid = 65534; // nogroup
        context.username = "anonymous";
        return true;
    }
    
    return false;
}

bool SecurityManager::checkPathAccess(const SecurityContext& context, const std::string& path, uint32_t requested_perms) {
    if (!initialized_ || !context.authenticated) {
        return false;
    }

    // Check if path is allowed
    if (!isPathAllowed(context, path)) {
        logAuthorization(context, path, false, "Path not allowed");
        return false;
    }

    // Check file-level permissions
    return checkFileAccess(context, path, requested_perms);
}

bool SecurityManager::checkFileAccess(const SecurityContext& context, const std::string& file_path, uint32_t requested_perms) {
    if (!initialized_ || !context.authenticated) {
        return false;
    }

    // Check ACL first if available
    {
        std::lock_guard<std::mutex> lock(acl_mutex_);
        auto it = file_acls_.find(file_path);
        if (it != file_acls_.end()) {
            bool has_access = it->second.hasPermission(context.uid, context.gid, context.gids, requested_perms);
            logAuthorization(context, file_path, has_access, "ACL-based access check");
            return has_access;
        }
    }
    
    // Fall back to Unix permissions
    bool has_access = checkUnixPermissions(context, file_path, requested_perms);
    logAuthorization(context, file_path, has_access, "Unix permissions check");
    return has_access;
}

bool SecurityManager::isPathAllowed(const SecurityContext& context, const std::string& path) {
    // Basic path validation
    if (path.empty() || path.find("..") != std::string::npos) {
        return false;
    }
    
    // TODO: Implement more sophisticated path access control
    // This could include export restrictions, client IP filtering, etc.
    return true;
}

bool SecurityManager::setFileAcl(const std::string& path, const FileAcl& acl) {
    if (!initialized_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(acl_mutex_);
    file_acls_[path] = acl;
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.acl_operations++;
    }
    
    logAccess(SecurityContext(), "SET_ACL", path, true, "ACL set for file");
    return true;
}

FileAcl SecurityManager::getFileAcl(const std::string& path) const {
    std::lock_guard<std::mutex> lock(acl_mutex_);
    auto it = file_acls_.find(path);
    if (it != file_acls_.end()) {
        return it->second;
    }
    return FileAcl{};
}

bool SecurityManager::removeFileAcl(const std::string& path) {
    if (!initialized_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(acl_mutex_);
    auto it = file_acls_.find(path);
    if (it != file_acls_.end()) {
        file_acls_.erase(it);
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.acl_operations++;
        }
        
        logAccess(SecurityContext(), "REMOVE_ACL", path, true, "ACL removed for file");
        return true;
    }
    
    return false;
}

bool SecurityManager::hasAcl(const std::string& path) const {
    std::lock_guard<std::mutex> lock(acl_mutex_);
    return file_acls_.find(path) != file_acls_.end();
}

std::string SecurityManager::createSession(const SecurityContext& context) {
    if (!initialized_) {
        return "";
    }

    std::string session_id = generateSessionId();
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        active_sessions_[session_id] = context;
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.active_sessions = active_sessions_.size();
    }
    
    return session_id;
}

bool SecurityManager::validateSession(const std::string& session_id, SecurityContext& context) {
    if (!initialized_ || session_id.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = active_sessions_.find(session_id);
    if (it != active_sessions_.end()) {
        if (isSessionExpired(it->second)) {
            active_sessions_.erase(it);
            return false;
        }
        context = it->second;
        return true;
    }
    
    return false;
}

void SecurityManager::destroySession(const std::string& session_id) {
    if (!initialized_ || session_id.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    active_sessions_.erase(session_id);
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.active_sessions = active_sessions_.size();
    }
}

void SecurityManager::cleanupExpiredSessions() {
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = active_sessions_.begin();
    while (it != active_sessions_.end()) {
        if (isSessionExpired(it->second)) {
            it = active_sessions_.erase(it);
        } else {
            ++it;
        }
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.active_sessions = active_sessions_.size();
    }
}

void SecurityManager::logAuditEvent(const AuditEntry& entry) {
    if (!initialized_ || !config_.enable_audit_logging) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(audit_mutex_);
        audit_log_.push_back(entry);
        
        // Keep only last 10000 entries in memory
        if (audit_log_.size() > 10000) {
            audit_log_.erase(audit_log_.begin(), audit_log_.begin() + 1000);
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.audit_events++;
    }
    
    writeAuditLog(entry);
}

void SecurityManager::logAccess(const SecurityContext& context, const std::string& operation, 
                                const std::string& resource, bool success, const std::string& details) {
    AuditEntry entry(context.client_ip, context.username, operation, resource, success, details);
    logAuditEvent(entry);
}

void SecurityManager::logAuthentication(const SecurityContext& context, bool success, const std::string& details) {
    AuditEntry entry(context.client_ip, context.username, "AUTHENTICATION", "SYSTEM", success, details);
    logAuditEvent(entry);
}

void SecurityManager::logAuthorization(const SecurityContext& context, const std::string& resource, 
                                      bool success, const std::string& details) {
    AuditEntry entry(context.client_ip, context.username, "AUTHORIZATION", resource, success, details);
    logAuditEvent(entry);
}

bool SecurityManager::encryptData(const std::vector<uint8_t>& data, std::vector<uint8_t>& encrypted) {
    if (!initialized_ || !config_.enable_encryption) {
        return false;
    }
    
    // TODO: Implement encryption
    // This would use the configured encryption certificate and key
    std::cout << "Encryption not yet implemented" << std::endl;
    return false;
}

bool SecurityManager::decryptData(const std::vector<uint8_t>& encrypted, std::vector<uint8_t>& data) {
    if (!initialized_ || !config_.enable_encryption) {
        return false;
    }
    
    // TODO: Implement decryption
    std::cout << "Decryption not yet implemented" << std::endl;
    return false;
}

bool SecurityManager::isEncryptionEnabled() const {
    return config_.enable_encryption;
}

SecurityManager::SecurityStats SecurityManager::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void SecurityManager::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = SecurityStats{};
}

bool SecurityManager::isHealthy() const {
    return initialized_;
}

std::vector<std::string> SecurityManager::getActiveSessions() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::string> sessions;
    for (const auto& pair : active_sessions_) {
        sessions.push_back(pair.first);
    }
    return sessions;
}

bool SecurityManager::parseAuthSysCredentials(const std::vector<uint8_t>& data, SecurityContext& context) {
    // TODO: Implement proper AUTH_SYS credential parsing
    // This is a simplified version
    if (data.size() < 20) {
        return false;
    }
    
    // Parse basic AUTH_SYS structure
    // In a real implementation, this would parse the XDR-encoded AUTH_SYS data
    context.uid = 1000;  // Default user
    context.gid = 1000;  // Default group
    context.gids = {1000};
    context.username = "user";
    context.machine_name = "client";
    
    return true;
}

bool SecurityManager::parseAuthDhCredentials(const std::vector<uint8_t>& data, SecurityContext& context) {
    // TODO: Implement AUTH_DH credential parsing
    return false;
}

bool SecurityManager::parseKerberosCredentials(const std::vector<uint8_t>& data, SecurityContext& context) {
    // TODO: Implement Kerberos credential parsing
    return false;
}

bool SecurityManager::validateCredentials(const SecurityContext& context) {
    // Basic credential validation
    return context.authenticated && !context.username.empty();
}

uint32_t SecurityManager::getFilePermissions(const std::string& path) const {
    try {
        std::filesystem::path file_path(path);
        if (!std::filesystem::exists(file_path)) {
            return 0;
        }
        
        auto perms = std::filesystem::status(file_path).permissions();
        uint32_t unix_perms = 0;
        
        // Convert std::filesystem::perms to Unix permission bits
        if ((perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none) unix_perms |= 0400;
        if ((perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none) unix_perms |= 0200;
        if ((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) unix_perms |= 0100;
        if ((perms & std::filesystem::perms::group_read) != std::filesystem::perms::none) unix_perms |= 0040;
        if ((perms & std::filesystem::perms::group_write) != std::filesystem::perms::none) unix_perms |= 0020;
        if ((perms & std::filesystem::perms::group_exec) != std::filesystem::perms::none) unix_perms |= 0010;
        if ((perms & std::filesystem::perms::others_read) != std::filesystem::perms::none) unix_perms |= 0004;
        if ((perms & std::filesystem::perms::others_write) != std::filesystem::perms::none) unix_perms |= 0002;
        if ((perms & std::filesystem::perms::others_exec) != std::filesystem::perms::none) unix_perms |= 0001;
        
        return unix_perms;
    } catch (const std::exception& e) {
        std::cerr << "Error getting file permissions: " << e.what() << std::endl;
        return 0;
    }
}

bool SecurityManager::checkUnixPermissions(const SecurityContext& context, const std::string& path, uint32_t requested_perms) {
    uint32_t file_perms = getFilePermissions(path);
    if (file_perms == 0) {
        return false;
    }
    
    // Check owner permissions
    if (context.uid == 0) { // Root user
        return true;
    }
    
    // TODO: Get file owner/group and check permissions properly
    // This is a simplified version
    return (file_perms & requested_perms) == requested_perms;
}

void SecurityManager::writeAuditLog(const AuditEntry& entry) {
    if (config_.audit_log_file.empty()) {
        return;
    }
    
    try {
        std::ofstream log_file(config_.audit_log_file, std::ios::app);
        if (log_file.is_open()) {
            auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
            log_file << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                     << " [" << (entry.success ? "SUCCESS" : "FAILURE") << "] "
                     << entry.client_ip << " " << entry.username << " "
                     << entry.operation << " " << entry.resource;
            if (!entry.details.empty()) {
                log_file << " (" << entry.details << ")";
            }
            log_file << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error writing audit log: " << e.what() << std::endl;
    }
}

std::string SecurityManager::generateSessionId() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

bool SecurityManager::isSessionExpired(const SecurityContext& context) const {
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - context.auth_time).count();
    return duration > config_.session_timeout;
}

void SecurityManager::loadDefaultAcls() {
    // Load default ACLs for common directories
    // This is a placeholder for more sophisticated ACL loading
}

void SecurityManager::saveAuditLog() {
    if (config_.audit_log_file.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(audit_mutex_);
    for (const auto& entry : audit_log_) {
        writeAuditLog(entry);
    }
}

// FileAcl implementation
bool FileAcl::hasPermission(uint32_t uid, uint32_t gid, const std::vector<uint32_t>& gids, uint32_t requested_perms) const {
    // Check ACL entries in order
    for (const auto& entry : entries) {
        bool matches = false;
        
        switch (entry.type) {
            case 1: // USER
                matches = (entry.id == uid);
                break;
            case 2: // GROUP
                matches = (entry.id == gid) || 
                         (std::find(gids.begin(), gids.end(), entry.id) != gids.end());
                break;
            case 3: // OTHER
                matches = true;
                break;
            default:
                continue;
        }
        
        if (matches) {
            return (entry.permissions & requested_perms) == requested_perms;
        }
    }
    
    // Default to no access if no matching entry found
    return false;
}

void FileAcl::addEntry(const AclEntry& entry) {
    entries.push_back(entry);
}

void FileAcl::removeEntry(uint32_t type, uint32_t id) {
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [type, id](const AclEntry& entry) {
                return entry.type == type && entry.id == id;
            }),
        entries.end()
    );
}

void FileAcl::clear() {
    entries.clear();
}

} // namespace SimpleNfsd
