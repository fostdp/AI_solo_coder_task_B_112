#include "ml/response_surface.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace haihunhou {
namespace ml {

static constexpr float R_GAS = 8.314f;
static constexpr float lambda0 = 450.0f;

ResponseSurfaceModel::ResponseSurfaceModel() {
    setMaterialCoefficients(BambooMaterial::QING_ZHU, getDefaultQingZhu());
    setMaterialCoefficients(BambooMaterial::HUANG_ZHU, getDefaultHuangZhu());
    setMaterialCoefficients(BambooMaterial::CEDAR, getDefaultCedar());
    setMaterialCoefficients(BambooMaterial::UNKNOWN, getDefaultUnknown());

    global_params_.mold_beta0 = 1.0f;
    global_params_.mold_beta1 = 0.08f;
    global_params_.mold_beta2 = 0.06f;
    global_params_.mold_beta3 = -0.0015f;
    global_params_.mold_beta4 = -0.0008f;
    global_params_.mold_beta5 = 0.001f;
}

void ResponseSurfaceModel::loadFromDatabase(void* db_client) {
    (void)db_client;
}

void ResponseSurfaceModel::setGlobalParams(const ModelParams& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    global_params_ = params;
}

void ResponseSurfaceModel::setMaterialCoefficients(BambooMaterial material,
                                                   const MaterialCoefficients& coeffs) {
    std::lock_guard<std::mutex> lock(mutex_);
    material_coeffs_[static_cast<uint8_t>(material)] = coeffs;
}

const MaterialCoefficients* ResponseSurfaceModel::getMaterialCoefficients(
    BambooMaterial material) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = material_coeffs_.find(static_cast<uint8_t>(material));
    if (it != material_coeffs_.end()) {
        return &it->second;
    }
    it = material_coeffs_.find(static_cast<uint8_t>(BambooMaterial::UNKNOWN));
    if (it != material_coeffs_.end()) {
        return &it->second;
    }
    return nullptr;
}

float ResponseSurfaceModel::calculateGrowthRate(float temperature, float humidity,
                                                BambooMaterial material) const {
    const MaterialCoefficients* coeffs = getMaterialCoefficients(material);
    if (!coeffs) return 0.0f;

    float rs = responseSurface(temperature, humidity, *coeffs);
    float gauss = gaussianGrowthRate(temperature, humidity, *coeffs);
    float combined = 0.6f * rs + 0.4f * gauss;
    return std::max(0.0f, combined * coeffs->mold_resistance_factor);
}

float ResponseSurfaceModel::calculateConcentration(float initial_concentration,
                                                   float temperature, float humidity,
                                                   float time_hours,
                                                   BambooMaterial material) const {
    float growth_rate = calculateGrowthRate(temperature, humidity, material);
    float time_days = time_hours / 24.0f;
    float result = initial_concentration * std::exp(growth_rate * time_days);
    return std::min(result, 10000.0f);
}

MoldPrediction ResponseSurfaceModel::predictSlip(uint32_t slip_id,
                                                 const std::vector<MicrobialData>& history,
                                                 float temperature, float humidity,
                                                 BambooMaterial material) const {
    MoldPrediction pred;
    pred.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    pred.slip_id = slip_id;

    if (history.empty()) {
        pred.current_concentration = 10.0f;
    } else {
        float avg_fungi = 0.0f;
        size_t recent = std::min<size_t>(history.size(), 10);
        for (size_t i = history.size() - recent; i < history.size(); i++) {
            avg_fungi += history[i].fungi_concentration;
        }
        pred.current_concentration = avg_fungi / recent;
    }

    pred.growth_rate = calculateGrowthRate(temperature, humidity, material);
    pred.predicted_7d = predictConcentration(pred.current_concentration, temperature, humidity, 7, material);
    pred.predicted_30d = predictConcentration(pred.current_concentration, temperature, humidity, 30, material);
    pred.predicted_90d = predictConcentration(pred.current_concentration, temperature, humidity, 90, material);
    pred.risk_level = assessRiskLevel(std::max(pred.current_concentration, pred.predicted_30d));

    return pred;
}

float ResponseSurfaceModel::predictConcentration(float current_concentration,
                                                 float temperature, float humidity,
                                                 float days_ahead,
                                                 BambooMaterial material) const {
    return calculateConcentration(current_concentration, temperature, humidity,
                                  days_ahead * 24.0f, material);
}

