#include "mold_engine.h"

#if defined(TEST_STUB)
#include "../tests/test_stub.h"
#elif __has_include(<boost/lockfree/queue.hpp>)
#include "message_bus.h"
#else
namespace haihunhou {
struct IngestMessage {
    enum Type { SPECTRAL = 1, MICROBIAL = 2 } type;
    std::vector<SpectralData> spectral;
    std::vector<MicrobialData> microbial;
};
struct AnalysisMessage {
    enum Type { FADING = 1, MOLD = 2 } type;
    std::vector<FadingAnalysis> fading;
    std::vector<MoldPrediction> mold;
    std::vector<SpectralData> raw_spectral;
    std::vector<MicrobialData> raw_microbial;
};
class MessageBus {
public:
    static MessageBus& instance() { static MessageBus b; return b; }
    bool publishIngest(IngestMessage&&) { return true; }
    bool publishAnalysis(AnalysisMessage&&) { return true; }
    bool consumeIngest(IngestMessage&, uint32_t) { return false; }
    bool consumeAnalysis(AnalysisMessage&, uint32_t) { return false; }
};
}
#endif

#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace haihunhou {

MoldEngine::MoldEngine(ClickHouseClient& db, MessageBus& bus)
    : db_(db), bus_(bus), running_(false) {
    loadMaterialCoefficients();
}

MoldEngine::~MoldEngine() {
    stop();
}

void MoldEngine::setParams(const MoldParams& params) {
    std::lock_guard<std::mutex> lock(params_mutex_);
    params_ = params;
}

MoldParams MoldEngine::getParams() const {
    std::lock_guard<std::mutex> lock(params_mutex_);
    return params_;
}

bool MoldEngine::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return false;
    worker_thread_ = std::thread(&MoldEngine::workerLoop, this);
    return true;
}

void MoldEngine::stop() {
    running_.store(false);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

bool MoldEngine::isRunning() const {
    return running_.load();
}

void MoldEngine::loadMaterialCoefficients() {
    material_coeffs_[BambooMaterial::QING_ZHU] = defaultMaterialCoefficients(BambooMaterial::QING_ZHU);
    material_coeffs_[BambooMaterial::HUANG_ZHU] = defaultMaterialCoefficients(BambooMaterial::HUANG_ZHU);
    material_coeffs_[BambooMaterial::CEDAR] = defaultMaterialCoefficients(BambooMaterial::CEDAR);
    material_coeffs_[BambooMaterial::UNKNOWN] = defaultMaterialCoefficients(BambooMaterial::UNKNOWN);
}

MaterialCoefficients MoldEngine::defaultMaterialCoefficients(BambooMaterial m) {
    switch (m) {
        case BambooMaterial::QING_ZHU:
            return {1.15f, 0.85f, 0.80f, 0.075f, 0.055f};
        case BambooMaterial::HUANG_ZHU:
            return {0.88f, 1.12f, 1.15f, 0.088f, 0.068f};
        case BambooMaterial::CEDAR:
            return {1.45f, 0.72f, 0.45f, 0.062f, 0.048f};
        default:
            return {1.0f, 1.0f, 1.0f, 0.080f, 0.060f};
    }
}

float MoldEngine::responseSurface(float temperature, float humidity, BambooMaterial material) const {
    float T = std::clamp(temperature, 0.0f, 40.0f);
    float RH = std::clamp(humidity, 30.0f, 95.0f);

    MoldParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }

    auto it = material_coeffs_.find(material);
    const MaterialCoefficients& mc = (it != material_coeffs_.end()) ? it->second : material_coeffs_.at(BambooMaterial::UNKNOWN);

    float log_cfu = mc.beta0_adj +
                    mc.beta1_adj * T +
                    mc.beta2_adj * RH +
                    p.beta3 * T * T +
                    p.beta4 * RH * RH +
                    p.beta5 * T * RH;

    return std::exp(log_cfu) * mc.anti_mold_factor;
}

float MoldEngine::gaussianGrowthRate(float temperature, float humidity) const {
    float T = std::clamp(temperature, 0.0f, 40.0f);
    float RH = std::clamp(humidity, 30.0f, 95.0f);

    MoldParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }

    float T_term = -(T - p.T_opt) * (T - p.T_opt) / (2.0f * p.sigma_T * p.sigma_T);
    float RH_term = -(RH - p.RH_opt) * (RH - p.RH_opt) / (2.0f * p.sigma_RH * p.sigma_RH);

    return p.mu_opt * std::exp(T_term + RH_term);
}

float MoldEngine::calculateGrowthRate(float temperature, float humidity, BambooMaterial material) const {
    MoldParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }

    float rs = responseSurface(temperature, humidity, material) / 1000.0f;
    float gs = gaussianGrowthRate(temperature, humidity);
    return p.response_weight * rs + p.gaussian_weight * gs;
}

