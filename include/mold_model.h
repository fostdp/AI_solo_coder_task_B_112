#pragma once
#include "common.h"
#include <cmath>
#include <vector>

namespace haihunhou {

class MoldModel {
public:
    MoldModel();

    void setParams(const ModelParams& params);

    float calculateConcentration(float initial_concentration, float temperature,
                                 float humidity, float time_hours) const;

    float calculateGrowthRate(float temperature, float humidity) const;

    MoldPrediction predictSlip(uint32_t slip_id,
                               const std::vector<MicrobialData>& history,
                               float temperature, float humidity) const;

    float predictConcentration(float current_concentration, float temperature,
                               float humidity, float days_ahead) const;

private:
    ModelParams params_;
    static constexpr float mu_opt = 0.05f;
    static constexpr float T_opt = 25.0f;
    static constexpr float RH_opt = 65.0f;
    static constexpr float sigma_T = 8.0f;
    static constexpr float sigma_RH = 15.0f;

    float responseSurface(float temperature, float humidity) const;
    float gaussianGrowthRate(float temperature, float humidity) const;
    uint8_t assessRiskLevel(float concentration) const;
};

}
