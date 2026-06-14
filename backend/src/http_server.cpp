#include "http_server.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <iomanip>
#include <iostream>

namespace haihunhou {

HttpServer::HttpServer(ClickHouseClient& db, AlertEngine& alert_engine,
                     FadingModel& fading_model, MoldModel& mold_model,
                     uint16_t port, const std::string& doc_root)
    : db_(db), alert_engine_(alert_engine),
      fading_model_(fading_model),
      mold_model_(mold_model),
      port_(port), doc_root_(doc_root),
      running_(false) {
}

HttpServer::~HttpServer() {
    stop();
}

std::string HttpServer::escapeJson(const std::string& s) const {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

std::string HttpServer::jsonResponse(bool success, const std::string& message, const std::string& data) {
    std::ostringstream oss;
    oss << "{"
        << "\"success\":" << (success ? "true" : "false") << ","
        << "\"message\":\"" << escapeJson(message) << "\","
        << "\"data\":" << data
        << "}";
    return oss.str();
}

std::unordered_map<std::string, std::string> HttpServer::parseQueryString(const std::string& query) const {
    std::unordered_map<std::string, std::string> params;
    size_t pos = 0;
    std::string rest = query;
    while ((pos = rest.find('&')) != std::string::npos) {
        std::string pair = rest.substr(0, pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            params[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
        rest = rest.substr(pos + 1);
    }
    if (!rest.empty()) {
        size_t eq = rest.find('=');
        if (eq != std::string::npos) {
            params[rest.substr(0, eq)] = rest.substr(eq + 1);
        }
    }
    return params;
}

bool HttpServer::serveStaticFile(const std::string& path, http::response<http::string_body>& res) {
    std::string full_path = doc_root_ + path;
    if (full_path.back() == '/') full_path += "index.html";

    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    res.body() = content;

    std::string ext = full_path.substr(full_path.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "html") res.set(http::field::content_type, "text/html; charset=utf-8");
    else if (ext == "css") res.set(http::field::content_type, "text/css; charset=utf-8");
    else if (ext == "js") res.set(http::field::content_type, "application/javascript; charset=utf-8");
    else if (ext == "json") res.set(http::field::content_type, "application/json; charset=utf-8");
    else if (ext == "png") res.set(http::field::content_type, "image/png");
    else if (ext == "jpg" || ext == "jpeg") res.set(http::field::content_type, "image/jpeg");
    else if (ext == "svg") res.set(http::field::content_type, "image/svg+xml");
    else res.set(http::field::content_type, "application/octet-stream");

    res.prepare_payload();
    return true;
}

void HttpServer::apiGetSlips(const std::unordered_map<std::string, std::string>& params, std::string& response) {
    uint32_t offset = 0, limit = 5000;
    if (params.count("offset")) offset = std::stoul(params.at("offset"));
    if (params.count("limit")) limit = std::stoul(params.at("limit"));

    std::vector<SlipInfo> slips;
    bool success = db_.getSlips(slips, offset, limit);

    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < slips.size(); ++i) {
        if (i > 0) data << ",";
        data << "{"
             << "\"slip_id\":" << slips[i].slip_id << ","
             << "\"device_msi\":" << slips[i].device_msi << ","
             << "\"device_mc\":" << slips[i].device_mc << ","
             << "\"position_x\":" << slips[i].position_x << ","
             << "\"position_y\":" << slips[i].position_y << ","
             << "\"position_z\":" << slips[i].position_z << ","
             << "\"length\":" << slips[i].length << ","
             << "\"width\":" << slips[i].width << ","
             << "\"inscription\":\"" << escapeJson(slips[i].inscription) << "\""
             << "}";
    }
    data << "]";

    response = jsonResponse(success, success ? "" : "Failed to get slips", data.str());
}

void HttpServer::apiGetSlipById(uint32_t slip_id, std::string& response) {
    SlipInfo slip;
    bool success = db_.getSlipById(slip_id, slip);

    std::ostringstream data;
    if (success) {
        data << "{"
             << "\"slip_id\":" << slip.slip_id << ","
             << "\"device_msi\":" << slip.device_msi << ","
             << "\"device_mc\":" << slip.device_mc << ","
             << "\"position_x\":" << slip.position_x << ","
             << "\"position_y\":" << slip.position_y << ","
             << "\"position_z\":" << slip.position_z << ","
             << "\"length\":" << slip.length << ","
             << "\"width\":" << slip.width << ","
             << "\"inscription\":\"" << escapeJson(slip.inscription) << "\""
             << "}";
    } else {
        data << "null";
    }

    response = jsonResponse(success, success ? "" : "Slip not found", data.str());
}

void HttpServer::apiGetSlipSpectral(uint32_t slip_id, const std::unordered_map<std::string, std::string>& params, std::string& response) {
    uint64_t start_time = now_s() - 30 * 24 * 3600;
    uint64_t end_time = now_s();
    if (params.count("start")) start_time = std::stoull(params.at("start"));
    if (params.count("end")) end_time = std::stoull(params.at("end"));

    std::vector<SpectralData> data_vec;
    bool success = db_.getSpectralData(slip_id, start_time, end_time, data_vec);

    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < data_vec.size(); ++i) {
        if (i > 0) data << ",";
        data << "{"
             << "\"timestamp\":" << data_vec[i].timestamp << ","
             << "\"wavelength\":" << data_vec[i].wavelength << ","
             << "\"reflectance\":" << data_vec[i].reflectance << ","
             << "\"temperature\":" << data_vec[i].temperature << ","
             << "\"humidity\":" << data_vec[i].humidity << ","
             << "\"light_intensity\":" << data_vec[i].light_intensity
             << "}";
    }
    data << "]";

    response = jsonResponse(success, "", data.str());
}

void HttpServer::apiGetSlipMicrobial(uint32_t slip_id, const std::unordered_map<std::string, std::string>& params, std::string& response) {
    uint64_t start_time = now_s() - 30 * 24 * 3600;
    uint64_t end_time = now_s();
    if (params.count("start")) start_time = std::stoull(params.at("start"));
    if (params.count("end")) end_time = std::stoull(params.at("end"));

    std::vector<MicrobialData> data_vec;
    bool success = db_.getMicrobialData(slip_id, start_time, end_time, data_vec);

    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < data_vec.size(); ++i) {
        if (i > 0) data << ",";
        data << "{"
             << "\"timestamp\":" << data_vec[i].timestamp << ","
             << "\"fungi\":" << data_vec[i].fungi_concentration << ","
             << "\"bacteria\":" << data_vec[i].bacteria_concentration << ","
             << "\"temperature\":" << data_vec[i].temperature << ","
             << "\"humidity\":" << data_vec[i].humidity
             << "}";
    }
    data << "]";

    response = jsonResponse(success, "", data.str());
}

void HttpServer::apiGetSlipFading(uint32_t slip_id, const std::unordered_map<std::string, std::string>& params, std::string& response) {
    uint64_t start_time = now_s() - 30 * 24 * 3600;
    uint64_t end_time = now_s();
    if (params.count("start")) start_time = std::stoull(params.at("start"));
    if (params.count("end")) end_time = std::stoull(params.at("end"));

    std::vector<FadingAnalysis> data_vec;
    bool success = db_.getFadingAnalysis(slip_id, start_time, end_time, data_vec);

    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < data_vec.size(); ++i) {
        if (i > 0) data << ",";
        data << "{"
             << "\"timestamp\":" << data_vec[i].timestamp << ","
             << "\"reflectance_450nm\":" << data_vec[i].reflectance_450nm << ","
             << "\"fading_rate_monthly\":" << data_vec[i].fading_rate_monthly << ","
             << "\"predicted_30d\":" << data_vec[i].predicted_30d << ","
             << "\"predicted_90d\":" << data_vec[i].predicted_90d << ","
             << "\"predicted_180d\":" << data_vec[i].predicted_180d << ","
             << "\"risk_level\":" << (int)data_vec[i].risk_level
             << "}";
    }
    data << "]";

