#include "alert_broker.h"

#if __has_include(<boost/lockfree/queue.hpp>)
#include "message_bus.h"
#else
namespace haihunhou {
struct AnalysisMessage {
    enum Type { FADING = 1, MOLD = 2 } type;
    std::vector<FadingAnalysis> fading;
    std::vector<MoldPrediction> mold;
    std::vector<SpectralData> raw_spectral;
    std::vector<MicrobialData> raw_microbial;
};
struct AlertMessage {
    Alert alert;
};
class MessageBus {
public:
    static MessageBus& instance() { static MessageBus b; return b; }
    bool publishAlert(AlertMessage&&) { return true; }
    bool publishAnalysis(AnalysisMessage&&) { return true; }
    bool consumeAnalysis(AnalysisMessage&, uint32_t) { return false; }
    bool consumeAlert(AlertMessage&, uint32_t) { return false; }
    size_t alertQueueSize() const { return 0; }
};
}
#endif

#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace haihunhou {

AlertBroker::AlertBroker(ClickHouseClient& db, MessageBus& bus)
    : db_(db), bus_(bus), running_(false) {}

AlertBroker::~AlertBroker() {
    stop();
}

void AlertBroker::setConfig(const AlertBrokerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

AlertBrokerConfig AlertBroker::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void AlertBroker::setNotificationConfig(const AlertConfig& notif_config) {
    notification_.setConfig(notif_config);
}

bool AlertBroker::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    worker_thread_ = std::thread(&AlertBroker::workerLoop, this);
    return true;
}

void AlertBroker::stop() {
    running_.store(false);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

bool AlertBroker::isRunning() const {
    return running_.load();
}

std::vector<Alert> AlertBroker::getActiveAlerts() {
    std::vector<Alert> alerts;
    uint64_t end = now_ms();
    uint64_t start = end - config_.suppression_window_ms;
    db_.getAlerts(start, end, alerts, static_cast<int>(Alert::NEW));
    return alerts;
}

bool AlertBroker::acknowledgeAlert(const std::string& alert_id, const std::string& user) {
    return db_.updateAlertStatus(alert_id, static_cast<int>(Alert::ACKNOWLEDGED), user);
}

bool AlertBroker::resolveAlert(const std::string& alert_id) {
    return db_.updateAlertStatus(alert_id, static_cast<int>(Alert::RESOLVED));
}

void AlertBroker::checkFadingAlert(uint32_t slip_id, float fading_rate_monthly) {
    if (!shouldAlert(slip_id, fading_rate_monthly, last_fading_alert_)) {
        return;
    }
    Alert::Severity severity = Alert::WARNING;
    if (fading_rate_monthly > config_.fading_threshold * 2.0f) {
        severity = Alert::CRITICAL;
    }
    std::ostringstream oss;
    oss << "Slip " << slip_id << " fading rate " << std::fixed << std::setprecision(2)
        << fading_rate_monthly << "%/month exceeds threshold "
        << config_.fading_threshold << "%/month";
    Alert alert = createAlert(slip_id, Alert::FADING, severity, oss.str(),
                              config_.fading_threshold, fading_rate_monthly);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& agg = aggregation_[Alert::FADING];
        if (agg.pending.empty()) {
            agg.type = Alert::FADING;
            agg.count = 0;
            agg.max_value = 0.0f;
            agg.window_start = now_ms();
        }
        agg.count++;
        agg.max_value = std::max(agg.max_value, fading_rate_monthly);
        agg.pending.push_back(alert);
    }
    db_.insertAlert(alert);
    if (config_.enable_dingtalk) {
        notification_.sendDingTalkAlert(alert);
    }
    if (config_.enable_email) {
        notification_.sendEmailAlert(alert);
    }
}

void AlertBroker::checkMoldAlert(uint32_t slip_id, float mold_concentration) {
    if (!shouldAlert(slip_id, mold_concentration, last_mold_alert_)) {
        return;
    }
    Alert::Severity severity = Alert::WARNING;
    if (mold_concentration > config_.mold_threshold * 2.0f) {
        severity = Alert::CRITICAL;
    }
    std::ostringstream oss;
    oss << "Slip " << slip_id << " mold concentration " << std::fixed << std::setprecision(2)
        << mold_concentration << " exceeds threshold " << config_.mold_threshold;
    Alert alert = createAlert(slip_id, Alert::MOLD, severity, oss.str(),
                              config_.mold_threshold, mold_concentration);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& agg = aggregation_[Alert::MOLD];
        if (agg.pending.empty()) {
            agg.type = Alert::MOLD;
            agg.count = 0;
            agg.max_value = 0.0f;
            agg.window_start = now_ms();
        }
        agg.count++;
        agg.max_value = std::max(agg.max_value, mold_concentration);
        agg.pending.push_back(alert);
    }
    db_.insertAlert(alert);
    if (config_.enable_dingtalk) {
        notification_.sendDingTalkAlert(alert);
    }
    if (config_.enable_email) {
        notification_.sendEmailAlert(alert);
    }
}

