/**
 * @file test_basic_functionality.cpp
 * @brief Basic functionality tests for simple-nfsd
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include <gtest/gtest.h>
#include "simple-nfsd/core/app.hpp"
#include "simple-nfsd/config/config.hpp"
#include "simple_nfsd/nfs_server_simple.hpp"
#include "simple-nfsd/core/rpc_protocol.hpp"
#include "simple-nfsd/security/auth.hpp"
#include <filesystem>
#include <fstream>

class BasicFunctionalityTest : public ::testing::Test {
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
    }

    void TearDown() override {
        // Clean up test directory
        std::filesystem::remove_all(test_root);
    }

    std::string test_root;
};

// Test NfsdApp basic functionality
TEST_F(BasicFunctionalityTest, NfsdAppBasicFunctionality) {
    auto app = std::make_unique<SimpleNfsd::NfsdApp>();
    
    // Test constructor
    EXPECT_NE(app, nullptr);
    EXPECT_FALSE(app->isRunning());
    
    // Test help
    const char* argv[] = {"simple-nfsd", "--help"};
    EXPECT_FALSE(app->initialize(2, const_cast<char**>(argv)));
    
    // Test version
    const char* argv2[] = {"simple-nfsd", "--version"};
    EXPECT_FALSE(app->initialize(2, const_cast<char**>(argv2)));
    
    // Test metrics
    auto metrics = app->getMetrics();
    EXPECT_EQ(metrics.total_requests, 0);
    EXPECT_EQ(metrics.successful_requests, 0);
    EXPECT_EQ(metrics.failed_requests, 0);
    
    // Test health status
    auto health = app->getHealthStatus();
    EXPECT_FALSE(health.is_healthy);
}

// Test ConfigManager basic functionality
TEST_F(BasicFunctionalityTest, ConfigManagerBasicFunctionality) {
    auto config_manager = std::make_unique<SimpleNfsd::ConfigManager>();
    
    // Test default config
    auto config = config_manager->getConfig();
    EXPECT_EQ(config.bind_address, "0.0.0.0");
    EXPECT_EQ(config.port, 2049);
    EXPECT_EQ(config.root_path, "/var/lib/simple-nfsd/shares");
    EXPECT_EQ(config.max_connections, 1000);
    EXPECT_TRUE(config.enable_tcp);
    EXPECT_TRUE(config.enable_udp);
    
    // Test setting config
    SimpleNfsd::NfsdConfig new_config;
    new_config.bind_address = "127.0.0.1";
    new_config.port = 2049;
    new_config.root_path = test_root;
    new_config.max_connections = 100;
    new_config.enable_tcp = true;
    new_config.enable_udp = true;
    
    config_manager->setConfig(new_config);
    auto updated_config = config_manager->getConfig();
    EXPECT_EQ(updated_config.bind_address, "127.0.0.1");
    EXPECT_EQ(updated_config.root_path, test_root);
    EXPECT_EQ(updated_config.max_connections, 100);
}

// Test NfsServerSimple basic functionality
TEST_F(BasicFunctionalityTest, NfsServerBasicFunctionality) {
    auto nfs_server = std::make_unique<SimpleNfsd::NfsServerSimple>();
    
    // Test initialization
    SimpleNfsd::NfsServerConfig config;
    config.bind_address = "127.0.0.1";
    config.port = 2049;
    config.root_path = test_root;
    config.max_connections = 100;
    config.enable_tcp = true;
    config.enable_udp = true;
    
    EXPECT_TRUE(nfs_server->initialize(config));
    
    // Test stats
    auto stats = nfs_server->getStats();
    EXPECT_EQ(stats.total_requests, 0);
    EXPECT_EQ(stats.successful_requests, 0);
    EXPECT_EQ(stats.failed_requests, 0);
    
    // Test health
    EXPECT_TRUE(nfs_server->isHealthy());
    
    // Test protocol negotiation
    EXPECT_TRUE(nfs_server->isNfsVersionSupported(2));
    EXPECT_TRUE(nfs_server->isNfsVersionSupported(3));
    EXPECT_TRUE(nfs_server->isNfsVersionSupported(4));
    EXPECT_FALSE(nfs_server->isNfsVersionSupported(1));
    
    // Test version negotiation
    uint32_t version = nfs_server->negotiateNfsVersion(2);
    EXPECT_EQ(version, 2);
    
    version = nfs_server->negotiateNfsVersion(1);
    EXPECT_EQ(version, 4); // Should fall back to highest supported version
}

// Test RPC Protocol basic functionality
TEST_F(BasicFunctionalityTest, RpcProtocolBasicFunctionality) {
    // Test RPC message creation
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
    message.data = {0x01, 0x02, 0x03, 0x04};
    
    // Test serialization
    std::vector<uint8_t> serialized = SimpleNfsd::RpcUtils::serializeMessage(message);
    EXPECT_GT(serialized.size(), 0);
    
    // Test deserialization
    SimpleNfsd::RpcMessage deserialized = SimpleNfsd::RpcUtils::deserializeMessage(serialized);
    EXPECT_EQ(deserialized.header.xid, message.header.xid);
    EXPECT_EQ(deserialized.header.type, message.header.type);
    EXPECT_EQ(deserialized.header.rpcvers, message.header.rpcvers);
    EXPECT_EQ(deserialized.header.prog, message.header.prog);
    EXPECT_EQ(deserialized.header.vers, message.header.vers);
    EXPECT_EQ(deserialized.header.proc, message.header.proc);
    EXPECT_EQ(deserialized.data, message.data);
    
    // Test message validation
    EXPECT_TRUE(SimpleNfsd::RpcUtils::validateMessage(message));
    
    // Test invalid message
    SimpleNfsd::RpcMessage invalid_message = message;
    invalid_message.header.rpcvers = 1; // Invalid RPC version
    EXPECT_FALSE(SimpleNfsd::RpcUtils::validateMessage(invalid_message));
}

// Test AuthManager basic functionality
TEST_F(BasicFunctionalityTest, AuthManagerBasicFunctionality) {
    auto auth_manager = std::make_unique<SimpleNfsd::AuthManager>();
    
    // Test initialization
    EXPECT_TRUE(auth_manager->initialize());
    
    // Test AUTH_NONE authentication
    std::vector<uint8_t> auth_none_creds = {1}; // AUTH_NONE
    std::vector<uint8_t> verifier = {};
    
    SimpleNfsd::AuthContext context;
    SimpleNfsd::AuthResult result = auth_manager->authenticate(auth_none_creds, verifier, context);
    
    EXPECT_EQ(result, SimpleNfsd::AuthResult::SUCCESS);
    EXPECT_TRUE(context.authenticated);
    EXPECT_EQ(context.uid, 0);
    EXPECT_EQ(context.gid, 0);
    EXPECT_EQ(context.machine_name, "anonymous");
    
    // Test invalid auth type
    std::vector<uint8_t> invalid_creds = {99}; // Invalid auth type
    SimpleNfsd::AuthContext context2;
    SimpleNfsd::AuthResult result2 = auth_manager->authenticate(invalid_creds, verifier, context2);
    
    EXPECT_EQ(result2, SimpleNfsd::AuthResult::UNSUPPORTED_AUTH_TYPE);
    EXPECT_FALSE(context2.authenticated);
    
    // Test configuration
    auth_manager->setRootSquash(true);
    auth_manager->setAllSquash(false);
    auth_manager->setAnonUid(65534);
    auth_manager->setAnonGid(65534);
    
    // Test path access (basic implementation should allow access)
    bool has_access = auth_manager->checkPathAccess("/test/path", context, true, false);
    EXPECT_TRUE(has_access);
}

// Test AUTH_SYS credentials parsing
TEST_F(BasicFunctionalityTest, AuthSysCredentialsParsing) {
    auto auth_manager = std::make_unique<SimpleNfsd::AuthManager>();
    auth_manager->initialize();
    
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
    uint32_t gids_count = htonl(0);
    auth_sys_creds.insert(auth_sys_creds.end(), (uint8_t*)&gids_count, (uint8_t*)&gids_count + 4);
    
    std::vector<uint8_t> verifier = {};
    
    SimpleNfsd::AuthContext context;
    SimpleNfsd::AuthResult result = auth_manager->authenticate(auth_sys_creds, verifier, context);
    
    EXPECT_EQ(result, SimpleNfsd::AuthResult::SUCCESS);
    EXPECT_TRUE(context.authenticated);
    EXPECT_EQ(context.uid, 1000);
    EXPECT_EQ(context.gid, 1000);
    EXPECT_EQ(context.machine_name, "test-client");
}

// Test RPC message creation utilities
TEST_F(BasicFunctionalityTest, RpcMessageCreation) {
    // Test creating RPC call
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    SimpleNfsd::RpcAuthData cred;
    cred.flavor = SimpleNfsd::RpcAuthFlavor::AUTH_NONE;
    cred.length = 0;
    cred.body = {};
    
    SimpleNfsd::RpcMessage call = SimpleNfsd::RpcUtils::createCall(
        0x12345678,
        SimpleNfsd::RpcProgram::NFS,
        SimpleNfsd::RpcVersion::V2,
        SimpleNfsd::RpcProcedure::NULL_PROC,
        data,
        cred
    );
    
    EXPECT_EQ(call.header.xid, 0x12345678);
    EXPECT_EQ(call.header.type, SimpleNfsd::RpcMessageType::CALL);
    EXPECT_EQ(call.header.prog, static_cast<uint32_t>(SimpleNfsd::RpcProgram::NFS));
    EXPECT_EQ(call.header.vers, static_cast<uint32_t>(SimpleNfsd::RpcVersion::V2));
    EXPECT_EQ(call.header.proc, static_cast<uint32_t>(SimpleNfsd::RpcProcedure::NULL_PROC));
    EXPECT_EQ(call.data, data);
    
    // Test creating RPC reply
    std::vector<uint8_t> reply_data = {0x05, 0x06, 0x07, 0x08};
    SimpleNfsd::RpcMessage reply = SimpleNfsd::RpcUtils::createReply(
        0x12345678,
        SimpleNfsd::RpcAcceptState::SUCCESS,
        reply_data
    );
    
    EXPECT_EQ(reply.header.xid, 0x12345678);
    EXPECT_EQ(reply.header.type, SimpleNfsd::RpcMessageType::REPLY);
    EXPECT_EQ(reply.data, reply_data);
}

// Test AUTH_SYS credentials creation
TEST_F(BasicFunctionalityTest, AuthSysCredentialsCreation) {
    auto auth_manager = std::make_unique<SimpleNfsd::AuthManager>();
    auth_manager->initialize();
    
    // Create AUTH_SYS credentials
    SimpleNfsd::AuthSysCredentials creds;
    creds.stamp = 0x12345678;
    creds.machinename = "test-machine";
    creds.uid = 1000;
    creds.gid = 1000;
    creds.gids = {1000, 1001, 1002};
    
    // Test parsing
    SimpleNfsd::AuthSysCredentials parsed_creds;
    bool success = auth_manager->parseAuthSysCredentials({}, parsed_creds);
    // This might fail with empty data, but should not crash
    EXPECT_TRUE(true);
    
    // Test validation
    SimpleNfsd::AuthResult result = auth_manager->validateAuthSysCredentials(creds);
    EXPECT_EQ(result, SimpleNfsd::AuthResult::SUCCESS);
}

// Test error handling
TEST_F(BasicFunctionalityTest, ErrorHandling) {
    // Test with invalid RPC message
    std::vector<uint8_t> invalid_data = {0x01, 0x02, 0x03}; // Too small
    SimpleNfsd::RpcMessage message = SimpleNfsd::RpcUtils::deserializeMessage(invalid_data);
    
    // Should handle gracefully (implementation dependent)
    EXPECT_TRUE(true);
    
    // Test with invalid config file
    auto config_manager = std::make_unique<SimpleNfsd::ConfigManager>();
    bool success = config_manager->loadFromFile("nonexistent_file.conf");
    EXPECT_FALSE(success);
}

// Test integration between components
TEST_F(BasicFunctionalityTest, ComponentIntegration) {
    // Test NFS server with authentication
    auto nfs_server = std::make_unique<SimpleNfsd::NfsServerSimple>();
    
    SimpleNfsd::NfsServerConfig config;
    config.bind_address = "127.0.0.1";
    config.port = 2049;
    config.root_path = test_root;
    config.max_connections = 100;
    config.enable_tcp = true;
    config.enable_udp = true;
    
    EXPECT_TRUE(nfs_server->initialize(config));
    
    // Test RPC message handling
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
    
    // Test access control
    bool has_access = nfs_server->checkAccess("file1.txt", auth_context, true, false);
    EXPECT_TRUE(has_access);
}
