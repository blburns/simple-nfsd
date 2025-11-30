/**
 * @file test_nfsd_app.cpp
 * @brief Unit tests for NfsdApp class
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include <gtest/gtest.h>
#include "simple-nfsd/core/app.hpp"

class NfsdAppTest : public ::testing::Test {
protected:
    void SetUp() override {
        app = std::make_unique<SimpleNfsd::NfsdApp>();
    }
    
    void TearDown() override {
        app.reset();
    }
    
    std::unique_ptr<SimpleNfsd::NfsdApp> app;
};

TEST_F(NfsdAppTest, Constructor) {
    EXPECT_NE(app, nullptr);
    EXPECT_FALSE(app->isRunning());
}

TEST_F(NfsdAppTest, InitializeWithHelp) {
    const char* argv[] = {"simple-nfsd", "--help"};
    EXPECT_FALSE(app->initialize(2, const_cast<char**>(argv)));
}

TEST_F(NfsdAppTest, InitializeWithVersion) {
    const char* argv[] = {"simple-nfsd", "--version"};
    EXPECT_FALSE(app->initialize(2, const_cast<char**>(argv)));
}

TEST_F(NfsdAppTest, InitializeWithValidArgs) {
    const char* argv[] = {"simple-nfsd", "--config", "/tmp/test.conf"};
    EXPECT_TRUE(app->initialize(3, const_cast<char**>(argv)));
}

TEST_F(NfsdAppTest, Stop) {
    app->stop();
    EXPECT_FALSE(app->isRunning());
}

TEST_F(NfsdAppTest, PerformanceMetrics) {
    // Test initial metrics
    auto metrics = app->getMetrics();
    EXPECT_EQ(metrics.total_requests, 0);
    EXPECT_EQ(metrics.successful_requests, 0);
    EXPECT_EQ(metrics.failed_requests, 0);
    EXPECT_EQ(metrics.bytes_sent, 0);
    EXPECT_EQ(metrics.bytes_received, 0);
    EXPECT_EQ(metrics.active_connections, 0);
    
    // Simulate some NFS operations
    app->simulateNfsRequest(true, 1024, 512);
    app->simulateNfsRequest(true, 2048, 1024);
    app->simulateNfsRequest(false, 0, 256);
    
    metrics = app->getMetrics();
    EXPECT_EQ(metrics.total_requests, 3);
    EXPECT_EQ(metrics.successful_requests, 2);
    EXPECT_EQ(metrics.failed_requests, 1);
    EXPECT_EQ(metrics.bytes_sent, 3072); // 1024 + 2048
    EXPECT_EQ(metrics.bytes_received, 1792); // 512 + 1024 + 256
}

TEST_F(NfsdAppTest, ConnectionSimulation) {
    // Test connection simulation
    app->simulateConnection();
    app->simulateConnection();
    
    auto metrics = app->getMetrics();
    EXPECT_EQ(metrics.active_connections, 2);
    
    // Test disconnection
    app->simulateDisconnection();
    metrics = app->getMetrics();
    EXPECT_EQ(metrics.active_connections, 1);
    
    app->simulateDisconnection();
    metrics = app->getMetrics();
    EXPECT_EQ(metrics.active_connections, 0);
    
    // Test disconnection when no connections
    app->simulateDisconnection();
    metrics = app->getMetrics();
    EXPECT_EQ(metrics.active_connections, 0);
}

TEST_F(NfsdAppTest, HealthStatus) {
    // Test health status when not running
    auto health = app->getHealthStatus();
    EXPECT_FALSE(health.is_healthy);
    EXPECT_EQ(health.status_message, "Application not running");
    
    // Simulate some operations
    app->simulateNfsRequest(true, 1024, 512);
    app->simulateConnection();
    
    // Health should still be unhealthy since app is not running
    health = app->getHealthStatus();
    EXPECT_FALSE(health.is_healthy);
}

TEST_F(NfsdAppTest, MetricsReset) {
    // Add some metrics
    app->simulateNfsRequest(true, 1024, 512);
    app->simulateConnection();
    
    auto metrics = app->getMetrics();
    EXPECT_EQ(metrics.total_requests, 1);
    EXPECT_EQ(metrics.active_connections, 1);
    
    // Reset metrics
    app->resetMetrics();
    
    metrics = app->getMetrics();
    EXPECT_EQ(metrics.total_requests, 0);
    EXPECT_EQ(metrics.successful_requests, 0);
    EXPECT_EQ(metrics.failed_requests, 0);
    EXPECT_EQ(metrics.bytes_sent, 0);
    EXPECT_EQ(metrics.bytes_received, 0);
    EXPECT_EQ(metrics.active_connections, 0);
}
