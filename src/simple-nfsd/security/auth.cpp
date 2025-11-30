/**
 * @file auth_manager.cpp
 * @brief Authentication manager implementation for NFS server
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include "simple-nfsd/security/auth.hpp"
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

namespace SimpleNfsd {

AuthManager::AuthManager() 
    : root_squash_(true), all_squash_(false), anon_uid_(65534), anon_gid_(65534) {
}

bool AuthManager::initialize() {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    
    try {
        loadUserDatabase();
        loadGroupDatabase();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize authentication manager: " << e.what() << std::endl;
        return false;
    }
}

AuthResult AuthManager::authenticate(const std::vector<uint8_t>& credentials, 
                                   const std::vector<uint8_t>& verifier,
                                   AuthContext& context) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    
    if (credentials.empty()) {
        return AuthResult::INVALID_CREDENTIALS;
    }
    
    // Check authentication type (first byte)
    uint32_t auth_type = credentials[0];
    
    switch (auth_type) {
        case 1: // AUTH_NONE
            context.authenticated = true;
            context.uid = 0;
            context.gid = 0;
            context.machine_name = "anonymous";
            return AuthResult::SUCCESS;
            
        case 2: // AUTH_SYS
            {
                AuthSysCredentials creds;
                if (!parseAuthSysCredentials(credentials, creds)) {
                    return AuthResult::INVALID_CREDENTIALS;
                }
                
                AuthResult result = validateAuthSysCredentials(creds);
                if (result == AuthResult::SUCCESS) {
                    context.authenticated = true;
                    context.uid = creds.uid;
                    context.gid = creds.gid;
                    context.gids = creds.gids;
                    context.machine_name = creds.machinename;
                    
                    // Apply squash rules
                    if (all_squash_) {
                        context.uid = anon_uid_;
                        context.gid = anon_gid_;
                        context.gids.clear();
                        context.gids.push_back(anon_gid_);
                    } else if (root_squash_ && context.uid == 0) {
                        context.uid = anon_uid_;
                        context.gid = anon_gid_;
                        context.gids.clear();
                        context.gids.push_back(anon_gid_);
                    }
                }
                return result;
            }
            
        default:
            return AuthResult::UNSUPPORTED_AUTH_TYPE;
    }
}

bool AuthManager::parseAuthSysCredentials(const std::vector<uint8_t>& data, 
                                        AuthSysCredentials& creds) {
    if (data.size() < 20) { // Minimum size for AUTH_SYS
        return false;
    }
    
    size_t offset = 1; // Skip auth type
    
    // Parse stamp
    if (offset + 4 > data.size()) return false;
    memcpy(&creds.stamp, data.data() + offset, 4);
    creds.stamp = ntohl(creds.stamp);
    offset += 4;
    
    // Parse machine name length
    if (offset + 4 > data.size()) return false;
    uint32_t machine_name_len = 0;
    memcpy(&machine_name_len, data.data() + offset, 4);
    machine_name_len = ntohl(machine_name_len);
    offset += 4;
    
    // Parse machine name
    if (offset + machine_name_len > data.size()) return false;
    creds.machinename = std::string(reinterpret_cast<const char*>(data.data() + offset), machine_name_len);
    offset += machine_name_len;
    
    // Align to 4-byte boundary
    while (offset % 4 != 0) {
        offset++;
        if (offset > data.size()) return false;
    }
    
    // Parse UID
    if (offset + 4 > data.size()) return false;
    memcpy(&creds.uid, data.data() + offset, 4);
    creds.uid = ntohl(creds.uid);
    offset += 4;
    
    // Parse GID
    if (offset + 4 > data.size()) return false;
    memcpy(&creds.gid, data.data() + offset, 4);
    creds.gid = ntohl(creds.gid);
    offset += 4;
    
    // Parse additional GIDs count
    if (offset + 4 > data.size()) return false;
    uint32_t gids_count = 0;
    memcpy(&gids_count, data.data() + offset, 4);
    gids_count = ntohl(gids_count);
    offset += 4;
    
    // Parse additional GIDs
    creds.gids.clear();
    for (uint32_t i = 0; i < gids_count && offset + 4 <= data.size(); ++i) {
        uint32_t gid = 0;
        memcpy(&gid, data.data() + offset, 4);
        gid = ntohl(gid);
        creds.gids.push_back(gid);
        offset += 4;
    }
    
    return true;
}

AuthResult AuthManager::validateAuthSysCredentials(const AuthSysCredentials& creds) {
    // Basic validation
    if (creds.machinename.empty()) {
        return AuthResult::INVALID_CREDENTIALS;
    }
    
    // Check if UID/GID are valid (basic check)
    if (!isValidUid(creds.uid) || !isValidGid(creds.gid)) {
        return AuthResult::INVALID_CREDENTIALS;
    }
    
    // Validate additional GIDs
    for (uint32_t gid : creds.gids) {
        if (!isValidGid(gid)) {
            return AuthResult::INVALID_CREDENTIALS;
        }
    }
    
    return AuthResult::SUCCESS;
}

std::vector<uint8_t> AuthManager::createAuthSysVerifier() {
    std::vector<uint8_t> verifier;
    
    // AUTH_SYS verifier is typically empty or contains a timestamp
    uint32_t timestamp = static_cast<uint32_t>(time(nullptr));
    timestamp = htonl(timestamp);
    
    verifier.insert(verifier.end(), (uint8_t*)&timestamp, (uint8_t*)&timestamp + 4);
    
    return verifier;
}

bool AuthManager::checkPathAccess(const std::string& path, const AuthContext& context, 
                                 bool read_access, bool write_access) {
    if (!context.authenticated) {
        return false;
    }
    
    // For now, implement basic access control
    // In a real implementation, this would check file permissions
    
    // Root user has full access
    if (context.uid == 0) {
        return true;
    }
    
    // For other users, check if path exists and is accessible
    // This is a simplified implementation
    return true;
}

bool AuthManager::getUserInfo(uint32_t uid, std::string& username) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    
    auto it = uid_to_name_.find(uid);
    if (it != uid_to_name_.end()) {
        username = it->second;
        return true;
    }
    
    // Try system call
    struct passwd* pw = getpwuid(uid);
    if (pw) {
        username = pw->pw_name;
        uid_to_name_[uid] = username;
        name_to_uid_[username] = uid;
        return true;
    }
    
    return false;
}

bool AuthManager::getGroupInfo(uint32_t gid, std::string& groupname) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    
    auto it = gid_to_name_.find(gid);
    if (it != gid_to_name_.end()) {
        groupname = it->second;
        return true;
    }
    
    // Try system call
    struct group* gr = getgrgid(gid);
    if (gr) {
        groupname = gr->gr_name;
        gid_to_name_[gid] = groupname;
        name_to_gid_[groupname] = gid;
        return true;
    }
    
    return false;
}

void AuthManager::loadUserDatabase() {
    // Load from /etc/passwd
    std::ifstream passwd_file("/etc/passwd");
    std::string line;
    
    while (std::getline(passwd_file, line)) {
        std::istringstream iss(line);
        std::string username, password, uid_str, gid_str, gecos, home, shell;
        
        if (std::getline(iss, username, ':') &&
            std::getline(iss, password, ':') &&
            std::getline(iss, uid_str, ':') &&
            std::getline(iss, gid_str, ':') &&
            std::getline(iss, gecos, ':') &&
            std::getline(iss, home, ':') &&
            std::getline(iss, shell, ':')) {
            
            try {
                uint32_t uid = static_cast<uint32_t>(std::stoul(uid_str));
                uid_to_name_[uid] = username;
                name_to_uid_[username] = uid;
            } catch (const std::exception& e) {
                // Skip invalid entries
            }
        }
    }
}

void AuthManager::loadGroupDatabase() {
    // Load from /etc/group
    std::ifstream group_file("/etc/group");
    std::string line;
    
    while (std::getline(group_file, line)) {
        std::istringstream iss(line);
        std::string groupname, password, gid_str, members;
        
        if (std::getline(iss, groupname, ':') &&
            std::getline(iss, password, ':') &&
            std::getline(iss, gid_str, ':') &&
            std::getline(iss, members, ':')) {
            
            try {
                uint32_t gid = static_cast<uint32_t>(std::stoul(gid_str));
                gid_to_name_[gid] = groupname;
                name_to_gid_[groupname] = gid;
            } catch (const std::exception& e) {
                // Skip invalid entries
            }
        }
    }
}

bool AuthManager::isValidUid(uint32_t uid) const {
    // Basic validation - UIDs are typically 0-65535
    return uid <= 65535;
}

bool AuthManager::isValidGid(uint32_t gid) const {
    // Basic validation - GIDs are typically 0-65535
    return gid <= 65535;
}

std::string AuthManager::getMachineName() const {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "unknown";
}

} // namespace SimpleNfsd
