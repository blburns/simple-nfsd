/**
 * @file test_nfs_server.cpp
 * @brief Unit tests for NfsServerSimple class
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include <gtest/gtest.h>
#include "simple_nfsd/nfs_server_simple.hpp"
#include "simple_nfsd/rpc_protocol.hpp"
#include "simple_nfsd/auth_manager.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>

class NfsServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_root = "/tmp/simple-nfsd-test";
        std::filesystem::create_directories(test_root);
        
        // Create test files
        std::ofstream file1(test_root + "/file1.txt");
        file1 << "Hello, World!";
        file1.close();
        
        std::ofstream file2(test_root + "/file2.txt");
        file2 << "Test content";
        file2.close();
        
        // Create test directory
        std::filesystem::create_directories(test_root + "/subdir");
        
        // Initialize NFS server
        nfs_server = std::make_unique<SimpleNfsd::NfsServerSimple>();
        
        // Configure server
        SimpleNfsd::NfsServerConfig config;
        config.listen_address = "127.0.0.1";
        config.listen_port = 2049;
        config.root_path = test_root;
        config.max_connections = 100;
        config.enable_tcp = true;
        config.enable_udp = true;
        
        nfs_server->initialize(config);
    }

    void TearDown() override {
        nfs_server.reset();
        // Clean up test directory
        std::filesystem::remove_all(test_root);
    }

    std::unique_ptr<SimpleNfsd::NfsServerSimple> nfs_server;
    std::string test_root;
};

TEST_F(NfsServerTest, Initialize) {
    SimpleNfsd::NfsServerConfig config;
    config.listen_address = "127.0.0.1";
    config.listen_port = 2049;
    config.root_path = "/tmp/test";
    config.max_connections = 100;
    config.enable_tcp = true;
    config.enable_udp = true;
    
    EXPECT_TRUE(nfs_server->initialize(config));
}

TEST_F(NfsServerTest, GetStats) {
    auto stats = nfs_server->getStats();
    EXPECT_EQ(stats.total_requests, 0);
    EXPECT_EQ(stats.successful_requests, 0);
    EXPECT_EQ(stats.failed_requests, 0);
    EXPECT_EQ(stats.bytes_read, 0);
    EXPECT_EQ(stats.bytes_written, 0);
    EXPECT_EQ(stats.active_connections, 0);
}

TEST_F(NfsServerTest, ResetStats) {
    // This would require simulating some operations first
    nfs_server->resetStats();
    auto stats = nfs_server->getStats();
    EXPECT_EQ(stats.total_requests, 0);
}

TEST_F(NfsServerTest, IsHealthy) {
    EXPECT_TRUE(nfs_server->isHealthy());
}

TEST_F(NfsServerTest, FileHandleManagement) {
    // Test getting handle for path
    uint32_t handle1 = nfs_server->getHandleForPath("file1.txt");
    EXPECT_GT(handle1, 0);
    
    uint32_t handle2 = nfs_server->getHandleForPath("file2.txt");
    EXPECT_GT(handle2, 0);
    EXPECT_NE(handle1, handle2); // Different files should have different handles
    
    // Test getting path from handle
    std::string path1 = nfs_server->getPathFromHandle(handle1);
    EXPECT_EQ(path1, "file1.txt");
    
    std::string path2 = nfs_server->getPathFromHandle(handle2);
    EXPECT_EQ(path2, "file2.txt");
    
    // Test invalid handle
    std::string invalid_path = nfs_server->getPathFromHandle(99999);
    EXPECT_TRUE(invalid_path.empty());
}

TEST_F(NfsServerTest, PathValidation) {
    // Test valid paths
    EXPECT_TRUE(nfs_server->validatePath("file1.txt"));
    EXPECT_TRUE(nfs_server->validatePath("subdir"));
    EXPECT_TRUE(nfs_server->validatePath("subdir/file.txt"));
    
    // Test invalid paths (path traversal)
    EXPECT_FALSE(nfs_server->validatePath("../file.txt"));
    EXPECT_FALSE(nfs_server->validatePath("../../etc/passwd"));
    EXPECT_FALSE(nfs_server->validatePath("/absolute/path"));
}

TEST_F(NfsServerTest, ReadDirectory) {
    auto entries = nfs_server->readDirectory("");
    EXPECT_GT(entries.size(), 0);
    
    // Should contain our test files
    bool found_file1 = false;
    bool found_file2 = false;
    bool found_subdir = false;
    
    for (const auto& entry : entries) {
        if (entry == "file1.txt") found_file1 = true;
        if (entry == "file2.txt") found_file2 = true;
        if (entry == "subdir") found_subdir = true;
    }
    
    EXPECT_TRUE(found_file1);
    EXPECT_TRUE(found_file2);
    EXPECT_TRUE(found_subdir);
}

TEST_F(NfsServerTest, FileOperations) {
    // Test file existence
    EXPECT_TRUE(nfs_server->fileExists(test_root + "/file1.txt"));
    EXPECT_FALSE(nfs_server->fileExists(test_root + "/nonexistent.txt"));
    
    // Test directory check
    EXPECT_TRUE(nfs_server->isDirectory(test_root + "/subdir"));
    EXPECT_FALSE(nfs_server->isDirectory(test_root + "/file1.txt"));
    
    // Test file check
    EXPECT_TRUE(nfs_server->isFile(test_root + "/file1.txt"));
    EXPECT_FALSE(nfs_server->isFile(test_root + "/subdir"));
}

TEST_F(NfsServerTest, ReadFile) {
    auto data = nfs_server->readFile(test_root + "/file1.txt", 0, 5);
    EXPECT_EQ(data.size(), 5);
    
    std::string content(data.begin(), data.end());
    EXPECT_EQ(content, "Hello");
    
    // Test reading beyond file size
    auto data2 = nfs_server->readFile(test_root + "/file1.txt", 0, 1000);
    EXPECT_EQ(data2.size(), 13); // "Hello, World!" is 13 characters
}

TEST_F(NfsServerTest, WriteFile) {
    std::vector<uint8_t> test_data = {'T', 'e', 's', 't'};
    
    // Test writing to existing file
    bool success = nfs_server->writeFile(test_root + "/file1.txt", 0, test_data);
    EXPECT_TRUE(success);
    
    // Verify the write
    auto data = nfs_server->readFile(test_root + "/file1.txt", 0, 4);
    std::string content(data.begin(), data.end());
    EXPECT_EQ(content, "Test");
}

TEST_F(NfsServerTest, ProtocolNegotiation) {
    // Test version negotiation
    uint32_t version2 = nfs_server->negotiateNfsVersion(2);
    EXPECT_EQ(version2, 2);
    
    uint32_t version3 = nfs_server->negotiateNfsVersion(3);
    EXPECT_EQ(version3, 3);
    
    uint32_t version4 = nfs_server->negotiateNfsVersion(4);
    EXPECT_EQ(version4, 4);
    
    // Test unsupported version
    uint32_t version1 = nfs_server->negotiateNfsVersion(1);
    EXPECT_EQ(version1, 2); // Should fall back to highest supported version
}

TEST_F(NfsServerTest, IsNfsVersionSupported) {
    EXPECT_TRUE(nfs_server->isNfsVersionSupported(2));
    EXPECT_TRUE(nfs_server->isNfsVersionSupported(3));
    EXPECT_TRUE(nfs_server->isNfsVersionSupported(4));
    EXPECT_FALSE(nfs_server->isNfsVersionSupported(1));
    EXPECT_FALSE(nfs_server->isNfsVersionSupported(5));
}

TEST_F(NfsServerTest, GetSupportedNfsVersions) {
    auto versions = nfs_server->getSupportedNfsVersions();
    EXPECT_EQ(versions.size(), 3);
    EXPECT_EQ(versions[0], 4); // Highest version first
    EXPECT_EQ(versions[1], 3);
    EXPECT_EQ(versions[2], 2);
}

TEST_F(NfsServerTest, RpcMessageHandling) {
    // Create a test RPC message
    SimpleNfsd::RpcMessage message;
    message.header.xid = 0x12345678;
    message.header.type = SimpleNfsd::RpcMessageType::CALL;
    message.header.rpcvers = 2;
    message.header.prog = static_cast<uint32_t>(SimpleNfsd::RpcProgram::NFS);
    message.header.vers = 2;
    message.header.proc = 0; // NULL procedure
    message.header.cred.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    message.header.cred.length = 0;
    message.header.cred.body = {};
    message.header.verf.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    message.header.verf.length = 0;
    message.header.verf.body = {};
    message.data = {};
    
    // Test authentication
    SimpleNfsd::AuthContext auth_context;
    bool auth_success = nfs_server->authenticateRequest(message, auth_context);
    EXPECT_TRUE(auth_success);
    EXPECT_TRUE(auth_context.authenticated);
}

TEST_F(NfsServerTest, AccessControl) {
    SimpleNfsd::AuthContext context;
    context.authenticated = true;
    context.uid = 1000;
    context.gid = 1000;
    context.machine_name = "test-client";
    
    // Test access control
    bool read_access = nfs_server->checkAccess("file1.txt", context, true, false);
    bool write_access = nfs_server->checkAccess("file1.txt", context, false, true);
    
    // Basic implementation should allow access
    EXPECT_TRUE(read_access);
    EXPECT_TRUE(write_access);
}

TEST_F(NfsServerTest, Nfsv2NullProcedure) {
    // Create NULL procedure message
    SimpleNfsd::RpcMessage message;
    message.header.xid = 0x12345678;
    message.header.type = SimpleNfsd::RpcMessageType::CALL;
    message.header.rpcvers = 2;
    message.header.prog = static_cast<uint32_t>(SimpleNfsd::RpcProgram::NFS);
    message.header.vers = 2;
    message.header.proc = 0; // NULL procedure
    message.header.cred.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    message.header.cred.length = 0;
    message.header.cred.body = {};
    message.header.verf.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    message.header.verf.length = 0;
    message.header.verf.body = {};
    message.data = {};
    
    SimpleNfsd::AuthContext auth_context;
    auth_context.authenticated = true;
    auth_context.uid = 0;
    auth_context.gid = 0;
    auth_context.machine_name = "test";
    
    // This should not crash
    nfs_server->handleNfsv2Null(message, auth_context);
    EXPECT_TRUE(true);
}

TEST_F(NfsServerTest, Nfsv2GetAttrProcedure) {
    // Get handle for test file
    uint32_t handle = nfs_server->getHandleForPath("file1.txt");
    
    // Create GETATTR procedure message
    SimpleNfsd::RpcMessage message;
    message.header.xid = 0x12345678;
    message.header.type = SimpleNfsd::RpcMessageType::CALL;
    message.header.rpcvers = 2;
    message.header.prog = static_cast<uint32_t>(SimpleNfsd::RpcProgram::NFS);
    message.header.vers = 2;
    message.header.proc = 1; // GETATTR procedure
    message.header.cred.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    message.header.cred.length = 0;
    message.header.cred.body = {};
    message.header.verf.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    message.header.verf.length = 0;
    message.header.verf.body = {};
    
    // Add file handle to message data
    uint32_t handle_net = htonl(handle);
    message.data.insert(message.data.end(), (uint8_t*)&handle_net, (uint8_t*)&handle_net + 4);
    
    SimpleNfsd::AuthContext auth_context;
    auth_context.authenticated = true;
    auth_context.uid = 0;
    auth_context.gid = 0;
    auth_context.machine_name = "test";
    
    // This should not crash
    nfs_server->handleNfsv2GetAttr(message, auth_context);
    EXPECT_TRUE(true);
}

TEST_F(NfsServerTest, Nfsv2LookupProcedure) {
    // Get handle for root directory
    uint32_t dir_handle = nfs_server->getHandleForPath("");
    
    // Create LOOKUP procedure message
    SimpleNfsd::RpcMessage message;
    message.header.xid = 0x12345678;
    message.header.type = SimpleNfsd::RpcMessageType::CALL;
    message.header.rpcvers = 2;
    message.header.prog = static_cast<uint32_t>(SimpleNfsd::RpcProgram::NFS);
    message.header.vers = 2;
    message.header.proc = 3; // LOOKUP procedure
    message.header.cred.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    message.header.cred.length = 0;
    message.header.cred.body = {};
    message.header.verf.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    message.header.verf.length = 0;
    message.header.verf.body = {};
    
    // Add directory handle and filename to message data
    uint32_t dir_handle_net = htonl(dir_handle);
    message.data.insert(message.data.end(), (uint8_t*)&dir_handle_net, (uint8_t*)&dir_handle_net + 4);
    
    std::string filename = "file1.txt";
    uint32_t name_len = htonl(filename.length());
    message.data.insert(message.data.end(), (uint8_t*)&name_len, (uint8_t*)&name_len + 4);
    message.data.insert(message.data.end(), filename.begin(), filename.end());
    
    SimpleNfsd::AuthContext auth_context;
    auth_context.authenticated = true;
    auth_context.uid = 0;
    auth_context.gid = 0;
    auth_context.machine_name = "test";
    
    // This should not crash
    nfs_server->handleNfsv2Lookup(message, auth_context);
    EXPECT_TRUE(true);
}

TEST_F(NfsServerTest, ErrorHandling) {
    // Test with invalid message
    SimpleNfsd::RpcMessage invalid_message;
    invalid_message.header.xid = 0x12345678;
    invalid_message.header.type = SimpleNfsd::RpcMessageType::CALL;
    invalid_message.header.rpcvers = 1; // Invalid RPC version
    invalid_message.header.prog = static_cast<uint32_t>(SimpleNfsd::RpcProgram::NFS);
    invalid_message.header.vers = 2;
    invalid_message.header.proc = 0;
    invalid_message.header.cred.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    invalid_message.header.cred.length = 0;
    invalid_message.header.cred.body = {};
    invalid_message.header.verf.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    invalid_message.header.verf.length = 0;
    invalid_message.header.verf.body = {};
    invalid_message.data = {};
    
    // This should handle the error gracefully
    nfs_server->handleRpcCall(invalid_message);
    EXPECT_TRUE(true);
}
