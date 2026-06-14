#pragma once
#include "common.h"
#include "clickhouse_client.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace haihunhou {

class AlertEngine {
public:
    AlertEngine(ClickHouseClient& db);

    void setConfig(const AlertConfig& config);

    void checkFadingAlert(uint32_t slip_id, float fading_rate_monthly);
    void checkMoldAlert(uint32_t slip_id, float mold_concentration);
    void checkDeviceAlert(uint16_t device_id, bool online);

    std::vector<Alert> getActiveAlerts();
    bool acknowledgeAlert(const std::string& alert_id, const std::string& user);
    bool resolveAlert(const std::string& alert_id);

    void processNewSpectralData(const std::vector<SpectralData>& data);
    void processNewMicrobialData(const std::vector<MicrobialData>& data);

private:
    ClickHouseClient& db_;
    AlertConfig config_;
    std::mutex mutex_;

    std::unordered_map<uint32_t, std::pair<uint64_t, float>> last_fading_alert_;
    std::unordered_map<uint32_t, std::pair<uint64_t, float>> last_mold_alert_;
    std::unordered_map<uint16_t, std::pair<uint64_t, bool>> last_device_alert_;

    static constexpr uint64_t ALERT_SUPPRESSION_WINDOW = 24 * 3600 * 1000;

    Alert createAlert(uint32_t slip_id, Alert::Type type, Alert::Severity severity,
                      const std::string& message, float threshold, float current_value);

    std::string generateUUID();

    bool shouldAlert(uint32_t id, float current_value,
                     std::unordered_map<uint32_t, std::pair<uint64_t, float>>& last_alerts);

    bool shouldDeviceAlert(uint16_t device_id, bool online);
};

}
