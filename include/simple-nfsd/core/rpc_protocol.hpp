/**
 * @file rpc_protocol.hpp
 * @brief RPC (Remote Procedure Call) protocol definitions
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#ifndef SIMPLE_NFSD_RPC_PROTOCOL_HPP
#define SIMPLE_NFSD_RPC_PROTOCOL_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace SimpleNfsd {

// RPC Message Types
enum class RpcMessageType : uint32_t {
    CALL = 0,
    REPLY = 1
};

// RPC Reply States
enum class RpcReplyState : uint32_t {
    MSG_ACCEPTED = 0,
    MSG_DENIED = 1
};

// RPC Accept States
enum class RpcAcceptState : uint32_t {
    SUCCESS = 0,
    PROG_UNAVAIL = 1,
    PROG_MISMATCH = 2,
    PROC_UNAVAIL = 3,
    GARBAGE_ARGS = 4,
    SYSTEM_ERR = 5
};

// RPC Reject States
enum class RpcRejectState : uint32_t {
    RPC_MISMATCH = 0,
    AUTH_ERROR = 1
};

// RPC Authentication Flavors
enum class RpcAuthFlavor : uint32_t {
    AUTH_NONE = 0,
    AUTH_SYS = 1,
    AUTH_SHORT = 2,
    AUTH_DH = 3,
    RPCSEC_GSS = 6
};

// RPC Program Numbers
enum class RpcProgram : uint32_t {
    PORTMAP = 100000,
    NFS = 100003,
    MOUNT = 100005,
    NLM = 100021,
    NSM = 100024
};

// RPC Version Numbers
enum class RpcVersion : uint32_t {
    PORTMAP_V2 = 2,
    NFS_V2 = 2,
    NFS_V3 = 3,
    NFS_V4 = 4,
    MOUNT_V1 = 1,
    MOUNT_V3 = 3,
    NLM_V1 = 1,
    NLM_V4 = 4
};

// RPC Procedure Numbers
enum class RpcProcedure : uint32_t {
    // Portmapper procedures
    PMAP_NULL = 0,
    PMAP_SET = 1,
    PMAP_UNSET = 2,
    PMAP_GETPORT = 3,
    PMAP_DUMP = 4,
    PMAP_CALLIT = 5,
    
    // NFS procedures
    NFSPROC_NULL = 0,
    NFSPROC_GETATTR = 1,
    NFSPROC_SETATTR = 2,
    NFSPROC_ROOT = 3,
    NFSPROC_LOOKUP = 4,
    NFSPROC_READLINK = 5,
    NFSPROC_READ = 6,
    NFSPROC_WRITECACHE = 7,
    NFSPROC_WRITE = 8,
    NFSPROC_CREATE = 9,
    NFSPROC_REMOVE = 10,
    NFSPROC_RENAME = 11,
    NFSPROC_LINK = 12,
    NFSPROC_SYMLINK = 13,
    NFSPROC_MKDIR = 14,
    NFSPROC_RMDIR = 15,
    NFSPROC_READDIR = 16,
    NFSPROC_STATFS = 17,
    
    // NFSv3 procedures (additional to NFSv2)
    NFSPROC_FSSTAT = 18,
    NFSPROC_FSINFO = 19,
    NFSPROC_PATHCONF = 20,
    NFSPROC_COMMIT = 21,
    
    // NFSv4 procedures (additional to NFSv3)
    NFSPROC_GETATTR_V4 = 1,
    NFSPROC_SETATTR_V4 = 2,
    NFSPROC_LOOKUP_V4 = 3,
    NFSPROC_ACCESS_V4 = 4,
    NFSPROC_READLINK_V4 = 5,
    NFSPROC_READ_V4 = 6,
    NFSPROC_WRITE_V4 = 7,
    NFSPROC_CREATE_V4 = 8,
    NFSPROC_MKDIR_V4 = 9,
    NFSPROC_SYMLINK_V4 = 10,
    NFSPROC_MKNOD_V4 = 11,
    NFSPROC_REMOVE_V4 = 12,
    NFSPROC_RMDIR_V4 = 13,
    NFSPROC_RENAME_V4 = 14,
    NFSPROC_LINK_V4 = 15,
    NFSPROC_READDIR_V4 = 16,
    NFSPROC_READDIRPLUS_V4 = 17,
    NFSPROC_FSSTAT_V4 = 18,
    NFSPROC_FSINFO_V4 = 19,
    NFSPROC_PATHCONF_V4 = 20,
    NFSPROC_COMMIT_V4 = 21,
    NFSPROC_DELEGRETURN_V4 = 22,
    NFSPROC_GETACL_V4 = 23,
    NFSPROC_SETACL_V4 = 24,
    NFSPROC_FS_LOCATIONS_V4 = 25,
    NFSPROC_RELEASE_LOCKOWNER_V4 = 26,
    NFSPROC_SECINFO_V4 = 27,
    NFSPROC_FSID_PRESENT_V4 = 28,
    NFSPROC_EXCHANGE_ID_V4 = 29,
    NFSPROC_CREATE_SESSION_V4 = 30,
    NFSPROC_DESTROY_SESSION_V4 = 31,
    NFSPROC_SEQUENCE_V4 = 32,
    NFSPROC_GET_DEVICE_INFO_V4 = 33,
    NFSPROC_BIND_CONN_TO_SESSION_V4 = 34,
    NFSPROC_DESTROY_CLIENTID_V4 = 35,
    NFSPROC_RECLAIM_COMPLETE_V4 = 36,
            NFSPROC_ILLEGAL_V4 = 10044,
            
            // NFSv4 specific procedures (using procedure numbers)
            NFSPROC_COMPOUND = 1
        };

// RPC Authentication Data
struct RpcAuthData {
    RpcAuthFlavor flavor;
    std::vector<uint8_t> data;
    
    RpcAuthData() : flavor(RpcAuthFlavor::AUTH_NONE) {}
    RpcAuthData(RpcAuthFlavor f, const std::vector<uint8_t>& d) 
        : flavor(f), data(d) {}
};

// RPC Opaque Authentication
struct RpcOpaqueAuth {
    RpcAuthFlavor flavor;
    uint32_t length;
    std::vector<uint8_t> body;
    
    RpcOpaqueAuth() : flavor(RpcAuthFlavor::AUTH_NONE), length(0) {}
};

// RPC Call Body
struct RpcCallBody {
    uint32_t rpcvers;
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;
    RpcOpaqueAuth cred;
    RpcOpaqueAuth verf;
};

// RPC Reply Body
struct RpcReplyBody {
    uint32_t rpcvers;
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;
    RpcReplyState state;
    RpcOpaqueAuth verf;
    
    // Union for different reply states
    union {
        struct {
            RpcAcceptState accept_state;
            // Additional fields based on accept state
        } accepted;
        struct {
            RpcRejectState reject_state;
            // Additional fields based on reject state
        } rejected;
    } data;
};

// RPC Message Header
struct RpcMessageHeader {
    uint32_t xid;           // Transaction ID
    RpcMessageType type;    // CALL or REPLY
    uint32_t rpcvers;       // RPC version (always 2)
    uint32_t prog;          // Program number
    uint32_t vers;          // Program version
    uint32_t proc;          // Procedure number
    RpcOpaqueAuth cred;     // Credentials
    RpcOpaqueAuth verf;     // Verifier
};

// RPC Message
struct RpcMessage {
    RpcMessageHeader header;
    std::vector<uint8_t> data;
    
    RpcMessage() : header{} {}
};

// AUTH_SYS Credentials
struct AuthSysCredentials {
    uint32_t stamp;
    std::string machinename;
    uint32_t uid;
    uint32_t gid;
    std::vector<uint32_t> gids;
    
    AuthSysCredentials() : stamp(0), uid(0), gid(0) {}
};

// RPC Error Codes
enum class RpcError : uint32_t {
    SUCCESS = 0,
    PROG_UNAVAIL = 1,
    PROG_MISMATCH = 2,
    PROC_UNAVAIL = 3,
    GARBAGE_ARGS = 4,
    SYSTEM_ERR = 5,
    RPC_MISMATCH = 6,
    AUTH_ERROR = 7
};

// RPC Server Interface
class RpcServer {
public:
    virtual ~RpcServer() = default;
    
    // Handle incoming RPC message
    virtual std::vector<uint8_t> handleMessage(const RpcMessage& message) = 0;
    
    // Register program and version
    virtual bool registerProgram(RpcProgram program, RpcVersion version, uint16_t port) = 0;
    
    // Unregister program
    virtual bool unregisterProgram(RpcProgram program, RpcVersion version) = 0;
    
    // Get program port
    virtual uint16_t getProgramPort(RpcProgram program, RpcVersion version) = 0;
};

// RPC Client Interface
class RpcClient {
public:
    virtual ~RpcClient() = default;
    
    // Send RPC call and get reply
    virtual std::vector<uint8_t> call(RpcProgram program, RpcVersion version, 
                                     RpcProcedure procedure, 
                                     const std::vector<uint8_t>& data) = 0;
    
    // Set authentication
    virtual void setAuth(const RpcAuthData& auth) = 0;
};

// RPC Utility Functions
class RpcUtils {
public:
    // Serialize RPC message to bytes
    static std::vector<uint8_t> serializeMessage(const RpcMessage& message);
    
    // Deserialize bytes to RPC message
    static RpcMessage deserializeMessage(const std::vector<uint8_t>& data);
    
    // Create AUTH_SYS credentials
    static RpcAuthData createAuthSys(const AuthSysCredentials& creds);
    
    // Parse AUTH_SYS credentials
    static AuthSysCredentials parseAuthSys(const RpcAuthData& auth);
    
    // Create RPC call message
    static RpcMessage createCall(uint32_t xid, RpcProgram program, RpcVersion version,
                                RpcProcedure procedure, const std::vector<uint8_t>& data,
                                const RpcAuthData& cred = RpcAuthData());
    
    // Create RPC reply message
    static RpcMessage createReply(uint32_t xid, RpcAcceptState state,
                                 const std::vector<uint8_t>& data,
                                 const RpcAuthData& verf = RpcAuthData());
    
    // Create RPC error reply
    static RpcMessage createErrorReply(uint32_t xid, RpcError error);
    
    // Validate RPC message
    static bool validateMessage(const RpcMessage& message);
    
    // Get RPC message size
    static size_t getMessageSize(const RpcMessage& message);
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_RPC_PROTOCOL_HPP
