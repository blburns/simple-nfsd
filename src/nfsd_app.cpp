/**
 * @file nfsd_app.cpp
 * @brief Implementation of NfsdApp class for Simple NFS Daemon
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include "simple_nfsd/nfsd_app.hpp"
#include "simple_nfsd/nfs_server_simple.hpp"
#include <iostream>
#include <fstream>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

namespace SimpleNfsd {

NfsdApp::NfsdApp() 
    : config_file_("/etc/simple-nfsd/simple-nfsd.conf")
    , log_file_("/var/log/simple-nfsd/simple-nfsd.log")
    , pid_file_("/var/run/simple-nfsd/simple-nfsd.pid")
    , daemon_mode_(false)
    , running_(false) {
    // Initialize metrics
    start_time_ = std::chrono::steady_clock::now();
    last_request_time_ = start_time_;
    last_health_check_ = std::chrono::steady_clock::now();
}

NfsdApp::~NfsdApp() {
    stop();
    removePidFile();
}

bool NfsdApp::initialize(int argc, char* argv[]) {
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            showHelp();
            return false;
        } else if (arg == "--version" || arg == "-v") {
            showVersion();
            return false;
        } else if (arg == "--daemon" || arg == "-d") {
            daemon_mode_ = true;
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 < argc) {
                config_file_ = argv[++i];
            } else {
                std::cerr << "Error: --config requires a filename" << std::endl;
                return false;
            }
        } else if (arg == "--log" || arg == "-l") {
            if (i + 1 < argc) {
                log_file_ = argv[++i];
            } else {
                std::cerr << "Error: --log requires a filename" << std::endl;
                return false;
            }
        } else if (arg == "--pid" || arg == "-p") {
            if (i + 1 < argc) {
                pid_file_ = argv[++i];
            } else {
                std::cerr << "Error: --pid requires a filename" << std::endl;
                return false;
            }
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            showHelp();
            return false;
        }
    }
    
    // Load configuration
    loadConfiguration();
    
    // Setup logging
    setupLogging();
    
    // Write PID file if in daemon mode
    if (daemon_mode_) {
        writePidFile();
    }
    
    return true;
}

void NfsdApp::run() {
    running_ = true;
    
    if (daemon_mode_) {
        // Fork to create daemon process
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "Failed to fork daemon process" << std::endl;
            return;
        } else if (pid > 0) {
            // Parent process exits
            return;
        }
        
        // Child process continues
        setsid();
        
        // Change to root directory
        chdir("/");
        
        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    // Start main loop
    mainLoop();
}

void NfsdApp::stop() {
    running_ = false;
    
    if (main_thread_ && main_thread_->joinable()) {
        main_thread_->join();
    }
}

bool NfsdApp::isRunning() const {
    return running_;
}

void NfsdApp::loadConfiguration() {
    // TODO: Implement configuration loading
    std::cout << "Loading configuration from: " << config_file_ << std::endl;
}

void NfsdApp::setupLogging() {
    // TODO: Implement logging setup
    std::cout << "Setting up logging to: " << log_file_ << std::endl;
}

void NfsdApp::writePidFile() {
    std::ofstream pid_file(pid_file_);
    if (pid_file.is_open()) {
        pid_file << getpid() << std::endl;
        pid_file.close();
    }
}

void NfsdApp::removePidFile() {
    unlink(pid_file_.c_str());
}

void NfsdApp::mainLoop() {
    std::cout << "Simple NFS Daemon starting..." << std::endl;
    
    // Initialize NFS server
    nfs_server_ = std::make_unique<SimpleNfsd::NfsServerSimple>();
    
    // Load configuration
    SimpleNfsd::NfsServerConfig nfs_config;
    nfs_config.bind_address = "0.0.0.0";
    nfs_config.port = 2049;
    nfs_config.root_path = "/var/lib/simple-nfsd/shares";
    nfs_config.max_connections = 1000;
    nfs_config.enable_tcp = true;
    nfs_config.enable_udp = true;
    
    if (!nfs_server_->initialize(nfs_config)) {
        std::cerr << "Failed to initialize NFS server" << std::endl;
        return;
    }
    
    if (!nfs_server_->start()) {
        std::cerr << "Failed to start NFS server" << std::endl;
        return;
    }
    
    std::cout << "NFS server started successfully" << std::endl;
    
    while (running_) {
        // Check server health
        if (!nfs_server_->isHealthy()) {
            std::cerr << "NFS server is not healthy" << std::endl;
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    // Stop NFS server
    nfs_server_->stop();
    nfs_server_.reset();
    
    std::cout << "Simple NFS Daemon stopping..." << std::endl;
}

void NfsdApp::showHelp() const {
    std::cout << "Simple NFS Daemon v0.1.0" << std::endl;
    std::cout << "Usage: simple-nfsd [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help     Show this help message" << std::endl;
    std::cout << "  -v, --version  Show version information" << std::endl;
    std::cout << "  -d, --daemon   Run as daemon" << std::endl;
    std::cout << "  -c, --config   Configuration file (default: /etc/simple-nfsd/simple-nfsd.conf)" << std::endl;
    std::cout << "  -l, --log      Log file (default: /var/log/simple-nfsd/simple-nfsd.log)" << std::endl;
    std::cout << "  -p, --pid      PID file (default: /var/run/simple-nfsd/simple-nfsd.pid)" << std::endl;
}

void NfsdApp::showVersion() const {
    std::cout << "Simple NFS Daemon v0.1.0" << std::endl;
    std::cout << "A lightweight and secure NFS server" << std::endl;
}

NfsdApp::PerformanceMetrics NfsdApp::getMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    PerformanceMetrics metrics;
    metrics.total_requests = total_requests_.load();
    metrics.successful_requests = successful_requests_.load();
    metrics.failed_requests = failed_requests_.load();
    metrics.bytes_sent = bytes_sent_.load();
    metrics.bytes_received = bytes_received_.load();
    metrics.active_connections = active_connections_.load();
    metrics.start_time = start_time_;
    metrics.last_request_time = last_request_time_;
    return metrics;
}

NfsdApp::HealthStatus NfsdApp::getHealthStatus() const {
    std::lock_guard<std::mutex> lock(health_mutex_);
    
    HealthStatus status;
    status.is_healthy = true;
    status.status_message = "OK";
    
    // Check if application is running
    if (!running_) {
        status.is_healthy = false;
        status.status_message = "Application not running";
        return status;
    }
    
    // Check uptime
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    
    status.details["uptime_seconds"] = std::to_string(uptime.count());
    status.details["total_requests"] = std::to_string(total_requests_.load());
    status.details["successful_requests"] = std::to_string(successful_requests_.load());
    status.details["failed_requests"] = std::to_string(failed_requests_.load());
    status.details["bytes_sent"] = std::to_string(bytes_sent_.load());
    status.details["bytes_received"] = std::to_string(bytes_received_.load());
    status.details["active_connections"] = std::to_string(active_connections_.load());
    
    // Check for high error rate
    uint64_t total = total_requests_.load();
    uint64_t failed = failed_requests_.load();
    
    if (total > 0) {
        double error_rate = static_cast<double>(failed) / total;
        if (error_rate > 0.1) { // More than 10% error rate
            status.is_healthy = false;
            status.status_message = "High error rate detected";
            status.details["error_rate"] = std::to_string(error_rate);
        }
    }
    
    // Check for memory usage (basic check)
    // This would be enhanced with actual memory monitoring
    status.details["memory_status"] = "OK";
    
    last_health_check_ = now;
    return status;
}

void NfsdApp::resetMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    total_requests_ = 0;
    successful_requests_ = 0;
    failed_requests_ = 0;
    bytes_sent_ = 0;
    bytes_received_ = 0;
    active_connections_ = 0;
    start_time_ = std::chrono::steady_clock::now();
    last_request_time_ = start_time_;
}

void NfsdApp::simulateNfsRequest(bool success, size_t bytes_sent, size_t bytes_received) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    total_requests_++;
    if (success) {
        successful_requests_++;
    } else {
        failed_requests_++;
    }
    
    bytes_sent_ += bytes_sent;
    bytes_received_ += bytes_received;
    last_request_time_ = std::chrono::steady_clock::now();
}

void NfsdApp::simulateConnection() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    active_connections_++;
}

void NfsdApp::simulateDisconnection() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    if (active_connections_ > 0) {
        active_connections_--;
    }
}

} // namespace SimpleNfsd