float ResponseSurfaceModel::predictReflectanceLoss(float current_reflectance,
                                                   float temperature, float humidity,
                                                   float light_intensity,
                                                   float days_ahead,
                                                   BambooMaterial material) const {
    const MaterialCoefficients* coeffs = getMaterialCoefficients(material);
    if (!coeffs) return current_reflectance;

    float T = temperature + 273.15f;
    float Ea1 = global_params_.fading_Ea1 * 1000.0f;
    float k1 = global_params_.fading_A1 * std::exp(-Ea1 / (R_GAS * T));
    float wavelength_factor = 1.0f;
    k1 *= wavelength_factor;

    float Ea2 = global_params_.fading_Ea2 * 1000.0f;
    float k2 = global_params_.fading_A2 * std::exp(-Ea2 / (R_GAS * T));
    float rh_factor = std::pow(humidity / 100.0f, global_params_.fading_beta);

    float susceptibility = coeffs->fading_susceptibility;
    float reflectance = current_reflectance;
    float dt = 0.1f;
    float total_time = days_ahead;

    for (float t = 0; t < total_time; t += dt) {
        float light_norm = light_intensity / 500.0f;
        float dR = -(k1 * light_norm * reflectance +
                     k2 * rh_factor * (1.0f - reflectance)) * dt * susceptibility;
        reflectance += dR;
        reflectance = std::max(0.05f, std::min(1.0f, reflectance));
    }

    return reflectance;
}

FadingAnalysis ResponseSurfaceModel::analyzeFading(uint32_t slip_id,
                                                   const std::vector<SpectralData>& history,
                                                   float temperature, float humidity,
                                                   float light_intensity,
                                                   BambooMaterial material) const {
    FadingAnalysis fa;
    fa.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    fa.slip_id = slip_id;

    if (history.size() < 2) {
        fa.reflectance_450nm = 0.8f;
        fa.fading_rate_monthly = 0.01f;
    } else {
        std::vector<float> r450;
        for (const auto& d : history) {
            if (d.wavelength == 450) {
                r450.push_back(d.reflectance);
            }
        }

        if (r450.size() >= 2) {
            fa.reflectance_450nm = r450.back();
            size_t recent = std::min<size_t>(r450.size(), 30);
            size_t start = r450.size() - recent;
            float sum_rate = 0.0f;
            for (size_t i = start + 1; i < r450.size(); i++) {
                if (r450[i - 1] > 0) {
                    sum_rate += (r450[i - 1] - r450[i]) / r450[i - 1];
                }
            }
            fa.fading_rate_monthly = sum_rate / (recent - 1) * 120.0f;
            fa.fading_rate_monthly = std::max(0.0f, std::min(1.0f, fa.fading_rate_monthly));
        } else {
            fa.reflectance_450nm = 0.8f;
            fa.fading_rate_monthly = 0.01f;
        }
    }

    fa.predicted_30d = predictReflectanceLoss(fa.reflectance_450nm, temperature, humidity,
                                               light_intensity, 30, material);
    fa.predicted_90d = predictReflectanceLoss(fa.reflectance_450nm, temperature, humidity,
                                               light_intensity, 90, material);
    fa.predicted_180d = predictReflectanceLoss(fa.reflectance_450nm, temperature, humidity,
                                                light_intensity, 180, material);
    fa.risk_level = assessFadingRisk(fa.fading_rate_monthly);

    return fa;
}

BambooMaterial ResponseSurfaceModel::parseMaterial(const std::string& material_str) {
    if (material_str == "青竹" || material_str == "qingzhu" || material_str == "QING_ZHU")
        return BambooMaterial::QING_ZHU;
    if (material_str == "黄竹" || material_str == "huangzhu" || material_str == "HUANG_ZHU")
        return BambooMaterial::HUANG_ZHU;
    if (material_str == "杉木" || material_str == "cedar" || material_str == "CEDAR")
        return BambooMaterial::CEDAR;
    return BambooMaterial::UNKNOWN;
}

const char* ResponseSurfaceModel::materialName(BambooMaterial material) {
    switch (material) {
        case BambooMaterial::QING_ZHU: return "青竹";
        case BambooMaterial::HUANG_ZHU: return "黄竹";
        case BambooMaterial::CEDAR: return "杉木";
        case BambooMaterial::OTHER: return "其他";
        default: return "未知";
    }
}

bool ResponseSurfaceModel::isMaterialLoaded(BambooMaterial material) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return material_coeffs_.find(static_cast<uint8_t>(material)) != material_coeffs_.end();
}

size_t ResponseSurfaceModel::loadedMaterialCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return material_coeffs_.size();
}

