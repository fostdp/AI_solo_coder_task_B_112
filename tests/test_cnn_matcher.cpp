#include "test_stub.h"
#include "cnn_matcher.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace haihunhou;

int main() {
    std::cout << "=== CNN Matcher Test ===" << std::endl;

    CnnMatcher matcher;

    CnnMatcherConfig cfg;
    cfg.match_threshold = 0.7f;
    cfg.max_candidates = 5;
    cfg.stroke_weight = 0.6f;
    cfg.contour_weight = 0.4f;
    matcher.setConfig(cfg);
    auto cfg_out = matcher.getConfig();
    assert(std::abs(cfg_out.match_threshold - 0.7f) < 1e-6);
    std::cout << "[PASS] setConfig/getConfig" << std::endl;

    bool loaded = matcher.loadModel();
    assert(loaded);
    assert(matcher.isModelLoaded());
    std::cout << "[PASS] loadModel" << std::endl;

    std::vector<SpectralData> spec_a, spec_b;
    for (uint16_t wl = 380; wl <= 780; wl += 50) {
        SpectralData d;
        d.slip_id = 1;
        d.wavelength = wl;
        d.reflectance = 0.6f + 0.05f * std::sin(wl * 0.01f);
        d.temperature = 22.0f;
        d.humidity = 55.0f;
        d.light_intensity = 50.0f;
        spec_a.push_back(d);
    }

    for (uint16_t wl = 380; wl <= 780; wl += 50) {
        SpectralData d;
        d.slip_id = 2;
        d.wavelength = wl;
        d.reflectance = 0.62f + 0.04f * std::sin(wl * 0.012f);
        d.temperature = 22.0f;
        d.humidity = 55.0f;
        d.light_intensity = 50.0f;
        spec_b.push_back(d);
    }

    StrokeFeatures fa, fb;
    fa.angles = {10.0f, 20.0f, 15.0f, 25.0f, 30.0f};
    fa.thickness = {1.0f, 1.2f, 0.8f, 1.5f, 1.1f};
    fa.pressure = {50.0f, 60.0f, 55.0f, 70.0f, 65.0f};
    fa.speed_profile = {1.0f, 1.2f, 0.9f, 1.1f, 0.8f};

    fb.angles = {10.5f, 19.5f, 15.5f, 24.5f, 30.5f};
    fb.thickness = {0.9f, 1.3f, 0.7f, 1.4f, 1.2f};
    fb.pressure = {48.0f, 62.0f, 53.0f, 68.0f, 67.0f};
    fb.speed_profile = {1.1f, 1.1f, 1.0f, 1.0f, 0.9f};

    float stroke_sim = matcher.computeStrokeSimilarity(fa, fb);
    assert(stroke_sim >= 0.0f && stroke_sim <= 1.0f);
    std::cout << "[PASS] computeStrokeSimilarity = " << stroke_sim << std::endl;

    ContourFeatures ca, cb;
    ca.aspect_ratio = 19.1667f;
    ca.perimeter = 48.4f;
    ca.curvature_variance = 0.05f;
    ca.edge_points = {{0, 0.6f}, {23, 0.6f}, {23, -0.6f}, {0, -0.6f}};

    cb.aspect_ratio = 19.1f;
    cb.perimeter = 48.2f;
    cb.curvature_variance = 0.055f;
    cb.edge_points = {{0, 0.62f}, {23, 0.58f}, {23, -0.62f}, {0, -0.58f}};

    float contour_sim = matcher.computeContourSimilarity(ca, cb);
    assert(contour_sim >= 0.0f && contour_sim <= 1.0f);
    std::cout << "[PASS] computeContourSimilarity = " << contour_sim << std::endl;

    EmbeddingVector ea, eb;
    ea.data.resize(128, 0.5f);
    eb.data.resize(128, 0.5f);
    float cos_sim = matcher.cosineSimilarity(ea, eb);
    assert(std::abs(cos_sim - 1.0f) < 1e-4);
    std::cout << "[PASS] cosineSimilarity (identical) = " << cos_sim << std::endl;

    eb.data.assign(128, -0.5f);
    float cos_sim_opp = matcher.cosineSimilarity(ea, eb);
    assert(std::abs(cos_sim_opp + 1.0f) < 1e-4);
    std::cout << "[PASS] cosineSimilarity (opposite) = " << cos_sim_opp << std::endl;

    std::vector<uint32_t> candidates = {2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::unordered_map<uint32_t, std::vector<SpectralData>> cand_spec;
    for (auto id : candidates) {
        std::vector<SpectralData> sd;
        for (uint16_t wl = 380; wl <= 780; wl += 50) {
            SpectralData d;
            d.slip_id = id;
            d.wavelength = wl;
            d.reflectance = 0.55f + 0.1f * std::sin(id * 0.1f + wl * 0.01f);
            sd.push_back(d);
        }
        cand_spec[id] = sd;
    }

    auto matches = matcher.findMatches(1, candidates, spec_a, cand_spec);
    assert(matches.size() <= cfg.max_candidates);
    std::cout << "[PASS] findMatches returned " << matches.size() << " results" << std::endl;

    for (auto& m : matches) {
        assert(m.composite_score >= cfg.match_threshold);
        assert(m.stroke_similarity >= 0.0f && m.stroke_similarity <= 1.0f);
        assert(m.contour_similarity >= 0.0f && m.contour_similarity <= 1.0f);
        assert(m.match_level >= 0 && m.match_level <= 4);
    }
    std::cout << "[PASS] match results validation" << std::endl;

    std::cout << "\n=== All CNN Matcher tests passed ===" << std::endl;
    return 0;
}
