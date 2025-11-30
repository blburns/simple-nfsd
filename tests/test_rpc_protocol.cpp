/**
 * @file test_rpc_protocol.cpp
 * @brief Unit tests for RPC protocol implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include <gtest/gtest.h>
#include "simple-nfsd/core/rpc_protocol.hpp"
#include <vector>
#include <cstring>

class RpcProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test data
        test_data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    }

    void TearDown() override {
        // Clean up
    }

    std::vector<uint8_t> test_data;
};

TEST_F(RpcProtocolTest, RpcMessageHeaderSerialization) {
    RpcMessageHeader header;
    header.xid = 0x12345678;
    header.type = RpcMessageType::CALL;
    header.rpcvers = 2;
    header.prog = static_cast<uint32_t>(RpcProgram::NFS);
    header.vers = 2;
    header.proc = 1;
    header.cred.flavor = RpcAuthFlavor::AUTH_SYS;
    header.cred.length = 0;
    header.cred.body = {};
    header.verf.flavor = RpcAuthFlavor::AUTH_NONE;
    header.verf.length = 0;
    header.verf.body = {};

    std::vector<uint8_t> serialized = RpcUtils::serializeMessageHeader(header);
    
    // Verify header size (should be 24 bytes for basic header)
    EXPECT_GE(serialized.size(), 24);
    
    // Verify XID is in network byte order
    uint32_t xid;
    memcpy(&xid, serialized.data(), 4);
    EXPECT_EQ(ntohl(xid), 0x12345678);
}

TEST_F(RpcProtocolTest, RpcMessageHeaderDeserialization) {
    // Create a test header
    RpcMessageHeader original_header;
    original_header.xid = 0x12345678;
    original_header.type = RpcMessageType::CALL;
    original_header.rpcvers = 2;
    original_header.prog = static_cast<uint32_t>(RpcProgram::NFS);
    original_header.vers = 2;
    original_header.proc = 1;
    original_header.cred.flavor = RpcAuthFlavor::AUTH_SYS;
    original_header.cred.length = 0;
    original_header.cred.body = {};
    original_header.verf.flavor = RpcAuthFlavor::AUTH_NONE;
    original_header.verf.length = 0;
    original_header.verf.body = {};

    std::vector<uint8_t> serialized = RpcUtils::serializeMessageHeader(original_header);
    RpcMessageHeader deserialized_header = RpcUtils::deserializeMessageHeader(serialized);

    EXPECT_EQ(deserialized_header.xid, original_header.xid);
    EXPECT_EQ(deserialized_header.type, original_header.type);
    EXPECT_EQ(deserialized_header.rpcvers, original_header.rpcvers);
    EXPECT_EQ(deserialized_header.prog, original_header.prog);
    EXPECT_EQ(deserialized_header.vers, original_header.vers);
    EXPECT_EQ(deserialized_header.proc, original_header.proc);
    EXPECT_EQ(deserialized_header.cred.flavor, original_header.cred.flavor);
    EXPECT_EQ(deserialized_header.verf.flavor, original_header.verf.flavor);
}

TEST_F(RpcProtocolTest, RpcMessageSerialization) {
    RpcMessage message;
    message.header.xid = 0x12345678;
    message.header.type = RpcMessageType::CALL;
    message.header.rpcvers = 2;
    message.header.prog = static_cast<uint32_t>(RpcProgram::NFS);
    message.header.vers = 2;
    message.header.proc = 1;
    message.header.cred.flavor = RpcAuthFlavor::AUTH_SYS;
    message.header.cred.length = 0;
    message.header.cred.body = {};
    message.header.verf.flavor = RpcAuthFlavor::AUTH_NONE;
    message.header.verf.length = 0;
    message.header.verf.body = {};
    message.data = test_data;

    std::vector<uint8_t> serialized = RpcUtils::serializeMessage(message);
    EXPECT_GT(serialized.size(), 0);
}

TEST_F(RpcProtocolTest, RpcMessageDeserialization) {
    RpcMessage original_message;
    original_message.header.xid = 0x12345678;
    original_message.header.type = RpcMessageType::CALL;
    original_message.header.rpcvers = 2;
    original_message.header.prog = static_cast<uint32_t>(RpcProgram::NFS);
    original_message.header.vers = 2;
    original_message.header.proc = 1;
    original_message.header.cred.flavor = RpcAuthFlavor::AUTH_SYS;
    original_message.header.cred.length = 0;
    original_message.header.cred.body = {};
    original_message.header.verf.flavor = RpcAuthFlavor::AUTH_NONE;
    original_message.header.verf.length = 0;
    original_message.header.verf.body = {};
    original_message.data = test_data;

    std::vector<uint8_t> serialized = RpcUtils::serializeMessage(original_message);
    RpcMessage deserialized_message = RpcUtils::deserializeMessage(serialized);

    EXPECT_EQ(deserialized_message.header.xid, original_message.header.xid);
    EXPECT_EQ(deserialized_message.header.type, original_message.header.type);
    EXPECT_EQ(deserialized_message.header.rpcvers, original_message.header.rpcvers);
    EXPECT_EQ(deserialized_message.header.prog, original_message.header.prog);
    EXPECT_EQ(deserialized_message.header.vers, original_message.header.vers);
    EXPECT_EQ(deserialized_message.header.proc, original_message.header.proc);
    EXPECT_EQ(deserialized_message.data, original_message.data);
}

TEST_F(RpcProtocolTest, RpcMessageValidation) {
    RpcMessage valid_message;
    valid_message.header.xid = 0x12345678;
    valid_message.header.type = RpcMessageType::CALL;
    valid_message.header.rpcvers = 2;
    valid_message.header.prog = static_cast<uint32_t>(RpcProgram::NFS);
    valid_message.header.vers = 2;
    valid_message.header.proc = 1;
    valid_message.header.cred.flavor = RpcAuthFlavor::AUTH_SYS;
    valid_message.header.cred.length = 0;
    valid_message.header.cred.body = {};
    valid_message.header.verf.flavor = RpcAuthFlavor::AUTH_NONE;
    valid_message.header.verf.length = 0;
    valid_message.header.verf.body = {};
    valid_message.data = test_data;

    EXPECT_TRUE(RpcUtils::validateMessage(valid_message));

    // Test invalid RPC version
    RpcMessage invalid_message = valid_message;
    invalid_message.header.rpcvers = 1; // Invalid RPC version
    EXPECT_FALSE(RpcUtils::validateMessage(invalid_message));
}

TEST_F(RpcProtocolTest, CreateReply) {
    RpcMessage original_message;
    original_message.header.xid = 0x12345678;
    original_message.header.type = RpcMessageType::CALL;
    original_message.header.rpcvers = 2;
    original_message.header.prog = static_cast<uint32_t>(RpcProgram::NFS);
    original_message.header.vers = 2;
    original_message.header.proc = 1;
    original_message.header.cred.flavor = RpcAuthFlavor::AUTH_SYS;
    original_message.header.cred.length = 0;
    original_message.header.cred.body = {};
    original_message.header.verf.flavor = RpcAuthFlavor::AUTH_NONE;
    original_message.header.verf.length = 0;
    original_message.header.verf.body = {};
    original_message.data = test_data;

    std::vector<uint8_t> reply_data = {0x01, 0x02, 0x03, 0x04};
    RpcMessage reply = RpcUtils::createReply(original_message.header.xid, RpcAcceptState::SUCCESS, reply_data);

    EXPECT_EQ(reply.header.xid, original_message.header.xid);
    EXPECT_EQ(reply.header.type, RpcMessageType::REPLY);
    EXPECT_EQ(reply.data, reply_data);
}

TEST_F(RpcProtocolTest, AuthSysCredentialsSerialization) {
    AuthSysCredentials creds;
    creds.stamp = 0x12345678;
    creds.machinename = "test-machine";
    creds.uid = 1000;
    creds.gid = 1000;
    creds.gids = {1000, 1001, 1002};

    std::vector<uint8_t> serialized = RpcUtils::serializeAuthSysCredentials(creds);
    EXPECT_GT(serialized.size(), 0);

    // Verify machine name is included
    std::string serialized_str(reinterpret_cast<const char*>(serialized.data()), serialized.size());
    EXPECT_NE(serialized_str.find("test-machine"), std::string::npos);
}

TEST_F(RpcProtocolTest, AuthSysCredentialsDeserialization) {
    AuthSysCredentials original_creds;
    original_creds.stamp = 0x12345678;
    original_creds.machinename = "test-machine";
    original_creds.uid = 1000;
    original_creds.gid = 1000;
    original_creds.gids = {1000, 1001, 1002};

    std::vector<uint8_t> serialized = RpcUtils::serializeAuthSysCredentials(original_creds);
    AuthSysCredentials deserialized_creds = RpcUtils::deserializeAuthSysCredentials(serialized);

    EXPECT_EQ(deserialized_creds.stamp, original_creds.stamp);
    EXPECT_EQ(deserialized_creds.machinename, original_creds.machinename);
    EXPECT_EQ(deserialized_creds.uid, original_creds.uid);
    EXPECT_EQ(deserialized_creds.gid, original_creds.gid);
    EXPECT_EQ(deserialized_creds.gids, original_creds.gids);
}

TEST_F(RpcProtocolTest, XdrUtils) {
    // Test 32-bit integer encoding
    uint32_t value32 = 0x12345678;
    std::vector<uint8_t> encoded32 = RpcUtils::encodeXdrUint32(value32);
    EXPECT_EQ(encoded32.size(), 4);
    
    uint32_t decoded32 = RpcUtils::decodeXdrUint32(encoded32);
    EXPECT_EQ(decoded32, value32);

    // Test 64-bit integer encoding
    uint64_t value64 = 0x123456789ABCDEF0;
    std::vector<uint8_t> encoded64 = RpcUtils::encodeXdrUint64(value64);
    EXPECT_EQ(encoded64.size(), 8);
    
    uint64_t decoded64 = RpcUtils::decodeXdrUint64(encoded64);
    EXPECT_EQ(decoded64, value64);

    // Test string encoding
    std::string test_string = "hello world";
    std::vector<uint8_t> encoded_string = RpcUtils::encodeXdrString(test_string);
    EXPECT_GT(encoded_string.size(), test_string.size()); // Should include length prefix
    
    std::string decoded_string = RpcUtils::decodeXdrString(encoded_string);
    EXPECT_EQ(decoded_string, test_string);
}

TEST_F(RpcProtocolTest, ErrorHandling) {
    // Test deserialization with insufficient data
    std::vector<uint8_t> insufficient_data = {0x01, 0x02, 0x03}; // Too small
    RpcMessage message = RpcUtils::deserializeMessage(insufficient_data);
    
    // Should handle gracefully (implementation dependent)
    // This test verifies the function doesn't crash
    EXPECT_TRUE(true);
}

TEST_F(RpcProtocolTest, MessageTypes) {
    // Test different message types
    RpcMessage call_message;
    call_message.header.type = RpcMessageType::CALL;
    EXPECT_TRUE(RpcUtils::validateMessage(call_message));

    RpcMessage reply_message;
    reply_message.header.type = RpcMessageType::REPLY;
    EXPECT_TRUE(RpcUtils::validateMessage(reply_message));
}

TEST_F(RpcProtocolTest, ProgramTypes) {
    // Test different RPC programs
    RpcMessage nfs_message;
    nfs_message.header.prog = static_cast<uint32_t>(RpcProgram::NFS);
    EXPECT_TRUE(RpcUtils::validateMessage(nfs_message));

    RpcMessage portmap_message;
    portmap_message.header.prog = static_cast<uint32_t>(RpcProgram::PORTMAP);
    EXPECT_TRUE(RpcUtils::validateMessage(portmap_message));
}
