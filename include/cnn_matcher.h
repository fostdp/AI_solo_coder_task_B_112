#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace haihunhou {

struct StrokeFeatures {
    std::vector<float> angles;
    std::vector<float> thickness;
    std::vector<float> pressure;
    std::vector<float> speed_profile;
};

struct ContourFeatures {
    std::vector<std::pair<float, float>> edge_points;
    float perimeter;
    float aspect_ratio;
    float curvature_variance;
};

struct EmbeddingVector {
    std::vector<float> data;
    size_t size() const { return data.size(); }
    float& operator[](size_t i) { return data[i]; }
    const float& operator[](size_t i) const { return data[i]; }
};

class CnnMatcher {
public:
    CnnMatcher();
    ~CnnMatcher();

    void setConfig(const CnnMatcherConfig& config);
    CnnMatcherConfig getConfig() const;

    bool loadModel();
    bool isModelLoaded() const;

    std::vector<SlipMatchResult> findMatches(
        uint32_t query_slip_id,
        const std::vector<uint32_t>& candidate_ids,
        const std::vector<SpectralData>& query_spectral,
        const std::unordered_map<uint32_t, std::vector<SpectralData>>& candidate_spectral);

    std::vector<SlipMatchResult> matchAllSlips(
        const std::vector<uint32_t>& slip_ids,
        const std::unordered_map<uint32_t, std::vector<SpectralData>>& spectral_data,
        float min_score = 0.5f);

    float computeStrokeSimilarity(
        const StrokeFeatures& a,
        const StrokeFeatures& b) const;

    float computeContourSimilarity(
        const ContourFeatures& a,
        const ContourFeatures& b) const;

    float cosineSimilarity(const EmbeddingVector& a, const EmbeddingVector& b) const;

private:
    CnnMatcherConfig config_;
    mutable std::mutex mutex_;
    std::atomic<bool> model_loaded_;
    std::unordered_map<uint32_t, EmbeddingVector> embedding_cache_;

    EmbeddingVector extractStrokeEmbedding(const std::vector<SpectralData>& spectral) const;
    EmbeddingVector extractContourEmbedding(const std::vector<SpectralData>& spectral) const;

    StrokeFeatures spectralToStrokeFeatures(const std::vector<SpectralData>& spectral) const;
    ContourFeatures spectralToContourFeatures(const std::vector<SpectralData>& spectral) const;

    float euclideanDistance(const std::vector<float>& a, const std::vector<float>& b) const;
    float dynamicTimeWarping(const std::vector<float>& a, const std::vector<float>& b) const;
    uint8_t classifyMatchLevel(float score) const;
};

}
