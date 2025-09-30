/*
 * Copyright 2024 SimpleDaemons
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
