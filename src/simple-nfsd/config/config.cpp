/**
 * @file config_manager.cpp
 * @brief Configuration management implementation for Simple NFS Daemon
 * @author SimpleDaemons
 * @copyright 2024 SimpleDaemons
 * @license Apache-2.0
 */

#include "simple-nfsd/config/config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

#ifdef ENABLE_JSON
#include <json/json.h>
#endif

#ifdef ENABLE_YAML
#include <yaml-cpp/yaml.h>
#endif

namespace SimpleNfsd {

NfsdConfig::NfsdConfig() 
    : server_name("Simple NFS Daemon")
    , server_version("0.1.0")
    , listen_address("0.0.0.0")
    , listen_port(2049)
    , security_mode("user")
    , root_squash(true)
    , all_squash(false)
    , anon_uid(65534)
    , anon_gid(65534)
    , max_connections(1000)
    , thread_count(8)
    , read_size(8192)
    , write_size(8192)
    , log_level("info")
    , log_file("/var/log/simple-nfsd/simple-nfsd.log")
    , log_max_size("100MB")
    , log_max_files(10)
    , cache_enabled(true)
    , cache_size("100MB")
    , cache_ttl(3600) {
}

ConfigManager::ConfigManager() {
}

ConfigManager::~ConfigManager() {
}

bool ConfigManager::loadFromFile(const std::string& filename) {
    ConfigFormat format = detectFormat(filename);
    
    switch (format) {
        case ConfigFormat::INI:
            return loadIni(filename);
        case ConfigFormat::JSON:
            return loadJson(filename);
        case ConfigFormat::YAML:
            return loadYaml(filename);
        default:
            std::cerr << "Unknown configuration format for file: " << filename << std::endl;
            return false;
    }
}

bool ConfigManager::saveToFile(const std::string& filename, ConfigFormat format) const {
    switch (format) {
        case ConfigFormat::INI:
            return saveIni(filename);
        case ConfigFormat::JSON:
            return saveJson(filename);
        case ConfigFormat::YAML:
            return saveYaml(filename);
        default:
            std::cerr << "Unknown configuration format: " << static_cast<int>(format) << std::endl;
            return false;
    }
}

const NfsdConfig& ConfigManager::getConfig() const {
    return config_;
}

void ConfigManager::setConfig(const NfsdConfig& config) {
    config_ = config;
}

ConfigFormat ConfigManager::detectFormat(const std::string& filename) {
    std::string extension = filename.substr(filename.find_last_of('.') + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    if (extension == "json") {
        return ConfigFormat::JSON;
    } else if (extension == "yaml" || extension == "yml") {
        return ConfigFormat::YAML;
    } else {
        return ConfigFormat::INI;
    }
}

bool ConfigManager::validate() const {
    // Basic validation
    if (config_.listen_port < 1 || config_.listen_port > 65535) {
        std::cerr << "Invalid listen port: " << config_.listen_port << std::endl;
        return false;
    }
    
    if (config_.max_connections < 1) {
        std::cerr << "Invalid max connections: " << config_.max_connections << std::endl;
        return false;
    }
    
    if (config_.thread_count < 1) {
        std::cerr << "Invalid thread count: " << config_.thread_count << std::endl;
        return false;
    }
    
    return true;
}

bool ConfigManager::loadIni(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open configuration file: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    std::string current_section;
    
    while (std::getline(file, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Check for section headers
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            current_section = line.substr(1, line.length() - 2);
            continue;
        }
        
        // Parse key-value pairs
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));
            
            // Parse based on section and key
            if (current_section == "global") {
                if (key == "server_name") {
                    config_.server_name = value;
                } else if (key == "server_version") {
                    config_.server_version = value;
                } else if (key == "listen_address") {
                    config_.listen_address = value;
                } else if (key == "listen_port") {
                    config_.listen_port = stringToInt(value);
                } else if (key == "security_mode") {
                    config_.security_mode = value;
                } else if (key == "root_squash") {
                    config_.root_squash = stringToBool(value);
                } else if (key == "all_squash") {
                    config_.all_squash = stringToBool(value);
                } else if (key == "anon_uid") {
                    config_.anon_uid = stringToInt(value);
                } else if (key == "anon_gid") {
                    config_.anon_gid = stringToInt(value);
                } else if (key == "max_connections") {
                    config_.max_connections = stringToInt(value);
                } else if (key == "thread_count") {
                    config_.thread_count = stringToInt(value);
                } else if (key == "read_size") {
                    config_.read_size = stringToInt(value);
                } else if (key == "write_size") {
                    config_.write_size = stringToInt(value);
                } else if (key == "log_level") {
                    config_.log_level = value;
                } else if (key == "log_file") {
                    config_.log_file = value;
                } else if (key == "log_max_size") {
                    config_.log_max_size = value;
                } else if (key == "log_max_files") {
                    config_.log_max_files = stringToInt(value);
                } else if (key == "cache_enabled") {
                    config_.cache_enabled = stringToBool(value);
                } else if (key == "cache_size") {
                    config_.cache_size = value;
                } else if (key == "cache_ttl") {
                    config_.cache_ttl = stringToInt(value);
                }
            }
        }
    }
    
    return true;
}

