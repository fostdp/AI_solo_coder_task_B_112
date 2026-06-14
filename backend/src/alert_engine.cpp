#include "alert_engine.h"
#include "notification.h"
#include "fading_model.h"
#include "mold_model.h"
#include <sstream>
#include <random>
#include <iomanip>
#include <iostream>

namespace haihunhou {

static NotificationService g_notificationService;

AlertEngine::AlertEngine(ClickHouseClient& db) : db_(db) {
    config_.fading_threshold = 20.0f;
    config_.mold_threshold = 100.0f;
}

void AlertEngine::setConfig(const AlertConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    g_notificationService.setConfig(config);
}

std::string AlertEngine::generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

Alert AlertEngine::createAlert(uint32_t slip_id, Alert::Type type, Alert::Severity severity,
                              const std::string& message, float threshold, float current_value) {
    Alert alert;
    alert.alert_id = generateUUID();
    alert.timestamp = now_s();
    alert.slip_id = slip_id;
    alert.alert_type = type;
    alert.severity = severity;
    alert.message = message;
    alert.threshold = threshold;
    alert.current_value = current_value;
    alert.status = Alert::NEW;
    alert.acknowledged_time = 0;
    alert.resolved_time = 0;
    return alert;
}

bool AlertEngine::shouldAlert(uint32_t id, float current_value,
                             std::unordered_map<uint32_t, std::pair<uint64_t, float>>& last_alerts) {
    uint64_t now = now_ms();
    auto it = last_alerts.find(id);
    if (it != last_alerts.end()) {
        if (now - it->second.first < ALERT_SUPPRESSION_WINDOW &&
            std::abs(current_value - it->second.second) < it->second.second * 0.1f) {
            return false;
        }
    }
    last_alerts[id] = {now, current_value};
    return true;
}

bool AlertEngine::shouldDeviceAlert(uint16_t device_id, bool online) {
    uint64_t now = now_ms();
    auto it = last_device_alert_.find(device_id);
    if (it != last_device_alert_.end()) {
        if (now - it->second.first < ALERT_SUPPRESSION_WINDOW &&
            online == it->second.second) {
            return false;
        }
    }
    last_device_alert_[device_id] = {now, online};
    return true;
}

void AlertEngine::checkFadingAlert(uint32_t slip_id, float fading_rate_monthly) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (fading_rate_monthly > config_.fading_threshold) {
        if (shouldAlert(slip_id, fading_rate_monthly, last_fading_alert_)) {
            Alert::Severity severity = fading_rate_monthly > config_.fading_threshold * 1.5f ?
                Alert::CRITICAL : Alert::WARNING;

            std::ostringstream oss;
            oss << "简牍#" << slip_id << " 墨迹褪色速率异常："
                << std::fixed << std::setprecision(2)
                << fading_rate_monthly << "%/月，超过阈值 "
                << config_.fading_threshold << "%/月";

            Alert alert = createAlert(slip_id, Alert::FADING, severity,
                                     oss.str(),
                                     config_.fading_threshold,
                                     fading_rate_monthly);

            db_.insertAlert(alert);
            g_notificationService.sendAlert(alert);
        }
    }
}

void AlertEngine::checkMoldAlert(uint32_t slip_id, float mold_concentration) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (mold_concentration > config_.mold_threshold) {
        if (shouldAlert(slip_id, mold_concentration, last_mold_alert_)) {
            Alert::Severity severity = mold_concentration > config_.mold_threshold * 1.5f ?
                Alert::CRITICAL : Alert::WARNING;

            std::ostringstream oss;
            oss << "简牍#" << slip_id << " 霉菌浓度异常："
                << std::fixed << std::setprecision(1)
                << mold_concentration << " CFU/cm²，超过阈值 "
                << config_.mold_threshold << " CFU/cm²";

            Alert alert = createAlert(slip_id, Alert::MOLD, severity,
                                     oss.str(),
                                     config_.mold_threshold,
                                     mold_concentration);

            db_.insertAlert(alert);
            g_notificationService.sendAlert(alert);
        }
    }
}

void AlertEngine::checkDeviceAlert(uint16_t device_id, bool online) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!online) {
        if (shouldDeviceAlert(device_id, online)) {
            std::ostringstream oss;
            oss << "设备#" << device_id << " 离线";

            Alert alert = createAlert(0, Alert::DEVICE, Alert::WARNING,
                                     oss.str(), 0, 0);

            db_.insertAlert(alert);
            g_notificationService.sendAlert(alert);
        }
    }
}

std::vector<Alert> AlertEngine::getActiveAlerts() {
    std::vector<Alert> alerts;
    uint64_t start = now_s() - 7 * 24 * 3600;
    uint64_t end = now_s();
    db_.getAlerts(start, end, alerts, 1);
    return alerts;
}

bool AlertEngine::acknowledgeAlert(const std::string& alert_id, const std::string& user) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.updateAlertStatus(alert_id, 2, user);
}

bool AlertEngine::resolveAlert(const std::string& alert_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return db_.updateAlertStatus(alert_id, 3);
}

void AlertEngine::processNewSpectralData(const std::vector<SpectralData>& data) {
    std::unordered_map<uint32_t, std::vector<SpectralData>> slip_data;
    for (const auto& d : data) {
        slip_data[d.slip_id].push_back(d);
    }

    FadingModel model;
    ModelParams params;
    AlertConfig alert_config;
    db_.getConfig(params, alert_config);
    model.setParams(params);
    setConfig(alert_config);

    std::vector<FadingAnalysis> all_analysis;

    for (auto& [slip_id, history] : slip_data) {
        float avg_temp = 0, avg_hum = 0, avg_light = 0;
        for (const auto& d : history) {
            avg_temp += d.temperature;
            avg_hum += d.humidity;
            avg_light += d.light_intensity;
        }
        if (!history.empty()) {
            avg_temp /= history.size();
            avg_hum /= history.size();
            avg_light /= history.size();
        }

        FadingAnalysis fa = model.analyzeSlip(slip_id, history, avg_temp, avg_hum, avg_light);
        all_analysis.push_back(fa);

        checkFadingAlert(slip_id, fa.fading_rate_monthly);
    }

    db_.insertFadingAnalysis(all_analysis);
}

void AlertEngine::processNewMicrobialData(const std::vector<MicrobialData>& data) {
    std::unordered_map<uint32_t, std::vector<MicrobialData>> slip_data;
    for (const auto& d : data) {
        slip_data[d.slip_id].push_back(d);
    }

    MoldModel model;
    ModelParams params;
    AlertConfig alert_config;
    db_.getConfig(params, alert_config);
    model.setParams(params);
    setConfig(alert_config);

    std::vector<MoldPrediction> all_predictions;

    for (auto& [slip_id, history] : slip_data) {
        float avg_temp = 0, avg_hum = 0;
        for (const auto& d : history) {
            avg_temp += d.temperature;
            avg_hum += d.humidity;
        }
        if (!history.empty()) {
            avg_temp /= history.size();
            avg_hum /= history.size();
        }

        MoldPrediction mp = model.predictSlip(slip_id, history, avg_temp, avg_hum);
        all_predictions.push_back(mp);

        checkMoldAlert(slip_id, mp.current_concentration);
    }

    db_.insertMoldPrediction(all_predictions);
}

}
