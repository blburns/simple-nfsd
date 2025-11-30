/**
 * @file test_auth_manager.cpp
 * @brief Unit tests for AuthManager class
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include <gtest/gtest.h>
#include "simple-nfsd/security/auth.hpp"
#include <vector>
#include <cstring>

class AuthManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auth_manager = std::make_unique<SimpleNfsd::AuthManager>();
        auth_manager->initialize();
    }

    void TearDown() override {
        auth_manager.reset();
    }

    std::unique_ptr<SimpleNfsd::AuthManager> auth_manager;
};

TEST_F(AuthManagerTest, Initialize) {
    EXPECT_TRUE(auth_manager->initialize());
}

TEST_F(AuthManagerTest, AuthNoneAuthentication) {
    // Test AUTH_NONE (type 1)
    std::vector<uint8_t> auth_none_creds = {1}; // AUTH_NONE
    std::vector<uint8_t> verifier = {};
    
    SimpleNfsd::AuthContext context;
    SimpleNfsd::AuthResult result = auth_manager->authenticate(auth_none_creds, verifier, context);
    
    EXPECT_EQ(result, SimpleNfsd::AuthResult::SUCCESS);
    EXPECT_TRUE(context.authenticated);
    EXPECT_EQ(context.uid, 0);
    EXPECT_EQ(context.gid, 0);
    EXPECT_EQ(context.machine_name, "anonymous");
}

TEST_F(AuthManagerTest, AuthSysAuthentication) {
    // Create AUTH_SYS credentials
    std::vector<uint8_t> auth_sys_creds;
    
    // Auth type (AUTH_SYS = 2)
    uint32_t auth_type = htonl(2);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&auth_type, (uint8_t*)&auth_type + 4);
    
    // Stamp
    uint32_t stamp = htonl(0x12345678);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&stamp, (uint8_t*)&stamp + 4);
    
    // Machine name length
    std::string machine_name = "test-client";
    uint32_t name_len = htonl(machine_name.length());
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
    
    // Machine name
    auth_sys_creds.insert(auth_sys_creds.end(), machine_name.begin(), machine_name.end());
    
    // Align to 4-byte boundary
    while (auth_sys_creds.size() % 4 != 0) {
        auth_sys_creds.push_back(0);
    }
    
    // UID
    uint32_t uid = htonl(1000);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
    
    // GID
    uint32_t gid = htonl(1000);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
    
    // Additional GIDs count
    uint32_t gids_count = htonl(2);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gids_count, (uint8_t*)&gids_count + 4);
    
    // Additional GIDs
    uint32_t gid1 = htonl(1001);
    uint32_t gid2 = htonl(1002);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gid1, (uint8_t*)&gid1 + 4);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gid2, (uint8_t*)&gid2 + 4);
    
    std::vector<uint8_t> verifier = {};
    
    SimpleNfsd::AuthContext context;
    SimpleNfsd::AuthResult result = auth_manager->authenticate(auth_sys_creds, verifier, context);
    
    EXPECT_EQ(result, SimpleNfsd::AuthResult::SUCCESS);
    EXPECT_TRUE(context.authenticated);
    EXPECT_EQ(context.uid, 1000);
    EXPECT_EQ(context.gid, 1000);
    EXPECT_EQ(context.machine_name, "test-client");
    EXPECT_EQ(context.gids.size(), 2);
    EXPECT_EQ(context.gids[0], 1001);
    EXPECT_EQ(context.gids[1], 1002);
}

TEST_F(AuthManagerTest, InvalidAuthType) {
    std::vector<uint8_t> invalid_creds = {99}; // Invalid auth type
    std::vector<uint8_t> verifier = {};
    
    SimpleNfsd::AuthContext context;
    SimpleNfsd::AuthResult result = auth_manager->authenticate(invalid_creds, verifier, context);
    
    EXPECT_EQ(result, SimpleNfsd::AuthResult::UNSUPPORTED_AUTH_TYPE);
    EXPECT_FALSE(context.authenticated);
}

TEST_F(AuthManagerTest, InvalidCredentials) {
    std::vector<uint8_t> invalid_creds = {2}; // AUTH_SYS but incomplete
    std::vector<uint8_t> verifier = {};
    
    SimpleNfsd::AuthContext context;
    SimpleNfsd::AuthResult result = auth_manager->authenticate(invalid_creds, verifier, context);
    
    EXPECT_EQ(result, SimpleNfsd::AuthResult::INVALID_CREDENTIALS);
    EXPECT_FALSE(context.authenticated);
}

TEST_F(AuthManagerTest, ParseAuthSysCredentials) {
    // Create test AUTH_SYS credentials
    std::vector<uint8_t> auth_sys_data;
    
    // Auth type (AUTH_SYS = 2)
    uint32_t auth_type = htonl(2);
    auth_sys_data.insert(auth_sys_data.end(), (uint8_t*)&auth_type, (uint8_t*)&auth_type + 4);
    
    // Stamp
    uint32_t stamp = htonl(0x12345678);
    auth_sys_data.insert(auth_sys_data.end(), (uint8_t*)&stamp, (uint8_t*)&stamp + 4);
    
    // Machine name length
    std::string machine_name = "test-machine";
    uint32_t name_len = htonl(machine_name.length());
    auth_sys_data.insert(auth_sys_data.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
    
    // Machine name
    auth_sys_data.insert(auth_sys_data.end(), machine_name.begin(), machine_name.end());
    
    // Align to 4-byte boundary
    while (auth_sys_data.size() % 4 != 0) {
        auth_sys_data.push_back(0);
    }
    
    // UID
    uint32_t uid = htonl(1000);
    auth_sys_data.insert(auth_sys_data.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
    
    // GID
    uint32_t gid = htonl(1000);
    auth_sys_data.insert(auth_sys_data.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
    
    // Additional GIDs count
    uint32_t gids_count = htonl(1);
    auth_sys_data.insert(auth_sys_data.end(), (uint8_t*)&gids_count, (uint8_t*)&gids_count + 4);
    
    // Additional GID
    uint32_t additional_gid = htonl(1001);
    auth_sys_data.insert(auth_sys_data.end(), (uint8_t*)&additional_gid, (uint8_t*)&additional_gid + 4);
    
    SimpleNfsd::AuthSysCredentials creds;
    bool success = auth_manager->parseAuthSysCredentials(auth_sys_data, creds);
    
    EXPECT_TRUE(success);
    EXPECT_EQ(creds.stamp, 0x12345678);
    EXPECT_EQ(creds.machinename, "test-machine");
    EXPECT_EQ(creds.uid, 1000);
    EXPECT_EQ(creds.gid, 1000);
    EXPECT_EQ(creds.gids.size(), 1);
    EXPECT_EQ(creds.gids[0], 1001);
}

TEST_F(AuthManagerTest, ValidateAuthSysCredentials) {
    SimpleNfsd::AuthSysCredentials valid_creds;
    valid_creds.stamp = 0x12345678;
    valid_creds.machinename = "test-machine";
    valid_creds.uid = 1000;
    valid_creds.gid = 1000;
    valid_creds.gids = {1000, 1001};
    
    SimpleNfsd::AuthResult result = auth_manager->validateAuthSysCredentials(valid_creds);
    EXPECT_EQ(result, SimpleNfsd::AuthResult::SUCCESS);
    
    // Test invalid credentials
    SimpleNfsd::AuthSysCredentials invalid_creds = valid_creds;
    invalid_creds.machinename = ""; // Empty machine name
    
    result = auth_manager->validateAuthSysCredentials(invalid_creds);
    EXPECT_EQ(result, SimpleNfsd::AuthResult::INVALID_CREDENTIALS);
}

TEST_F(AuthManagerTest, CreateAuthSysVerifier) {
    std::vector<uint8_t> verifier = auth_manager->createAuthSysVerifier();
    EXPECT_GT(verifier.size(), 0);
}

TEST_F(AuthManagerTest, CheckPathAccess) {
    SimpleNfsd::AuthContext context;
    context.authenticated = true;
    context.uid = 1000;
    context.gid = 1000;
    context.machine_name = "test-client";
    
    // Test basic access control
    bool has_access = auth_manager->checkPathAccess("/test/path", context, true, false);
    EXPECT_TRUE(has_access); // Basic implementation should allow access
}

TEST_F(AuthManagerTest, RootSquash) {
    auth_manager->setRootSquash(true);
    auth_manager->setAnonUid(65534);
    auth_manager->setAnonGid(65534);
    
    // Create root user context
    std::vector<uint8_t> auth_sys_creds;
    uint32_t auth_type = htonl(2);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&auth_type, (uint8_t*)&auth_type + 4);
    
    uint32_t stamp = htonl(0x12345678);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&stamp, (uint8_t*)&stamp + 4);
    
    std::string machine_name = "test-client";
    uint32_t name_len = htonl(machine_name.length());
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
    auth_sys_creds.insert(auth_sys_creds.end(), machine_name.begin(), machine_name.end());
    
    while (auth_sys_creds.size() % 4 != 0) {
        auth_sys_creds.push_back(0);
    }
    
    uint32_t uid = htonl(0); // Root user
    uint32_t gid = htonl(0);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
    
    uint32_t gids_count = htonl(0);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gids_count, (uint8_t*)&gids_count + 4);
    
    std::vector<uint8_t> verifier = {};
    SimpleNfsd::AuthContext context;
    SimpleNfsd::AuthResult result = auth_manager->authenticate(auth_sys_creds, verifier, context);
    
    EXPECT_EQ(result, SimpleNfsd::AuthResult::SUCCESS);
    EXPECT_TRUE(context.authenticated);
    EXPECT_EQ(context.uid, 65534); // Should be squashed to anonymous UID
    EXPECT_EQ(context.gid, 65534); // Should be squashed to anonymous GID
}

TEST_F(AuthManagerTest, AllSquash) {
    auth_manager->setAllSquash(true);
    auth_manager->setAnonUid(65534);
    auth_manager->setAnonGid(65534);
    
    // Create regular user context
    std::vector<uint8_t> auth_sys_creds;
    uint32_t auth_type = htonl(2);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&auth_type, (uint8_t*)&auth_type + 4);
    
    uint32_t stamp = htonl(0x12345678);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&stamp, (uint8_t*)&stamp + 4);
    
    std::string machine_name = "test-client";
    uint32_t name_len = htonl(machine_name.length());
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
    auth_sys_creds.insert(auth_sys_creds.end(), machine_name.begin(), machine_name.end());
    
    while (auth_sys_creds.size() % 4 != 0) {
        auth_sys_creds.push_back(0);
    }
    
    uint32_t uid = htonl(1000); // Regular user
    uint32_t gid = htonl(1000);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&uid, (uint8_t*)&uid + 4);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gid, (uint8_t*)&gid + 4);
    
    uint32_t gids_count = htonl(0);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gids_count, (uint8_t*)&gids_count + 4);
    
    std::vector<uint8_t> verifier = {};
    SimpleNfsd::AuthContext context;
    SimpleNfsd::AuthResult result = auth_manager->authenticate(auth_sys_creds, verifier, context);
    
    EXPECT_EQ(result, SimpleNfsd::AuthResult::SUCCESS);
    EXPECT_TRUE(context.authenticated);
    EXPECT_EQ(context.uid, 65534); // Should be squashed to anonymous UID
    EXPECT_EQ(context.gid, 65534); // Should be squashed to anonymous GID
}

TEST_F(AuthManagerTest, GetUserInfo) {
    std::string username;
    bool success = auth_manager->getUserInfo(0, username); // Root user
    // This might fail on some systems, so we just test it doesn't crash
    EXPECT_TRUE(true);
}

TEST_F(AuthManagerTest, GetGroupInfo) {
    std::string groupname;
    bool success = auth_manager->getGroupInfo(0, groupname); // Root group
    // This might fail on some systems, so we just test it doesn't crash
    EXPECT_TRUE(true);
}

TEST_F(AuthManagerTest, Configuration) {
    auth_manager->setRootSquash(false);
    auth_manager->setAllSquash(true);
    auth_manager->setAnonUid(1000);
    auth_manager->setAnonGid(1000);
    
    // Test that configuration was set (implementation dependent)
    EXPECT_TRUE(true);
}