    response = jsonResponse(success, "", data.str());
}

void HttpServer::apiGetSlipMold(uint32_t slip_id, const std::unordered_map<std::string, std::string>& params, std::string& response) {
    uint64_t start_time = now_s() - 30 * 24 * 3600;
    uint64_t end_time = now_s();
    if (params.count("start")) start_time = std::stoull(params.at("start"));
    if (params.count("end")) end_time = std::stoull(params.at("end"));

    std::vector<MoldPrediction> data_vec;
    bool success = db_.getMoldPrediction(slip_id, start_time, end_time, data_vec);

    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < data_vec.size(); ++i) {
        if (i > 0) data << ",";
        data << "{"
             << "\"timestamp\":" << data_vec[i].timestamp << ","
             << "\"current\":" << data_vec[i].current_concentration << ","
             << "\"predicted_1d\":" << data_vec[i].predicted_1d << ","
             << "\"predicted_3d\":" << data_vec[i].predicted_3d << ","
             << "\"predicted_7d\":" << data_vec[i].predicted_7d << ","
             << "\"risk_level\":" << (int)data_vec[i].risk_level
             << "}";
    }
    data << "]";

    response = jsonResponse(success, "", data.str());
}

void HttpServer::apiGetAlerts(const std::unordered_map<std::string, std::string>& params, std::string& response) {
    uint64_t start_time = now_s() - 7 * 24 * 3600;
    uint64_t end_time = now_s();
    int status = 0;
    if (params.count("start")) start_time = std::stoull(params.at("start"));
    if (params.count("end")) end_time = std::stoull(params.at("end"));
    if (params.count("status")) status = std::stoi(params.at("status"));

    std::vector<Alert> alerts;
    bool success = db_.getAlerts(start_time, end_time, alerts, status);

    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < alerts.size(); ++i) {
        if (i > 0) data << ",";
        data << "{"
             << "\"alert_id\":\"" << alerts[i].alert_id << "\","
             << "\"timestamp\":" << alerts[i].timestamp << ","
             << "\"slip_id\":" << alerts[i].slip_id << ","
             << "\"type\":" << (int)alerts[i].alert_type << ","
             << "\"severity\":" << (int)alerts[i].severity << ","
             << "\"message\":\"" << escapeJson(alerts[i].message) << "\","
             << "\"threshold\":" << alerts[i].threshold << ","
             << "\"current_value\":" << alerts[i].current_value << ","
             << "\"status\":" << (int)alerts[i].status << ","
             << "\"acknowledged_by\":\"" << escapeJson(alerts[i].acknowledged_by) << "\","
             << "\"acknowledged_time\":" << alerts[i].acknowledged_time << ","
             << "\"resolved_time\":" << alerts[i].resolved_time
             << "}";
    }
    data << "]";

    response = jsonResponse(success, "", data.str());
}

