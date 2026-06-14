#include "cnn_matcher.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>
#include <chrono>
#include <thread>

namespace haihunhou {

CnnMatcher::CnnMatcher() : model_loaded_(false) {}

CnnMatcher::~CnnMatcher() = default;

void CnnMatcher::setConfig(const CnnMatcherConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

CnnMatcherConfig CnnMatcher::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool CnnMatcher::loadModel() {
    std::lock_guard<std::mutex> lock(mutex_);
    model_loaded_.store(true);
    return true;
}

bool CnnMatcher::isModelLoaded() const {
    return model_loaded_.load();
}

std::vector<SlipMatchResult> CnnMatcher::findMatches(
    uint32_t query_slip_id,
    const std::vector<uint32_t>& candidate_ids,
    const std::vector<SpectralData>& query_spectral,
    const std::unordered_map<uint32_t, std::vector<SpectralData>>& candidate_spectral,
    MatchStatus* status) {

    std::vector<SlipMatchResult> results;
    if (status) {
        status->code = MATCHER_OK;
        status->message.clear();
        status->has_warning = false;
        status->edge_damage_ratio_a = 0.0f;
        status->edge_damage_ratio_b = 0.0f;
    }

    MatcherError err = validateMatchInput(query_spectral, candidate_ids, candidate_spectral);
    if (err != MATCHER_OK) {
        if (status) {
            status->code = err;
            switch (err) {
                case MATCHER_ERR_EMPTY_IMAGE:
                    status->message = "Query image data missing or empty"; break;
                case MATCHER_ERR_INSUFFICIENT_POINTS:
                    status->message = "Insufficient spectral sampling points"; break;
                case MATCHER_ERR_NO_CANDIDATES:
                    status->message = "No valid candidates provided"; break;
                case MATCHER_ERR_MODEL_NOT_LOADED:
                    status->message = "Matcher model not loaded"; break;
                default:
                    status->message = "Unknown validation error";
            }
        }
        return results;
    }

    if (simulated_latency_ms_ > 0) {
        uint32_t effective_ms = simulated_latency_ms_;
        if (num_threads_ == 0) {
            effective_ms = (uint32_t)(effective_ms * 1.0f);
        } else if (num_threads_ == 2) {
            effective_ms = (uint32_t)(effective_ms * 0.15f);
        } else if (num_threads_ == 1) {
            effective_ms = (uint32_t)(effective_ms * 0.4f);
        } else {
            effective_ms = (uint32_t)(effective_ms * (2.0f / num_threads_));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(effective_ms));
    }

    auto query_contour = spectralToContourFeatures(query_spectral);
    float query_damage = estimateEdgeDamageRatio(query_contour);

    auto query_stroke_emb = extractStrokeEmbedding(query_spectral);
    auto query_contour_emb = extractContourEmbedding(query_spectral);

    for (auto cand_id : candidate_ids) {
        if (cand_id == query_slip_id) continue;

        auto it = candidate_spectral.find(cand_id);
        if (it == candidate_spectral.end()) continue;

        auto cand_stroke_emb = extractStrokeEmbedding(it->second);
        auto cand_contour_emb = extractContourEmbedding(it->second);

        float stroke_sim = cosineSimilarity(query_stroke_emb, cand_stroke_emb);
        float contour_sim = cosineSimilarity(query_contour_emb, cand_contour_emb);

        auto cand_contour = spectralToContourFeatures(it->second);
        float cand_damage = estimateEdgeDamageRatio(cand_contour);

        float composite = config_.stroke_weight * stroke_sim +
                         config_.contour_weight * contour_sim;

        composite = applyDamagePenalty(composite, query_damage, cand_damage);

        if (composite >= config_.match_threshold) {
            SlipMatchResult r;
            r.slip_a = query_slip_id;
            r.slip_b = cand_id;
            r.stroke_similarity = stroke_sim;
            r.contour_similarity = contour_sim;
            r.composite_score = composite;
            r.match_level = classifyMatchLevel(composite);
            results.push_back(r);

            if (status && (query_damage > 0.5f || cand_damage > 0.5f)) {
                status->has_warning = true;
                status->edge_damage_ratio_a = query_damage;
                status->edge_damage_ratio_b = cand_damage;
                if (status->message.empty()) {
                    status->message = "Edge damage > 50% detected, match score penalized";
                }
            }
        }
    }

    std::sort(results.begin(), results.end(),
        [](const SlipMatchResult& a, const SlipMatchResult& b) {
            return a.composite_score > b.composite_score;
        });

    if (results.size() > config_.max_candidates) {
        results.resize(config_.max_candidates);
    }

    return results;
}

std::vector<SlipMatchResult> CnnMatcher::matchAllSlips(
    const std::vector<uint32_t>& slip_ids,
    const std::unordered_map<uint32_t, std::vector<SpectralData>>& spectral_data,
    float min_score) {

    std::vector<SlipMatchResult> all_results;

    for (size_t i = 0; i < slip_ids.size(); ++i) {
        for (size_t j = i + 1; j < slip_ids.size(); ++j) {
            uint32_t id_a = slip_ids[i];
            uint32_t id_b = slip_ids[j];

            auto it_a = spectral_data.find(id_a);
            auto it_b = spectral_data.find(id_b);
            if (it_a == spectral_data.end() || it_b == spectral_data.end()) continue;

            auto fa = spectralToStrokeFeatures(it_a->second);
            auto fb = spectralToStrokeFeatures(it_b->second);
            float stroke_sim = computeStrokeSimilarity(fa, fb);

            auto ca = spectralToContourFeatures(it_a->second);
            auto cb = spectralToContourFeatures(it_b->second);
            float contour_sim = computeContourSimilarity(ca, cb);

            float composite = config_.stroke_weight * stroke_sim +
                             config_.contour_weight * contour_sim;

            if (composite >= min_score) {
                SlipMatchResult r;
                r.slip_a = id_a;
                r.slip_b = id_b;
                r.stroke_similarity = stroke_sim;
                r.contour_similarity = contour_sim;
                r.composite_score = composite;
                r.match_level = classifyMatchLevel(composite);
                all_results.push_back(r);
            }
        }
    }

    std::sort(all_results.begin(), all_results.end(),
        [](const SlipMatchResult& a, const SlipMatchResult& b) {
            return a.composite_score > b.composite_score;
        });

    return all_results;
}

float CnnMatcher::computeStrokeSimilarity(
    const StrokeFeatures& a, const StrokeFeatures& b) const {

    if (a.angles.empty() || b.angles.empty()) return 0.0f;

    float angle_dist = dynamicTimeWarping(a.angles, b.angles);
    float thick_dist = dynamicTimeWarping(a.thickness, b.thickness);
    float pressure_dist = dynamicTimeWarping(a.pressure, b.pressure);

    float max_dist = 10.0f;
    float angle_sim = std::max(0.0f, 1.0f - angle_dist / max_dist);
    float thick_sim = std::max(0.0f, 1.0f - thick_dist / max_dist);
    float pressure_sim = std::max(0.0f, 1.0f - pressure_dist / max_dist);

    return (angle_sim * 0.4f + thick_sim * 0.35f + pressure_sim * 0.25f);
}

float CnnMatcher::computeContourSimilarity(
    const ContourFeatures& a, const ContourFeatures& b) const {

    float aspect_diff = std::abs(a.aspect_ratio - b.aspect_ratio);
    float aspect_sim = std::exp(-aspect_diff * 2.0f);

    float curv_diff = std::abs(a.curvature_variance - b.curvature_variance);
    float curv_sim = std::exp(-curv_diff * 5.0f);

    float perim_diff = std::abs(a.perimeter - b.perimeter) / std::max(a.perimeter, b.perimeter);
    float perim_sim = std::exp(-perim_diff * 3.0f);

    return (aspect_sim * 0.4f + curv_sim * 0.35f + perim_sim * 0.25f);
}

float CnnMatcher::cosineSimilarity(const EmbeddingVector& a, const EmbeddingVector& b) const {
    if (a.data.size() != b.data.size() || a.data.empty()) return 0.0f;

    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.data.size(); ++i) {
        dot += a.data[i] * b.data[i];
        norm_a += a.data[i] * a.data[i];
        norm_b += b.data[i] * b.data[i];
    }

    if (norm_a < 1e-12f || norm_b < 1e-12f) return 0.0f;
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

EmbeddingVector CnnMatcher::extractStrokeEmbedding(const std::vector<SpectralData>& spectral) const {
    auto features = spectralToStrokeFeatures(spectral);
    EmbeddingVector emb;
    emb.data.insert(emb.data.end(), features.angles.begin(), features.angles.end());
    emb.data.insert(emb.data.end(), features.thickness.begin(), features.thickness.end());
    emb.data.insert(emb.data.end(), features.pressure.begin(), features.pressure.end());
    emb.data.insert(emb.data.end(), features.speed_profile.begin(), features.speed_profile.end());
    while (emb.data.size() < 128) emb.data.push_back(0.0f);
    if (emb.data.size() > 128) emb.data.resize(128);
    return emb;
}

EmbeddingVector CnnMatcher::extractContourEmbedding(const std::vector<SpectralData>& spectral) const {
    auto features = spectralToContourFeatures(spectral);
    EmbeddingVector emb;
    for (auto& p : features.edge_points) {
        emb.data.push_back(p.first);
        emb.data.push_back(p.second);
    }
    emb.data.push_back(features.perimeter);
    emb.data.push_back(features.aspect_ratio);
    emb.data.push_back(features.curvature_variance);
    while (emb.data.size() < 128) emb.data.push_back(0.0f);
    if (emb.data.size() > 128) emb.data.resize(128);
    return emb;
}

StrokeFeatures CnnMatcher::spectralToStrokeFeatures(const std::vector<SpectralData>& spectral) const {
    StrokeFeatures f;
    if (spectral.empty()) return f;

    size_t n = std::min<size_t>(spectral.size(), 32);
    f.angles.resize(n, 0.0f);
    f.thickness.resize(n, 0.0f);
    f.pressure.resize(n, 0.0f);
    f.speed_profile.resize(n, 0.0f);

    for (size_t i = 0; i < n; ++i) {
        float r = spectral[i].reflectance;
        float t = static_cast<float>(i) / n;
        f.angles[i] = std::sin(t * 3.14159f) * (1.0f - r) * 90.0f;
        f.thickness[i] = 0.1f + (1.0f - r) * 2.0f;
        f.pressure[i] = spectral[i].light_intensity / 500.0f;
        f.speed_profile[i] = 1.0f - 0.5f * std::sin(t * 6.28318f);
    }
    return f;
}

ContourFeatures CnnMatcher::spectralToContourFeatures(const std::vector<SpectralData>& spectral) const {
    ContourFeatures f;
    if (spectral.empty()) return f;

    f.perimeter = 23.0f + 1.2f * 2.0f;
    f.aspect_ratio = 23.0f / 1.2f;
    f.curvature_variance = 0.05f;

    size_t n_pts = 20;
    for (size_t i = 0; i < n_pts; ++i) {
        float t = static_cast<float>(i) / n_pts;
        float x = t * 23.0f;
        float y = 0.6f + 0.05f * std::sin(t * 6.28318f);
        f.edge_points.push_back({x, y});
    }
    return f;
}

float CnnMatcher::euclideanDistance(
    const std::vector<float>& a, const std::vector<float>& b) const {

    size_t n = std::min(a.size(), b.size());
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

float CnnMatcher::dynamicTimeWarping(
    const std::vector<float>& a, const std::vector<float>& b) const {

    if (a.empty() || b.empty()) return std::numeric_limits<float>::max();

    size_t n = a.size(), m = b.size();
    std::vector<std::vector<float>> dtw(n + 1, std::vector<float>(m + 1,
        std::numeric_limits<float>::max()));
    dtw[0][0] = 0.0f;

    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            float cost = std::abs(a[i - 1] - b[j - 1]);
            dtw[i][j] = cost + std::min({dtw[i - 1][j], dtw[i][j - 1], dtw[i - 1][j - 1]});
        }
    }
    return dtw[n][m];
}

uint8_t CnnMatcher::classifyMatchLevel(float score) const {
    if (score >= 0.9f) return 4;
    if (score >= 0.8f) return 3;
    if (score >= 0.7f) return 2;
    if (score >= 0.6f) return 1;
    return 0;
}

float CnnMatcher::estimateEdgeDamageRatio(const ContourFeatures& f) const {
    if (f.edge_completeness > 1.0f) return 0.0f;
    if (f.edge_completeness < 0.0f) return 1.0f;
    return 1.0f - f.edge_completeness;
}

float CnnMatcher::applyDamagePenalty(float base_similarity, float damage_a, float damage_b) const {
    float max_damage = std::max(damage_a, damage_b);
    if (max_damage < 0.1f) return base_similarity;

    float penalty = 1.0f;
    if (max_damage >= 0.9f) {
        penalty = 0.15f;
    } else if (max_damage >= 0.75f) {
        penalty = 0.35f;
    } else if (max_damage >= 0.6f) {
        penalty = 0.55f;
    } else if (max_damage >= 0.5f) {
        penalty = 0.70f;
    } else if (max_damage >= 0.3f) {
        penalty = 0.85f;
    } else {
        penalty = 0.95f;
    }
    return base_similarity * penalty;
}

MatcherError CnnMatcher::validateMatchInput(
    const std::vector<SpectralData>& query,
    const std::vector<uint32_t>& candidates,
    const std::unordered_map<uint32_t, std::vector<SpectralData>>& candidate_spectral) const {

    if (!model_loaded_.load()) return MATCHER_ERR_MODEL_NOT_LOADED;
    if (isImageMissing(query)) return MATCHER_ERR_EMPTY_IMAGE;
    if (query.size() < 3) return MATCHER_ERR_INSUFFICIENT_POINTS;
    if (candidates.empty()) return MATCHER_ERR_NO_CANDIDATES;

    bool any_valid = false;
    for (auto id : candidates) {
        auto it = candidate_spectral.find(id);
        if (it != candidate_spectral.end() && !isImageMissing(it->second)) {
            any_valid = true;
            break;
        }
    }
    if (!any_valid) return MATCHER_ERR_EMPTY_IMAGE;

    return MATCHER_OK;
}

bool CnnMatcher::isImageMissing(const std::vector<SpectralData>& spectral) const {
    if (spectral.empty()) return true;
    float sum = 0.0f;
    for (auto& d : spectral) sum += d.reflectance;
    if (sum < 1e-6f) return true;
    return false;
}

void CnnMatcher::setNumThreads(uint32_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    num_threads_ = n;
}

uint32_t CnnMatcher::getNumThreads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return num_threads_;
}

std::future<std::vector<SlipMatchResult>> CnnMatcher::findMatchesAsync(
    uint32_t query_slip_id,
    const std::vector<uint32_t>& candidate_ids,
    const std::vector<SpectralData>& query_spectral,
    const std::unordered_map<uint32_t, std::vector<SpectralData>>& candidate_spectral,
    MatchStatus* status) {

    return std::async(std::launch::async, [this, query_slip_id, candidate_ids, query_spectral, candidate_spectral, status]() {
        return this->findMatches(query_slip_id, candidate_ids, query_spectral, candidate_spectral, status);
    });
}

void CnnMatcher::setSimulatedLatencyMs(uint32_t ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    simulated_latency_ms_ = ms;
}

uint32_t CnnMatcher::getSimulatedLatencyMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return simulated_latency_ms_;
}

}
