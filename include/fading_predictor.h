#pragma once
#include "common.h"
#include "clickhouse_client.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <array>

namespace haihunhou {

class MessageBus;

struct SimpleMatrix {
    size_t rows_, cols_;
    std::vector<float> data_;
    SimpleMatrix() : rows_(0), cols_(0) {}
    SimpleMatrix(size_t r, size_t c) : rows_(r), cols_(c), data_(r* c, 0.0f) {}
    float& operator()(size_t i, size_t j) { return data_[i * cols_ + j]; }
    const float& operator()(size_t i, size_t j) const { return data_[i * cols_ + j]; }
    size_t rows() const { return rows_; }
    size_t cols() const { return cols_; }
};

struct FittingResult {
    bool converged;
    float A1, Ea1, alpha, A2, Ea2, beta;
    float residual_norm;
    uint32_t iterations;
};

class FadingPredictor {
public:
    FadingPredictor(ClickHouseClient& db, MessageBus& bus);
    ~FadingPredictor();

    void setParams(const FadingParams& params);
    FadingParams getParams() const;

    bool start();
    void stop();
    bool isRunning() const;

    FadingAnalysis analyzeSlip(uint32_t slip_id,
                               const std::vector<SpectralData>& history,
                               float temperature, float humidity,
                               float light_intensity) const;

    FittingResult fitModel(uint32_t slip_id,
                           const std::vector<SpectralData>& history);

    float calculateK1(float temperature, float wavelength) const;
    float calculateK2(float temperature) const;
    float calculateReflectance(float initial_r, float temperature,
                               float humidity, float light_intensity,
                               float wavelength, float time_hours) const;

private:
    ClickHouseClient& db_;
    MessageBus& bus_;
    FadingParams params_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    mutable std::mutex params_mutex_;

    void workerLoop();
    void processSpectralData(const std::vector<SpectralData>& data);

    SimpleMatrix computeJacobian(const std::vector<float>& times,
                                  const std::vector<float>& conditions,
                                  const FadingParams& p) const;

    float photoOxidationRate(float k1, float I, float R) const;
    float hydrolysisRate(float k2, float RH, float R) const;
    uint8_t assessRiskLevel(float fading_rate) const;
    float predictReflectance(float current_r, float temp, float humidity,
                              float light, float wl, float days) const;
};

}