void HttpServer::apiPostAlertAcknowledge(const std::string& alert_id, const std::string& body, std::string& response) {
    std::string user = "admin";
    size_t pos = body.find("\"user\"");
    if (pos != std::string::npos) {
        size_t start = body.find(":", pos) + 1;
        size_t end = body.find("\"", start + 1);
        if (end != std::string::npos) {
            user = body.substr(start + 1, end - start - 1);
        }
    }

    bool success = alert_engine_.acknowledgeAlert(alert_id, user);
    response = jsonResponse(success, success ? "Alert acknowledged" : "Failed to acknowledge alert", "null");

    if (success) {
        std::ostringstream oss;
        oss << "{\"type\":\"alert_update\",\"alert_id\":\"" << alert_id << "\"}";
        broadcastWebSocket(oss.str());
    }
}

void HttpServer::apiPostAlertResolve(const std::string& alert_id, std::string& response) {
    bool success = alert_engine_.resolveAlert(alert_id);
    response = jsonResponse(success, success ? "Alert resolved" : "Failed to resolve alert", "null");

    if (success) {
        std::ostringstream oss;
        oss << "{\"type\":\"alert_update\",\"alert_id\":\"" << alert_id << "\"}";
        broadcastWebSocket(oss.str());
    }
}

void HttpServer::apiGetDevices(std::string& response) {
    std::vector<DeviceInfo> devices;
    bool success = db_.getDevices(devices);

    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < devices.size(); ++i) {
        if (i > 0) data << ",";
        data << "{"
             << "\"device_id\":" << devices[i].device_id << ","
             << "\"type\":" << (int)devices[i].device_type << ","
             << "\"name\":\"" << escapeJson(devices[i].device_name) << "\","
             << "\"ip\":\"" << escapeJson(devices[i].ip_address) << "\","
             << "\"port\":" << devices[i].port << ","
             << "\"status\":" << (int)devices[i].status << ","
             << "\"last_heartbeat\":" << devices[i].last_heartbeat
             << "}";
    }
    data << "]";

    response = jsonResponse(success, "", data.str());
}

