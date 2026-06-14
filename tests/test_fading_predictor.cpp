#include "test_stub.h"
#include "fading_predictor.h"
#include "clickhouse_client.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace haihunhou;

int main() {
    std::cout << "=== FadingPredictor Test ===" << std::endl;

    ClickHouseClient db("127.0.0.1", 8123, "test");

    FadingPredictor predictor(db, MessageBus::instance());

    FadingParams params;
    params.A1 = 1.0e-3f;
    params.Ea1 = 20.0f;
    params.alpha = 0.8f;
    params.A2 = 5.0e-3f;
    params.Ea2 = 25.0f;
    params.beta = 1.5f;
    predictor.setParams(params);

    float k1 = predictor.calculateK1(25.0f, 450.0f);
    assert(k1 > 0);
    std::cout << "[PASS] calculateK1: " << k1 << std::endl;

    float k2 = predictor.calculateK2(25.0f);
    assert(k2 > 0);
    std::cout << "[PASS] calculateK2: " << k2 << std::endl;

    float R = predictor.calculateReflectance(0.8f, 35.0f, 80.0f, 200.0f, 450.0f, 720.0f);
    assert(R > 0 && R < 0.79f);
    std::cout << "[PASS] calculateReflectance: " << R << " (should be < 0.8)" << std::endl;

    float R_long = predictor.calculateReflectance(0.8f, 35.0f, 80.0f, 200.0f, 450.0f, 4320.0f);
    assert(R_long < R - 0.01f);
    std::cout << "[PASS] Long-term fading: " << R_long << " < " << R << std::endl;

    float R_high_temp = predictor.calculateReflectance(0.8f, 45.0f, 80.0f, 200.0f, 450.0f, 720.0f);
    assert(R_high_temp < R - 0.001f);
    std::cout << "[PASS] High temp accelerates fading: " << R_high_temp << " < " << R << std::endl;

    float R_high_hum = predictor.calculateReflectance(0.8f, 35.0f, 95.0f, 200.0f, 450.0f, 4320.0f);
    assert(R_high_hum < R_long);
    std::cout << "[PASS] High humidity accelerates fading: " << R_high_hum << " < " << R_long << std::endl;

    std::vector<SpectralData> history;
    for (int i = 0; i < 10; i++) {
        SpectralData sd;
        sd.timestamp = 1000000ULL + i * 86400ULL;
        sd.device_id = 1;
        sd.slip_id = 1;
        sd.wavelength = 450;
        sd.reflectance = 0.8f - i * 0.005f;
        sd.temperature = 22.0f;
        sd.humidity = 55.0f;
        sd.light_intensity = 50.0f;
        history.push_back(sd);
    }

    auto analysis = predictor.analyzeSlip(1, history, 22.0f, 55.0f, 50.0f);
    assert(analysis.slip_id == 1);
    assert(analysis.reflectance_450nm > 0);
    std::cout << "[PASS] analyzeSlip: R=" << analysis.reflectance_450nm
              << " rate=" << analysis.fading_rate_monthly << "%/mo" << std::endl;

    std::vector<SpectralData> empty;
    auto empty_analysis = predictor.analyzeSlip(2, empty, 22.0f, 55.0f, 50.0f);
    assert(empty_analysis.reflectance_450nm > 0);
    std::cout << "[PASS] analyzeSlip with empty history" << std::endl;

    auto fit_result = predictor.fitModel(1, history);
    std::cout << "[INFO] fitModel: converged=" << (fit_result.converged ? "YES" : "NO")
              << " iterations=" << fit_result.iterations
              << " residual=" << fit_result.residual_norm << std::endl;

    std::cout << "\n=== All FadingPredictor tests passed ===" << std::endl;
    return 0;
}
