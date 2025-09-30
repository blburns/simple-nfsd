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

#ifndef SIMPLE_NFSD_CONFIG_MANAGER_HPP
#define SIMPLE_NFSD_CONFIG_MANAGER_HPP

#include <string>
#include <map>
#include <memory>
#include <vector>

namespace SimpleNfsd {

enum class ConfigFormat {
    INI,
    JSON,
    YAML
};

struct NfsdConfig {
    // Global settings
    std::string server_name;
    std::string server_version;
    std::string listen_address;
    int listen_port;
    
    // Security settings
    std::string security_mode;
    bool root_squash;
    bool all_squash;
    int anon_uid;
    int anon_gid;
    
    // Performance settings
    int max_connections;
    int thread_count;
    int read_size;
    int write_size;
    
    // Logging settings
    std::string log_level;
    std::string log_file;
    std::string log_max_size;
    int log_max_files;
    
    // Cache settings
    bool cache_enabled;
    std::string cache_size;
    int cache_ttl;
    
    // Export settings
    struct Export {
        std::string name;
        std::string path;
        std::string clients;
        std::string options;
        std::string comment;
    };
    std::vector<Export> exports;
    
    // Default constructor
    NfsdConfig();
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
    // Load configuration from file
    bool loadFromFile(const std::string& filename);
    
    // Save configuration to file
    bool saveToFile(const std::string& filename, ConfigFormat format = ConfigFormat::INI) const;
    
    // Get configuration
    const NfsdConfig& getConfig() const;
    
    // Set configuration
    void setConfig(const NfsdConfig& config);
    
    // Auto-detect file format
    static ConfigFormat detectFormat(const std::string& filename);
    
    // Validate configuration
    bool validate() const;
    
private:
    NfsdConfig config_;
    
    // Format-specific loaders
    bool loadIni(const std::string& filename);
    bool loadJson(const std::string& filename);
    bool loadYaml(const std::string& filename);
    
    // Format-specific savers
    bool saveIni(const std::string& filename) const;
    bool saveJson(const std::string& filename) const;
    bool saveYaml(const std::string& filename) const;
    
    // Helper methods
    std::string trim(const std::string& str) const;
    std::vector<std::string> split(const std::string& str, char delimiter) const;
    bool isNumeric(const std::string& str) const;
    int stringToInt(const std::string& str) const;
    bool stringToBool(const std::string& str) const;
};

} // namespace SimpleNfsd

#endif // SIMPLE_NFSD_CONFIG_MANAGER_HPP
