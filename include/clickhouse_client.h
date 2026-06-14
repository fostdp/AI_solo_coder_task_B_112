#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <mutex>
#include <memory>

namespace haihunhou {

class ClickHouseClient {
public:
    ClickHouseClient(const std::string& host = "127.0.0.1", uint16_t port = 8123,
                     const std::string& database = "haihunhou_slips");
    ~ClickHouseClient();

    bool connect();
    void disconnect();
    bool isConnected() const;

    bool insertSpectralData(const std::vector<SpectralData>& data);
    bool insertMicrobialData(const std::vector<MicrobialData>& data);
    bool insertFadingAnalysis(const std::vector<FadingAnalysis>& data);
    bool insertMoldPrediction(const std::vector<MoldPrediction>& data);
    bool insertAlert(const Alert& alert);

    bool getSlips(std::vector<SlipInfo>& slips, uint32_t offset = 0, uint32_t limit = 5000);
    bool getSlipById(uint32_t slip_id, SlipInfo& slip);
    bool getDevices(std::vector<DeviceInfo>& devices);
    bool getSpectralData(uint32_t slip_id, uint64_t start_time, uint64_t end_time,
                         std::vector<SpectralData>& data);
    bool getMicrobialData(uint32_t slip_id, uint64_t start_time, uint64_t end_time,
                          std::vector<MicrobialData>& data);
    bool getFadingAnalysis(uint32_t slip_id, uint64_t start_time, uint64_t end_time,
                           std::vector<FadingAnalysis>& data);
    bool getMoldPrediction(uint32_t slip_id, uint64_t start_time, uint64_t end_time,
                           std::vector<MoldPrediction>& data);
    bool getAlerts(uint64_t start_time, uint64_t end_time,
                   std::vector<Alert>& alerts, int status = 0);

    bool updateAlertStatus(const std::string& alert_id, int status,
                           const std::string& user = "");

    bool getConfig(ModelParams& model_params, AlertConfig& alert_config);

    bool getDashboardStats(int& total_slips, int& online_devices,
                           int& critical_alerts, int& warning_alerts,
                           float& avg_fading_rate, float& avg_mold_concentration);

    bool getAllSlipsStatus(std::vector<std::tuple<uint32_t, uint8_t, uint8_t>>& statuses);

    std::string query(const std::string& sql);

private:
    std::string host_;
    uint16_t port_;
    std::string database_;
    std::string session_id_;
    bool connected_;
    mutable std::mutex mutex_;

    std::string escapeString(const std::string& s) const;
    bool executeInsert(const std::string& table, const std::string& values);
};

}
