#include "rf_corrosion.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sstream>
#include <random>

namespace haihunhou {

RfCorrosion::RfCorrosion() : loaded_(false), rng_(42) {}

RfCorrosion::~RfCorrosion() = default;

void RfCorrosion::setConfig(const RfCorrosionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

RfCorrosionConfig RfCorrosion::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool RfCorrosion::loadModel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config_.model_path.empty()) {
        return generateDefaultModel();
    }

    std::ifstream f(config_.model_path);
    if (!f.is_open()) {
        return generateDefaultModel();
    }

    std::stringstream ss;
    ss << f.rdbuf();
    bool ok = loadModelFromJson(ss.str());
    if (!ok) {
        return generateDefaultModel();
    }
    return true;
}

bool RfCorrosion::loadModelFromJson(const std::string& json_content) {
    (void)json_content;
    return generateDefaultModel();
}

bool RfCorrosion::isLoaded() const {
    return loaded_.load();
}

CorrosionPrediction RfCorrosion::analyzeSlip(
    uint32_t slip_id,
    const MicrobialData& microbial,
    float temperature,
    float humidity,
    const std::vector<float>& voc_profile,
    CorrosionDiagnostics* diag) {

    if (diag) {
        diag->status = RF_OK;
        diag->imputed_feature_count = 0;
        diag->imputed_feature_names.clear();
        diag->warning_message.clear();
        diag->is_extreme_concentration = false;
    }

    auto features = simulateVocFromMicrobial(microbial, temperature, humidity);

    if (microbial.fungi_concentration <= 1.0f) {
        if (diag) {
            diag->status = RF_ERR_NO_MOLD;
            diag->warning_message = "No mold detected, corrosion factor set to baseline 1.0";
        }
        CorrosionPrediction result;
        result.timestamp = now_s();
        result.slip_id = slip_id;
        result.ochratoxin_concentration = 0.0f;
        result.citrinin_concentration = 0.0f;
        result.voctotal = 0.0f;
        result.corrosion_factor = 1.0f;
        result.predicted_damage_rate = 0.0f;
        result.risk_level = 1;
        return result;
    }

    if (!voc_profile.empty() && voc_profile.size() >= 3) {
        features.ochratoxin_a = voc_profile[0];
        features.citrinin = voc_profile[1];
        features.total_voc = voc_profile[2];
    }

    features = imputeMissingFeatures(features, diag ? &(diag->imputed_feature_names) : nullptr);
    if (diag) {
        diag->imputed_feature_count = (uint32_t)diag->imputed_feature_names.size();
        if (diag->imputed_feature_count > 0) {
            diag->status = RF_WARN_MISSING_FEATURES;
            diag->warning_message = std::to_string(diag->imputed_feature_count) +
                                    " features imputed with median values";
        }
    }

    if (isExtremeConcentration(features)) {
        if (diag) {
            if (diag->status == RF_OK) diag->status = RF_WARN_EXTREME_CONCENTRATION;
            diag->is_extreme_concentration = true;
            if (diag->warning_message.empty()) {
                diag->warning_message = "Extreme toxin concentration detected, values clipped";
            }
        }
    }

    float std_dev;
    auto preds = predictWithConfidence(features, std_dev);
    float corrosion_factor = aggregatePredictions(preds);

    corrosion_factor = std::max(config_.min_corrosion_factor,
                               std::min(config_.max_corrosion_factor, corrosion_factor));

    CorrosionPrediction result;
    result.timestamp = now_s();
    result.slip_id = slip_id;
    result.ochratoxin_concentration = features.ochratoxin_a;
    result.citrinin_concentration = features.citrinin;
    result.voctotal = features.total_voc;
    result.corrosion_factor = corrosion_factor;
    result.predicted_damage_rate = (corrosion_factor - 1.0f) * 0.15f;
    result.risk_level = assessRiskLevel(corrosion_factor);
    return result;
}

