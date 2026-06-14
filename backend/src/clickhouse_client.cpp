#include "clickhouse_client.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <sstream>
#include <iostream>
#include <random>

namespace haihunhou {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

ClickHouseClient::ClickHouseClient(const std::string& host, uint16_t port,
                                   const std::string& database)
    : host_(host), port_(port), database_(database), connected_(false) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    session_id_ = "session_" + std::to_string(dis(gen));
}

ClickHouseClient::~ClickHouseClient() {
    disconnect();
}

bool ClickHouseClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::string result = query("SELECT 1");
        connected_ = !result.empty();
        return connected_;
    } catch (...) {
        connected_ = false;
        return false;
    }
}

void ClickHouseClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
}

bool ClickHouseClient::isConnected() const {
    return connected_;
}

std::string ClickHouseClient::escapeString(const std::string& s) const {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '\'': result += "\\'"; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

std::string ClickHouseClient::query(const std::string& sql) {
    try {
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve(host_, std::to_string(port_));
        stream.connect(results);

        std::string url = "/?database=" + database_ + "&session_id=" + session_id_;

        http::request<http::string_body> req{http::verb::post, url, 11};
        req.set(http::field::host, host_);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set("X-ClickHouse-User", "default");
        req.set("X-ClickHouse-Database", database_);
        req.body() = sql;
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return res.body();
    } catch (std::exception const& e) {
        std::cerr << "ClickHouse query error: " << e.what() << std::endl;
        return "";
    }
}

bool ClickHouseClient::executeInsert(const std::string& table, const std::string& values) {
    std::string sql = "INSERT INTO " + table + " VALUES " + values;
    std::string result = query(sql);
    return result.empty();
}

bool ClickHouseClient::insertSpectralData(const std::vector<SpectralData>& data) {
    if (data.empty()) return true;

    std::string values;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) values += ",";
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "('1970-01-01 00:00:00' + INTERVAL %llu SECOND, %u, %u, %u, %.6f, %.2f, %.2f, %.2f)",
            (unsigned long long)data[i].timestamp, data[i].device_id, data[i].slip_id,
            data[i].wavelength, data[i].reflectance, data[i].temperature,
            data[i].humidity, data[i].light_intensity);
        values += buf;
    }

    return executeInsert("spectral_data", values);
}

bool ClickHouseClient::insertMicrobialData(const std::vector<MicrobialData>& data) {
    if (data.empty()) return true;

    std::string values;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) values += ",";
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "('1970-01-01 00:00:00' + INTERVAL %llu SECOND, %u, %u, %.2f, %.2f, %.2f, %.2f)",
            (unsigned long long)data[i].timestamp, data[i].device_id, data[i].slip_id,
            data[i].fungi_concentration, data[i].bacteria_concentration,
            data[i].temperature, data[i].humidity);
        values += buf;
    }

    return executeInsert("microbial_data", values);
}

bool ClickHouseClient::insertFadingAnalysis(const std::vector<FadingAnalysis>& data) {
    if (data.empty()) return true;

    std::string values;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) values += ",";
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "('1970-01-01 00:00:00' + INTERVAL %llu SECOND, %u, %.6f, %.4f, %.6f, %.6f, %.6f, %u)",
            (unsigned long long)data[i].timestamp, data[i].slip_id, data[i].reflectance_450nm,
            data[i].fading_rate_monthly, data[i].predicted_30d, data[i].predicted_90d,
            data[i].predicted_180d, data[i].risk_level);
        values += buf;
    }

    return executeInsert("fading_analysis", values);
}

bool ClickHouseClient::insertMoldPrediction(const std::vector<MoldPrediction>& data) {
    if (data.empty()) return true;

    std::string values;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) values += ",";
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "('1970-01-01 00:00:00' + INTERVAL %llu SECOND, %u, %.2f, %.2f, %.2f, %.2f, %u)",
            (unsigned long long)data[i].timestamp, data[i].slip_id,
            data[i].current_concentration, data[i].predicted_1d,
            data[i].predicted_3d, data[i].predicted_7d, data[i].risk_level);
        values += buf;
    }

    return executeInsert("mold_prediction", values);
}

