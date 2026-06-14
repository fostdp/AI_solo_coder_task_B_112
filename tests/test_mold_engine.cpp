#include "test_stub.h"
#include "mold_engine.h"
#include "clickhouse_client.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace haihunhou;

int main() {
    std::cout << "=== MoldEngine Test ===" << std::endl;

    ClickHouseClient db("127.0.0.1", 8123, "test");

    MoldEngine engine(db, MessageBus::instance());
    engine.loadMaterialCoefficients();

    MoldParams params;
    params.beta0 = -2.5f;
    params.beta1 = 0.12f;
    params.beta2 = 0.08f;
    params.beta3 = -0.0015f;
    params.beta4 = -0.0008f;
    params.beta5 = 0.0005f;
    params.mu_opt = 0.05f;
    params.T_opt = 25.0f;
    params.RH_opt = 65.0f;
    params.sigma_T = 8.0f;
    params.sigma_RH = 15.0f;
    engine.setParams(params);

    float rs_qing = engine.responseSurface(25.0f, 65.0f, BambooMaterial::QING_ZHU);
    float rs_huang = engine.responseSurface(25.0f, 65.0f, BambooMaterial::HUANG_ZHU);
    assert(rs_qing > 0);
    assert(rs_huang > 0);
    std::cout << "[PASS] responseSurface: QING=" << rs_qing << " HUANG=" << rs_huang << std::endl;

    float rs_cedar = engine.responseSurface(25.0f, 65.0f, BambooMaterial::CEDAR);
    float rs_unknown = engine.responseSurface(25.0f, 65.0f, BambooMaterial::UNKNOWN);
    assert(rs_cedar < rs_unknown);
    std::cout << "[PASS] Cedar more resistant: CEDAR=" << rs_cedar << " < UNKNOWN=" << rs_unknown << std::endl;

    float gr_optimal = engine.gaussianGrowthRate(25.0f, 65.0f);
    float gr_cold = engine.gaussianGrowthRate(5.0f, 65.0f);
    float gr_dry = engine.gaussianGrowthRate(25.0f, 30.0f);
    assert(gr_optimal > gr_cold);
    assert(gr_optimal > gr_dry);
    std::cout << "[PASS] Gaussian growth: optimal=" << gr_optimal
              << " cold=" << gr_cold << " dry=" << gr_dry << std::endl;

    float combined = engine.calculateGrowthRate(25.0f, 65.0f, BambooMaterial::QING_ZHU);
    assert(combined > 0);
    std::cout << "[PASS] calculateGrowthRate: " << combined << std::endl;

    std::vector<MicrobialData> history;
    for (int i = 0; i < 10; i++) {
        MicrobialData md;
        md.timestamp = 1000000ULL + i * 86400ULL;
        md.device_id = 101;
        md.slip_id = 1;
        md.fungi_concentration = 20.0f + i * 2.0f;
        md.bacteria_concentration = 15.0f + i * 1.5f;
        md.temperature = 22.0f + i * 0.5f;
        md.humidity = 55.0f + i * 1.0f;
        history.push_back(md);
    }

    auto pred = engine.predictSlip(1, history, 25.0f, 65.0f);
    assert(pred.slip_id == 1);
    assert(pred.current_concentration > 0);
    std::cout << "[PASS] predictSlip: current=" << pred.current_concentration
              << " 1d=" << pred.predicted_1d
              << " 7d=" << pred.predicted_7d << std::endl;

    assert(pred.predicted_7d >= pred.predicted_1d);
    std::cout << "[PASS] Predictions increase over time" << std::endl;

    auto pred_empty = engine.predictSlip(2, {}, 25.0f, 65.0f);
    assert(pred_empty.current_concentration > 0);
    std::cout << "[PASS] predictSlip with empty history" << std::endl;

    std::cout << "\n=== All MoldEngine tests passed ===" << std::endl;
    return 0;
}
