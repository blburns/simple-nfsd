/**
 * @file rpc_protocol.cpp
 * @brief RPC (Remote Procedure Call) protocol implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include "simple-nfsd/core/rpc_protocol.hpp"
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace SimpleNfsd {

// Helper function to read uint32_t from byte array
static uint32_t readUint32(const uint8_t* data, size_t& offset) {
    uint32_t value = 0;
    value |= (static_cast<uint32_t>(data[offset]) << 24);
    value |= (static_cast<uint32_t>(data[offset + 1]) << 16);
    value |= (static_cast<uint32_t>(data[offset + 2]) << 8);
    value |= static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return value;
}

// Helper function to write uint32_t to byte array
static void writeUint32(uint8_t* data, size_t& offset, uint32_t value) {
    data[offset] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>(value & 0xFF);
    offset += 4;
}

// Helper function to align to 4-byte boundary
static size_t align4(size_t offset) {
    return (offset + 3) & ~3;
}

std::vector<uint8_t> RpcUtils::serializeMessage(const RpcMessage& message) {
    std::vector<uint8_t> result;
    
    // Calculate total size needed
    size_t totalSize = 4 * 7; // Header fields (28 bytes)
    totalSize += 4 + message.header.cred.length; // Credentials (4 + length)
    totalSize = align4(totalSize);
    totalSize += 4 + message.header.verf.length; // Verifier (4 + length)
    totalSize = align4(totalSize);
    totalSize += message.data.size(); // Data
    
    result.resize(totalSize);
    
    size_t offset = 0;
    
    // Serialize header
    writeUint32(result.data(), offset, message.header.xid);
    writeUint32(result.data(), offset, static_cast<uint32_t>(message.header.type));
    writeUint32(result.data(), offset, message.header.rpcvers);
    writeUint32(result.data(), offset, message.header.prog);
    writeUint32(result.data(), offset, message.header.vers);
    writeUint32(result.data(), offset, message.header.proc);
    
    // Serialize credentials
    writeUint32(result.data(), offset, static_cast<uint32_t>(message.header.cred.flavor));
    writeUint32(result.data(), offset, message.header.cred.length);
    if (message.header.cred.length > 0) {
        memcpy(result.data() + offset, message.header.cred.body.data(), message.header.cred.length);
        offset += message.header.cred.length;
    }
    
    // Align to 4-byte boundary
    offset = align4(offset);
    
    // Serialize verifier
    writeUint32(result.data(), offset, static_cast<uint32_t>(message.header.verf.flavor));
    writeUint32(result.data(), offset, message.header.verf.length);
    if (message.header.verf.length > 0) {
        memcpy(result.data() + offset, message.header.verf.body.data(), message.header.verf.length);
        offset += message.header.verf.length;
    }
    
    // Align to 4-byte boundary
    offset = align4(offset);
    
    // Serialize data
    if (!message.data.empty()) {
        memcpy(result.data() + offset, message.data.data(), message.data.size());
    }
    
    return result;
}

RpcMessage RpcUtils::deserializeMessage(const std::vector<uint8_t>& data) {
    RpcMessage message;
    size_t offset = 0;
    
    if (data.size() < 28) { // Minimum RPC message size
        throw std::runtime_error("Invalid RPC message: too short");
    }
    
    // Deserialize header
    message.header.xid = readUint32(data.data(), offset);
    message.header.type = static_cast<RpcMessageType>(readUint32(data.data(), offset));
    message.header.rpcvers = readUint32(data.data(), offset);
    message.header.prog = readUint32(data.data(), offset);
    message.header.vers = readUint32(data.data(), offset);
    message.header.proc = readUint32(data.data(), offset);
    
    // Deserialize credentials
    message.header.cred.flavor = static_cast<RpcAuthFlavor>(readUint32(data.data(), offset));
    message.header.cred.length = readUint32(data.data(), offset);
    
    if (message.header.cred.length > 0) {
        if (offset + message.header.cred.length > data.size()) {
            throw std::runtime_error("Invalid RPC message: credentials too long");
        }
        message.header.cred.body.assign(data.begin() + offset, 
                                       data.begin() + offset + message.header.cred.length);
        offset += message.header.cred.length;
    }
    
    // Align to 4-byte boundary
    offset = align4(offset);
    
    // Deserialize verifier
    message.header.verf.flavor = static_cast<RpcAuthFlavor>(readUint32(data.data(), offset));
    message.header.verf.length = readUint32(data.data(), offset);
    
    if (message.header.verf.length > 0) {
        if (offset + message.header.verf.length > data.size()) {
            throw std::runtime_error("Invalid RPC message: verifier too long");
        }
        message.header.verf.body.assign(data.begin() + offset, 
                                       data.begin() + offset + message.header.verf.length);
        offset += message.header.verf.length;
    }
    
    // Align to 4-byte boundary
    offset = align4(offset);
    
    // Deserialize data
    if (offset < data.size()) {
        message.data.assign(data.begin() + offset, data.end());
    }
    
    return message;
}

RpcAuthData RpcUtils::createAuthSys(const AuthSysCredentials& creds) {
    RpcAuthData auth;
    auth.flavor = RpcAuthFlavor::AUTH_SYS;
    
    // Serialize AUTH_SYS credentials
    std::vector<uint8_t> data;
    size_t offset = 0;
    
    // Calculate total size
    size_t totalSize = 4; // stamp
    totalSize += 4 + creds.machinename.length(); // machinename
    totalSize += 4; // uid
    totalSize += 4; // gid
    totalSize += 4 + creds.gids.size() * 4; // gids array
    
    data.resize(totalSize);
    
    // Serialize stamp
    writeUint32(data.data(), offset, creds.stamp);
    
    // Serialize machinename
    writeUint32(data.data(), offset, creds.machinename.length());
    std::memcpy(data.data() + offset, creds.machinename.c_str(), creds.machinename.length());
    offset += creds.machinename.length();
    offset = align4(offset);
    
    // Serialize uid
    writeUint32(data.data(), offset, creds.uid);
    
    // Serialize gid
    writeUint32(data.data(), offset, creds.gid);
    
    // Serialize gids
    writeUint32(data.data(), offset, creds.gids.size());
    for (uint32_t gid : creds.gids) {
        writeUint32(data.data(), offset, gid);
    }
    
    auth.data = std::move(data);
    return auth;
}

AuthSysCredentials RpcUtils::parseAuthSys(const RpcAuthData& auth) {
    if (auth.flavor != RpcAuthFlavor::AUTH_SYS) {
        throw std::runtime_error("Invalid auth flavor for AUTH_SYS parsing");
    }
    
    AuthSysCredentials creds;
    size_t offset = 0;
    
    if (auth.data.size() < 16) { // Minimum AUTH_SYS size
        throw std::runtime_error("Invalid AUTH_SYS credentials: too short");
    }
    
    // Parse stamp
    creds.stamp = readUint32(auth.data.data(), offset);
    
    // Parse machinename
    uint32_t nameLen = readUint32(auth.data.data(), offset);
    if (offset + nameLen > auth.data.size()) {
        throw std::runtime_error("Invalid AUTH_SYS credentials: machinename too long");
    }
    creds.machinename.assign(reinterpret_cast<const char*>(auth.data.data() + offset), nameLen);
    offset += nameLen;
    offset = align4(offset);
    
    // Parse uid
    creds.uid = readUint32(auth.data.data(), offset);
    
    // Parse gid
    creds.gid = readUint32(auth.data.data(), offset);
    
    // Parse gids
    uint32_t gidCount = readUint32(auth.data.data(), offset);
    creds.gids.reserve(gidCount);
    for (uint32_t i = 0; i < gidCount; ++i) {
        if (offset + 4 > auth.data.size()) {
            throw std::runtime_error("Invalid AUTH_SYS credentials: gids too long");
        }
        creds.gids.push_back(readUint32(auth.data.data(), offset));
    }
    
    return creds;
}

RpcMessage RpcUtils::createCall(uint32_t xid, RpcProgram program, RpcVersion version,
                               RpcProcedure procedure, const std::vector<uint8_t>& data,
                               const RpcAuthData& cred) {
    RpcMessage message;
    message.header.xid = xid;
    message.header.type = RpcMessageType::CALL;
    message.header.rpcvers = 2;
    message.header.prog = static_cast<uint32_t>(program);
    message.header.vers = static_cast<uint32_t>(version);
    message.header.proc = static_cast<uint32_t>(procedure);
    
    // Set credentials
    message.header.cred.flavor = cred.flavor;
    message.header.cred.length = cred.data.size();
    message.header.cred.body = cred.data;
    
    // Set verifier (empty for now)
    message.header.verf.flavor = RpcAuthFlavor::AUTH_NONE;
    message.header.verf.length = 0;
    
    message.data = data;
    return message;
}

RpcMessage RpcUtils::createReply(uint32_t xid, RpcAcceptState state,
                                const std::vector<uint8_t>& data,
                                const RpcAuthData& verf) {
    RpcMessage message;
    message.header.xid = xid;
    message.header.type = RpcMessageType::REPLY;
    message.header.rpcvers = 2;
    message.header.prog = 0; // Not used in replies
    message.header.vers = 0; // Not used in replies
    message.header.proc = 0; // Not used in replies
    
    // Set credentials (empty for replies)
    message.header.cred.flavor = RpcAuthFlavor::AUTH_NONE;
    message.header.cred.length = 0;
    
    // Set verifier
    message.header.verf.flavor = verf.flavor;
    message.header.verf.length = verf.data.size();
    message.header.verf.body = verf.data;
    
    message.data = data;
    return message;
}

RpcMessage RpcUtils::createErrorReply(uint32_t xid, RpcError error) {
    RpcMessage message;
    message.header.xid = xid;
    message.header.type = RpcMessageType::REPLY;
    message.header.rpcvers = 2;
    message.header.prog = 0;
    message.header.vers = 0;
    message.header.proc = 0;
    
    // Set credentials (empty for replies)
    message.header.cred.flavor = RpcAuthFlavor::AUTH_NONE;
    message.header.cred.length = 0;
    
    // Set verifier (empty for error replies)
    message.header.verf.flavor = RpcAuthFlavor::AUTH_NONE;
    message.header.verf.length = 0;
    
    // Set error data
    std::vector<uint8_t> errorData(8);
    size_t offset = 0;
    writeUint32(errorData.data(), offset, static_cast<uint32_t>(RpcReplyState::MSG_DENIED));
    writeUint32(errorData.data(), offset, static_cast<uint32_t>(error));
    message.data = errorData;
    
    return message;
}

bool RpcUtils::validateMessage(const RpcMessage& message) {
    // Check RPC version
    if (message.header.rpcvers != 2) {
        return false;
    }
    
    // Check message type
    if (message.header.type != RpcMessageType::CALL && 
        message.header.type != RpcMessageType::REPLY) {
        return false;
    }
    
    // Check program number
    if (message.header.prog == 0) {
        return false;
    }
    
    // Check version
    if (message.header.vers == 0) {
        return false;
    }
    
    return true;
}

size_t RpcUtils::getMessageSize(const RpcMessage& message) {
    size_t size = 4 * 7; // Header fields
    size += 4 + message.header.cred.length; // Credentials
    size = align4(size);
    size += 4 + message.header.verf.length; // Verifier
    size = align4(size);
    size += message.data.size(); // Data
    return size;
}

} // namespace SimpleNfsd
