#pragma once
#include "common.h"
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace haihunhou {
namespace ml {

enum class BambooMaterial : uint8_t {
    UNKNOWN = 0,
    QING_ZHU = 1,
    HUANG_ZHU = 2,
    CEDAR = 3,
    OTHER = 4
};

struct MaterialCoefficients {
    BambooMaterial material;
    std::string material_name;

    float beta0;
    float beta1;
    float beta2;
    float beta3;
    float beta4;
    float beta5;

    float cellulose_content;
    float lignin_content;
    float moisture_equilibrium;
    float ph_value;

    float mold_resistance_factor;
    float fading_susceptibility;
};

class ResponseSurfaceModel {
public:
    ResponseSurfaceModel();

    void loadFromDatabase(void* db_client);

    void setGlobalParams(const ModelParams& params);

    void setMaterialCoefficients(BambooMaterial material,
                                 const MaterialCoefficients& coeffs);

    const MaterialCoefficients* getMaterialCoefficients(BambooMaterial material) const;

    float calculateGrowthRate(float temperature, float humidity,
                              BambooMaterial material = BambooMaterial::UNKNOWN) const;

    float calculateConcentration(float initial_concentration,
                                 float temperature, float humidity,
                                 float time_hours,
                                 BambooMaterial material = BambooMaterial::UNKNOWN) const;

    MoldPrediction predictSlip(uint32_t slip_id,
                               const std::vector<MicrobialData>& history,
                               float temperature, float humidity,
                               BambooMaterial material = BambooMaterial::UNKNOWN) const;

    float predictConcentration(float current_concentration,
                               float temperature, float humidity,
                               float days_ahead,
                               BambooMaterial material = BambooMaterial::UNKNOWN) const;

    float predictReflectanceLoss(float current_reflectance,
                                 float temperature, float humidity,
                                 float light_intensity,
                                 float days_ahead,
                                 BambooMaterial material = BambooMaterial::UNKNOWN) const;

    FadingAnalysis analyzeFading(uint32_t slip_id,
                                 const std::vector<SpectralData>& history,
                                 float temperature, float humidity, float light_intensity,
                                 BambooMaterial material = BambooMaterial::UNKNOWN) const;

    static BambooMaterial parseMaterial(const std::string& material_str);
    static const char* materialName(BambooMaterial material);

    bool isMaterialLoaded(BambooMaterial material) const;
    size_t loadedMaterialCount() const;

private:
    ModelParams global_params_;
    std::unordered_map<uint8_t, MaterialCoefficients> material_coeffs_;
    mutable std::mutex mutex_;

    float responseSurface(float temperature, float humidity,
                          const MaterialCoefficients& coeffs) const;

    float gaussianGrowthRate(float temperature, float humidity,
                             const MaterialCoefficients& coeffs) const;

    uint8_t assessRiskLevel(float concentration) const;
    uint8_t assessFadingRisk(float fading_rate_monthly) const;

    static MaterialCoefficients getDefaultQingZhu();
    static MaterialCoefficients getDefaultHuangZhu();
    static MaterialCoefficients getDefaultCedar();
    static MaterialCoefficients getDefaultUnknown();
};

}
}