void AlertBroker::workerLoop() {
    while (running_.load()) {
        AnalysisMessage analysis_msg;
        if (bus_.consumeAnalysis(analysis_msg, 100)) {
            processAnalysisResults(analysis_msg);
        }

        processAlertQueue();

        flushAggregation();
    }
}

void AlertBroker::processAnalysisResults(const AnalysisMessage& msg) {
    for (const auto& fa : msg.fading) {
        checkFadingAlert(fa.slip_id, fa.fading_rate_monthly);
    }
    for (const auto& mp : msg.mold) {
        checkMoldAlert(mp.slip_id, mp.current_concentration);
    }
}

void AlertBroker::processAlertQueue() {
    AlertMessage alert_msg;
    while (bus_.consumeAlert(alert_msg, 0)) {
        db_.insertAlert(alert_msg.alert);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& agg = aggregation_[alert_msg.alert.alert_type];
            if (agg.pending.empty()) {
                agg.type = alert_msg.alert.alert_type;
                agg.count = 0;
                agg.max_value = 0.0f;
                agg.window_start = now_ms();
            }
            agg.count++;
            agg.max_value = std::max(agg.max_value, alert_msg.alert.current_value);
            agg.pending.push_back(alert_msg.alert);
        }
        if (config_.enable_dingtalk) {
            notification_.sendDingTalkAlert(alert_msg.alert);
        }
        if (config_.enable_email) {
            notification_.sendEmailAlert(alert_msg.alert);
        }
    }
}

void AlertBroker::flushAggregation() {
    std::unordered_map<Alert::Type, AlertAggregate> to_flush;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t now = now_ms();
        uint64_t window_ms = static_cast<uint64_t>(config_.aggregation_window_sec) * 1000ULL;
        for (auto it = aggregation_.begin(); it != aggregation_.end(); ) {
            auto& agg = it->second;
            if (!agg.pending.empty() && (now - agg.window_start) >= window_ms) {
                to_flush[it->first] = std::move(agg);
                it = aggregation_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& [type, agg] : to_flush) {
        if (agg.count <= 1) {
            continue;
        }
        std::string type_name = (type == Alert::FADING) ? "Fading" : "Mold";
        std::ostringstream oss;
        oss << type_name << " alert summary: " << agg.count
            << " alerts, max value " << std::fixed << std::setprecision(2) << agg.max_value;

        Alert summary;
        summary.alert_id = generateUUID();
        summary.timestamp = now_ms();
        summary.slip_id = 0;
        summary.alert_type = type;
        summary.severity = Alert::CRITICAL;
        summary.message = oss.str();
        summary.threshold = 0.0f;
        summary.current_value = agg.max_value;
        summary.status = Alert::NEW;
        summary.acknowledged_by = "";
        summary.acknowledged_time = 0;
        summary.resolved_time = 0;

        db_.insertAlert(summary);

        for (auto& alert : agg.pending) {
            db_.insertAlert(alert);
        }

        if (config_.enable_dingtalk) {
            notification_.sendDingTalkAlert(summary);
        }
        if (config_.enable_email) {
            notification_.sendEmailAlert(summary);
        }
    }
}

bool AlertBroker::shouldAlert(uint32_t id, float value,
                              std::unordered_map<uint32_t, std::pair<uint64_t, float>>& last) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t now = now_ms();
    auto it = last.find(id);
    if (it != last.end()) {
        uint64_t elapsed = now - it->second.first;
        if (elapsed < config_.suppression_window_ms) {
            float prev_value = it->second.second;
            if (std::abs(value - prev_value) < prev_value * 0.1f) {
                return false;
            }
        }
    }
    last[id] = {now, value};
    return true;
}

Alert AlertBroker::createAlert(uint32_t slip_id, Alert::Type type, Alert::Severity severity,
                               const std::string& message, float threshold, float value) {
    Alert alert;
    alert.alert_id = generateUUID();
    alert.timestamp = now_ms();
    alert.slip_id = slip_id;
    alert.alert_type = type;
    alert.severity = severity;
    alert.message = message;
    alert.threshold = threshold;
    alert.current_value = value;
    alert.status = Alert::NEW;
    alert.acknowledged_by = "";
    alert.acknowledged_time = 0;
    alert.resolved_time = 0;
    return alert;
}

std::string AlertBroker::generateUUID() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << (dist(rng) & 0xFFFFFFFF) << "-";
    oss << std::setw(4) << (dist(rng) & 0xFFFF) << "-";
    oss << std::setw(4) << ((dist(rng) & 0x0FFF) | 0x4000) << "-";
    oss << std::setw(4) << ((dist(rng) & 0x3FFF) | 0x8000) << "-";
    oss << std::setw(12) << (dist(rng) & 0xFFFFFFFFFFFF);
    return oss.str();
}

}
