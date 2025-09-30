/**
 * @file nfsd_app.hpp
 * @brief Main application class for Simple NFS Daemon
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#ifndef SIMPLE_NFSD_NFSD_APP_HPP
#define SIMPLE_NFSD_NFSD_APP_HPP

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

namespace SimpleNfsd {

// Forward declarations
class NfsServerSimple;

class NfsdApp {
public:
    NfsdApp();
    ~NfsdApp();
    
    // Initialize the application
    bool initialize(int argc, char* argv[]);
    
    // Run the main application loop
    void run();
    
    // Stop the application gracefully
    void stop();
    
    // Check if the application is running
    bool isRunning() const;
    
    // Performance monitoring
    struct PerformanceMetrics {
        uint64_t total_requests{0};
        uint64_t successful_requests{0};
        uint64_t failed_requests{0};
        uint64_t bytes_sent{0};
        uint64_t bytes_received{0};
        uint64_t active_connections{0};
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point last_request_time;
    };
    
    // Health check
    struct HealthStatus {
        bool is_healthy;
        std::string status_message;
        std::map<std::string, std::string> details;
    };
    
    // Get performance metrics
    PerformanceMetrics getMetrics() const;
    
    // Get health status
    HealthStatus getHealthStatus() const;
    
    // Reset metrics
    void resetMetrics();
    
    // Test methods for simulation (Phase 1)
    void simulateNfsRequest(bool success = true, size_t bytes_sent = 1024, size_t bytes_received = 512);
    void simulateConnection();
    void simulateDisconnection();
    
private:
    // Configuration
    std::string config_file_;
    std::string log_file_;
    std::string pid_file_;
    bool daemon_mode_;
    
    // Application state
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> main_thread_;
    
    // Performance monitoring
    mutable std::atomic<uint64_t> total_requests_{0};
    mutable std::atomic<uint64_t> successful_requests_{0};
    mutable std::atomic<uint64_t> failed_requests_{0};
    mutable std::atomic<uint64_t> bytes_sent_{0};
    mutable std::atomic<uint64_t> bytes_received_{0};
    mutable std::atomic<uint64_t> active_connections_{0};
    mutable std::chrono::steady_clock::time_point start_time_;
    mutable std::chrono::steady_clock::time_point last_request_time_;
    mutable std::mutex metrics_mutex_;
    
    // Health monitoring
    mutable std::mutex health_mutex_;
    mutable std::chrono::steady_clock::time_point last_health_check_;
    
    // NFS Server
    std::unique_ptr<NfsServerSimple> nfs_server_;
    
    // Private methods
    void loadConfiguration();
    void setupLogging();
    void writePidFile();
    void removePidFile();
    void mainLoop();
    void showHelp() const;
    void showVersion() const;
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_NFSD_APP_HPP