float ResponseSurfaceModel::responseSurface(float temperature, float humidity,
                                            const MaterialCoefficients& coeffs) const {
    float T = std::max(0.0f, std::min(60.0f, temperature));
    float RH = std::max(0.0f, std::min(100.0f, humidity));

    float log_cfu = coeffs.beta0 +
                    coeffs.beta1 * T +
                    coeffs.beta2 * RH +
                    coeffs.beta3 * T * T +
                    coeffs.beta4 * RH * RH +
                    coeffs.beta5 * T * RH;

    return std::exp(std::max(-5.0f, std::min(10.0f, log_cfu))) / 1000.0f;
}

float ResponseSurfaceModel::gaussianGrowthRate(float temperature, float humidity,
                                               const MaterialCoefficients& coeffs) const {
    (void)coeffs;
    static constexpr float T_opt = 25.0f;
    static constexpr float RH_opt = 65.0f;
    static constexpr float sigma_T = 8.0f;
    static constexpr float sigma_RH = 15.0f;
    static constexpr float mu_opt = 0.05f;

    float dT = temperature - T_opt;
    float dRH = humidity - RH_opt;
    float exponent = -(dT * dT) / (2 * sigma_T * sigma_T)
                     - (dRH * dRH) / (2 * sigma_RH * sigma_RH);
    return mu_opt * std::exp(exponent);
}

uint8_t ResponseSurfaceModel::assessRiskLevel(float concentration) const {
    if (concentration < 20.0f) return 1;
    if (concentration < 50.0f) return 2;
    if (concentration < 100.0f) return 3;
    return 4;
}

uint8_t ResponseSurfaceModel::assessFadingRisk(float fading_rate_monthly) const {
    if (fading_rate_monthly < 0.05f) return 1;
    if (fading_rate_monthly < 0.1f) return 2;
    if (fading_rate_monthly < 0.2f) return 3;
    return 4;
}

MaterialCoefficients ResponseSurfaceModel::getDefaultQingZhu() {
    MaterialCoefficients c;
    c.material = BambooMaterial::QING_ZHU;
    c.material_name = "青竹";

    c.beta0 = 0.8f;
    c.beta1 = 0.075f;
    c.beta2 = 0.055f;
    c.beta3 = -0.0014f;
    c.beta4 = -0.00075f;
    c.beta5 = 0.0009f;

    c.cellulose_content = 0.52f;
    c.lignin_content = 0.26f;
    c.moisture_equilibrium = 0.12f;
    c.ph_value = 5.2f;

    c.mold_resistance_factor = 1.15f;
    c.fading_susceptibility = 0.85f;
    return c;
}

MaterialCoefficients ResponseSurfaceModel::getDefaultHuangZhu() {
    MaterialCoefficients c;
    c.material = BambooMaterial::HUANG_ZHU;
    c.material_name = "黄竹";

    c.beta0 = 1.15f;
    c.beta1 = 0.088f;
    c.beta2 = 0.068f;
    c.beta3 = -0.0017f;
    c.beta4 = -0.00092f;
    c.beta5 = 0.00115f;

    c.cellulose_content = 0.48f;
    c.lignin_content = 0.30f;
    c.moisture_equilibrium = 0.10f;
    c.ph_value = 5.6f;

    c.mold_resistance_factor = 0.88f;
    c.fading_susceptibility = 1.12f;
    return c;
}

MaterialCoefficients ResponseSurfaceModel::getDefaultCedar() {
    MaterialCoefficients c;
    c.material = BambooMaterial::CEDAR;
    c.material_name = "杉木";

    c.beta0 = 0.45f;
    c.beta1 = 0.062f;
    c.beta2 = 0.048f;
    c.beta3 = -0.0012f;
    c.beta4 = -0.0006f;
    c.beta5 = 0.00075f;

    c.cellulose_content = 0.45f;
    c.lignin_content = 0.34f;
    c.moisture_equilibrium = 0.08f;
    c.ph_value = 5.8f;

    c.mold_resistance_factor = 1.45f;
    c.fading_susceptibility = 0.72f;
    return c;
}

MaterialCoefficients ResponseSurfaceModel::getDefaultUnknown() {
    MaterialCoefficients c;
    c.material = BambooMaterial::UNKNOWN;
    c.material_name = "未知";

    c.beta0 = 1.0f;
    c.beta1 = 0.08f;
    c.beta2 = 0.06f;
    c.beta3 = -0.0015f;
    c.beta4 = -0.0008f;
    c.beta5 = 0.001f;

    c.cellulose_content = 0.50f;
    c.lignin_content = 0.28f;
    c.moisture_equilibrium = 0.11f;
    c.ph_value = 5.5f;

    c.mold_resistance_factor = 1.0f;
    c.fading_susceptibility = 1.0f;
    return c;
}

}
}
