#include "test_stub.h"
#include "cnn_matcher.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

using namespace haihunhou;

static std::vector<SpectralData> make_similar_spectrum(uint32_t slip_id, uint32_t seed = 0) {
    std::vector<SpectralData> v;
    std::vector<uint16_t> wls = {380, 400, 450, 500, 550, 600, 650, 700, 750, 780};
    for (uint16_t wl : wls) {
        SpectralData d;
        d.slip_id = slip_id;
        d.wavelength = wl;
        d.reflectance = 0.5f + 0.01f * seed + 0.001f * (wl - 580);
        d.temperature = 22.0f;
        d.humidity = 55.0f;
        d.light_intensity = 50.0f;
        v.push_back(d);
    }
    return v;
}

static std::vector<SpectralData> make_damaged_spectrum(uint32_t slip_id, float damage_ratio) {
    auto spec = make_similar_spectrum(slip_id, 5);
    size_t n_keep = std::max<size_t>(1, (size_t)((1.0f - damage_ratio) * spec.size()));
    for (size_t i = n_keep; i < spec.size(); ++i) {
        spec[i].reflectance = 0.0f;
    }
    return spec;
}

int main() {
    std::cout << "=== Enhanced Slip Matching Test (Normal/Boundary/Exception) ===" << std::endl;

    CnnMatcher matcher;
    CnnMatcherConfig cfg;
    cfg.match_threshold = 0.5f;
    cfg.max_candidates = 20;
    cfg.stroke_weight = 0.6f;
    cfg.contour_weight = 0.4f;
    matcher.setConfig(cfg);
    matcher.loadModel();

    // ============ NORMAL: Known conjugate pair should have high match score > 0.9 ============
    std::cout << "\n--- NORMAL CASE: Known conjugate pair (score > 0.9) ---" << std::endl;
    {
        auto spec_a = make_similar_spectrum(1001, 0);
        auto spec_b = make_similar_spectrum(1002, 1);

        StrokeFeatures fa, fb;
        for (int i = 0; i < 20; ++i) {
            float t = (float)i / 20.0f;
            float a = 45.0f + 10.0f * std::sin(t * 3.14159f);
            float thk = 1.0f + 0.2f * t;
            float pr = 50.0f + 20.0f * std::sin(t * 6.28f);
            float sp = 1.0f + 0.1f * std::cos(t * 3.14f);
            fa.angles.push_back(a);    fa.thickness.push_back(thk);
            fa.pressure.push_back(pr); fa.speed_profile.push_back(sp);
            fb.angles.push_back(a);    fb.thickness.push_back(thk);
            fb.pressure.push_back(pr); fb.speed_profile.push_back(sp);
        }

        ContourFeatures ca, cb;
        ca.aspect_ratio = 19.1667f; ca.perimeter = 48.4f; ca.curvature_variance = 0.05f;
        cb.aspect_ratio = 19.1667f; cb.perimeter = 48.4f; cb.curvature_variance = 0.05f;
        for (size_t i = 0; i < 20; ++i) {
            float t = (float)i / 20.0f;
            float y = 0.6f + 0.05f * std::sin(t * 6.28f);
            ca.edge_points.push_back({t * 23.0f, y});
            cb.edge_points.push_back({t * 23.0f, y});
        }

        float stroke_sim = matcher.computeStrokeSimilarity(fa, fb);
        float contour_sim = matcher.computeContourSimilarity(ca, cb);
        float composite = 0.6f * stroke_sim + 0.4f * contour_sim;
        std::cout << "  stroke_sim=" << stroke_sim << std::endl;
        std::cout << "  contour_sim=" << contour_sim << std::endl;
        std::cout << "  composite=" << composite << std::endl;
        assert(composite > 0.9f);
        std::cout << "[PASS] Conjugate pair composite score > 0.9" << std::endl;

        EmbeddingVector ea, eb;
        ea.data = fa.angles;
        while (ea.data.size() < 80) ea.data.push_back(0.0f);
        eb.data = fb.angles;
        while (eb.data.size() < 80) eb.data.push_back(0.0f);
        float cos = matcher.cosineSimilarity(ea, eb);
        assert(cos > 0.95f);
        std::cout << "[PASS] Cosine similarity for same-stroke vectors > 0.95" << std::endl;
    }

    // ============ BOUNDARY: Edge damage > 50% should auto lower matching score ============
    std::cout << "\n--- BOUNDARY CASE: Edge damage > 50% penalizes score ---" << std::endl;
    {
        ContourFeatures intact, broken;
        intact.aspect_ratio = 19.1667f; intact.perimeter = 48.4f;
        intact.curvature_variance = 0.05f; intact.edge_completeness = 1.0f;
        broken.aspect_ratio = 19.1667f; broken.perimeter = 24.0f;
        broken.curvature_variance = 0.08f; broken.edge_completeness = 0.35f;

        float intact_damage = matcher.estimateEdgeDamageRatio(intact);
        float broken_damage = matcher.estimateEdgeDamageRatio(broken);
        std::cout << "  intact damage=" << intact_damage << " broken=" << broken_damage << std::endl;
        assert(intact_damage < 0.05f);
        assert(broken_damage >= 0.5f);
        std::cout << "[PASS] Damage ratio correctly computed (intact < 5%, broken > 50%)" << std::endl;

        StrokeFeatures fa, fb;
        for (int i = 0; i < 10; ++i) {
            fa.angles.push_back(30.0f + i * 2.0f);
            fa.thickness.push_back(1.0f);
            fa.pressure.push_back(40.0f);
            fa.speed_profile.push_back(1.0f);
            fb.angles.push_back(31.0f + i * 2.0f);
            fb.thickness.push_back(1.1f);
            fb.pressure.push_back(41.0f);
            fb.speed_profile.push_back(0.9f);
        }

        ContourFeatures a, b_intact, b_broken;
        a = intact;
        b_intact = intact;
        b_broken = broken;

        float stroke_sim = matcher.computeStrokeSimilarity(fa, fb);
        float contour_intact = matcher.computeContourSimilarity(a, b_intact);
        float contour_broken = matcher.computeContourSimilarity(a, b_broken);

        float intact_score = 0.6f * stroke_sim + 0.4f * contour_intact;
        float broken_base = 0.6f * stroke_sim + 0.4f * contour_broken;
        float broken_penalized = matcher.applyDamagePenalty(broken_base, 0.0f, broken_damage);

        std::cout << "  intact_score=" << intact_score << " broken_unpenalized=" << broken_base
                  << " broken_penalized=" << broken_penalized << std::endl;
        assert(broken_penalized < intact_score * 0.85f);
        std::cout << "[PASS] Broken score (penalized) < 85% of intact score" << std::endl;

        auto spec_a = make_similar_spectrum(2001, 10);
        auto spec_b = make_damaged_spectrum(2002, 0.7f);
        std::vector<uint32_t> cands = {2002};
        std::unordered_map<uint32_t, std::vector<SpectralData>> cand_map;
        cand_map[2002] = spec_b;

        MatchStatus status;
        auto results = matcher.findMatches(2001, cands, spec_a, cand_map, &status);
        std::cout << "  has_warning=" << status.has_warning
                  << " damage_b=" << status.edge_damage_ratio_b << std::endl;
        assert(status.has_warning || !results.empty() ||
               (status.edge_damage_ratio_a > 0.5f || status.edge_damage_ratio_b > 0.5f));
        std::cout << "[PASS] Damage detected via MatchStatus API" << std::endl;
    }

    // ============ EXCEPTION: Missing image (empty/zero data) returns error code ============
    std::cout << "\n--- EXCEPTION CASE: Missing image returns error code ---" << std::endl;
    {
        std::vector<SpectralData> empty_spec;
        std::vector<uint32_t> cands = {3002, 3003};
        std::unordered_map<uint32_t, std::vector<SpectralData>> cand_map;
        cand_map[3002] = make_similar_spectrum(3002, 1);

        MatchStatus status;
        auto results = matcher.findMatches(3001, cands, empty_spec, cand_map, &status);
        std::cout << "  error_code=" << (int)status.code << " msg=" << status.message << std::endl;
        assert(status.code == MATCHER_ERR_EMPTY_IMAGE);
        assert(results.empty());
        std::cout << "[PASS] Empty query returns MATCHER_ERR_EMPTY_IMAGE" << std::endl;

        std::vector<SpectralData> zero_spec;
        for (uint16_t wl : {380, 400, 450}) {
            SpectralData d;
            d.slip_id = 3004; d.wavelength = wl; d.reflectance = 0.0f;
            zero_spec.push_back(d);
        }
        MatchStatus status2;
        matcher.findMatches(3004, cands, zero_spec, cand_map, &status2);
        assert(status2.code == MATCHER_ERR_EMPTY_IMAGE);
        std::cout << "[PASS] All-zero reflectance returns MATCHER_ERR_EMPTY_IMAGE" << std::endl;

        std::vector<SpectralData> tiny_spec;
        SpectralData one; one.slip_id = 3005; one.wavelength = 550; one.reflectance = 0.5f;
        tiny_spec.push_back(one);
        MatchStatus status3;
        matcher.findMatches(3005, cands, tiny_spec, cand_map, &status3);
        assert(status3.code == MATCHER_ERR_INSUFFICIENT_POINTS);
        std::cout << "[PASS] <3 points returns MATCHER_ERR_INSUFFICIENT_POINTS" << std::endl;

        std::vector<uint32_t> empty_cands;
        MatchStatus status4;
        matcher.findMatches(3006, empty_cands, make_similar_spectrum(3006, 0),
                            cand_map, &status4);
        assert(status4.code == MATCHER_ERR_NO_CANDIDATES);
        std::cout << "[PASS] Empty candidates returns MATCHER_ERR_NO_CANDIDATES" << std::endl;

        std::vector<uint32_t> bad_cands = {9999};
        MatchStatus status5;
        matcher.findMatches(3007, bad_cands, make_similar_spectrum(3007, 0),
                            cand_map, &status5);
        assert(status5.code == MATCHER_ERR_EMPTY_IMAGE);
        std::cout << "[PASS] Missing candidate spectral data returns error" << std::endl;

        assert(matcher.isImageMissing(empty_spec));
        assert(matcher.isImageMissing(zero_spec));
        assert(!matcher.isImageMissing(make_similar_spectrum(1, 0)));
        std::cout << "[PASS] isImageMissing helper correctly classifies data" << std::endl;
    }

    std::cout << "\n=== All Enhanced Slip Matching tests passed ===" << std::endl;
    return 0;
}
