#include "fading_predictor.h"

#if defined(TEST_STUB)
#include "../tests/test_stub.h"
#elif __has_include(<boost/lockfree/queue.hpp>)
#include "message_bus.h"
#else
namespace haihunhou {
struct IngestMessage {
    enum Type { SPECTRAL = 1, MICROBIAL = 2 } type;
    std::vector<SpectralData> spectral;
    std::vector<MicrobialData> microbial;
};
struct AnalysisMessage {
    enum Type { FADING = 1, MOLD = 2 } type;
    std::vector<FadingAnalysis> fading;
    std::vector<MoldPrediction> mold;
    std::vector<SpectralData> raw_spectral;
    std::vector<MicrobialData> raw_microbial;
};
class MessageBus {
public:
    static MessageBus& instance() { static MessageBus b; return b; }
    bool publishIngest(IngestMessage&&) { return true; }
    bool publishAnalysis(AnalysisMessage&&) { return true; }
    bool consumeIngest(IngestMessage&, uint32_t) { return false; }
    bool consumeAnalysis(AnalysisMessage&, uint32_t) { return false; }
    size_t ingestQueueSize() const { return 0; }
    size_t analysisQueueSize() const { return 0; }
};
}
#endif

#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <limits>
#include <iostream>

namespace haihunhou {

static SimpleMatrix matTranspose(const SimpleMatrix& a) {
    SimpleMatrix r(a.cols_, a.rows_);
    for (size_t i = 0; i < a.rows_; ++i)
        for (size_t j = 0; j < a.cols_; ++j)
            r(j, i) = a(i, j);
    return r;
}

static SimpleMatrix matMul(const SimpleMatrix& a, const SimpleMatrix& b) {
    SimpleMatrix r(a.rows_, b.cols_);
    for (size_t i = 0; i < a.rows_; ++i)
        for (size_t j = 0; j < b.cols_; ++j) {
            float s = 0.0f;
            for (size_t k = 0; k < a.cols_; ++k)
                s += a(i, k) * b(k, j);
            r(i, j) = s;
        }
    return r;
}

static SimpleMatrix matInverse(const SimpleMatrix& m) {
    size_t n = m.rows_;
    SimpleMatrix aug(n, 2 * n);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j)
            aug(i, j) = m(i, j);
        aug(i, n + i) = 1.0f;
    }
    for (size_t col = 0; col < n; ++col) {
        size_t max_row = col;
        for (size_t row = col + 1; row < n; ++row)
            if (std::abs(aug(row, col)) > std::abs(aug(max_row, col)))
                max_row = row;
        for (size_t j = 0; j < 2 * n; ++j)
            std::swap(aug(col, j), aug(max_row, j));
        float pivot = aug(col, col);
        if (std::abs(pivot) < 1e-12f) pivot = 1e-12f;
        for (size_t j = 0; j < 2 * n; ++j)
            aug(col, j) /= pivot;
        for (size_t row = 0; row < n; ++row) {
            if (row == col) continue;
            float f = aug(row, col);
            for (size_t j = 0; j < 2 * n; ++j)
                aug(row, j) -= f * aug(col, j);
        }
    }
    SimpleMatrix r(n, n);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j)
            r(i, j) = aug(i, n + j);
    return r;
}

FadingPredictor::FadingPredictor(ClickHouseClient& db, MessageBus& bus)
    : db_(db), bus_(bus), running_(false) {}

FadingPredictor::~FadingPredictor() {
    stop();
}

void FadingPredictor::setParams(const FadingParams& params) {
    std::lock_guard<std::mutex> lock(params_mutex_);
    params_ = params;
}

FadingParams FadingPredictor::getParams() const {
    std::lock_guard<std::mutex> lock(params_mutex_);
    return params_;
}

bool FadingPredictor::start() {
    if (running_.exchange(true)) return false;
    worker_thread_ = std::thread(&FadingPredictor::workerLoop, this);
    std::cout << "FadingPredictor started" << std::endl;
    return true;
}

void FadingPredictor::stop() {
    if (!running_.exchange(false)) return;
    if (worker_thread_.joinable()) worker_thread_.join();
    std::cout << "FadingPredictor stopped" << std::endl;
}

bool FadingPredictor::isRunning() const {
    return running_;
}

float FadingPredictor::calculateK1(float temperature, float wavelength) const {
    FadingParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }
    float T = temperature + 273.15f;
    float arrhenius = p.A1 * std::exp(-p.Ea1 * 1000.0f / (p.R_gas * T));
    float wl_factor = std::pow(wavelength / p.lambda0, p.alpha);
    return arrhenius * wl_factor;
}