bool ClickHouseClient::insertAlert(const Alert& alert) {
    std::string values;
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "('%s', '1970-01-01 00:00:00' + INTERVAL %llu SECOND, %u, %d, %d, '%s', %.2f, %.2f, %d, %s, %s, %s)",
        alert.alert_id.c_str(), (unsigned long long)alert.timestamp, alert.slip_id,
        (int)alert.alert_type, (int)alert.severity, escapeString(alert.message).c_str(),
        alert.threshold, alert.current_value, (int)alert.status,
        alert.acknowledged_by.empty() ? "NULL" : ("'" + escapeString(alert.acknowledged_by) + "'").c_str(),
        alert.acknowledged_time == 0 ? "NULL" : ("'1970-01-01 00:00:00' + INTERVAL " + std::to_string(alert.acknowledged_time) + " SECOND").c_str(),
        alert.resolved_time == 0 ? "NULL" : ("'1970-01-01 00:00:00' + INTERVAL " + std::to_string(alert.resolved_time) + " SECOND").c_str());
    values = buf;

    return executeInsert("alerts", values);
}

bool ClickHouseClient::getSlips(std::vector<SlipInfo>& slips, uint32_t offset, uint32_t limit) {
    std::string sql = "SELECT slip_id, device_msi, device_mc, position_x, position_y, position_z, length, width, inscription "
                      "FROM slips ORDER BY slip_id LIMIT " + std::to_string(limit) +
                      " OFFSET " + std::to_string(offset) + " FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        SlipInfo s;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 8) {
            s.slip_id = std::stoul(tokens[0]);
            s.device_msi = std::stoul(tokens[1]);
            s.device_mc = std::stoul(tokens[2]);
            s.position_x = std::stof(tokens[3]);
            s.position_y = std::stof(tokens[4]);
            s.position_z = std::stof(tokens[5]);
            s.length = std::stof(tokens[6]);
            s.width = std::stof(tokens[7]);
            if (tokens.size() > 8) s.inscription = tokens[8];
            slips.push_back(s);
        }
    }
    return !slips.empty();
}

bool ClickHouseClient::getSlipById(uint32_t slip_id, SlipInfo& slip) {
    std::vector<SlipInfo> slips;
    std::string sql = "SELECT slip_id, device_msi, device_mc, position_x, position_y, position_z, length, width, inscription "
                      "FROM slips WHERE slip_id = " + std::to_string(slip_id) +
                      " LIMIT 1 FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    if (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 8) {
            slip.slip_id = std::stoul(tokens[0]);
            slip.device_msi = std::stoul(tokens[1]);
            slip.device_mc = std::stoul(tokens[2]);
            slip.position_x = std::stof(tokens[3]);
            slip.position_y = std::stof(tokens[4]);
            slip.position_z = std::stof(tokens[5]);
            slip.length = std::stof(tokens[6]);
            slip.width = std::stof(tokens[7]);
            if (tokens.size() > 8) slip.inscription = tokens[8];
            return true;
        }
    }
    return false;
}

bool ClickHouseClient::getDevices(std::vector<DeviceInfo>& devices) {
    std::string sql = "SELECT device_id, CAST(device_type, 'Int8'), device_name, ip_address, port, "
                      "CAST(status, 'Int8'), CAST(last_heartbeat, 'UInt32') "
                      "FROM devices FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        DeviceInfo d;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 7) {
            d.device_id = std::stoul(tokens[0]);
            d.device_type = (DeviceInfo::Type)std::stoi(tokens[1]);
            d.device_name = tokens[2];
            d.ip_address = tokens[3];
            d.port = std::stoul(tokens[4]);
            d.status = (DeviceInfo::Status)std::stoi(tokens[5]);
            d.last_heartbeat = std::stoull(tokens[6]);
            devices.push_back(d);
        }
    }
    return !devices.empty();
}

