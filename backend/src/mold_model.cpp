#include "mold_model.h"
#include <cmath>
#include <algorithm>

namespace haihunhou {

MoldModel::MoldModel() {
    params_.mold_beta0 = -2.5f;
    params_.mold_beta1 = 0.12f;
    params_.mold_beta2 = 0.08f;
    params_.mold_beta3 = -0.0015f;
    params_.mold_beta4 = -0.0008f;
    params_.mold_beta5 = 0.0005f;
}

void MoldModel::setParams(const ModelParams& params) {
    params_ = params;
}

float MoldModel::responseSurface(float temperature, float humidity) const {
    float T = std::clamp(temperature, 0.0f, 40.0f);
    float RH = std::clamp(humidity, 30.0f, 95.0f);

    float log_cfu = params_.mold_beta0 +
                    params_.mold_beta1 * T +
                    params_.mold_beta2 * RH +
                    params_.mold_beta3 * T * T +
                    params_.mold_beta4 * RH * RH +
                    params_.mold_beta5 * T * RH;

    return std::exp(log_cfu);
}

float MoldModel::gaussianGrowthRate(float temperature, float humidity) const {
    float T = std::clamp(temperature, 0.0f, 40.0f);
    float RH = std::clamp(humidity, 30.0f, 95.0f);

    float T_term = -(T - T_opt) * (T - T_opt) / (2.0f * sigma_T * sigma_T);
    float RH_term = -(RH - RH_opt) * (RH - RH_opt) / (2.0f * sigma_RH * sigma_RH);

    return mu_opt * std::exp(T_term + RH_term);
}

float MoldModel::calculateGrowthRate(float temperature, float humidity) const {
    float rs_rate = responseSurface(temperature, humidity) / 1000.0f;
    float gs_rate = gaussianGrowthRate(temperature, humidity);
    return 0.5f * (rs_rate + gs_rate);
}

float MoldModel::calculateConcentration(float initial_concentration, float temperature,
                                        float humidity, float time_hours) const {
    float mu = calculateGrowthRate(temperature, humidity);
    float factor = std::exp(mu * time_hours);
    float result = initial_concentration * factor;

    float eq_concentration = responseSurface(temperature, humidity);
    return std::min(result, eq_concentration * 2.0f);
}

float MoldModel::predictConcentration(float current_concentration, float temperature,
                                      float humidity, float days_ahead) const {
    return calculateConcentration(current_concentration, temperature, humidity,
                                  days_ahead * 24.0f);
}

uint8_t MoldModel::assessRiskLevel(float concentration) const {
    if (concentration > 100.0f) return 4;
    if (concentration > 70.0f) return 3;
    if (concentration > 40.0f) return 2;
    return 1;
}

MoldPrediction MoldModel::predictSlip(uint32_t slip_id,
                                      const std::vector<MicrobialData>& history,
                                      float temperature, float humidity) const {
    MoldPrediction result;
    result.timestamp = now_s();
    result.slip_id = slip_id;

    if (history.empty()) {
        result.current_concentration = responseSurface(temperature, humidity) * 0.3f;
        result.predicted_1d = result.current_concentration;
        result.predicted_3d = result.current_concentration;
        result.predicted_7d = result.current_concentration;
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

    result.predicted_1d = predictConcentration(result.current_concentration,
                                               use_temperature, use_humidity, 1);
    result.predicted_3d = predictConcentration(result.current_concentration,
                                               use_temperature, use_humidity, 3);
    result.predicted_7d = predictConcentration(result.current_concentration,
                                               use_temperature, use_humidity, 7);

    result.risk_level = assessRiskLevel(result.current_concentration);

    return result;
}

}
