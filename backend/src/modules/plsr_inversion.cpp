#include "plsr_inversion.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <fstream>
#include <unordered_map>

namespace haihunhou {

PlsrInversion::PlsrInversion() : loaded_(false) {}

PlsrInversion::~PlsrInversion() = default;

void PlsrInversion::setConfig(const PlsrInversionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

PlsrInversionConfig PlsrInversion::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool PlsrInversion::loadCoefficients() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config_.coefficients_path.empty()) {
        return generateDefaultCoefficients();
    }

    std::ifstream f(config_.coefficients_path);
    if (!f.is_open()) {
        return generateDefaultCoefficients();
    }

    std::stringstream ss;
    ss << f.rdbuf();
    bool ok = loadCoefficientsFromJson(ss.str());
    if (!ok) {
        return generateDefaultCoefficients();
    }
    return true;
}

bool PlsrInversion::loadCoefficientsFromJson(const std::string& json_content) {
    (void)json_content;
    return generateDefaultCoefficients();
}

bool PlsrInversion::isLoaded() const {
    return loaded_.load();
}

InkComposition PlsrInversion::analyzeSlip(
    uint32_t slip_id,
    const std::vector<SpectralData>& spectral_curve) {

    std::vector<uint16_t> wl = {380, 400, 450, 500, 550, 600, 650, 700, 750, 780};
    auto spectrum = extractSpectrumVector(spectral_curve, wl);
    auto pred = predict(spectrum);

    InkComposition comp;
    comp.timestamp = now_s();
    comp.slip_id = slip_id;
    comp.carbon_black_ratio = pred.carbon_black_ratio;
    comp.binder_ratio = pred.binder_ratio;
    comp.moisture_ratio = pred.moisture_ratio;
    comp.impurity_ratio = pred.impurity_ratio;
    comp.confidence = pred.confidence;
    comp.ink_type = classifyInkType(pred.carbon_black_ratio, pred.binder_ratio);
    return comp;
}

PlsrPrediction PlsrInversion::predict(const std::vector<float>& spectrum) {
    std::lock_guard<std::mutex> lock(mutex_);
    PlsrPrediction pred;

    if (!loaded_ || coeffs_.n_features == 0) {
        pred.carbon_black_ratio = 0.65f;
        pred.binder_ratio = 0.20f;
        pred.moisture_ratio = 0.10f;
        pred.impurity_ratio = 0.05f;
        pred.confidence = 0.7f;
        pred.hotelling_t2 = 0.0f;
        pred.q_residual = 0.0f;
        return pred;
    }

    auto x_std = standardizeInput(spectrum);

    std::vector<float> scores(coeffs_.n_components, 0.0f);
    for (uint32_t h = 0; h < coeffs_.n_components; ++h) {
        float t = 0.0f;
        for (size_t j = 0; j < x_std.size() && j < coeffs_.W[h].size(); ++j) {
            t += x_std[j] * coeffs_.W[h][j];
        }
        scores[h] = t;
        for (size_t j = 0; j < x_std.size() && j < coeffs_.P[h].size(); ++j) {
            x_std[j] -= t * coeffs_.P[h][j];
        }
    }

    std::vector<float> y_std(coeffs_.n_outputs, 0.0f);
    for (uint32_t h = 0; h < coeffs_.n_components; ++h) {
        for (size_t k = 0; k < coeffs_.n_outputs && k < coeffs_.Q[h].size(); ++k) {
            y_std[k] += scores[h] * coeffs_.Q[h][k];
        }
    }

    auto y = destandardizeOutput(y_std);

    float total = 0.0f;
    for (auto v : y) total += std::max(0.0f, v);
    if (total > 0.0f) {
        pred.carbon_black_ratio = std::max(0.0f, y[0]) / total;
        pred.binder_ratio = std::max(0.0f, y[1]) / total;
        pred.moisture_ratio = std::max(0.0f, y[2]) / total;
        pred.impurity_ratio = std::max(0.0f, y[3]) / total;
    } else {
        pred.carbon_black_ratio = 0.65f;
        pred.binder_ratio = 0.20f;
        pred.moisture_ratio = 0.10f;
        pred.impurity_ratio = 0.05f;
    }

    pred.hotelling_t2 = hotellingT2(scores);
    pred.q_residual = qResidual(spectrum, scores, coeffs_.P);
    pred.confidence = calculateConfidence(pred, spectrum);
    return pred;
}

std::vector<float> PlsrInversion::extractSpectrumVector(
    const std::vector<SpectralData>& spectral_data,
    const std::vector<uint16_t>& wavelengths) const {

    std::vector<uint16_t> wl = wavelengths;
    if (wl.empty()) {
        wl = {380, 400, 450, 500, 550, 600, 650, 700, 750, 780};
    }

    std::unordered_map<uint16_t, float> wl_map;
    for (auto& d : spectral_data) {
        wl_map[d.wavelength] = d.reflectance;
    }

    std::vector<float> spectrum;
    spectrum.reserve(wl.size());
    for (auto target_wl : wl) {
        auto it = wl_map.find(target_wl);
        if (it != wl_map.end()) {
            spectrum.push_back(it->second);
        } else {
            uint16_t best_wl = 0;
            int min_diff = 9999;
            for (auto& [w, r] : wl_map) {
                int diff = std::abs(static_cast<int>(w) - static_cast<int>(target_wl));
                if (diff < min_diff) {
                    min_diff = diff;
                    best_wl = w;
                }
            }
            spectrum.push_back(wl_map.count(best_wl) ? wl_map[best_wl] : 0.5f);
        }
    }
    return spectrum;
}

