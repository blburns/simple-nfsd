#include "simple_nfsd/nfsd_app.hpp"
#include <iostream>
#include <csignal>
#include <memory>

std::unique_ptr<SimpleNfsd::NfsdApp> g_app;

void signal_handler(int signal) {
    if (g_app) {
        std::cout << "Received signal " << signal << ", shutting down gracefully..." << std::endl;
        g_app->stop();
    }
}

int main(int argc, char* argv[]) {
    try {
        // Set up signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Create and initialize the application
        g_app = std::make_unique<SimpleNfsd::NfsdApp>();
        
        if (!g_app->initialize(argc, argv)) {
            std::cerr << "Failed to initialize NFS daemon" << std::endl;
            return 1;
        }
        
        // Run the application
        g_app->run();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