bool ConfigManager::loadJson(const std::string& filename) {
#ifdef ENABLE_JSON
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open configuration file: " << filename << std::endl;
        return false;
    }
    
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        std::cerr << "Failed to parse JSON configuration: " << errors << std::endl;
        return false;
    }
    
    // Parse global settings
    if (root.isMember("global")) {
        const Json::Value& global = root["global"];
        
        if (global.isMember("server_name")) {
            config_.server_name = global["server_name"].asString();
        }
        if (global.isMember("server_version")) {
            config_.server_version = global["server_version"].asString();
        }
        if (global.isMember("listen_address")) {
            config_.listen_address = global["listen_address"].asString();
        }
        if (global.isMember("listen_port")) {
            config_.listen_port = global["listen_port"].asInt();
        }
        if (global.isMember("security_mode")) {
            config_.security_mode = global["security_mode"].asString();
        }
        if (global.isMember("root_squash")) {
            config_.root_squash = global["root_squash"].asBool();
        }
        if (global.isMember("all_squash")) {
            config_.all_squash = global["all_squash"].asBool();
        }
        if (global.isMember("anon_uid")) {
            config_.anon_uid = global["anon_uid"].asInt();
        }
        if (global.isMember("anon_gid")) {
            config_.anon_gid = global["anon_gid"].asInt();
        }
        if (global.isMember("max_connections")) {
            config_.max_connections = global["max_connections"].asInt();
        }
        if (global.isMember("thread_count")) {
            config_.thread_count = global["thread_count"].asInt();
        }
        if (global.isMember("read_size")) {
            config_.read_size = global["read_size"].asInt();
        }
        if (global.isMember("write_size")) {
            config_.write_size = global["write_size"].asInt();
        }
        if (global.isMember("log_level")) {
            config_.log_level = global["log_level"].asString();
        }
        if (global.isMember("log_file")) {
            config_.log_file = global["log_file"].asString();
        }
        if (global.isMember("log_max_size")) {
            config_.log_max_size = global["log_max_size"].asString();
        }
        if (global.isMember("log_max_files")) {
            config_.log_max_files = global["log_max_files"].asInt();
        }
        if (global.isMember("cache_enabled")) {
            config_.cache_enabled = global["cache_enabled"].asBool();
        }
        if (global.isMember("cache_size")) {
            config_.cache_size = global["cache_size"].asString();
        }
        if (global.isMember("cache_ttl")) {
            config_.cache_ttl = global["cache_ttl"].asInt();
        }
    }
    
    // Parse exports
    if (root.isMember("exports")) {
        const Json::Value& exports = root["exports"];
        config_.exports.clear();
        
        for (const auto& export_name : exports.getMemberNames()) {
            const Json::Value& export_config = exports[export_name];
            NfsdConfig::Export export_obj;
            
            export_obj.name = export_name;
            if (export_config.isMember("path")) {
                export_obj.path = export_config["path"].asString();
            }
            if (export_config.isMember("clients")) {
                export_obj.clients = export_config["clients"].asString();
            }
            if (export_config.isMember("options")) {
                export_obj.options = export_config["options"].asString();
            }
            if (export_config.isMember("comment")) {
                export_obj.comment = export_config["comment"].asString();
            }
            
            config_.exports.push_back(export_obj);
        }
    }
    
    return true;
#else
    std::cerr << "JSON support not enabled. Rebuild with ENABLE_JSON=ON" << std::endl;
    return false;
#endif
}

