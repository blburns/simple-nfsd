/**
 * @file test_config_manager.cpp
 * @brief Unit tests for ConfigManager class
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include <gtest/gtest.h>
#include "simple-nfsd/config/config.hpp"
#include <fstream>
#include <sstream>

class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_manager = std::make_unique<SimpleNfsd::ConfigManager>();
    }

    void TearDown() override {
        config_manager.reset();
    }

    std::unique_ptr<SimpleNfsd::ConfigManager> config_manager;
};

TEST_F(ConfigManagerTest, LoadValidJsonConfig) {
    // Create a temporary JSON config file
    std::string json_config = R"({
        "nfs": {
            "listen": ["0.0.0.0:2049"],
            "exports": [
                {
                    "path": "/var/lib/simple-nfsd/shares",
                    "clients": ["*"],
                    "options": ["rw", "sync", "no_subtree_check"]
                }
            ],
            "logging": {
                "enable": true,
                "level": "info"
            }
        }
    })";

    std::ofstream file("test_config.json");
    file << json_config;
    file.close();

    EXPECT_TRUE(config_manager->loadConfig("test_config.json"));
    EXPECT_EQ(config_manager->getString("nfs.listen[0]"), "0.0.0.0:2049");
    EXPECT_EQ(config_manager->getString("nfs.exports[0].path"), "/var/lib/simple-nfsd/shares");
    EXPECT_EQ(config_manager->getString("nfs.logging.level"), "info");
    EXPECT_TRUE(config_manager->getBool("nfs.logging.enable"));

    // Clean up
    std::remove("test_config.json");
}

TEST_F(ConfigManagerTest, LoadValidYamlConfig) {
    // Create a temporary YAML config file
    std::string yaml_config = R"(nfs:
  listen:
    - "0.0.0.0:2049"
  exports:
    - path: "/var/lib/simple-nfsd/shares"
      clients: ["*"]
      options: ["rw", "sync", "no_subtree_check"]
  logging:
    enable: true
    level: "info"
)";

    std::ofstream file("test_config.yaml");
    file << yaml_config;
    file.close();

    EXPECT_TRUE(config_manager->loadConfig("test_config.yaml"));
    EXPECT_EQ(config_manager->getString("nfs.listen[0]"), "0.0.0.0:2049");
    EXPECT_EQ(config_manager->getString("nfs.exports[0].path"), "/var/lib/simple-nfsd/shares");
    EXPECT_EQ(config_manager->getString("nfs.logging.level"), "info");
    EXPECT_TRUE(config_manager->getBool("nfs.logging.enable"));

    // Clean up
    std::remove("test_config.yaml");
}

TEST_F(ConfigManagerTest, LoadValidIniConfig) {
    // Create a temporary INI config file
    std::string ini_config = R"([nfs]
listen = 0.0.0.0:2049
log_level = info
log_enable = true

[exports]
export1_path = /var/lib/simple-nfsd/shares
export1_clients = *
export1_options = rw,sync,no_subtree_check
)";

    std::ofstream file("test_config.ini");
    file << ini_config;
    file.close();

    EXPECT_TRUE(config_manager->loadConfig("test_config.ini"));
    EXPECT_EQ(config_manager->getString("nfs.listen"), "0.0.0.0:2049");
    EXPECT_EQ(config_manager->getString("nfs.log_level"), "info");
    EXPECT_TRUE(config_manager->getBool("nfs.log_enable"));

    // Clean up
    std::remove("test_config.ini");
}

TEST_F(ConfigManagerTest, HandleInvalidConfigFile) {
    EXPECT_FALSE(config_manager->loadConfig("nonexistent_config.json"));
    EXPECT_FALSE(config_manager->loadConfig(""));
}

TEST_F(ConfigManagerTest, HandleMalformedJson) {
    std::string malformed_json = R"({
        "nfs": {
            "listen": ["0.0.0.0:2049"],
            "exports": [
                {
                    "path": "/var/lib/simple-nfsd/shares",
                    "clients": ["*"],
                    "options": ["rw", "sync", "no_subtree_check"
                }
            ]
        }
    })";

    std::ofstream file("malformed_config.json");
    file << malformed_json;
    file.close();

    EXPECT_FALSE(config_manager->loadConfig("malformed_config.json"));

    // Clean up
    std::remove("malformed_config.json");
}

TEST_F(ConfigManagerTest, GetStringWithDefault) {
    config_manager->loadConfig("test_config.json");
    
    EXPECT_EQ(config_manager->getString("nonexistent.key", "default_value"), "default_value");
    EXPECT_EQ(config_manager->getString("nfs.listen[0]", "default"), "0.0.0.0:2049");
}

TEST_F(ConfigManagerTest, GetIntWithDefault) {
    std::string json_config = R"({
        "nfs": {
            "port": 2049,
            "max_connections": 1000
        }
    })";

    std::ofstream file("test_config.json");
    file << json_config;
    file.close();

    config_manager->loadConfig("test_config.json");
    
    EXPECT_EQ(config_manager->getInt("nfs.port", 0), 2049);
    EXPECT_EQ(config_manager->getInt("nfs.max_connections", 0), 1000);
    EXPECT_EQ(config_manager->getInt("nonexistent.key", 42), 42);

    // Clean up
    std::remove("test_config.json");
}

TEST_F(ConfigManagerTest, GetBoolWithDefault) {
    std::string json_config = R"({
        "nfs": {
            "logging": {
                "enable": true
            },
            "security": {
                "encryption": false
            }
        }
    })";

    std::ofstream file("test_config.json");
    file << json_config;
    file.close();

    config_manager->loadConfig("test_config.json");
    
    EXPECT_TRUE(config_manager->getBool("nfs.logging.enable", false));
    EXPECT_FALSE(config_manager->getBool("nfs.security.encryption", true));
    EXPECT_TRUE(config_manager->getBool("nonexistent.key", true));

    // Clean up
    std::remove("test_config.json");
}

TEST_F(ConfigManagerTest, GetStringArray) {
    std::string json_config = R"({
        "nfs": {
            "listen": ["0.0.0.0:2049", "[::]:2049"],
            "exports": [
                {
                    "clients": ["192.168.1.0/24", "10.0.0.0/8"]
                }
            ]
        }
    })";

    std::ofstream file("test_config.json");
    file << json_config;
    file.close();

    config_manager->loadConfig("test_config.json");
    
    auto listen_addresses = config_manager->getStringArray("nfs.listen");
    EXPECT_EQ(listen_addresses.size(), 2);
    EXPECT_EQ(listen_addresses[0], "0.0.0.0:2049");
    EXPECT_EQ(listen_addresses[1], "[::]:2049");

    auto clients = config_manager->getStringArray("nfs.exports[0].clients");
    EXPECT_EQ(clients.size(), 2);
    EXPECT_EQ(clients[0], "192.168.1.0/24");
    EXPECT_EQ(clients[1], "10.0.0.0/8");

    // Clean up
    std::remove("test_config.json");
}

TEST_F(ConfigManagerTest, ValidateConfiguration) {
    std::string valid_config = R"({
        "nfs": {
            "listen": ["0.0.0.0:2049"],
            "exports": [
                {
                    "path": "/var/lib/simple-nfsd/shares",
                    "clients": ["*"],
                    "options": ["rw", "sync", "no_subtree_check"]
                }
            ],
            "logging": {
                "enable": true,
                "level": "info"
            }
        }
    })";

    std::ofstream file("test_config.json");
    file << valid_config;
    file.close();

    EXPECT_TRUE(config_manager->loadConfig("test_config.json"));
    EXPECT_TRUE(config_manager->validateConfig());

    // Clean up
    std::remove("test_config.json");
}

TEST_F(ConfigManagerTest, HotReloadConfiguration) {
    // Create initial config
    std::string initial_config = R"({
        "nfs": {
            "listen": ["0.0.0.0:2049"],
            "logging": {
                "level": "info"
            }
        }
    })";

    std::ofstream file("test_config.json");
    file << initial_config;
    file.close();

    EXPECT_TRUE(config_manager->loadConfig("test_config.json"));
    EXPECT_EQ(config_manager->getString("nfs.logging.level"), "info");

    // Update config
    std::string updated_config = R"({
        "nfs": {
            "listen": ["0.0.0.0:2049"],
            "logging": {
                "level": "debug"
            }
        }
    })";

    file.open("test_config.json");
    file << updated_config;
    file.close();

    EXPECT_TRUE(config_manager->reloadConfig());
    EXPECT_EQ(config_manager->getString("nfs.logging.level"), "debug");

    // Clean up
    std::remove("test_config.json");
}
