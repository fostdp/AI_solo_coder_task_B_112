#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace haihunhou {

constexpr uint32_t TOTAL_SLIPS = 5000;
constexpr uint16_t MSI_DEVICE_COUNT = 20;
constexpr uint16_t MC_DEVICE_COUNT = 30;
constexpr uint16_t MSI_SLIPS_PER_DEVICE = 250;
constexpr uint16_t MC_SLIPS_PER_DEVICE = 167;
constexpr uint32_t OPCUA_POLL_INTERVAL = 6 * 3600;

struct SpectralData {
    uint64_t timestamp;
    uint16_t device_id;
    uint32_t slip_id;
    uint16_t wavelength;
    float reflectance;
    float temperature;
    float humidity;
    float light_intensity;
};

struct MicrobialData {
    uint64_t timestamp;
    uint16_t device_id;
    uint32_t slip_id;
    float fungi_concentration;
    float bacteria_concentration;
    float temperature;
    float humidity;
};

struct SlipInfo {
    uint32_t slip_id;
    uint16_t device_msi;
    uint16_t device_mc;
    float position_x;
    float position_y;
    float position_z;
    float length;
    float width;
    std::string inscription;
};

struct FadingAnalysis {
    uint64_t timestamp;
    uint32_t slip_id;
    float reflectance_450nm;
    float fading_rate_monthly;
    float predicted_30d;
    float predicted_90d;
    float predicted_180d;
    uint8_t risk_level;
};

struct MoldPrediction {
    uint64_t timestamp;
    uint32_t slip_id;
    float current_concentration;
    float growth_rate;
    float predicted_1d;
    float predicted_3d;
    float predicted_7d;
    float predicted_30d;
    float predicted_90d;
    uint8_t risk_level;
};

struct Alert {
    std::string alert_id;
    uint64_t timestamp;
    uint32_t slip_id;
    enum Type { FADING = 1, MOLD = 2, DEVICE = 3 } alert_type;
    enum Severity { INFO = 1, WARNING = 2, CRITICAL = 3 } severity;
    std::string message;
    float threshold;
    float current_value;
    enum Status { NEW = 1, ACKNOWLEDGED = 2, RESOLVED = 3, CLOSED = 4 } status;
    std::string acknowledged_by;
    uint64_t acknowledged_time;
    uint64_t resolved_time;
};

struct DeviceInfo {
    uint16_t device_id;
    enum Type { MSI = 1, MC = 2 } device_type;
    std::string device_name;
    std::string ip_address;
    uint16_t port;
    enum Status { ONLINE = 1, OFFLINE = 2, ERROR = 3 } status;
    uint64_t last_heartbeat;
};

struct ModelParams {
    float fading_A1;
    float fading_Ea1;
    float fading_alpha;
    float fading_A2;
    float fading_Ea2;
    float fading_beta;
    float mold_beta0;
    float mold_beta1;
    float mold_beta2;
    float mold_beta3;
    float mold_beta4;
    float mold_beta5;
};

struct AlertConfig {
    float fading_threshold;
    float mold_threshold;
    std::string dingtalk_webhook;
    std::string dingtalk_secret;
    std::string smtp_host;
    uint16_t smtp_port;
    std::string smtp_user;
    std::string smtp_password;
    std::string smtp_from;
    std::vector<std::string> smtp_to;
};

inline uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline uint64_t now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

enum class BambooMaterial : uint8_t {
    QING_ZHU = 0,
    HUANG_ZHU = 1,
    CEDAR = 2,
    UNKNOWN = 3
};

struct FadingParams {
    float A1 = 1.2e-8f;
    float Ea1 = 45.0f;
    float alpha = 0.8f;
    float A2 = 8.5e-7f;
    float Ea2 = 55.0f;
    float beta = 1.5f;
    float lambda0 = 450.0f;
    float R_gas = 8.314f;
    uint32_t min_data_points = 8;
    uint32_t max_lm_iterations = 100;
    float lm_damping_init = 0.001f;
    float lm_tolerance = 1e-6f;
};

struct MoldParams {
    float beta0 = -2.5f;
    float beta1 = 0.12f;
    float beta2 = 0.08f;
    float beta3 = -0.0015f;
    float beta4 = -0.0008f;
    float beta5 = 0.0005f;
    float mu_opt = 0.05f;
    float T_opt = 25.0f;
    float RH_opt = 65.0f;
    float sigma_T = 8.0f;
    float sigma_RH = 15.0f;
    float response_weight = 0.6f;
    float gaussian_weight = 0.4f;
};

struct AlertBrokerConfig {
    float fading_threshold = 20.0f;
    float mold_threshold = 100.0f;
    uint64_t suppression_window_ms = 24ULL * 3600 * 1000;
    uint32_t aggregation_window_sec = 60;
    uint32_t max_alerts_per_window = 50;
    bool enable_dingtalk = false;
    bool enable_email = false;
};

}
