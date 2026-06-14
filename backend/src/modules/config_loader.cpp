#include "config_loader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace haihunhou {

ConfigLoader::ConfigLoader(const std::string& config_path)
    : config_path_(config_path) {
    load();
}

bool ConfigLoader::load() {
    std::string content = readFile(config_path_);
    if (content.empty()) {
        std::cerr << "ConfigLoader: empty or missing config file: " << config_path_ << std::endl;
        return false;
    }

    if (!parseYaml(content)) {
        std::cerr << "ConfigLoader: failed to parse config" << std::endl;
        return false;
    }

    last_mtime_ = getFileModTime();
    flattenConfig();
    return true;
}

bool ConfigLoader::reload() {
    auto mtime = getFileModTime();
    if (mtime <= last_mtime_) return false;

    std::string old_prefix = "";
    if (load()) {
        notifyListeners(old_prefix);
        return true;
    }
    return false;
}

const ServerConfig& ConfigLoader::config() const {
    return config_;
}

std::string ConfigLoader::configPath() const {
    return config_path_;
}

float ConfigLoader::getFloat(const std::string& key, float default_val) const {
    auto it = flat_cache_.find(key);
    if (it != flat_cache_.end()) {
        try { return std::stof(it->second); } catch (...) {}
    }
    return default_val;
}

int ConfigLoader::getInt(const std::string& key, int default_val) const {
    auto it = flat_cache_.find(key);
    if (it != flat_cache_.end()) {
        try { return std::stoi(it->second); } catch (...) {}
    }
    return default_val;
}

std::string ConfigLoader::getString(const std::string& key, const std::string& default_val) const {
    auto it = flat_cache_.find(key);
    return it != flat_cache_.end() ? it->second : default_val;
}

void ConfigLoader::registerListener(const std::string& key_prefix, ChangeCallback cb) {
    listeners_.emplace_back(key_prefix, std::move(cb));
}

void ConfigLoader::checkAndReload() {
    reload();
}

