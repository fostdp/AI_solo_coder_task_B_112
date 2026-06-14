#pragma once
#include "common.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

namespace haihunhou {

struct ServerConfig {
    struct ClickHouse {
        std::string host = "127.0.0.1";
        uint16_t port = 8123;
        std::string database = "haihunhou";
        std::string user = "default";
        std::string password;
        uint32_t timeout_ms = 10000;
        uint32_t pool_size = 10;
    } clickhouse;

    struct HttpServer {
        std::string host = "0.0.0.0";
        uint16_t port = 8080;
        std::string frontend_dir = "./frontend";
    } http_server;

    struct WebSocket {
        uint32_t ping_interval_sec = 30;
        uint32_t max_connections = 100;
    } websocket;

    struct OpcUa {
        bool enabled = false;
        std::string server_url = "opc.tcp://localhost:4840";
        uint32_t polling_interval_sec = 21600;
        uint32_t timeout_ms = 5000;
        bool simulation_mode = true;
    } opcua;

    FadingParams fading_params;
    MoldParams mold_params;
    AlertBrokerConfig alert_config;
    AlertConfig notification_config;
};

class ConfigLoader {
public:
    explicit ConfigLoader(const std::string& config_path);

    bool load();
    bool reload();
    const ServerConfig& config() const;
    std::string configPath() const;

    float getFloat(const std::string& key, float default_val = 0.0f) const;
    int getInt(const std::string& key, int default_val = 0) const;
    std::string getString(const std::string& key, const std::string& default_val = "") const;

    using ChangeCallback = std::function<void(const std::string& key)>;
    void registerListener(const std::string& key_prefix, ChangeCallback cb);
    void checkAndReload();

private:
    std::string config_path_;
    ServerConfig config_;
    std::unordered_map<std::string, std::string> flat_cache_;
    std::chrono::system_clock::time_point last_mtime_;
    std::vector<std::pair<std::string, ChangeCallback>> listeners_;
    mutable std::mutex mutex_;

    bool parseYaml(const std::string& content);
    std::string readFile(const std::string& path) const;
    std::chrono::system_clock::time_point getFileModTime() const;
    void flattenConfig();
    void notifyListeners(const std::string& changed_prefix);
};

}
