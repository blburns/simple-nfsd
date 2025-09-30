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

namespace SimpleNfsd {

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
    
private:
    // Configuration
    std::string config_file_;
    std::string log_file_;
    std::string pid_file_;
    bool daemon_mode_;
    
    // Application state
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> main_thread_;
    
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
