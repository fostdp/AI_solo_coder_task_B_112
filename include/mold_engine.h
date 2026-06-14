#pragma once
#include "common.h"
#include "clickhouse_client.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace haihunhou {

class MessageBus;

struct MaterialCoefficients {
    float anti_mold_factor;
    float fading_sensitivity;
    float beta0_adj;
    float beta1_adj;
    float beta2_adj;
};

class MoldEngine {
public:
    MoldEngine(ClickHouseClient& db, MessageBus& bus);
    ~MoldEngine();

    void setParams(const MoldParams& params);
    MoldParams getParams() const;

    bool start();
    void stop();
    bool isRunning() const;

    void loadMaterialCoefficients();

    MoldPrediction predictSlip(uint32_t slip_id,
                               const std::vector<MicrobialData>& history,
                               float temperature, float humidity) const;

    float responseSurface(float temperature, float humidity,
                          BambooMaterial material = BambooMaterial::UNKNOWN) const;
    float gaussianGrowthRate(float temperature, float humidity) const;
    float calculateGrowthRate(float temperature, float humidity,
                              BambooMaterial material = BambooMaterial::UNKNOWN) const;

private:
    ClickHouseClient& db_;
    MessageBus& bus_;
    MoldParams params_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    mutable std::mutex params_mutex_;

    std::unordered_map<BambooMaterial, MaterialCoefficients> material_coeffs_;

    void workerLoop();
    void processMicrobialData(const std::vector<MicrobialData>& data);

    float calculateConcentration(float initial, float temp, float humidity,
                                  float time_hours, BambooMaterial material) const;
    float predictConcentration(float current, float temp, float humidity,
                                float days, BambooMaterial material) const;
    uint8_t assessRiskLevel(float concentration) const;
    BambooMaterial getSlipMaterial(uint32_t slip_id) const;

    static MaterialCoefficients defaultMaterialCoefficients(BambooMaterial m);
};

}
