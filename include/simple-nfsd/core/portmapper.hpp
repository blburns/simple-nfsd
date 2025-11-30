/**
 * @file portmapper.hpp
 * @brief RPC Portmapper service implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#ifndef SIMPLE_NFSD_PORTMAPPER_HPP
#define SIMPLE_NFSD_PORTMAPPER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include "simple-nfsd/core/rpc_protocol.hpp"

namespace SimpleNfsd {

// Portmapper mapping entry
struct PortmapMapping {
    uint32_t program;
    uint32_t version;
    uint32_t protocol;  // IPPROTO_TCP or IPPROTO_UDP
    uint32_t port;
    std::string owner;  // Process owner of the mapping
    uint64_t timestamp; // When the mapping was created
};

// Portmapper statistics
struct PortmapStats {
    uint64_t total_requests = 0;
    uint64_t successful_requests = 0;
    uint64_t failed_requests = 0;
    uint64_t mappings_registered = 0;
    uint64_t mappings_unregistered = 0;
    uint64_t lookups_performed = 0;
};

class Portmapper {
public:
    Portmapper();
    ~Portmapper();

    // Initialize the portmapper service
    bool initialize();
    
    // Shutdown the portmapper service
    void shutdown();

    // Handle RPC calls to portmapper
    void handleRpcCall(const RpcMessage& message);

    // Portmapper procedures
    void handleNull(const RpcMessage& message);
    void handleSet(const RpcMessage& message);
    void handleUnset(const RpcMessage& message);
    void handleGetPort(const RpcMessage& message);
    void handleDump(const RpcMessage& message);
    void handleCallIt(const RpcMessage& message);

    // Service registration and discovery
    bool registerService(uint32_t program, uint32_t version, uint32_t protocol, uint32_t port, const std::string& owner = "");
    bool unregisterService(uint32_t program, uint32_t version, uint32_t protocol);
    bool unregisterAll(uint32_t program, uint32_t version);
    uint32_t getPort(uint32_t program, uint32_t version, uint32_t protocol) const;
    std::vector<PortmapMapping> getAllMappings() const;
    std::vector<PortmapMapping> getMappingsForProgram(uint32_t program) const;

    // Statistics and monitoring
    PortmapStats getStats() const;
    void resetStats();
    bool isHealthy() const;

    // Configuration
    void setMaxMappings(uint32_t max_mappings);
    void setMappingTimeout(uint64_t timeout_seconds);
    void enableAutoCleanup(bool enable);

private:
    // Internal state
    bool initialized_;
    bool running_;
    mutable std::mutex mappings_mutex_;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, PortmapMapping> mappings_;
    
    // Configuration
    uint32_t max_mappings_;
    uint64_t mapping_timeout_;
    bool auto_cleanup_enabled_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    PortmapStats stats_;

    // Helper methods
    bool isValidProgram(uint32_t program) const;
    bool isValidVersion(uint32_t version) const;
    bool isValidProtocol(uint32_t protocol) const;
    bool isValidPort(uint32_t port) const;
    void cleanupExpiredMappings();
    std::vector<uint8_t> serializeMapping(const PortmapMapping& mapping) const;
    bool deserializeMapping(const std::vector<uint8_t>& data, PortmapMapping& mapping) const;
    
    // RPC response helpers
    RpcMessage createSuccessReply(const RpcMessage& request, const std::vector<uint8_t>& data = {}) const;
    RpcMessage createErrorReply(const RpcMessage& request, RpcAcceptState error_state) const;
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_PORTMAPPER_HPP