float FadingPredictor::calculateK2(float temperature) const {
    FadingParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }
    float T = temperature + 273.15f;
    return p.A2 * std::exp(-p.Ea2 * 1000.0f / (p.R_gas * T));
}

float FadingPredictor::photoOxidationRate(float k1, float I, float R) const {
    return -k1 * I * R;
}

float FadingPredictor::hydrolysisRate(float k2, float RH, float R) const {
    FadingParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }
    float rh_frac = RH / 100.0f;
    return -k2 * (1.0f - R) * std::pow(rh_frac, p.beta);
}

uint8_t FadingPredictor::assessRiskLevel(float fading_rate) const {
    if (fading_rate > 20.0f) return 4;
    if (fading_rate > 10.0f) return 3;
    if (fading_rate > 5.0f) return 2;
    return 1;
}

float FadingPredictor::calculateReflectance(float initial_r, float temperature,
                                            float humidity, float light_intensity,
                                            float wavelength, float time_hours) const {
    FadingParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }
    float T = temperature + 273.15f;
    float k1 = p.A1 * std::exp(-p.Ea1 * 1000.0f / (p.R_gas * T))
               * std::pow(wavelength / p.lambda0, p.alpha);
    float k2 = p.A2 * std::exp(-p.Ea2 * 1000.0f / (p.R_gas * T));

    float dt = 0.1f;
    float R = initial_r;
    float t = 0.0f;
    while (t < time_hours && R > 0.01f) {
        float dR_dt = -k1 * light_intensity * R
                      - k2 * (1.0f - R) * std::pow(humidity / 100.0f, p.beta);
        R += dR_dt * dt;
        R = std::max(0.01f, std::min(0.99f, R));
        t += dt;
    }
    return R;
}

float FadingPredictor::predictReflectance(float current_r, float temp, float humidity,
                                          float light, float wl, float days) const {
    return calculateReflectance(current_r, temp, humidity, light, wl, days * 24.0f);
}

FadingAnalysis FadingPredictor::analyzeSlip(uint32_t slip_id,
                                            const std::vector<SpectralData>& history,
                                            float temperature, float humidity,
                                            float light_intensity) const {
    FadingAnalysis result;
    result.timestamp = now_s();
    result.slip_id = slip_id;

    if (history.empty()) {
        result.reflectance_450nm = 0.7f;
        result.fading_rate_monthly = 0.0f;
        result.predicted_30d = 0.7f;
        result.predicted_90d = 0.7f;
        result.predicted_180d = 0.7f;
        result.risk_level = 1;
        return result;
    }

    float current_r = 0.0f;
    float initial_r = 0.0f;
    uint64_t earliest_ts = UINT64_MAX;
    uint64_t latest_ts = 0;

    for (const auto& d : history) {
        if (d.wavelength == 450) {
            if (d.timestamp > latest_ts) {
                latest_ts = d.timestamp;
                current_r = d.reflectance;
            }
            if (d.timestamp < earliest_ts) {
                earliest_ts = d.timestamp;
                initial_r = d.reflectance;
            }
        }
    }

    if (current_r <= 0) current_r = 0.7f;
    if (initial_r <= 0) initial_r = current_r;

    result.reflectance_450nm = current_r;

    float time_days = static_cast<float>(latest_ts - earliest_ts) / 86400.0f;
    if (time_days < 1.0f) time_days = 1.0f;

    float decline = initial_r - current_r;
    result.fading_rate_monthly = (decline / initial_r) * (30.0f / time_days) * 100.0f;
    if (result.fading_rate_monthly < 0.0f) result.fading_rate_monthly = 0.0f;

    result.predicted_30d = predictReflectance(current_r, temperature, humidity,
                                              light_intensity, 450, 30);
    result.predicted_90d = predictReflectance(current_r, temperature, humidity,
                                              light_intensity, 450, 90);
    result.predicted_180d = predictReflectance(current_r, temperature, humidity,
                                               light_intensity, 450, 180);

    result.risk_level = assessRiskLevel(result.fading_rate_monthly);

    return result;
}

void FadingPredictor::workerLoop() {
    while (running_) {
        IngestMessage msg;
        if (bus_.consumeIngest(msg, 1000)) {
            if (msg.type == IngestMessage::SPECTRAL && !msg.spectral.empty()) {
                processSpectralData(msg.spectral);
            }
        }
    }
}

