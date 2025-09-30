/**
 * @file test_nfsd_app.cpp
 * @brief Unit tests for NfsdApp class
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include <gtest/gtest.h>
#include "simple_nfsd/nfsd_app.hpp"

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
