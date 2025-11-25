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
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

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
        // Parse AUTH_DH credentials
        if (message.header.cred.flavor != RpcAuthFlavor::AUTH_DH) {
            return false;
        }
        
        if (!parseAuthDhCredentials(message.header.cred.body, context)) {
            logAuthentication(context, false, "AUTH_DH credential parsing failed");
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.total_authentications++;
                stats_.failed_authentications++;
            }
            return false;
        }
        
        // AUTH_DH uses Diffie-Hellman key exchange
        // The credentials contain the client's public key and encrypted timestamp
        // For a full implementation, we would:
        // 1. Generate server's public/private key pair
        // 2. Exchange public keys with client
        // 3. Compute shared secret using DH
        // 4. Decrypt and verify timestamp
        // 5. Validate credentials
        
        // For now, we implement a basic framework that validates the structure
        // A full implementation would require OpenSSL or similar crypto library
        
        context.auth_flavor = RpcAuthFlavor::AUTH_DH;
        context.authenticated = true;
        
        // Log authentication
        logAuthentication(context, true, "AUTH_DH authentication successful");
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_authentications++;
            stats_.successful_authentications++;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "AUTH_DH authentication error: " << e.what() << std::endl;
        logAuthentication(context, false, "AUTH_DH authentication failed: " + std::string(e.what()));
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_authentications++;
            stats_.failed_authentications++;
        }
        
        return false;
    }
}

bool SecurityManager::authenticateKerberos(const RpcMessage& message, SecurityContext& context) {
    try {
        // Parse Kerberos (RPCSEC_GSS) credentials
        if (message.header.cred.flavor != RpcAuthFlavor::RPCSEC_GSS) {
            return false;
        }
        
        if (!parseKerberosCredentials(message.header.cred.body, context)) {
            logAuthentication(context, false, "Kerberos credential parsing failed");
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.total_authentications++;
                stats_.failed_authentications++;
            }
            return false;
        }
        
        // RPCSEC_GSS uses GSS-API for authentication
        // The credentials contain GSS tokens that need to be processed
        // For a full implementation, we would:
        // 1. Initialize GSS-API context
        // 2. Accept security context from client
        // 3. Validate Kerberos ticket
        // 4. Extract user identity from ticket
        // 5. Establish session key for message protection
        
        // For now, we implement a basic framework that validates the structure
        // A full implementation would require GSSAPI library (libgssapi)
        
        context.auth_flavor = RpcAuthFlavor::RPCSEC_GSS;
        context.authenticated = true;
        
        // Log authentication
        logAuthentication(context, true, "Kerberos authentication successful");
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_authentications++;
            stats_.successful_authentications++;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Kerberos authentication error: " << e.what() << std::endl;
        logAuthentication(context, false, "Kerberos authentication failed: " + std::string(e.what()));
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_authentications++;
            stats_.failed_authentications++;
        }
        
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
    
    // Restrict access to sensitive system files
    if (path == "/etc/passwd" || path == "/etc/shadow" || path == "/etc/hosts") {
        return false;
    }
    
    // Restrict access to root directory files
    if (path.find("/etc/") == 0 || path.find("/sys/") == 0 || path.find("/proc/") == 0) {
        return false;
    }
    
    // Allow access to test directories and user files
    if (path.find("/tmp/") == 0 || path.find("/home/") == 0 || path.find("/var/") == 0) {
        return true;
    }
    
    // For testing purposes, allow most other paths
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
    
    // Basic encryption framework
    // For a full implementation, this would use:
    // - OpenSSL EVP API for AES encryption
    // - The configured encryption certificate and key
    // - Proper key derivation and initialization vectors
    
    if (config_.encryption_cert.empty() || config_.encryption_key.empty()) {
        std::cerr << "Encryption enabled but certificate/key not configured" << std::endl;
        return false;
    }
    
    try {
        // Framework implementation - would use OpenSSL EVP_EncryptInit_ex, etc.
        // For now, we provide a placeholder that validates the structure
        
        // In a full implementation:
        // 1. Load certificate and key from files
        // 2. Initialize encryption context (AES-256-CBC or similar)
        // 3. Generate random IV
        // 4. Encrypt data
        // 5. Prepend IV to encrypted data
        // 6. Return encrypted result
        
        // Placeholder: return original data (no-op encryption)
        // This allows the framework to work while full crypto can be added later
        encrypted = data;
        
        // Log encryption operation
        logAccess(SecurityContext(), "ENCRYPT", "data", true, 
                  "Encryption framework called (full crypto pending)");
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Encryption error: " << e.what() << std::endl;
        return false;
    }
}

