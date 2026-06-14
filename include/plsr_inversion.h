#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace haihunhou {

struct PlsrCoefficients {
    std::vector<std::vector<float>> W;
    std::vector<std::vector<float>> P;
    std::vector<std::vector<float>> Q;
    std::vector<float> Y_mean;
    std::vector<float> X_mean;
    std::vector<float> X_std;
    std::vector<float> Y_std;
    uint32_t n_components = 15;
    uint32_t n_features = 0;
    uint32_t n_outputs = 0;
};

struct PlsrPrediction {
    float carbon_black_ratio;
    float binder_ratio;
    float moisture_ratio;
    float impurity_ratio;
    float confidence;
    std::vector<float> residuals;
    float hotelling_t2;
    float q_residual;
};

class PlsrInversion {
public:
    PlsrInversion();
    ~PlsrInversion();

    void setConfig(const PlsrInversionConfig& config);
    PlsrInversionConfig getConfig() const;

    bool loadCoefficients();
    bool loadCoefficientsFromJson(const std::string& json_content);
    bool isLoaded() const;

    InkComposition analyzeSlip(
        uint32_t slip_id,
        const std::vector<SpectralData>& spectral_curve);

    PlsrPrediction predict(const std::vector<float>& spectrum);

    std::vector<float> extractSpectrumVector(
        const std::vector<SpectralData>& spectral_data,
        const std::vector<uint16_t>& wavelengths = {}) const;

    float calculateConfidence(
        const PlsrPrediction& pred,
        const std::vector<float>& input) const;

    std::string classifyInkType(float carbon_black, float binder) const;

private:
    PlsrInversionConfig config_;
    PlsrCoefficients coeffs_;
    mutable std::mutex mutex_;
    std::atomic<bool> loaded_;

    std::vector<float> standardizeInput(const std::vector<float>& x) const;
    std::vector<float> destandardizeOutput(const std::vector<float>& y) const;
    std::vector<float> matrixVectorMul(
        const std::vector<std::vector<float>>& M,
        const std::vector<float>& v) const;

    float hotellingT2(const std::vector<float>& scores) const;
    float qResidual(
        const std::vector<float>& x,
        const std::vector<float>& scores,
        const std::vector<std::vector<float>>& P) const;

    bool generateDefaultCoefficients();
    bool saveCoefficientsToJson(const std::string& path) const;
};

}