void FadingPredictor::processSpectralData(const std::vector<SpectralData>& data) {
    std::set<uint32_t> slip_ids;
    for (const auto& d : data) {
        slip_ids.insert(d.slip_id);
    }

    FadingParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }

    std::vector<FadingAnalysis> results;

    for (uint32_t slip_id : slip_ids) {
        std::vector<SpectralData> history;
        uint64_t end_time = now_s();
        uint64_t start_time = end_time - 180ULL * 86400ULL;
        if (!db_.getSpectralData(slip_id, start_time, end_time, history)) continue;
        if (history.size() < p.min_data_points) continue;

        float avg_temp = 0.0f, avg_hum = 0.0f, avg_light = 0.0f;
        for (const auto& d : history) {
            avg_temp += d.temperature;
            avg_hum += d.humidity;
            avg_light += d.light_intensity;
        }
        auto n = static_cast<float>(history.size());
        avg_temp /= n;
        avg_hum /= n;
        avg_light /= n;

        fitModel(slip_id, history);
        FadingAnalysis analysis = analyzeSlip(slip_id, history, avg_temp, avg_hum, avg_light);
        results.push_back(analysis);
    }

    if (!results.empty()) {
        db_.insertFadingAnalysis(results);

        AnalysisMessage amsg;
        amsg.type = AnalysisMessage::FADING;
        amsg.fading = std::move(results);
        amsg.raw_spectral = data;
        bus_.publishAnalysis(std::move(amsg));
    }
}

SimpleMatrix FadingPredictor::computeJacobian(const std::vector<float>& times,
                                                 const std::vector<float>& conditions,
                                                 const FadingParams& p) const {
    int n = static_cast<int>(times.size());
    SimpleMatrix J(n, 6);

    for (int i = 0; i < n; ++i) {
        float t = times[i];
        float temp = conditions[static_cast<size_t>(i) * 5 + 0];
        float RH  = conditions[static_cast<size_t>(i) * 5 + 1];
        float I   = conditions[static_cast<size_t>(i) * 5 + 2];
        float wl  = conditions[static_cast<size_t>(i) * 5 + 3];
        float R0  = conditions[static_cast<size_t>(i) * 5 + 4];

        float T = temp + 273.15f;
        float RH_frac = RH / 100.0f;
        float wl_ratio = wl / p.lambda0;

        float k1 = p.A1 * std::exp(-p.Ea1 * 1000.0f / (p.R_gas * T))
                   * std::pow(wl_ratio, p.alpha);
        float k2 = p.A2 * std::exp(-p.Ea2 * 1000.0f / (p.R_gas * T));
        float RHb = std::pow(RH_frac, p.beta);
        float K = k1 * I + k2 * RHb;
        float R_pred = R0 * std::exp(-K * t);

        float exp1 = std::exp(-p.Ea1 * 1000.0f / (p.R_gas * T));
        float wl_alpha = std::pow(wl_ratio, p.alpha);
        float exp2 = std::exp(-p.Ea2 * 1000.0f / (p.R_gas * T));

        float dk1_dA1     = exp1 * wl_alpha;
        float dk1_dEa1    = k1 * (-1000.0f / (p.R_gas * T));
        float dk1_dalpha  = k1 * std::log(wl_ratio);

        float dk2_dA2     = exp2;
        float dk2_dEa2    = k2 * (-1000.0f / (p.R_gas * T));

        float dRHb_dbeta  = (RH_frac > 0.0f) ? RHb * std::log(RH_frac) : 0.0f;

        float dK_dA1    = I * dk1_dA1;
        float dK_dEa1   = I * dk1_dEa1;
        float dK_dalpha = I * dk1_dalpha;
        float dK_dA2    = RHb * dk2_dA2;
        float dK_dEa2   = RHb * dk2_dEa2;
        float dK_dbeta  = k2 * dRHb_dbeta;

        J(i, 0) = -R_pred * t * dK_dA1;
        J(i, 1) = -R_pred * t * dK_dEa1;
        J(i, 2) = -R_pred * t * dK_dalpha;
        J(i, 3) = -R_pred * t * dK_dA2;
        J(i, 4) = -R_pred * t * dK_dEa2;
        J(i, 5) = -R_pred * t * dK_dbeta;
    }

    return J;
}