bool ConfigLoader::parseYaml(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    std::vector<std::string> path;

    while (std::getline(stream, line)) {
        size_t comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.pop_back();
        if (line.empty()) continue;

        size_t indent = 0;
        while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) indent++;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(indent, colon - indent);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        while (!value.empty() && value.back() == ' ') value.pop_back();

        size_t depth = indent / 2;
        while (path.size() > depth) path.pop_back();
        path.push_back(key);

        if (!value.empty()) {
            std::string full_key;
            for (size_t i = 0; i < path.size(); i++) {
                if (i > 0) full_key += ".";
                full_key += path[i];
            }
            flat_cache_[full_key] = value;
        }
    }

    config_.clickhouse.host = getString("clickhouse.host", "127.0.0.1");
    config_.clickhouse.port = static_cast<uint16_t>(getInt("clickhouse.port", 8123));
    config_.clickhouse.database = getString("clickhouse.database", "haihunhou");
    config_.clickhouse.user = getString("clickhouse.user", "default");
    config_.clickhouse.password = getString("clickhouse.password", "");
    config_.clickhouse.timeout_ms = static_cast<uint32_t>(getInt("clickhouse.timeout_ms", 10000));
    config_.clickhouse.pool_size = static_cast<uint32_t>(getInt("clickhouse.pool_size", 10));

    config_.http_server.host = getString("http_server.host", "0.0.0.0");
    config_.http_server.port = static_cast<uint16_t>(getInt("http_server.port", 8080));
    config_.http_server.frontend_dir = getString("http_server.frontend_dir", "./frontend");

    config_.websocket.ping_interval_sec = static_cast<uint32_t>(getInt("websocket.ping_interval_sec", 30));
    config_.websocket.max_connections = static_cast<uint32_t>(getInt("websocket.max_connections", 100));

    config_.opcua.enabled = getString("opcua.enabled", "false") == "true";
    config_.opcua.server_url = getString("opcua.server_url", "opc.tcp://localhost:4840");
    config_.opcua.polling_interval_sec = static_cast<uint32_t>(getInt("opcua.polling_interval_sec", 21600));
    config_.opcua.timeout_ms = static_cast<uint32_t>(getInt("opcua.timeout_ms", 5000));
    config_.opcua.simulation_mode = getString("opcua.simulation_mode", "true") == "true";

    config_.fading_params.A1 = getFloat("algorithm.fading_model.A1", 1.2e-8f);
    config_.fading_params.Ea1 = getFloat("algorithm.fading_model.Ea1", 45.0f);
    config_.fading_params.alpha = getFloat("algorithm.fading_model.alpha", 0.8f);
    config_.fading_params.A2 = getFloat("algorithm.fading_model.A2", 8.5e-7f);
    config_.fading_params.Ea2 = getFloat("algorithm.fading_model.Ea2", 55.0f);
    config_.fading_params.beta = getFloat("algorithm.fading_model.beta", 1.5f);
    config_.fading_params.lambda0 = getFloat("algorithm.fading_model.lambda0", 450.0f);
    config_.fading_params.min_data_points = static_cast<uint32_t>(getInt("algorithm.fading_model.min_data_points", 8));
    config_.fading_params.max_lm_iterations = static_cast<uint32_t>(getInt("algorithm.fading_model.max_lm_iterations", 100));
    config_.fading_params.lm_damping_init = getFloat("algorithm.fading_model.lm_damping_init", 0.001f);
    config_.fading_params.lm_tolerance = getFloat("algorithm.fading_model.lm_tolerance", 1e-6f);

    config_.mold_params.beta0 = getFloat("algorithm.mold_model.beta0", -2.5f);
    config_.mold_params.beta1 = getFloat("algorithm.mold_model.beta1", 0.12f);
    config_.mold_params.beta2 = getFloat("algorithm.mold_model.beta2", 0.08f);
    config_.mold_params.beta3 = getFloat("algorithm.mold_model.beta3", -0.0015f);
    config_.mold_params.beta4 = getFloat("algorithm.mold_model.beta4", -0.0008f);
    config_.mold_params.beta5 = getFloat("algorithm.mold_model.beta5", 0.0005f);
    config_.mold_params.mu_opt = getFloat("algorithm.mold_model.mu_opt", 0.05f);
    config_.mold_params.T_opt = getFloat("algorithm.mold_model.T_opt", 25.0f);
    config_.mold_params.RH_opt = getFloat("algorithm.mold_model.RH_opt", 65.0f);
    config_.mold_params.sigma_T = getFloat("algorithm.mold_model.sigma_T", 8.0f);
    config_.mold_params.sigma_RH = getFloat("algorithm.mold_model.sigma_RH", 15.0f);
    config_.mold_params.response_weight = getFloat("algorithm.mold_model.response_weight", 0.6f);
    config_.mold_params.gaussian_weight = getFloat("algorithm.mold_model.gaussian_weight", 0.4f);

    config_.alert_config.fading_threshold = getFloat("alert.fading_threshold", 20.0f);
    config_.alert_config.mold_threshold = getFloat("alert.mold_threshold", 100.0f);
    config_.alert_config.suppression_window_ms = static_cast<uint64_t>(getInt("alert.suppression_window_sec", 86400)) * 1000ULL;
    config_.alert_config.aggregation_window_sec = static_cast<uint32_t>(getInt("alert.aggregation_window_sec", 60));
    config_.alert_config.max_alerts_per_window = static_cast<uint32_t>(getInt("alert.max_alerts_per_window", 50));
    config_.alert_config.enable_dingtalk = getString("notification.dingtalk.enabled", "false") == "true";
    config_.alert_config.enable_email = getString("notification.email.enabled", "false") == "true";

    config_.notification_config.fading_threshold = config_.alert_config.fading_threshold;
    config_.notification_config.mold_threshold = config_.alert_config.mold_threshold;
    config_.notification_config.dingtalk_webhook = getString("notification.dingtalk.webhook_url", "");
    config_.notification_config.dingtalk_secret = getString("notification.dingtalk.secret", "");
    config_.notification_config.smtp_host = getString("notification.email.smtp_server", "");
    config_.notification_config.smtp_port = static_cast<uint16_t>(getInt("notification.email.smtp_port", 465));
    config_.notification_config.smtp_user = getString("notification.email.username", "");
    config_.notification_config.smtp_password = getString("notification.email.password", "");
    config_.notification_config.smtp_from = getString("notification.email.sender_name", "");

    std::string recipients = getString("notification.email.recipients", "");
    if (!recipients.empty()) {
        std::istringstream rs(recipients);
        std::string r;
        while (std::getline(rs, r, ',')) {
            while (!r.empty() && r.front() == ' ') r.erase(r.begin());
            while (!r.empty() && r.back() == ' ') r.pop_back();
            if (!r.empty()) config_.notification_config.smtp_to.push_back(r);
        }
    }

    return true;
}

std::string ConfigLoader::readFile(const std::string& path) const {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::chrono::system_clock::time_point ConfigLoader::getFileModTime() const {
    return std::chrono::system_clock::now();
}

void ConfigLoader::flattenConfig() {
}

void ConfigLoader::notifyListeners(const std::string&) {
    for (auto& [prefix, cb] : listeners_) {
        cb(prefix);
    }
}

}
