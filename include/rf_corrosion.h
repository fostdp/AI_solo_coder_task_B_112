#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <cmath>
#include <random>

namespace haihunhou {

struct RfTree {
    struct Node {
        bool is_leaf = false;
        uint32_t feature_index = 0;
        float threshold = 0.0f;
        float value = 0.0f;
        uint32_t left_child = 0;
        uint32_t right_child = 0;
    };
    std::vector<Node> nodes;
    std::vector<uint32_t> feature_subset;

    float predict(const std::vector<float>& features) const {
        if (nodes.empty()) return 3.0f;
        uint32_t idx = 0;
        while (idx < nodes.size() && !nodes[idx].is_leaf) {
            const auto& node = nodes[idx];
            uint32_t next_idx = features[node.feature_index] <= node.threshold ? node.left_child : node.right_child;
            if (next_idx >= nodes.size()) return 3.0f;
            idx = next_idx;
        }
        return idx < nodes.size() ? nodes[idx].value : 3.0f;
    }
};

struct RfModel {
    std::vector<RfTree> trees;
    uint32_t n_features = 8;
    uint32_t n_estimators = 200;
    std::vector<std::string> feature_names;
    float min_value = 1.0f;
    float max_value = 10.0f;
};

struct CorrosionFeatures {
    float temperature;
    float humidity;
    float fungi_concentration;
    float ochratoxin_a;
    float citrinin;
    float patulin;
    float total_voc;
    float ph_value;
    bool has_feature[8] = {true, true, true, true, true, true, true, true};
};

enum RfStatus {
    RF_OK = 0,
    RF_ERR_NO_MOLD = 1,
    RF_WARN_MISSING_FEATURES = 2,
    RF_ERR_MODEL_NOT_LOADED = 3,
    RF_WARN_EXTREME_CONCENTRATION = 4
};

struct CorrosionDiagnostics {
    RfStatus status = RF_OK;
    uint32_t imputed_feature_count = 0;
    std::vector<std::string> imputed_feature_names;
    std::string warning_message;
    bool is_extreme_concentration = false;
};

class RfCorrosion {
public:
    RfCorrosion();
    ~RfCorrosion();

    void setConfig(const RfCorrosionConfig& config);
    RfCorrosionConfig getConfig() const;

    bool loadModel();
    bool loadModelFromJson(const std::string& json_content);
    bool isLoaded() const;

    CorrosionPrediction analyzeSlip(
        uint32_t slip_id,
        const MicrobialData& microbial,
        float temperature,
        float humidity,
        const std::vector<float>& voc_profile = {},
        CorrosionDiagnostics* diag = nullptr);

    float predictCorrosionFactor(const CorrosionFeatures& features,
                                 CorrosionDiagnostics* diag = nullptr);

    std::vector<float> predictWithConfidence(
        const CorrosionFeatures& features,
        float& std_dev);

    float predictFadingAcceleration(
        float base_fading_rate,
        const CorrosionFeatures& features);

    std::vector<std::pair<std::string, float>> getFeatureImportance() const;

    CorrosionFeatures imputeMissingFeatures(CorrosionFeatures f,
                                            std::vector<std::string>* imputed_names = nullptr) const;

    static const float* getFeatureMedians();

    bool isExtremeConcentration(const CorrosionFeatures& f) const;

private:
    RfCorrosionConfig config_;
    RfModel model_;
    mutable std::mutex mutex_;
    std::atomic<bool> loaded_;
    std::vector<float> feature_importance_;
    std::mt19937 rng_;

    std::vector<float> extractFeatures(const CorrosionFeatures& input) const;
    float aggregatePredictions(const std::vector<float>& tree_preds) const;
    float computeStdDev(const std::vector<float>& values, float mean) const;
    uint8_t assessRiskLevel(float corrosion_factor) const;

    CorrosionFeatures simulateVocFromMicrobial(
        const MicrobialData& microbial,
        float temperature,
        float humidity) const;

    bool generateDefaultModel();
    bool saveModelToJson(const std::string& path) const;
};

}