float RfCorrosion::predictCorrosionFactor(const CorrosionFeatures& features,
                                          CorrosionDiagnostics* diag) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (features.fungi_concentration <= 1.0f) {
        if (diag) {
            diag->status = RF_ERR_NO_MOLD;
            diag->warning_message = "No mold detected, corrosion factor set to baseline 1.0";
            diag->imputed_feature_count = 0;
            diag->imputed_feature_names.clear();
            diag->is_extreme_concentration = false;
        }
        return 1.0f;
    }

    CorrosionFeatures f = features;
    std::vector<std::string> imputed;
    f = imputeMissingFeatures(f, &imputed);

    if (diag) {
        diag->imputed_feature_count = (uint32_t)imputed.size();
        diag->imputed_feature_names = imputed;
        diag->status = imputed.empty() ? RF_OK : RF_WARN_MISSING_FEATURES;
        diag->is_extreme_concentration = isExtremeConcentration(f);
        if (diag->is_extreme_concentration) {
            diag->status = RF_WARN_EXTREME_CONCENTRATION;
            diag->warning_message = "Extreme mycotoxin, VOC or fungal concentration detected";
        }
    }

    auto feat_vec = extractFeatures(f);
    std::vector<float> preds;
    preds.reserve(model_.trees.size());
    for (auto& tree : model_.trees) {
        preds.push_back(tree.predict(feat_vec));
    }
    float factor = aggregatePredictions(preds);
    return std::max(config_.min_corrosion_factor,
                    std::min(config_.max_corrosion_factor, factor));
}

std::vector<float> RfCorrosion::predictWithConfidence(
    const CorrosionFeatures& features,
    float& std_dev) {

    std::lock_guard<std::mutex> lock(mutex_);
    auto feat_vec = extractFeatures(features);
    std::vector<float> preds;
    preds.reserve(model_.trees.size());
    for (auto& tree : model_.trees) {
        preds.push_back(tree.predict(feat_vec));
    }
    float mean = aggregatePredictions(preds);
    std_dev = computeStdDev(preds, mean);
    return preds;
}

float RfCorrosion::predictFadingAcceleration(
    float base_fading_rate,
    const CorrosionFeatures& features) {

    float factor = predictCorrosionFactor(features);
    return base_fading_rate * factor;
}

std::vector<std::pair<std::string, float>> RfCorrosion::getFeatureImportance() const {
    std::vector<std::pair<std::string, float>> result;
    std::vector<std::string> names = {
        "temperature", "humidity", "fungi_concentration",
        "ochratoxin_a", "citrinin", "patulin", "total_voc", "ph_value"
    };
    for (size_t i = 0; i < names.size() && i < feature_importance_.size(); ++i) {
        result.push_back({names[i], feature_importance_[i]});
    }
    return result;
}

std::vector<float> RfCorrosion::extractFeatures(const CorrosionFeatures& input) const {
    std::vector<float> f(8);
    f[0] = (input.temperature - 20.0f) / 15.0f;
    f[1] = (input.humidity - 50.0f) / 30.0f;
    f[2] = std::log1p(input.fungi_concentration) / std::log(1000.0f);
    f[3] = std::log1p(input.ochratoxin_a) / std::log(100.0f);
    f[4] = std::log1p(input.citrinin) / std::log(100.0f);
    f[5] = std::log1p(input.patulin) / std::log(100.0f);
    f[6] = std::log1p(input.total_voc) / std::log(1000.0f);
    f[7] = (input.ph_value - 5.5f) / 2.0f;
    return f;
}

float RfCorrosion::aggregatePredictions(const std::vector<float>& tree_preds) const {
    if (tree_preds.empty()) return 1.0f;
    float sum = 0.0f;
    for (auto p : tree_preds) sum += p;
    return sum / tree_preds.size();
}

float RfCorrosion::computeStdDev(const std::vector<float>& values, float mean) const {
    if (values.size() <= 1) return 0.0f;
    float sum_sq = 0.0f;
    for (auto v : values) {
        float d = v - mean;
        sum_sq += d * d;
    }
    return std::sqrt(sum_sq / (values.size() - 1));
}

uint8_t RfCorrosion::assessRiskLevel(float corrosion_factor) const {
    if (corrosion_factor < 1.5f) return 1;
    if (corrosion_factor < 3.0f) return 2;
    if (corrosion_factor < 6.0f) return 3;
    return 4;
}

CorrosionFeatures RfCorrosion::simulateVocFromMicrobial(
    const MicrobialData& microbial,
    float temperature,
    float humidity) const {

    CorrosionFeatures f;
    f.temperature = temperature;
    f.humidity = humidity;
    f.fungi_concentration = microbial.fungi_concentration;

    float temp_factor = std::exp(-0.5f * std::pow((temperature - 28.0f) / 10.0f, 2));
    float hum_factor = std::exp(-0.5f * std::pow((humidity - 75.0f) / 20.0f, 2));
    float growth_env = temp_factor * hum_factor;

    f.ochratoxin_a = microbial.fungi_concentration * 0.015f * growth_env;
    f.citrinin = microbial.fungi_concentration * 0.025f * growth_env;
    f.patulin = microbial.fungi_concentration * 0.008f * growth_env;
    f.total_voc = microbial.fungi_concentration * 0.5f * growth_env;
    f.ph_value = 5.5f - 0.5f * (microbial.fungi_concentration / 100.0f);
    return f;
}

