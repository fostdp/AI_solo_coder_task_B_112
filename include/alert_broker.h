#pragma once
#include "common.h"
#include "clickhouse_client.h"
#include "notification.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <functional>

namespace haihunhou {

class MessageBus;

struct AlertAggregate {
    Alert::Type type;
    uint32_t count;
    float max_value;
    uint64_t window_start;
    std::vector<Alert> pending;
};

class AlertBroker {
public:
    AlertBroker(ClickHouseClient& db, MessageBus& bus);
    ~AlertBroker();

    void setConfig(const AlertBrokerConfig& config);
    AlertBrokerConfig getConfig() const;
    void setNotificationConfig(const AlertConfig& notif_config);

    bool start();
    void stop();
    bool isRunning() const;

    std::vector<Alert> getActiveAlerts();
    bool acknowledgeAlert(const std::string& alert_id, const std::string& user);
    bool resolveAlert(const std::string& alert_id);

    void checkFadingAlert(uint32_t slip_id, float fading_rate_monthly);
    void checkMoldAlert(uint32_t slip_id, float mold_concentration);

private:
    ClickHouseClient& db_;
    MessageBus& bus_;
    AlertBrokerConfig config_;
    NotificationService notification_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    mutable std::mutex mutex_;

    std::unordered_map<uint32_t, std::pair<uint64_t, float>> last_fading_alert_;
    std::unordered_map<uint32_t, std::pair<uint64_t, float>> last_mold_alert_;
    std::unordered_map<Alert::Type, AlertAggregate> aggregation_;

    void workerLoop();
    void processAnalysisResults(const AnalysisMessage& msg);
    void processAlertQueue();
    void flushAggregation();

    bool shouldAlert(uint32_t id, float value,
                     std::unordered_map<uint32_t, std::pair<uint64_t, float>>& last);
    Alert createAlert(uint32_t slip_id, Alert::Type type, Alert::Severity severity,
                      const std::string& message, float threshold, float value);
    std::string generateUUID();
};

}