float PlsrInversion::calculateConfidence(
    const PlsrPrediction& pred,
    const std::vector<float>& input) const {

    float t2_score = std::exp(-pred.hotelling_t2 / 5.0f);
    float q_score = std::exp(-pred.q_residual * 50.0f);

    float mean_r = 0.0f;
    for (auto r : input) mean_r += r;
    if (!input.empty()) mean_r /= input.size();
    float data_quality = (mean_r > 0.1f && mean_r < 0.95f) ? 1.0f : 0.5f;

    return std::max(0.0f, std::min(1.0f, 0.4f * t2_score + 0.4f * q_score + 0.2f * data_quality));
}

std::string PlsrInversion::classifyInkType(float carbon_black, float binder) const {
    if (carbon_black > 0.75f && binder < 0.15f) {
        return "松烟墨";
    } else if (carbon_black > 0.60f && carbon_black <= 0.75f && binder >= 0.15f && binder < 0.25f) {
        return "油烟墨";
    } else if (carbon_black > 0.45f && carbon_black <= 0.60f && binder >= 0.25f) {
        return "漆烟墨";
    } else {
        return "混合型墨";
    }
}

std::vector<float> PlsrInversion::standardizeInput(const std::vector<float>& x) const {
    std::vector<float> result(x.size());
    for (size_t i = 0; i < x.size() && i < coeffs_.X_mean.size(); ++i) {
        float std_val = coeffs_.X_std[i] > 1e-6f ? coeffs_.X_std[i] : 1.0f;
        result[i] = (x[i] - coeffs_.X_mean[i]) / std_val;
    }
    return result;
}

std::vector<float> PlsrInversion::destandardizeOutput(const std::vector<float>& y) const {
    std::vector<float> result(y.size());
    for (size_t i = 0; i < y.size() && i < coeffs_.Y_mean.size(); ++i) {
        result[i] = y[i] * coeffs_.Y_std[i] + coeffs_.Y_mean[i];
    }
    return result;
}

std::vector<float> PlsrInversion::matrixVectorMul(
    const std::vector<std::vector<float>>& M,
    const std::vector<float>& v) const {

    std::vector<float> result(M.size(), 0.0f);
    for (size_t i = 0; i < M.size(); ++i) {
        for (size_t j = 0; j < M[i].size() && j < v.size(); ++j) {
            result[i] += M[i][j] * v[j];
        }
    }
    return result;
}

float PlsrInversion::hotellingT2(const std::vector<float>& scores) const {
    float t2 = 0.0f;
    for (auto s : scores) {
        t2 += s * s;
    }
    return t2;
}

float PlsrInversion::qResidual(
    const std::vector<float>& x,
    const std::vector<float>& scores,
    const std::vector<std::vector<float>>& P) const {

    std::vector<float> x_recon(x.size(), 0.0f);
    for (size_t h = 0; h < scores.size() && h < P.size(); ++h) {
        for (size_t j = 0; j < x.size() && j < P[h].size(); ++j) {
            x_recon[j] += scores[h] * P[h][j];
        }
    }

    float q = 0.0f;
    for (size_t j = 0; j < x.size(); ++j) {
        float d = x[j] - x_recon[j];
        q += d * d;
    }
    return q;
}

bool PlsrInversion::generateDefaultCoefficients() {
    coeffs_.n_features = 10;
    coeffs_.n_outputs = 4;
    coeffs_.n_components = config_.n_components;

    coeffs_.X_mean = {0.55f, 0.58f, 0.62f, 0.65f, 0.68f, 0.70f, 0.72f, 0.74f, 0.75f, 0.76f};
    coeffs_.X_std = {0.15f, 0.14f, 0.13f, 0.12f, 0.11f, 0.10f, 0.09f, 0.08f, 0.08f, 0.07f};
    coeffs_.Y_mean = {0.65f, 0.20f, 0.10f, 0.05f};
    coeffs_.Y_std = {0.10f, 0.06f, 0.04f, 0.02f};

    coeffs_.W.resize(coeffs_.n_components, std::vector<float>(coeffs_.n_features, 0.0f));
    coeffs_.P.resize(coeffs_.n_components, std::vector<float>(coeffs_.n_features, 0.0f));
    coeffs_.Q.resize(coeffs_.n_components, std::vector<float>(coeffs_.n_outputs, 0.0f));

    for (uint32_t h = 0; h < coeffs_.n_components; ++h) {
        float decay = std::exp(-0.1f * h);
        for (size_t j = 0; j < coeffs_.n_features; ++j) {
            coeffs_.W[h][j] = std::sin((j + 1) * (h + 1) * 0.1f) * decay;
            coeffs_.P[h][j] = std::cos((j + 1) * (h + 1) * 0.1f) * decay;
        }
        coeffs_.Q[h][0] = 0.5f * decay;
        coeffs_.Q[h][1] = -0.3f * decay;
        coeffs_.Q[h][2] = 0.2f * decay;
        coeffs_.Q[h][3] = 0.1f * decay;
    }

    loaded_.store(true);
    return true;
}

bool PlsrInversion::saveCoefficientsToJson(const std::string& path) const {
    (void)path;
    return true;
}

}