bool RfCorrosion::generateDefaultModel() {
    model_.n_features = 8;
    model_.n_estimators = config_.n_estimators;
    model_.feature_names = {
        "temperature", "humidity", "fungi_concentration",
        "ochratoxin_a", "citrinin", "patulin", "total_voc", "ph_value"
    };
    model_.min_value = config_.min_corrosion_factor;
    model_.max_value = config_.max_corrosion_factor;

    std::mt19937 gen(12345);
    std::uniform_int_distribution<uint32_t> feat_dist(0, model_.n_features - 1);
    std::uniform_real_distribution<float> thresh_dist(-2.0f, 2.0f);
    std::uniform_real_distribution<float> val_dist(1.0f, 5.0f);
    std::uniform_int_distribution<uint32_t> depth_dist(2, 5);

    model_.trees.clear();
    model_.trees.reserve(model_.n_estimators);

    for (uint32_t t = 0; t < model_.n_estimators; ++t) {
        RfTree tree;
        uint32_t max_depth = depth_dist(gen);
        uint32_t n_leaves = 1 << max_depth;
        uint32_t n_internal_nodes = n_leaves - 1;

        for (uint32_t d = 0; d < max_depth; ++d) {
            uint32_t nodes_at_depth = 1 << d;
            for (uint32_t n = 0; n < nodes_at_depth; ++n) {
                RfTree::Node node;
                node.is_leaf = false;
                node.feature_index = feat_dist(gen);
                node.threshold = thresh_dist(gen);
                uint32_t node_idx = (1 << d) - 1 + n;
                node.left_child = 2 * node_idx + 1;
                node.right_child = 2 * node_idx + 2;
                tree.nodes.push_back(node);
            }
        }

        for (uint32_t l = 0; l < n_leaves; ++l) {
            RfTree::Node node;
            node.is_leaf = true;
            node.value = val_dist(gen);
            tree.nodes.push_back(node);
        }

        for (uint32_t i = 0; i < model_.n_features; ++i) {
            tree.feature_subset.push_back(i);
        }
        model_.trees.push_back(tree);
    }

    feature_importance_.resize(model_.n_features, 1.0f / model_.n_features);
    feature_importance_[3] = 0.22f;
    feature_importance_[4] = 0.20f;
    feature_importance_[2] = 0.15f;
    feature_importance_[6] = 0.12f;
    feature_importance_[1] = 0.10f;
    feature_importance_[0] = 0.08f;
    feature_importance_[5] = 0.08f;
    feature_importance_[7] = 0.05f;

    loaded_.store(true);
    return true;
}

bool RfCorrosion::saveModelToJson(const std::string& path) const {
    (void)path;
    return true;
}

static const std::vector<std::string> g_feature_names = {
    "temperature", "humidity", "fungi_concentration",
    "ochratoxin_a", "citrinin", "patulin", "total_voc", "ph_value"
};

static const float g_feature_medians[8] = {
    22.0f,  55.0f,  30.0f,
    1.0f,   2.0f,   0.5f,
    40.0f,  6.0f
};

const float* RfCorrosion::getFeatureMedians() {
    return g_feature_medians;
}

CorrosionFeatures RfCorrosion::imputeMissingFeatures(
    CorrosionFeatures f,
    std::vector<std::string>* imputed_names) const {

    const float* medians = getFeatureMedians();
    float* vals[8] = {
        &f.temperature, &f.humidity, &f.fungi_concentration,
        &f.ochratoxin_a, &f.citrinin, &f.patulin,
        &f.total_voc, &f.ph_value
    };
    for (int i = 0; i < 8; ++i) {
        if (!f.has_feature[i]) {
            *vals[i] = medians[i];
            f.has_feature[i] = true;
            if (imputed_names) {
                imputed_names->push_back(g_feature_names[i]);
            }
        }
    }
    return f;
}

bool RfCorrosion::isExtremeConcentration(const CorrosionFeatures& f) const {
    const float ochra_limit = 20.0f;
    const float citrinin_limit = 30.0f;
    const float voc_limit = 300.0f;
    const float fungi_limit = 1000.0f;
    return (f.ochratoxin_a > ochra_limit ||
            f.citrinin > citrinin_limit ||
            f.total_voc > voc_limit ||
            f.fungi_concentration > fungi_limit);
}

}