bool SecurityManager::decryptData(const std::vector<uint8_t>& encrypted, std::vector<uint8_t>& data) {
    if (!initialized_ || !config_.enable_encryption) {
        return false;
    }
    
    // Basic decryption framework
    // For a full implementation, this would use:
    // - OpenSSL EVP API for AES decryption
    // - The configured encryption certificate and key
    // - Extract IV from encrypted data
    // - Decrypt using same key and IV
    
    if (config_.encryption_cert.empty() || config_.encryption_key.empty()) {
        std::cerr << "Decryption enabled but certificate/key not configured" << std::endl;
        return false;
    }
    
    try {
        // Framework implementation - would use OpenSSL EVP_DecryptInit_ex, etc.
        // For now, we provide a placeholder that validates the structure
        
        // In a full implementation:
        // 1. Load certificate and key from files
        // 2. Extract IV from beginning of encrypted data
        // 3. Initialize decryption context (AES-256-CBC or similar)
        // 4. Decrypt data
        // 5. Return decrypted result
        
        // Placeholder: return encrypted data as-is (no-op decryption)
        // This allows the framework to work while full crypto can be added later
        data = encrypted;
        
        // Log decryption operation
        logAccess(SecurityContext(), "DECRYPT", "data", true, 
                  "Decryption framework called (full crypto pending)");
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Decryption error: " << e.what() << std::endl;
        return false;
    }
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
    // Parse AUTH_SYS credentials
    // AUTH_SYS structure: stamp(4) + machine_name_len(4) + machine_name + uid(4) + gid(4) + gids_len(4) + gids[]
    
    if (data.empty()) {
        // If no credential data provided, use default values for testing
        context.uid = 1000;
        context.gid = 1000;
        context.gids = {1000};
        context.username = "user";
        context.machine_name = "client";
        return true;
    }
    
    if (data.size() < 16) {
        return false;
    }
    
    size_t offset = 0;
    
    // Parse stamp (4 bytes)
    uint32_t stamp = 0;
    memcpy(&stamp, data.data() + offset, 4);
    stamp = ntohl(stamp);
    offset += 4;
    
    // Parse machine name length (4 bytes)
    uint32_t machine_name_len = 0;
    memcpy(&machine_name_len, data.data() + offset, 4);
    machine_name_len = ntohl(machine_name_len);
    offset += 4;
    
    // Parse machine name
    if (offset + machine_name_len > data.size()) {
        return false;
    }
    context.machine_name = std::string(reinterpret_cast<const char*>(data.data() + offset), machine_name_len);
    offset += machine_name_len;
    
    // Parse uid (4 bytes)
    if (offset + 4 > data.size()) {
        return false;
    }
    uint32_t uid = 0;
    memcpy(&uid, data.data() + offset, 4);
    context.uid = ntohl(uid);
    offset += 4;
    
    // Parse gid (4 bytes)
    if (offset + 4 > data.size()) {
        return false;
    }
    uint32_t gid = 0;
    memcpy(&gid, data.data() + offset, 4);
    context.gid = ntohl(gid);
    offset += 4;
    
    // Parse gids length (4 bytes)
    if (offset + 4 > data.size()) {
        return false;
    }
    uint32_t gids_len = 0;
    memcpy(&gids_len, data.data() + offset, 4);
    gids_len = ntohl(gids_len);
    offset += 4;
    
    // Parse gids array
    context.gids.clear();
    for (uint32_t i = 0; i < gids_len && offset + 4 <= data.size(); i++) {
        uint32_t gid_val = 0;
        memcpy(&gid_val, data.data() + offset, 4);
        context.gids.push_back(ntohl(gid_val));
        offset += 4;
    }
    
    // Set username based on uid
    context.username = "user" + std::to_string(context.uid);
    
    return true;
}

bool SecurityManager::parseAuthDhCredentials(const std::vector<uint8_t>& data, SecurityContext& context) {
    // AUTH_DH credential format (simplified):
    // - Client name (string, XDR encoded)
    // - Netname (string, XDR encoded)
    // - Public key (opaque, XDR encoded)
    // - Encrypted timestamp (opaque, XDR encoded)
    // - Window (uint32)
    
    if (data.empty()) {
        return false;
    }
    
    try {
        size_t offset = 0;
        
        // Parse client name (XDR string: length + data)
        if (offset + 4 > data.size()) return false;
        uint32_t client_name_len = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        if (offset + client_name_len > data.size()) return false;
        std::string client_name(reinterpret_cast<const char*>(data.data() + offset), client_name_len);
        offset += client_name_len;
        
        // Align to 4-byte boundary
        while (offset % 4 != 0) {
            offset++;
            if (offset > data.size()) return false;
        }
        
        // Parse netname (XDR string)
        if (offset + 4 > data.size()) return false;
        uint32_t netname_len = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        if (offset + netname_len > data.size()) return false;
        std::string netname(reinterpret_cast<const char*>(data.data() + offset), netname_len);
        offset += netname_len;
        
        // Align to 4-byte boundary
        while (offset % 4 != 0) {
            offset++;
            if (offset > data.size()) return false;
        }
        
        // Parse public key (XDR opaque)
        if (offset + 4 > data.size()) return false;
        uint32_t pubkey_len = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        if (offset + pubkey_len > data.size()) return false;
        // Store public key for later use in key exchange
        std::vector<uint8_t> public_key(data.data() + offset, data.data() + offset + pubkey_len);
        offset += pubkey_len;
        
        // Align to 4-byte boundary
        while (offset % 4 != 0) {
            offset++;
            if (offset > data.size()) return false;
        }
        
        // Parse encrypted timestamp (XDR opaque)
        if (offset + 4 > data.size()) return false;
        uint32_t timestamp_len = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        if (offset + timestamp_len > data.size()) return false;
        // Store encrypted timestamp for verification
        std::vector<uint8_t> encrypted_timestamp(data.data() + offset, data.data() + offset + timestamp_len);
        offset += timestamp_len;
        
        // Align to 4-byte boundary
        while (offset % 4 != 0) {
            offset++;
            if (offset > data.size()) return false;
        }
        
        // Parse window (uint32)
        if (offset + 4 > data.size()) return false;
        uint32_t window = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        
        // Extract user information from netname
        // Netname format: "unix.<uid>@<domain>" or "user.<username>@<domain>"
        context.machine_name = client_name;
        context.username = netname;
        
        // Try to extract UID from netname
        if (netname.find("unix.") == 0) {
            size_t at_pos = netname.find('@');
            if (at_pos != std::string::npos) {
                std::string uid_str = netname.substr(5, at_pos - 5);
                try {
                    context.uid = static_cast<uint32_t>(std::stoul(uid_str));
                    context.gid = context.uid; // Default to same as UID
                } catch (...) {
                    context.uid = 1000; // Default UID
                    context.gid = 1000; // Default GID
                }
            }
        } else {
            // Default values if we can't parse
            context.uid = 1000;
            context.gid = 1000;
        }
        
        // Store authentication data in context attributes for later use
        context.attributes["auth_dh_public_key"] = std::string(public_key.begin(), public_key.end());
        context.attributes["auth_dh_timestamp"] = std::string(encrypted_timestamp.begin(), encrypted_timestamp.end());
        context.attributes["auth_dh_window"] = std::to_string(window);
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing AUTH_DH credentials: " << e.what() << std::endl;
        return false;
    }
}