bool ClickHouseClient::getSpectralData(uint32_t slip_id, uint64_t start_time, uint64_t end_time,
                                       std::vector<SpectralData>& data) {
    std::string sql = "SELECT CAST(timestamp, 'UInt32'), device_id, slip_id, wavelength, reflectance, temperature, humidity, light_intensity "
                      "FROM spectral_data WHERE slip_id = " + std::to_string(slip_id) +
                      " AND timestamp >= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(start_time) + " SECOND " +
                      " AND timestamp <= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(end_time) + " SECOND " +
                      " ORDER BY timestamp FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        SpectralData d;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 8) {
            d.timestamp = std::stoull(tokens[0]);
            d.device_id = std::stoul(tokens[1]);
            d.slip_id = std::stoul(tokens[2]);
            d.wavelength = std::stoul(tokens[3]);
            d.reflectance = std::stof(tokens[4]);
            d.temperature = std::stof(tokens[5]);
            d.humidity = std::stof(tokens[6]);
            d.light_intensity = std::stof(tokens[7]);
            data.push_back(d);
        }
    }
    return true;
}

bool ClickHouseClient::getMicrobialData(uint32_t slip_id, uint64_t start_time, uint64_t end_time,
                                        std::vector<MicrobialData>& data) {
    std::string sql = "SELECT CAST(timestamp, 'UInt32'), device_id, slip_id, fungi_concentration, bacteria_concentration, temperature, humidity "
                      "FROM microbial_data WHERE slip_id = " + std::to_string(slip_id) +
                      " AND timestamp >= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(start_time) + " SECOND " +
                      " AND timestamp <= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(end_time) + " SECOND " +
                      " ORDER BY timestamp FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        MicrobialData d;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 7) {
            d.timestamp = std::stoull(tokens[0]);
            d.device_id = std::stoul(tokens[1]);
            d.slip_id = std::stoul(tokens[2]);
            d.fungi_concentration = std::stof(tokens[3]);
            d.bacteria_concentration = std::stof(tokens[4]);
            d.temperature = std::stof(tokens[5]);
            d.humidity = std::stof(tokens[6]);
            data.push_back(d);
        }
    }
    return true;
}

bool ClickHouseClient::getFadingAnalysis(uint32_t slip_id, uint64_t start_time, uint64_t end_time,
                                         std::vector<FadingAnalysis>& data) {
    std::string sql = "SELECT CAST(timestamp, 'UInt32'), slip_id, reflectance_450nm, fading_rate_monthly, predicted_30d, predicted_90d, predicted_180d, risk_level "
                      "FROM fading_analysis WHERE slip_id = " + std::to_string(slip_id) +
                      " AND timestamp >= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(start_time) + " SECOND " +
                      " AND timestamp <= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(end_time) + " SECOND " +
                      " ORDER BY timestamp DESC LIMIT 100 FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        FadingAnalysis d;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 8) {
            d.timestamp = std::stoull(tokens[0]);
            d.slip_id = std::stoul(tokens[1]);
            d.reflectance_450nm = std::stof(tokens[2]);
            d.fading_rate_monthly = std::stof(tokens[3]);
            d.predicted_30d = std::stof(tokens[4]);
            d.predicted_90d = std::stof(tokens[5]);
            d.predicted_180d = std::stof(tokens[6]);
            d.risk_level = std::stoul(tokens[7]);
            data.push_back(d);
        }
    }
    return true;
}

bool ClickHouseClient::getMoldPrediction(uint32_t slip_id, uint64_t start_time, uint64_t end_time,
                                         std::vector<MoldPrediction>& data) {
    std::string sql = "SELECT CAST(timestamp, 'UInt32'), slip_id, current_concentration, predicted_1d, predicted_3d, predicted_7d, risk_level "
                      "FROM mold_prediction WHERE slip_id = " + std::to_string(slip_id) +
                      " AND timestamp >= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(start_time) + " SECOND " +
                      " AND timestamp <= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(end_time) + " SECOND " +
                      " ORDER BY timestamp DESC LIMIT 100 FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        MoldPrediction d;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 7) {
            d.timestamp = std::stoull(tokens[0]);
            d.slip_id = std::stoul(tokens[1]);
            d.current_concentration = std::stof(tokens[2]);
            d.predicted_1d = std::stof(tokens[3]);
            d.predicted_3d = std::stof(tokens[4]);
            d.predicted_7d = std::stof(tokens[5]);
            d.risk_level = std::stoul(tokens[6]);
            data.push_back(d);
        }
    }
    return true;
}