bool ConfigManager::loadYaml(const std::string& filename) {
#ifdef ENABLE_YAML
    try {
        YAML::Node config = YAML::LoadFile(filename);
        
        // Parse global settings
        if (config["global"]) {
            const YAML::Node& global = config["global"];
            
            if (global["server_name"]) {
                config_.server_name = global["server_name"].as<std::string>();
            }
            if (global["server_version"]) {
                config_.server_version = global["server_version"].as<std::string>();
            }
            if (global["listen_address"]) {
                config_.listen_address = global["listen_address"].as<std::string>();
            }
            if (global["listen_port"]) {
                config_.listen_port = global["listen_port"].as<int>();
            }
            if (global["security_mode"]) {
                config_.security_mode = global["security_mode"].as<std::string>();
            }
            if (global["root_squash"]) {
                config_.root_squash = global["root_squash"].as<bool>();
            }
            if (global["all_squash"]) {
                config_.all_squash = global["all_squash"].as<bool>();
            }
            if (global["anon_uid"]) {
                config_.anon_uid = global["anon_uid"].as<int>();
            }
            if (global["anon_gid"]) {
                config_.anon_gid = global["anon_gid"].as<int>();
            }
            if (global["max_connections"]) {
                config_.max_connections = global["max_connections"].as<int>();
            }
            if (global["thread_count"]) {
                config_.thread_count = global["thread_count"].as<int>();
            }
            if (global["read_size"]) {
                config_.read_size = global["read_size"].as<int>();
            }
            if (global["write_size"]) {
                config_.write_size = global["write_size"].as<int>();
            }
            if (global["log_level"]) {
                config_.log_level = global["log_level"].as<std::string>();
            }
            if (global["log_file"]) {
                config_.log_file = global["log_file"].as<std::string>();
            }
            if (global["log_max_size"]) {
                config_.log_max_size = global["log_max_size"].as<std::string>();
            }
            if (global["log_max_files"]) {
                config_.log_max_files = global["log_max_files"].as<int>();
            }
            if (global["cache_enabled"]) {
                config_.cache_enabled = global["cache_enabled"].as<bool>();
            }
            if (global["cache_size"]) {
                config_.cache_size = global["cache_size"].as<std::string>();
            }
            if (global["cache_ttl"]) {
                config_.cache_ttl = global["cache_ttl"].as<int>();
            }
        }
        
        // Parse exports
        if (config["exports"]) {
            const YAML::Node& exports = config["exports"];
            config_.exports.clear();
            
            for (const auto& export_node : exports) {
                NfsdConfig::Export export_obj;
                
                if (export_node["name"]) {
                    export_obj.name = export_node["name"].as<std::string>();
                }
                if (export_node["path"]) {
                    export_obj.path = export_node["path"].as<std::string>();
                }
                if (export_node["clients"]) {
                    export_obj.clients = export_node["clients"].as<std::string>();
                }
                if (export_node["options"]) {
                    export_obj.options = export_node["options"].as<std::string>();
                }
                if (export_node["comment"]) {
                    export_obj.comment = export_node["comment"].as<std::string>();
                }
                
                config_.exports.push_back(export_obj);
            }
        }
        
        return true;
    } catch (const YAML::Exception& e) {
        std::cerr << "Failed to parse YAML configuration: " << e.what() << std::endl;
        return false;
    }
#else
    std::cerr << "YAML support not enabled. Rebuild with ENABLE_YAML=ON" << std::endl;
    return false;
#endif
}

bool ConfigManager::saveIni(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to create configuration file: " << filename << std::endl;
        return false;
    }
    
    file << "# Simple NFS Daemon Configuration File" << std::endl;
    file << "# Copyright 2024 SimpleDaemons" << std::endl;
    file << std::endl;
    
    // Global settings
    file << "[global]" << std::endl;
    file << "server_name = " << config_.server_name << std::endl;
    file << "server_version = " << config_.server_version << std::endl;
    file << "listen_address = " << config_.listen_address << std::endl;
    file << "listen_port = " << config_.listen_port << std::endl;
    file << "security_mode = " << config_.security_mode << std::endl;
    file << "root_squash = " << (config_.root_squash ? "true" : "false") << std::endl;
    file << "all_squash = " << (config_.all_squash ? "true" : "false") << std::endl;
    file << "anon_uid = " << config_.anon_uid << std::endl;
    file << "anon_gid = " << config_.anon_gid << std::endl;
    file << "max_connections = " << config_.max_connections << std::endl;
    file << "thread_count = " << config_.thread_count << std::endl;
    file << "read_size = " << config_.read_size << std::endl;
    file << "write_size = " << config_.write_size << std::endl;
    file << "log_level = " << config_.log_level << std::endl;
    file << "log_file = " << config_.log_file << std::endl;
    file << "log_max_size = " << config_.log_max_size << std::endl;
    file << "log_max_files = " << config_.log_max_files << std::endl;
    file << "cache_enabled = " << (config_.cache_enabled ? "true" : "false") << std::endl;
    file << "cache_size = " << config_.cache_size << std::endl;
    file << "cache_ttl = " << config_.cache_ttl << std::endl;
    file << std::endl;
    
    // Exports
    if (!config_.exports.empty()) {
        file << "[exports]" << std::endl;
        for (const auto& export_obj : config_.exports) {
            file << "# " << export_obj.comment << std::endl;
            file << "[" << export_obj.name << "]" << std::endl;
            file << "path = " << export_obj.path << std::endl;
            file << "clients = " << export_obj.clients << std::endl;
            file << "options = " << export_obj.options << std::endl;
            file << std::endl;
        }
    }
    
    return true;
}

