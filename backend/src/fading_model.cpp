#include "fading_model.h"
#include <cmath>
#include <algorithm>

namespace haihunhou {

FadingModel::FadingModel() {
    params_.fading_A1 = 1.2e-8f;
    params_.fading_Ea1 = 45.0f;
    params_.fading_alpha = 0.8f;
    params_.fading_A2 = 8.5e-7f;
    params_.fading_Ea2 = 55.0f;
    params_.fading_beta = 1.5f;
}

void FadingModel::setParams(const ModelParams& params) {
    params_ = params;
}

float FadingModel::calculateK1(float temperature, float wavelength) const {
    float T = temperature + 273.15f;
    float Ea = params_.fading_Ea1 * 1000.0f;
    float arrhenius = params_.fading_A1 * std::exp(-Ea / (R * T));
    float wavelength_factor = std::pow(wavelength / lambda0, params_.fading_alpha);
    return arrhenius * wavelength_factor;
}

float FadingModel::calculateK2(float temperature) const {
    float T = temperature + 273.15f;
    float Ea = params_.fading_Ea2 * 1000.0f;
    return params_.fading_A2 * std::exp(-Ea / (R * T));
}

float FadingModel::photoOxidationRate(float k1, float light_intensity, float reflectance) const {
    return -k1 * light_intensity * reflectance;
}

float FadingModel::hydrolysisRate(float k2, float humidity, float reflectance) const {
    float RH = humidity / 100.0f;
    return -k2 * (1.0f - reflectance) * std::pow(RH, params_.fading_beta);
}

float FadingModel::calculateReflectance(float initial_reflectance, float temperature,
                                        float humidity, float light_intensity,
                                        float wavelength, float time_hours) const {
    float k1 = calculateK1(temperature, wavelength);
    float k2 = calculateK2(temperature);

    float dt = 0.1f;
    float R = initial_reflectance;
    float t = 0.0f;

    while (t < time_hours && R > 0.01f) {
        float dR_dt = photoOxidationRate(k1, light_intensity, R) +
                      hydrolysisRate(k2, humidity, R);
        R += dR_dt * dt;
        R = std::max(0.01f, std::min(0.99f, R));
        t += dt;
    }

    return R;
}

float FadingModel::calculateFadingRate(float initial_reflectance, float current_reflectance,
                                       float time_days) const {
    if (time_days <= 0 || initial_reflectance <= 0) return 0.0f;
    float decline = initial_reflectance - current_reflectance;
    float monthly_rate = (decline / initial_reflectance) * (30.0f / time_days) * 100.0f;
    return std::max(0.0f, monthly_rate);
}

float FadingModel::predictReflectance(float current_reflectance, float temperature,
                                      float humidity, float light_intensity,
                                      float wavelength, float days_ahead) const {
    return calculateReflectance(current_reflectance, temperature, humidity,
                                light_intensity, wavelength, days_ahead * 24.0f);
}

uint8_t FadingModel::assessRiskLevel(float fading_rate) const {
    if (fading_rate > 20.0f) return 4;
    if (fading_rate > 10.0f) return 3;
    if (fading_rate > 5.0f) return 2;
    return 1;
}

FadingAnalysis FadingModel::analyzeSlip(uint32_t slip_id,
                                        const std::vector<SpectralData>& history,
                                        float temperature, float humidity,
                                        float light_intensity) const {
    FadingAnalysis result;
    result.timestamp = now_s();
    result.slip_id = slip_id;

    if (history.empty()) {
        result.reflectance_450nm = 0.7f;
        result.fading_rate_monthly = 0.0f;
        result.predicted_30d = 0.7f;
        result.predicted_90d = 0.7f;
        result.predicted_180d = 0.7f;
        result.risk_level = 1;
        return result;
    }

    float current_r = 0.0f;
    float initial_r = 0.0f;
    uint64_t earliest_ts = ~0ULL;
    uint64_t latest_ts = 0;

    for (const auto& d : history) {
        if (d.wavelength == 450) {
            if (d.timestamp > latest_ts) {
                latest_ts = d.timestamp;
                current_r = d.reflectance;
            }
            if (d.timestamp < earliest_ts) {
                earliest_ts = d.timestamp;
                initial_r = d.reflectance;
            }
        }
    }

    if (current_r <= 0) current_r = 0.7f;
    if (initial_r <= 0) initial_r = current_r;

    result.reflectance_450nm = current_r;

    float time_days = (latest_ts - earliest_ts) / 86400.0f;
    if (time_days < 1.0f) time_days = 1.0f;

    result.fading_rate_monthly = calculateFadingRate(initial_r, current_r, time_days);

    result.predicted_30d = predictReflectance(current_r, temperature, humidity,
                                              light_intensity, 450, 30);
    result.predicted_90d = predictReflectance(current_r, temperature, humidity,
                                              light_intensity, 450, 90);
    result.predicted_180d = predictReflectance(current_r, temperature, humidity,
                                               light_intensity, 450, 180);

    result.risk_level = assessRiskLevel(result.fading_rate_monthly);

    return result;
}

}