bool ClickHouseClient::getAlerts(uint64_t start_time, uint64_t end_time,
                                 std::vector<Alert>& alerts, int status) {
    std::string sql = "SELECT toString(alert_id), CAST(timestamp, 'UInt32'), slip_id, CAST(alert_type, 'Int8'), "
                      "CAST(severity, 'Int8'), message, threshold, current_value, CAST(status, 'Int8'), "
                      "acknowledged_by, CAST(acknowledged_time, 'UInt32'), CAST(resolved_time, 'UInt32') "
                      "FROM alerts WHERE timestamp >= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(start_time) + " SECOND " +
                      " AND timestamp <= '1970-01-01 00:00:00' + INTERVAL " + std::to_string(end_time) + " SECOND ";
    if (status > 0) {
        sql += " AND status = " + std::to_string(status);
    }
    sql += " ORDER BY timestamp DESC LIMIT 200 FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        Alert a;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 12) {
            a.alert_id = tokens[0];
            a.timestamp = std::stoull(tokens[1]);
            a.slip_id = std::stoul(tokens[2]);
            a.alert_type = (Alert::Type)std::stoi(tokens[3]);
            a.severity = (Alert::Severity)std::stoi(tokens[4]);
            a.message = tokens[5];
            a.threshold = std::stof(tokens[6]);
            a.current_value = std::stof(tokens[7]);
            a.status = (Alert::Status)std::stoi(tokens[8]);
            a.acknowledged_by = tokens[9];
            a.acknowledged_time = std::stoull(tokens[10]);
            a.resolved_time = std::stoull(tokens[11]);
            alerts.push_back(a);
        }
    }
    return true;
}

bool ClickHouseClient::updateAlertStatus(const std::string& alert_id, int status,
                                         const std::string& user) {
    std::string sql;
    if (status == 2) {
        sql = "ALTER TABLE alerts UPDATE status = 2, acknowledged_by = '" + escapeString(user) +
              "', acknowledged_time = now() WHERE alert_id = '" + escapeString(alert_id) + "'";
    } else if (status == 3) {
        sql = "ALTER TABLE alerts UPDATE status = 3, resolved_time = now() "
              "WHERE alert_id = '" + escapeString(alert_id) + "'";
    } else if (status == 4) {
        sql = "ALTER TABLE alerts UPDATE status = 4 WHERE alert_id = '" + escapeString(alert_id) + "'";
    } else {
        return false;
    }
    query(sql);
    return true;
}

bool ClickHouseClient::getConfig(ModelParams& model_params, AlertConfig& alert_config) {
    std::string sql = "SELECT config_key, config_value FROM system_config FORMAT TabSeparated";
    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    std::unordered_map<std::string, std::string> config;
    while (std::getline(iss, line)) {
        size_t pos = line.find('\t');
        if (pos != std::string::npos) {
            config[line.substr(0, pos)] = line.substr(pos + 1);
        }
    }

    if (config.count("alert.fading.threshold"))
        alert_config.fading_threshold = std::stof(config["alert.fading.threshold"]);
    if (config.count("alert.mold.threshold"))
        alert_config.mold_threshold = std::stof(config["alert.mold.threshold"]);
    if (config.count("notification.dingtalk.webhook"))
        alert_config.dingtalk_webhook = config["notification.dingtalk.webhook"];
    if (config.count("notification.dingtalk.secret"))
        alert_config.dingtalk_secret = config["notification.dingtalk.secret"];
    if (config.count("notification.smtp.host"))
        alert_config.smtp_host = config["notification.smtp.host"];
    if (config.count("notification.smtp.port"))
        alert_config.smtp_port = std::stoul(config["notification.smtp.port"]);
    if (config.count("notification.smtp.user"))
        alert_config.smtp_user = config["notification.smtp.user"];
    if (config.count("notification.smtp.password"))
        alert_config.smtp_password = config["notification.smtp.password"];
    if (config.count("notification.smtp.from"))
        alert_config.smtp_from = config["notification.smtp.from"];
    if (config.count("notification.smtp.to")) {
        std::string to = config["notification.smtp.to"];
        size_t pos = 0;
        while ((pos = to.find(',')) != std::string::npos) {
            alert_config.smtp_to.push_back(to.substr(0, pos));
            to.erase(0, pos + 1);
        }
        if (!to.empty()) alert_config.smtp_to.push_back(to);
    }

    if (config.count("model.fading.A1"))
        model_params.fading_A1 = std::stof(config["model.fading.A1"]);
    if (config.count("model.fading.Ea1"))
        model_params.fading_Ea1 = std::stof(config["model.fading.Ea1"]);
    if (config.count("model.fading.alpha"))
        model_params.fading_alpha = std::stof(config["model.fading.alpha"]);
    if (config.count("model.fading.A2"))
        model_params.fading_A2 = std::stof(config["model.fading.A2"]);
    if (config.count("model.fading.Ea2"))
        model_params.fading_Ea2 = std::stof(config["model.fading.Ea2"]);
    if (config.count("model.fading.beta"))
        model_params.fading_beta = std::stof(config["model.fading.beta"]);
    if (config.count("model.mold.beta0"))
        model_params.mold_beta0 = std::stof(config["model.mold.beta0"]);
    if (config.count("model.mold.beta1"))
        model_params.mold_beta1 = std::stof(config["model.mold.beta1"]);
    if (config.count("model.mold.beta2"))
        model_params.mold_beta2 = std::stof(config["model.mold.beta2"]);
    if (config.count("model.mold.beta3"))
        model_params.mold_beta3 = std::stof(config["model.mold.beta3"]);
    if (config.count("model.mold.beta4"))
        model_params.mold_beta4 = std::stof(config["model.mold.beta4"]);
    if (config.count("model.mold.beta5"))
        model_params.mold_beta5 = std::stof(config["model.mold.beta5"]);

    return true;
}

