/**
 * @file portmapper.cpp
 * @brief RPC Portmapper service implementation
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include "simple-nfsd/core/portmapper.hpp"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace SimpleNfsd {

Portmapper::Portmapper() 
    : initialized_(false), running_(false), max_mappings_(1000), 
      mapping_timeout_(3600), auto_cleanup_enabled_(true) {
}

Portmapper::~Portmapper() {
    shutdown();
}

bool Portmapper::initialize() {
    if (initialized_) {
        return true;
    }

    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    // Clear any existing mappings
    mappings_.clear();
    
    // Reset statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_ = PortmapStats{};
    }
    
    initialized_ = true;
    running_ = true;
    
    std::cout << "Portmapper service initialized successfully" << std::endl;
    return true;
}

void Portmapper::shutdown() {
    if (!initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    running_ = false;
    mappings_.clear();
    initialized_ = false;
    
    std::cout << "Portmapper service shutdown" << std::endl;
}

void Portmapper::handleRpcCall(const RpcMessage& message) {
    if (!initialized_ || !running_) {
        std::cerr << "Portmapper not initialized or not running" << std::endl;
        return;
    }

    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_requests++;
    }

    try {
        // Route to appropriate portmapper procedure
        switch (message.header.proc) {
            case static_cast<uint32_t>(RpcProcedure::PMAP_NULL):
                handleNull(message);
                break;
            case static_cast<uint32_t>(RpcProcedure::PMAP_SET):
                handleSet(message);
                break;
            case static_cast<uint32_t>(RpcProcedure::PMAP_UNSET):
                handleUnset(message);
                break;
            case static_cast<uint32_t>(RpcProcedure::PMAP_GETPORT):
                handleGetPort(message);
                break;
            case static_cast<uint32_t>(RpcProcedure::PMAP_DUMP):
                handleDump(message);
                break;
            case static_cast<uint32_t>(RpcProcedure::PMAP_CALLIT):
                handleCallIt(message);
                break;
            default:
                std::cerr << "Unknown portmapper procedure: " << message.header.proc << std::endl;
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.failed_requests++;
                }
                break;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error handling portmapper RPC call: " << e.what() << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
    }
}

void Portmapper::handleNull(const RpcMessage& message) {
    // NULL procedure always succeeds
    RpcMessage reply = createSuccessReply(message);
    std::cout << "Handled portmapper NULL procedure" << std::endl;
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.successful_requests++;
    }
}

void Portmapper::handleSet(const RpcMessage& message) {
    // Parse the mapping from the request
    PortmapMapping mapping;
    if (!deserializeMapping(message.data, mapping)) {
        std::cerr << "Failed to parse portmap mapping data" << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
        return;
    }
    
    // Register the mapping
    if (registerService(mapping.program, mapping.version, mapping.protocol, mapping.port, mapping.owner)) {
        std::cout << "Registered mapping: program=" << mapping.program 
                  << ", version=" << mapping.version 
                  << ", protocol=" << mapping.protocol 
                  << ", port=" << mapping.port << std::endl;
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.successful_requests++;
            stats_.mappings_registered++;
        }
    } else {
        std::cerr << "Failed to register mapping" << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
    }
}

void Portmapper::handleUnset(const RpcMessage& message) {
    // Parse the mapping to unregister
    PortmapMapping mapping;
    if (!deserializeMapping(message.data, mapping)) {
        std::cerr << "Failed to parse portmap mapping data" << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
        return;
    }
    
    // Unregister the mapping
    if (unregisterService(mapping.program, mapping.version, mapping.protocol)) {
        std::cout << "Unregistered mapping: program=" << mapping.program 
                  << ", version=" << mapping.version 
                  << ", protocol=" << mapping.protocol << std::endl;
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.successful_requests++;
            stats_.mappings_unregistered++;
        }
    } else {
        std::cerr << "Failed to unregister mapping" << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
    }
}

void Portmapper::handleGetPort(const RpcMessage& message) {
    // Parse program, version, and protocol from request
    // XDR format: program (uint32), version (uint32), protocol (uint32)
    if (message.data.size() < 12) {
        std::cerr << "Invalid GETPORT request: insufficient data" << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
        return;
    }
    
    size_t offset = 0;
    uint32_t program = ntohl(*reinterpret_cast<const uint32_t*>(message.data.data() + offset));
    offset += 4;
    uint32_t version = ntohl(*reinterpret_cast<const uint32_t*>(message.data.data() + offset));
    offset += 4;
    uint32_t protocol = ntohl(*reinterpret_cast<const uint32_t*>(message.data.data() + offset));
    
    // Lookup the port
    uint32_t port = getPort(program, version, protocol);
    
    if (port != 0) {
        std::cout << "GETPORT lookup: program=" << program 
                  << ", version=" << version 
                  << ", protocol=" << protocol 
                  << ", port=" << port << std::endl;
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.successful_requests++;
            stats_.lookups_performed++;
        }
    } else {
        std::cout << "GETPORT lookup failed: program=" << program 
                  << ", version=" << version 
                  << ", protocol=" << protocol << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
    }
}

void Portmapper::handleDump(const RpcMessage& message) {
    // Return all registered mappings
    std::cout << "Handled portmapper DUMP procedure" << std::endl;
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.successful_requests++;
    }
}

void Portmapper::handleCallIt(const RpcMessage& message) {
    // Call a procedure on a remote program
    // XDR format: program (uint32), version (uint32), procedure (uint32), args (opaque)
    if (message.data.size() < 12) {
        std::cerr << "Invalid CALLIT request: insufficient data" << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
        return;
    }
    
    size_t offset = 0;
    uint32_t program = ntohl(*reinterpret_cast<const uint32_t*>(message.data.data() + offset));
    offset += 4;
    uint32_t version = ntohl(*reinterpret_cast<const uint32_t*>(message.data.data() + offset));
    offset += 4;
    uint32_t procedure = ntohl(*reinterpret_cast<const uint32_t*>(message.data.data() + offset));
    offset += 4;
    
    // Get the port for this program/version/protocol
    // Note: We default to TCP for CALLIT
    uint32_t port = getPort(program, version, IPPROTO_TCP);
    
    if (port == 0) {
        std::cerr << "CALLIT failed: no port found for program=" << program 
                  << ", version=" << version << std::endl;
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.failed_requests++;
        }
        return;
    }
    
    // Extract procedure arguments (remaining data after program/version/procedure)
    std::vector<uint8_t> args(message.data.begin() + offset, message.data.end());
    
    std::cout << "CALLIT: program=" << program 
              << ", version=" << version 
              << ", procedure=" << procedure 
              << ", port=" << port 
              << ", args_size=" << args.size() << std::endl;
    
    // Note: Full RPC forwarding would require establishing a connection to the target
    // and forwarding the call. For now, we log the request and mark it as handled.
    // A full implementation would need socket connection management.
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.successful_requests++;
    }
}

bool Portmapper::registerService(uint32_t program, uint32_t version, uint32_t protocol, uint32_t port, const std::string& owner) {
    if (!isValidProgram(program) || !isValidVersion(version) || !isValidProtocol(protocol) || !isValidPort(port)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    // Check if we're at the mapping limit
    if (mappings_.size() >= max_mappings_) {
        std::cerr << "Maximum number of mappings reached" << std::endl;
        return false;
    }
    
    // Create mapping entry
    PortmapMapping mapping;
    mapping.program = program;
    mapping.version = version;
    mapping.protocol = protocol;
    mapping.port = port;
    mapping.owner = owner;
    mapping.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Register the mapping
    auto key = std::make_tuple(program, version, protocol);
    mappings_[key] = mapping;
    
    std::cout << "Registered service: program=" << program 
              << ", version=" << version 
              << ", protocol=" << protocol 
              << ", port=" << port << std::endl;
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.mappings_registered++;
    }
    
    return true;
}

bool Portmapper::unregisterService(uint32_t program, uint32_t version, uint32_t protocol) {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    auto key = std::make_tuple(program, version, protocol);
    auto it = mappings_.find(key);
    
    if (it != mappings_.end()) {
        mappings_.erase(it);
        
        std::cout << "Unregistered service: program=" << program 
                  << ", version=" << version 
                  << ", protocol=" << protocol << std::endl;
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.mappings_unregistered++;
        }
        
        return true;
    }
    
    return false;
}

bool Portmapper::unregisterAll(uint32_t program, uint32_t version) {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    size_t removed = 0;
    auto it = mappings_.begin();
    
    while (it != mappings_.end()) {
        if (std::get<0>(it->first) == program && std::get<1>(it->first) == version) {
            it = mappings_.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    
    if (removed > 0) {
        std::cout << "Unregistered " << removed << " mappings for program=" << program 
                  << ", version=" << version << std::endl;
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.mappings_unregistered += removed;
        }
    }
    
    return removed > 0;
}

uint32_t Portmapper::getPort(uint32_t program, uint32_t version, uint32_t protocol) const {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    auto key = std::make_tuple(program, version, protocol);
    auto it = mappings_.find(key);
    
    if (it != mappings_.end()) {
        // Note: We can't update stats in a const method, so we'll skip the stats update here
        // In a real implementation, we might use mutable or a different approach
        return it->second.port;
    }
    
    return 0; // Not found
}

std::vector<PortmapMapping> Portmapper::getAllMappings() const {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    std::vector<PortmapMapping> result;
    result.reserve(mappings_.size());
    
    for (const auto& pair : mappings_) {
        result.push_back(pair.second);
    }
    
    return result;
}

std::vector<PortmapMapping> Portmapper::getMappingsForProgram(uint32_t program) const {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    std::vector<PortmapMapping> result;
    
    for (const auto& pair : mappings_) {
        if (std::get<0>(pair.first) == program) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

PortmapStats Portmapper::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void Portmapper::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = PortmapStats{};
}

bool Portmapper::isHealthy() const {
    return initialized_ && running_;
}

void Portmapper::setMaxMappings(uint32_t max_mappings) {
    max_mappings_ = max_mappings;
}

void Portmapper::setMappingTimeout(uint64_t timeout_seconds) {
    mapping_timeout_ = timeout_seconds;
}

void Portmapper::enableAutoCleanup(bool enable) {
    auto_cleanup_enabled_ = enable;
}

bool Portmapper::isValidProgram(uint32_t program) const {
    // Valid RPC program numbers (basic validation)
    return program > 0 && program <= 0x7FFFFFFF;
}

bool Portmapper::isValidVersion(uint32_t version) const {
    // Valid version numbers (basic validation)
    return version > 0 && version <= 0x7FFFFFFF;
}

bool Portmapper::isValidProtocol(uint32_t protocol) const {
    // Valid protocols: TCP (6) and UDP (17)
    return protocol == 6 || protocol == 17;
}

bool Portmapper::isValidPort(uint32_t port) const {
    // Valid port numbers
    return port > 0 && port <= 65535;
}

void Portmapper::cleanupExpiredMappings() {
    if (!auto_cleanup_enabled_) {
        return;
    }
    
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    auto it = mappings_.begin();
    while (it != mappings_.end()) {
        if (now - it->second.timestamp > mapping_timeout_) {
            std::cout << "Cleaning up expired mapping: program=" << it->second.program 
                      << ", version=" << it->second.version << std::endl;
            it = mappings_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<uint8_t> Portmapper::serializeMapping(const PortmapMapping& mapping) const {
    // XDR serialization of PortmapMapping
    // Format: program (uint32), version (uint32), protocol (uint32), port (uint32)
    // Note: owner and timestamp are not part of standard XDR portmap mapping
    std::vector<uint8_t> data;
    data.resize(16); // 4 uint32_t values
    
    size_t offset = 0;
    uint32_t program_net = htonl(mapping.program);
    memcpy(data.data() + offset, &program_net, 4);
    offset += 4;
    
    uint32_t version_net = htonl(mapping.version);
    memcpy(data.data() + offset, &version_net, 4);
    offset += 4;
    
    uint32_t protocol_net = htonl(mapping.protocol);
    memcpy(data.data() + offset, &protocol_net, 4);
    offset += 4;
    
    uint32_t port_net = htonl(mapping.port);
    memcpy(data.data() + offset, &port_net, 4);
    
    return data;
}

bool Portmapper::deserializeMapping(const std::vector<uint8_t>& data, PortmapMapping& mapping) const {
    // XDR deserialization of PortmapMapping
    // Format: program (uint32), version (uint32), protocol (uint32), port (uint32)
    if (data.size() < 16) {
        return false;
    }
    
    size_t offset = 0;
    uint32_t program_net = 0;
    memcpy(&program_net, data.data() + offset, 4);
    mapping.program = ntohl(program_net);
    offset += 4;
    
    uint32_t version_net = 0;
    memcpy(&version_net, data.data() + offset, 4);
    mapping.version = ntohl(version_net);
    offset += 4;
    
    uint32_t protocol_net = 0;
    memcpy(&protocol_net, data.data() + offset, 4);
    mapping.protocol = ntohl(protocol_net);
    offset += 4;
    
    uint32_t port_net = 0;
    memcpy(&port_net, data.data() + offset, 4);
    mapping.port = ntohl(port_net);
    
    // Set default values for optional fields
    mapping.owner = "";
    mapping.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return true;
}

RpcMessage Portmapper::createSuccessReply(const RpcMessage& request, const std::vector<uint8_t>& data) const {
    return RpcUtils::createReply(request.header.xid, RpcAcceptState::SUCCESS, data);
}

RpcMessage Portmapper::createErrorReply(const RpcMessage& request, RpcAcceptState error_state) const {
    return RpcUtils::createReply(request.header.xid, error_state, {});
}

} // namespace SimpleNfsd