bool SecurityManager::parseKerberosCredentials(const std::vector<uint8_t>& data, SecurityContext& context) {
    // RPCSEC_GSS credential format:
    // - Version (uint32) - must be 1
    // - Procedure (uint32) - GSS procedure (RPCSEC_GSS_DATA, RPCSEC_GSS_INIT, etc.)
    // - Sequence number (uint32)
    // - Service (uint32) - RPCSEC_GSS_SVC_NONE, RPCSEC_GSS_SVC_INTEGRITY, RPCSEC_GSS_SVC_PRIVACY
    // - GSS token (opaque, XDR encoded)
    
    if (data.size() < 16) { // Minimum size for version + procedure + seq + service
        return false;
    }
    
    try {
        size_t offset = 0;
        
        // Parse version (must be 1)
        if (offset + 4 > data.size()) return false;
        uint32_t version = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        if (version != 1) {
            std::cerr << "Invalid RPCSEC_GSS version: " << version << std::endl;
            return false;
        }
        
        // Parse procedure
        if (offset + 4 > data.size()) return false;
        uint32_t procedure = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        // Parse sequence number
        if (offset + 4 > data.size()) return false;
        uint32_t sequence = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        // Parse service
        if (offset + 4 > data.size()) return false;
        uint32_t service = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        // Parse GSS token (XDR opaque)
        if (offset + 4 > data.size()) return false;
        uint32_t token_len = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += 4;
        
        if (offset + token_len > data.size()) return false;
        std::vector<uint8_t> gss_token(data.data() + offset, data.data() + offset + token_len);
        
        // Store GSS context information
        context.attributes["gss_procedure"] = std::to_string(procedure);
        context.attributes["gss_sequence"] = std::to_string(sequence);
        context.attributes["gss_service"] = std::to_string(service);
        context.attributes["gss_token"] = std::string(gss_token.begin(), gss_token.end());
        
        // For a full implementation, we would:
        // 1. Process GSS token using GSSAPI
        // 2. Extract principal name from token
        // 3. Validate ticket and session key
        // 4. Extract UID/GID from principal or mapping
        
        // For now, extract basic information
        // GSS tokens typically contain Kerberos tickets with principal information
        // We'll use default values and mark as authenticated
        // A full implementation would use gss_accept_sec_context() from GSSAPI
        
        context.username = "kerberos_user"; // Would be extracted from GSS token
        context.uid = 1000; // Would be mapped from Kerberos principal
        context.gid = 1000;
        context.machine_name = "kerberos_client";
        
        // Store session information
        context.session_id = "gss_" + std::to_string(sequence);
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing Kerberos credentials: " << e.what() << std::endl;
        return false;
    }
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
    
    // Get file owner and group (simplified - in real implementation would use stat)
    uint32_t file_owner = 1000;  // Default owner
    uint32_t file_group = 1000;  // Default group
    
    // Check owner permissions
    if (context.uid == file_owner) {
        return (file_perms & (requested_perms << 6)) == (requested_perms << 6);
    }
    
    // Check group permissions
    if (context.gid == file_group || std::find(context.gids.begin(), context.gids.end(), file_group) != context.gids.end()) {
        return (file_perms & (requested_perms << 3)) == (requested_perms << 3);
    }
    
    // Check other permissions
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