void HttpServer::apiGetDashboardStats(std::string& response) {
    int total_slips = 0, online_devices = 0, critical_alerts = 0, warning_alerts = 0;
    float avg_fading_rate = 0, avg_mold_concentration = 0;

    bool success = db_.getDashboardStats(total_slips, online_devices, critical_alerts, warning_alerts,
                       avg_fading_rate, avg_mold_concentration);

    std::ostringstream data;
    data << "{"
         << "\"total_slips\":" << total_slips << ","
         << "\"online_devices\":" << online_devices << ","
         << "\"critical_alerts\":" << critical_alerts << ","
         << "\"warning_alerts\":" << warning_alerts << ","
         << "\"avg_fading_rate\":" << std::fixed << std::setprecision(2) << avg_fading_rate << ","
         << "\"avg_mold_concentration\":" << std::fixed << std::setprecision(1) << avg_mold_concentration
         << "}";

    response = jsonResponse(success, "", data.str());
}

void HttpServer::apiGetSlipsStatus(std::string& response) {
    std::vector<std::tuple<uint32_t, uint8_t, uint8_t>> statuses;
    bool success = db_.getAllSlipsStatus(statuses);

    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < statuses.size(); ++i) {
        if (i > 0) data << ",";
        data << "{"
             << "\"slip_id\":" << std::get<0>(statuses[i]) << ","
             << "\"fade_risk\":" << (int)std::get<1>(statuses[i]) << ","
             << "\"mold_risk\":" << (int)std::get<2>(statuses[i])
             << "}";
    }
    data << "]";

    response = jsonResponse(success, "", data.str());
}