FittingResult FadingPredictor::fitModel(uint32_t slip_id,
                                        const std::vector<SpectralData>& history) {
    FadingParams p;
    {
        std::lock_guard<std::mutex> lock(params_mutex_);
        p = params_;
    }

    std::map<uint16_t, std::pair<uint64_t, float>> wl_first;
    for (const auto& d : history) {
        auto it = wl_first.find(d.wavelength);
        if (it == wl_first.end() || d.timestamp < it->second.first) {
            wl_first[d.wavelength] = {d.timestamp, d.reflectance};
        }
    }

    uint64_t global_earliest = UINT64_MAX;
    for (const auto& kv : wl_first) {
        if (kv.second.first < global_earliest)
            global_earliest = kv.second.first;
    }

    std::vector<float> times;
    std::vector<float> obs;
    std::vector<float> conditions;

    for (const auto& d : history) {
        float t_hours = static_cast<float>(d.timestamp - global_earliest) / 3600.0f;
        float R0 = wl_first[d.wavelength].second;
        times.push_back(t_hours);
        obs.push_back(d.reflectance);
        conditions.push_back(d.temperature);
        conditions.push_back(d.humidity);
        conditions.push_back(d.light_intensity);
        conditions.push_back(static_cast<float>(d.wavelength));
        conditions.push_back(R0);
    }

    int n = static_cast<int>(times.size());
    if (n < static_cast<int>(p.min_data_points)) {
        FittingResult result{};
        result.converged = false;
        result.A1 = p.A1; result.Ea1 = p.Ea1; result.alpha = p.alpha;
        result.A2 = p.A2; result.Ea2 = p.Ea2; result.beta = p.beta;
        result.residual_norm = 0.0f;
        result.iterations = 0;
        return result;
    }

    float damping = p.lm_damping_init;
    float prev_residual = std::numeric_limits<float>::max();

    FittingResult result{};
    result.converged = false;
    result.iterations = 0;

    for (uint32_t iter = 0; iter < p.max_lm_iterations; ++iter) {
        result.iterations = iter + 1;

        SimpleMatrix J = computeJacobian(times, conditions, p);

        SimpleMatrix residual(n, 1);
        float res_norm_sq = 0.0f;
        for (int i = 0; i < n; ++i) {
            float t   = times[i];
            float temp = conditions[static_cast<size_t>(i) * 5 + 0];
            float RH  = conditions[static_cast<size_t>(i) * 5 + 1];
            float I   = conditions[static_cast<size_t>(i) * 5 + 2];
            float wl  = conditions[static_cast<size_t>(i) * 5 + 3];
            float R0  = conditions[static_cast<size_t>(i) * 5 + 4];
            float T = temp + 273.15f;
            float k1 = p.A1 * std::exp(-p.Ea1 * 1000.0f / (p.R_gas * T))
                       * std::pow(wl / p.lambda0, p.alpha);
            float k2 = p.A2 * std::exp(-p.Ea2 * 1000.0f / (p.R_gas * T));
            float K = k1 * I + k2 * std::pow(RH / 100.0f, p.beta);
            float R_pred = R0 * std::exp(-K * t);
            float r = obs[i] - R_pred;
            residual(i, 0) = r;
            res_norm_sq += r * r;
        }
        float res_norm = std::sqrt(res_norm_sq);

        if (std::abs(prev_residual - res_norm) < p.lm_tolerance) {
            result.converged = true;
            result.residual_norm = res_norm;
            break;
        }
        prev_residual = res_norm;

        SimpleMatrix Jt = matTranspose(J);
        SimpleMatrix JtJ = matMul(Jt, J);
        SimpleMatrix Jtr = matMul(Jt, residual);

        SimpleMatrix H = JtJ;
        for (int i = 0; i < 6; ++i) {
            H(i, i) += damping * (std::abs(JtJ(i, i)) + 1e-12f);
        }

        SimpleMatrix Hinv = matInverse(H);
        SimpleMatrix delta = matMul(Hinv, Jtr);

        p.A1    += delta(0, 0);
        p.Ea1   += delta(1, 0);
        p.alpha += delta(2, 0);
        p.A2    += delta(3, 0);
        p.Ea2   += delta(4, 0);
        p.beta  += delta(5, 0);

        p.A1    = std::max(1e-12f, p.A1);
        p.Ea1   = std::max(0.1f, p.Ea1);
        p.alpha = std::max(0.01f, p.alpha);
        p.A2    = std::max(1e-12f, p.A2);
        p.Ea2   = std::max(0.1f, p.Ea2);
        p.beta  = std::max(0.01f, p.beta);

        damping *= 0.9f;
        if (damping < 1e-10f) damping = 1e-10f;
    }

    result.A1 = p.A1;
    result.Ea1 = p.Ea1;
    result.alpha = p.alpha;
    result.A2 = p.A2;
    result.Ea2 = p.Ea2;
    result.beta = p.beta;
    if (!result.converged) result.residual_norm = prev_residual;

    return result;
}

}