bool ConfigManager::saveJson(const std::string& filename) const {
#ifdef ENABLE_JSON
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to create configuration file: " << filename << std::endl;
        return false;
    }
    
    Json::Value root;
    
    // Global settings
    Json::Value global;
    global["server_name"] = config_.server_name;
    global["server_version"] = config_.server_version;
    global["listen_address"] = config_.listen_address;
    global["listen_port"] = config_.listen_port;
    global["security_mode"] = config_.security_mode;
    global["root_squash"] = config_.root_squash;
    global["all_squash"] = config_.all_squash;
    global["anon_uid"] = config_.anon_uid;
    global["anon_gid"] = config_.anon_gid;
    global["max_connections"] = config_.max_connections;
    global["thread_count"] = config_.thread_count;
    global["read_size"] = config_.read_size;
    global["write_size"] = config_.write_size;
    global["log_level"] = config_.log_level;
    global["log_file"] = config_.log_file;
    global["log_max_size"] = config_.log_max_size;
    global["log_max_files"] = config_.log_max_files;
    global["cache_enabled"] = config_.cache_enabled;
    global["cache_size"] = config_.cache_size;
    global["cache_ttl"] = config_.cache_ttl;
    
    root["global"] = global;
    
    // Exports
    Json::Value exports;
    for (const auto& export_obj : config_.exports) {
        Json::Value export_json;
        export_json["name"] = export_obj.name;
        export_json["path"] = export_obj.path;
        export_json["clients"] = export_obj.clients;
        export_json["options"] = export_obj.options;
        export_json["comment"] = export_obj.comment;
        
        exports[export_obj.name] = export_json;
    }
    root["exports"] = exports;
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    
    return true;
#else
    std::cerr << "JSON support not enabled. Rebuild with ENABLE_JSON=ON" << std::endl;
    return false;
#endif
}

bool ConfigManager::saveYaml(const std::string& filename) const {
#ifdef ENABLE_YAML
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to create configuration file: " << filename << std::endl;
        return false;
    }
    
    YAML::Node root;
    
    // Global settings
    YAML::Node global;
    global["server_name"] = config_.server_name;
    global["server_version"] = config_.server_version;
    global["listen_address"] = config_.listen_address;
    global["listen_port"] = config_.listen_port;
    global["security_mode"] = config_.security_mode;
    global["root_squash"] = config_.root_squash;
    global["all_squash"] = config_.all_squash;
    global["anon_uid"] = config_.anon_uid;
    global["anon_gid"] = config_.anon_gid;
    global["max_connections"] = config_.max_connections;
    global["thread_count"] = config_.thread_count;
    global["read_size"] = config_.read_size;
    global["write_size"] = config_.write_size;
    global["log_level"] = config_.log_level;
    global["log_file"] = config_.log_file;
    global["log_max_size"] = config_.log_max_size;
    global["log_max_files"] = config_.log_max_files;
    global["cache_enabled"] = config_.cache_enabled;
    global["cache_size"] = config_.cache_size;
    global["cache_ttl"] = config_.cache_ttl;
    
    root["global"] = global;
    
    // Exports
    YAML::Node exports;
    for (const auto& export_obj : config_.exports) {
        YAML::Node export_yaml;
        export_yaml["name"] = export_obj.name;
        export_yaml["path"] = export_obj.path;
        export_yaml["clients"] = export_obj.clients;
        export_yaml["options"] = export_obj.options;
        export_yaml["comment"] = export_obj.comment;
        
        exports.push_back(export_yaml);
    }
    root["exports"] = exports;
    
    file << root;
    
    return true;
#else
    std::cerr << "YAML support not enabled. Rebuild with ENABLE_YAML=ON" << std::endl;
    return false;
#endif
}

std::string ConfigManager::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

std::vector<std::string> ConfigManager::split(const std::string& str, char delimiter) const {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

bool ConfigManager::isNumeric(const std::string& str) const {
    return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
}

int ConfigManager::stringToInt(const std::string& str) const {
    try {
        return std::stoi(str);
    } catch (const std::exception&) {
        return 0;
    }
}

bool ConfigManager::stringToBool(const std::string& str) const {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "true" || lower == "1" || lower == "yes" || lower == "on";
}

} // namespace SimpleNfsd