bool ClickHouseClient::getDashboardStats(int& total_slips, int& online_devices,
                                         int& critical_alerts, int& warning_alerts,
                                         float& avg_fading_rate, float& avg_mold_concentration) {
    std::string sql = "SELECT "
                      "(SELECT count() FROM slips), "
                      "(SELECT count() FROM devices WHERE status = 1), "
                      "(SELECT count() FROM alerts WHERE severity = 3 AND status = 1), "
                      "(SELECT count() FROM alerts WHERE severity = 2 AND status = 1), "
                      "(SELECT avg(fading_rate_monthly) FROM fading_analysis WHERE timestamp >= now() - INTERVAL 1 DAY), "
                      "(SELECT avg(current_concentration) FROM mold_prediction WHERE timestamp >= now() - INTERVAL 1 DAY) "
                      "FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    if (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 6) {
            total_slips = std::stoi(tokens[0]);
            online_devices = std::stoi(tokens[1]);
            critical_alerts = std::stoi(tokens[2]);
            warning_alerts = std::stoi(tokens[3]);
            avg_fading_rate = tokens[4].empty() ? 0 : std::stof(tokens[4]);
            avg_mold_concentration = tokens[5].empty() ? 0 : std::stof(tokens[5]);
            return true;
        }
    }
    return false;
}

bool ClickHouseClient::getAllSlipsStatus(std::vector<std::tuple<uint32_t, uint8_t, uint8_t>>& statuses) {
    std::string sql = "SELECT "
                      "s.slip_id, "
                      "COALESCE(f.risk_level, 1), "
                      "COALESCE(m.risk_level, 1) "
                      "FROM slips s "
                      "LEFT JOIN ("
                      "    SELECT slip_id, max(risk_level) as risk_level "
                      "    FROM fading_analysis "
                      "    WHERE timestamp >= now() - INTERVAL 1 DAY "
                      "    GROUP BY slip_id"
                      ") f ON s.slip_id = f.slip_id "
                      "LEFT JOIN ("
                      "    SELECT slip_id, max(risk_level) as risk_level "
                      "    FROM mold_prediction "
                      "    WHERE timestamp >= now() - INTERVAL 1 DAY "
                      "    GROUP BY slip_id"
                      ") m ON s.slip_id = m.slip_id "
                      "ORDER BY s.slip_id "
                      "FORMAT TabSeparated";

    std::string result = query(sql);
    if (result.empty()) return false;

    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ls, token, '\t')) tokens.push_back(token);
        if (tokens.size() >= 3) {
            uint32_t slip_id = std::stoul(tokens[0]);
            uint8_t fade_risk = std::stoul(tokens[1]);
            uint8_t mold_risk = std::stoul(tokens[2]);
            statuses.emplace_back(slip_id, fade_risk, mold_risk);
        }
    }
    return !statuses.empty();
}

}
