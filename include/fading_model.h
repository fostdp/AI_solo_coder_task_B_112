#pragma once
#include "common.h"
#include <cmath>
#include <vector>

namespace haihunhou {

class FadingModel {
public:
    FadingModel();

    void setParams(const ModelParams& params);

    float calculateReflectance(float initial_reflectance, float temperature,
                               float humidity, float light_intensity,
                               float wavelength, float time_hours) const;

    float calculateFadingRate(float initial_reflectance, float current_reflectance,
                              float time_days) const;

    FadingAnalysis analyzeSlip(uint32_t slip_id,
                               const std::vector<SpectralData>& history,
                               float temperature, float humidity,
                               float light_intensity) const;

    float predictReflectance(float current_reflectance, float temperature,
                             float humidity, float light_intensity,
                             float wavelength, float days_ahead) const;

private:
    ModelParams params_;
    static constexpr float R = 8.314f;
    static constexpr float lambda0 = 450.0f;

    float calculateK1(float temperature, float wavelength) const;
    float calculateK2(float temperature) const;
    float photoOxidationRate(float k1, float light_intensity, float reflectance) const;
    float hydrolysisRate(float k2, float humidity, float reflectance) const;
    uint8_t assessRiskLevel(float fading_rate) const;
};

}
