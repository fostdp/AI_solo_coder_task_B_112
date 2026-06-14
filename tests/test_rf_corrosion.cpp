#include "test_stub.h"
#include "rf_corrosion.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace haihunhou;

int main() {
    std::cout << "=== RF Corrosion Test ===" << std::endl;

    RfCorrosion rf;

    RfCorrosionConfig cfg;
    cfg.n_estimators = 50;
    cfg.min_corrosion_factor = 1.0f;
    cfg.max_corrosion_factor = 10.0f;
    rf.setConfig(cfg);
    auto cfg_out = rf.getConfig();
    assert(cfg_out.n_estimators == 50);
    std::cout << "[PASS] setConfig/getConfig" << std::endl;

    bool loaded = rf.loadModel();
    assert(loaded);
    assert(rf.isLoaded());
    std::cout << "[PASS] loadModel" << std::endl;

    CorrosionFeatures features;
    features.temperature = 25.0f;
    features.humidity = 65.0f;
    features.fungi_concentration = 100.0f;
    features.ochratoxin_a = 2.5f;
    features.citrinin = 4.0f;
    features.patulin = 1.2f;
    features.total_voc = 80.0f;
    features.ph_value = 5.2f;

    float factor = rf.predictCorrosionFactor(features);
    assert(factor >= cfg.min_corrosion_factor);
    assert(factor <= cfg.max_corrosion_factor);
    std::cout << "[PASS] predictCorrosionFactor = " << factor << std::endl;

    float std_dev;
    auto preds = rf.predictWithConfidence(features, std_dev);
    assert(preds.size() == cfg.n_estimators);
    assert(std_dev >= 0.0f);
    std::cout << "[PASS] predictWithConfidence, std_dev = " << std_dev << std::endl;

    float base_rate = 0.1f;
    float accel_rate = rf.predictFadingAcceleration(base_rate, features);
    assert(accel_rate >= base_rate);
    assert(std::abs(accel_rate - base_rate * factor) < 1e-4);
    std::cout << "[PASS] predictFadingAcceleration = " << accel_rate << std::endl;

    CorrosionFeatures low_features = features;
    low_features.fungi_concentration = 10.0f;
    low_features.ochratoxin_a = 0.1f;
    low_features.citrinin = 0.2f;
    float low_factor = rf.predictCorrosionFactor(low_features);
    std::cout << "[INFO] Low contamination factor: " << low_factor << std::endl;

    CorrosionFeatures high_features = features;
    high_features.fungi_concentration = 500.0f;
    high_features.ochratoxin_a = 15.0f;
    high_features.citrinin = 25.0f;
    float high_factor = rf.predictCorrosionFactor(high_features);
    std::cout << "[INFO] High contamination factor: " << high_factor << std::endl;

    assert(high_factor > low_factor * 0.5f);
    std::cout << "[PASS] concentration correlation" << std::endl;

    MicrobialData md;
    md.slip_id = 1;
    md.fungi_concentration = 150.0f;
    md.bacteria_concentration = 400.0f;
    md.temperature = 28.0f;
    md.humidity = 75.0f;

    auto pred = rf.analyzeSlip(1, md, 28.0f, 75.0f);
    assert(pred.slip_id == 1);
    assert(pred.corrosion_factor >= cfg.min_corrosion_factor);
    assert(pred.corrosion_factor <= cfg.max_corrosion_factor);
    assert(pred.ochratoxin_concentration > 0.0f);
    assert(pred.citrinin_concentration > 0.0f);
    assert(pred.voctotal > 0.0f);
    assert(pred.risk_level >= 1 && pred.risk_level <= 4);
    std::cout << "[PASS] analyzeSlip" << std::endl;
    std::cout << "  corrosion_factor: " << pred.corrosion_factor << std::endl;
    std::cout << "  risk_level: " << (int)pred.risk_level << std::endl;

    auto importance = rf.getFeatureImportance();
    assert(importance.size() == 8);
    float total_imp = 0.0f;
    for (auto& p : importance) {
        assert(p.second >= 0.0f);
        total_imp += p.second;
    }
    assert(std::abs(total_imp - 1.0f) < 0.1f);
    std::cout << "[PASS] getFeatureImportance (sum ~= 1.0)" << std::endl;
    for (auto& p : importance) {
        std::cout << "  " << p.first << ": " << p.second << std::endl;
    }

    std::cout << "\n=== All RF Corrosion tests passed ===" << std::endl;
    return 0;
}