float MoldEngine::calculateConcentration(float initial, float temp, float humidity,
                                          float time_hours, BambooMaterial material) const {
    float mu = calculateGrowthRate(temp, humidity, material);
    float factor = std::exp(mu * time_hours);
    float result = initial * factor;
    float eq = responseSurface(temp, humidity, material);
    return std::min(result, eq * 2.0f);
}

float MoldEngine::predictConcentration(float current, float temp, float humidity,
                                        float days, BambooMaterial material) const {
    return calculateConcentration(current, temp, humidity, days * 24.0f, material);
}

uint8_t MoldEngine::assessRiskLevel(float concentration) const {
    if (concentration > 100.0f) return 4;
    if (concentration > 70.0f) return 3;
    if (concentration > 40.0f) return 2;
    return 1;
}

BambooMaterial MoldEngine::getSlipMaterial(uint32_t slip_id) const {
    return static_cast<BambooMaterial>(slip_id % 4);
}

MoldPrediction MoldEngine::predictSlip(uint32_t slip_id,
                                        const std::vector<MicrobialData>& history,
                                        float temperature, float humidity) const {
    MoldPrediction result{};
    result.timestamp = now_s();
    result.slip_id = slip_id;

    BambooMaterial material = getSlipMaterial(slip_id);

    if (history.empty()) {
        result.current_concentration = responseSurface(temperature, humidity, material) * 0.3f;
        result.growth_rate = calculateGrowthRate(temperature, humidity, material);
        result.predicted_1d = result.current_concentration;
        result.predicted_3d = result.current_concentration;
        result.predicted_7d = result.current_concentration;
        result.predicted_30d = result.current_concentration;
        result.predicted_90d = result.current_concentration;
        result.risk_level = 1;
        return result;
    }

    float avg_temp = 0.0f;
    float avg_humidity = 0.0f;
    float avg_concentration = 0.0f;
    float latest_concentration = 0.0f;
    uint64_t latest_ts = 0;

    for (const auto& d : history) {
        avg_temp += d.temperature;
        avg_humidity += d.humidity;
        avg_concentration += d.fungi_concentration;
        if (d.timestamp > latest_ts) {
            latest_ts = d.timestamp;
            latest_concentration = d.fungi_concentration;
        }
    }

    size_t n = history.size();
    avg_temp /= n;
    avg_humidity /= n;
    avg_concentration /= n;

    float use_temperature = (temperature > 0) ? temperature : avg_temp;
    float use_humidity = (humidity > 0) ? humidity : avg_humidity;

    result.current_concentration = latest_concentration > 0 ? latest_concentration : avg_concentration;
    result.growth_rate = calculateGrowthRate(use_temperature, use_humidity, material);

    result.predicted_1d = predictConcentration(result.current_concentration, use_temperature, use_humidity, 1, material);
    result.predicted_3d = predictConcentration(result.current_concentration, use_temperature, use_humidity, 3, material);
    result.predicted_7d = predictConcentration(result.current_concentration, use_temperature, use_humidity, 7, material);
    result.predicted_30d = predictConcentration(result.current_concentration, use_temperature, use_humidity, 30, material);
    result.predicted_90d = predictConcentration(result.current_concentration, use_temperature, use_humidity, 90, material);

    result.risk_level = assessRiskLevel(result.current_concentration);

    return result;
}

void MoldEngine::workerLoop() {
    while (running_.load()) {
        IngestMessage msg;
        if (bus_.consumeIngest(msg, 500)) {
            if (msg.type == IngestMessage::MICROBIAL && !msg.microbial.empty()) {
                processMicrobialData(msg.microbial);
            }
        }
    }
}

void MoldEngine::processMicrobialData(const std::vector<MicrobialData>& data) {
    std::unordered_map<uint32_t, std::vector<MicrobialData>> by_slip;
    for (const auto& d : data) {
        by_slip[d.slip_id].push_back(d);
    }

    std::vector<MoldPrediction> predictions;
    predictions.reserve(by_slip.size());

    for (auto& [slip_id, history] : by_slip) {
        float temp = 0.0f;
        float hum = 0.0f;
        for (const auto& d : history) {
            temp += d.temperature;
            hum += d.humidity;
        }
        temp /= history.size();
        hum /= history.size();

        MoldPrediction pred = predictSlip(slip_id, history, temp, hum);
        predictions.push_back(std::move(pred));
    }

    if (!predictions.empty()) {
        db_.insertMoldPrediction(predictions);
    }

    AnalysisMessage analysis;
    analysis.type = AnalysisMessage::MOLD;
    analysis.mold = std::move(predictions);
    analysis.raw_microbial = data;
    bus_.publishAnalysis(std::move(analysis));
}

}