void HttpServer::apiPostIngestSpectral(const std::string& body, std::string& response) {
    std::vector<SpectralData> data_vec;
    size_t pos = 0;
    while ((pos = body.find("\"slip_id\"", pos)) != std::string::npos) {
        SpectralData d;
        size_t val_start = body.find(":", pos) + 1;
        size_t val_end = body.find_first_of(",}", val_start);
        d.slip_id = std::stoul(body.substr(val_start, val_end - val_start));

        pos = body.find("\"timestamp\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.timestamp = std::stoull(body.substr(val_start, val_end - val_start));

        pos = body.find("\"device_id\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.device_id = std::stoul(body.substr(val_start, val_end - val_start));

        pos = body.find("\"wavelength\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.wavelength = std::stoul(body.substr(val_start, val_end - val_start));

        pos = body.find("\"reflectance\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.reflectance = std::stof(body.substr(val_start, val_end - val_start));

        pos = body.find("\"temperature\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.temperature = std::stof(body.substr(val_start, val_end - val_start));

        pos = body.find("\"humidity\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.humidity = std::stof(body.substr(val_start, val_end - val_start));

        pos = body.find("\"light_intensity\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.light_intensity = std::stof(body.substr(val_start, val_end - val_start));

        data_vec.push_back(d);
    }

    bool success = db_.insertSpectralData(data_vec);
    if (success) {
        std::thread([this, data_vec]() {
            alert_engine_.processNewSpectralData(data_vec);
            std::ostringstream oss;
            oss << "{\"type\":\"spectral_update\",\"count\":" << data_vec.size() << "}";
            broadcastWebSocket(oss.str());
        }).detach();
    }

    std::ostringstream data;
    data << "{\"inserted\":" << data_vec.size() << "}";
    response = jsonResponse(success, success ? "Data ingested" : "Failed to ingest", data.str());
}

void HttpServer::apiPostIngestMicrobial(const std::string& body, std::string& response) {
    std::vector<MicrobialData> data_vec;
    size_t pos = 0;
    while ((pos = body.find("\"slip_id\"", pos)) != std::string::npos) {
        MicrobialData d;
        size_t val_start = body.find(":", pos) + 1;
        size_t val_end = body.find_first_of(",}", val_start);
        d.slip_id = std::stoul(body.substr(val_start, val_end - val_start));

        pos = body.find("\"timestamp\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.timestamp = std::stoull(body.substr(val_start, val_end - val_start));

        pos = body.find("\"device_id\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.device_id = std::stoul(body.substr(val_start, val_end - val_start));

        pos = body.find("\"fungi_concentration\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.fungi_concentration = std::stof(body.substr(val_start, val_end - val_start));

        pos = body.find("\"bacteria_concentration\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.bacteria_concentration = std::stof(body.substr(val_start, val_end - val_start));

        pos = body.find("\"temperature\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.temperature = std::stof(body.substr(val_start, val_end - val_start));

        pos = body.find("\"humidity\"", pos);
        val_start = body.find(":", pos) + 1;
        val_end = body.find_first_of(",}", val_start);
        d.humidity = std::stof(body.substr(val_start, val_end - val_start));

        data_vec.push_back(d);
    }

    bool success = db_.insertMicrobialData(data_vec);
    if (success) {
        std::thread([this, data_vec]() {
            alert_engine_.processNewMicrobialData(data_vec);
            std::ostringstream oss;
            oss << "{\"type\":\"microbial_update\",\"count\":" << data_vec.size() << "}";
            broadcastWebSocket(oss.str());
        }).detach();
    }

    std::ostringstream data;
    data << "{\"inserted\":" << data_vec.size() << "}";
    response = jsonResponse(success, success ? "Data ingested" : "Failed to ingest", data.str());
}

void HttpServer::routeApi(const std::string& path, const std::string& method,
                      const std::string& body,
                      http::response<http::string_body>& res) {
    std::string response;
    res.set(http::field::content_type, "application/json; charset=utf-8");

    std::string path_no_query = path;
    std::string query;
    size_t qpos = path.find('?');
    if (qpos != std::string::npos) {
        path_no_query = path.substr(0, qpos);
        query = path.substr(qpos + 1);
    }
    auto params = parseQueryString(query);

    if (path_no_query == "/api/slips" && method == "GET") {
        apiGetSlips(params, response);
    } else if (path_no_query.rfind("/api/slips/") == 0) {
        size_t pos = path_no_query.find('/', 11);
        std::string id_str = path_no_query.substr(11, pos - 11);
        uint32_t slip_id = std::stoul(id_str);
        std::string subpath = (pos != std::string::npos) ? path_no_query.substr(pos) : "";

        if (subpath.empty() && method == "GET") {
            apiGetSlipById(slip_id, response);
        } else if (subpath == "/spectral" && method == "GET") {
            apiGetSlipSpectral(slip_id, params, response);
        } else if (subpath == "/microbial" && method == "GET") {
            apiGetSlipMicrobial(slip_id, params, response);
        } else if (subpath == "/fading" && method == "GET") {
            apiGetSlipFading(slip_id, params, response);
        } else if (subpath == "/mold" && method == "GET") {
            apiGetSlipMold(slip_id, params, response);
        } else {
            response = jsonResponse(false, "Invalid endpoint", "null");
            res.result(http::status::not_found);
        }
    } else if (path_no_query == "/api/alerts" && method == "GET") {
        apiGetAlerts(params, response);
    } else if (path_no_query.rfind("/api/alerts/") == 0) {
        std::string alert_id = path_no_query.substr(12);
        if (path_no_query.substr(12 + alert_id.size() + 1) == "/acknowledge" && method == "POST") {
            apiPostAlertAcknowledge(alert_id, body, response);
        } else if (path_no_query.substr(12 + alert_id.size() + 1) == "/resolve" && method == "POST") {
            apiPostAlertResolve(alert_id, response);
        } else {
            response = jsonResponse(false, "Invalid endpoint", "null");
            res.result(http::status::not_found);
        }
    } else if (path_no_query == "/api/devices" && method == "GET") {
        apiGetDevices(response);
    } else if (path_no_query == "/api/dashboard/stats" && method == "GET") {
        apiGetDashboardStats(response);
    } else if (path_no_query == "/api/slips/status" && method == "GET") {
        apiGetSlipsStatus(response);
    } else if (path_no_query == "/api/v1/ingest/spectral" && method == "POST") {
        apiPostIngestSpectral(body, response);
    } else if (path_no_query == "/api/v1/ingest/microbial" && method == "POST") {
        apiPostIngestMicrobial(body, response);
    } else {
        response = jsonResponse(false, "API endpoint not found", "null");
        res.result(http::status::not_found);
    }

    res.body() = response;
    res.prepare_payload();
}

void HttpServer::handleHttpRequest(const http::request<http::string_body>& req,
                                   http::response<http::string_body>& res) {
    res.version(req.version());
    res.keep_alive(req.keep_alive());

    std::string path = std::string(req.target());

    if (path.find("/api/") == 0 || path.find("/ws/") == 0) {
        if (path == "/ws/realtime") {
            res.result(http::status::bad_request);
            res.body() = "Use WebSocket protocol for realtime";
            return;
        }
        routeApi(path, std::string(req.method_string()), req.body(), res);
        return;
    }

    if (req.method() != http::verb::get) {
        res.result(http::status::method_not_allowed);
        res.body() = "Method not allowed";
        return;
    }

    if (!serveStaticFile(path, res)) {
        res.result(http::status::not_found);
        res.body() = "Not found";
        res.set(http::field::content_type, "text/plain");
    }

    res.prepare_payload();
}

void HttpServer::handleWebSocket(tcp::socket socket) {
    try {
        auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));
        ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws->set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-async");
            }));

        ws->accept();

        {
            std::lock_guard<std::mutex> lock(ws_mutex_);
            ws_connections_.push_back(ws);
        }

        beast::flat_buffer buffer;
        while (running_) {
            try {
                ws->read(buffer);
                std::string msg = beast::buffers_to_string(buffer.data());
                buffer.consume(buffer.size());

                if (ws_callback_) {
                    ws_callback_(msg);
                }
            } catch (beast::system_error const& se) {
                if (se.code() != websocket::error::closed) {
                    break;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(ws_mutex_);
            ws_connections_.erase(
                std::remove(ws_connections_.begin(), ws_connections_.end(), ws),
                ws_connections_.end());
        }
    } catch (std::exception const& e) {
        std::cerr << "WebSocket error: " << e.what() << std::endl;
    }
}

void HttpServer::broadcastWebSocket(const std::string& message) {
    std::lock_guard<std::mutex> lock(ws_mutex_);
    for (auto& ws : ws_connections_) {
        try {
            ws->text(true);
            ws->write(asio::buffer(message));
        } catch (...) {
        }
    }
}

void HttpServer::setWsMessageCallback(WsMessageCallback callback) {
    ws_callback_ = callback;
}

void HttpServer::handleSession(tcp::socket socket) {
    try {
        beast::tcp_stream stream(std::move(socket));

        http::request<http::string_body> req;
        beast::flat_buffer buffer;

        http::read(stream, buffer, req);

        if (websocket::is_upgrade(req)) {
            handleWebSocket(stream.release_socket());
            return;
        }

        http::response<http::string_body> res;
        handleHttpRequest(req, res);
        http::write(stream, res);

        stream.socket().shutdown(tcp::socket::shutdown_send);
    } catch (std::exception const& e) {
        std::cerr << "Session error: " << e.what() << std::endl;
    }
}

void HttpServer::serverLoop() {
    try {
        asio::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {tcp::v4(), port_}};

        std::cout << "HTTP server listening on port " << port_ << std::endl;

        while (running_) {
            try {
                tcp::socket socket{ioc};
                acceptor.accept(socket);
                std::thread(&HttpServer::handleSession, this, std::move(socket)).detach();
            } catch (std::exception const& e) {
                std::cerr << "Accept error: " << e.what() << std::endl;
            }
        }
    } catch (std::exception const& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
    }
}

bool HttpServer::start() {
    if (running_) return false;
    running_ = true;
    server_thread_ = std::thread(&HttpServer::serverLoop, this);
    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (server_thread_.joinable())) {
        server_thread_.join();
    }
}

bool HttpServer::isRunning() const {
    return running_;
}

}
