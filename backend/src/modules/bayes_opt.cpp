#include "bayes_opt.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <limits>

namespace haihunhou {

static float normalCDF(float x) {
    return 0.5f * std::erfc(-x / std::sqrt(2.0f));
}

static float normalPDF(float x) {
    return std::exp(-0.5f * x * x) / std::sqrt(2.0f * 3.14159265358979323846f);
}

BayesOpt::BayesOpt() : rng_(std::random_device{}()) {}

BayesOpt::~BayesOpt() = default;

void BayesOpt::setConfig(const BayesOptConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

BayesOptConfig BayesOpt::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void BayesOpt::setGaussianParams(const GaussianParams& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    gp_params_ = params;
}

GaussianParams BayesOpt::getGaussianParams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gp_params_;
}

void BayesOpt::setObjectiveFunction(ObjectiveFunction fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    objective_fn_ = fn;
}

EnvOptimizationResult BayesOpt::optimize(
    uint32_t zone_id,
    const EnvParameters& current_env,
    float current_fading_rate) {

    std::vector<ObservationPoint> observations;

    ObservationPoint current_pt;
    current_pt.params = current_env;
    current_pt.objective_value = objective_fn_ ? -objective_fn_(current_env) : -defaultObjective(current_env);
    observations.push_back(current_pt);

    std::uniform_real_distribution<float> temp_dist(config_.temperature_min, config_.temperature_max);
    std::uniform_real_distribution<float> hum_dist(config_.humidity_min, config_.humidity_max);
    std::uniform_real_distribution<float> filter_dist(config_.light_filter_min, config_.light_filter_max);

    for (uint32_t i = 0; i < 5; ++i) {
        EnvParameters p;
        p.temperature = temp_dist(rng_);
        p.humidity = hum_dist(rng_);
        p.light_filter = filter_dist(rng_);
        ObservationPoint pt;
        pt.params = p;
        pt.objective_value = objective_fn_ ? -objective_fn_(p) : -defaultObjective(p);
        observations.push_back(pt);
    }

    for (uint32_t iter = 0; iter < config_.max_iterations; ++iter) {
        auto next = suggestNextPoint(observations);
        ObservationPoint pt;
        pt.params = next.params;
        pt.objective_value = objective_fn_ ? -objective_fn_(pt.params) : -defaultObjective(pt.params);
        observations.push_back(pt);
    }

    float best_val = std::numeric_limits<float>::lowest();
    EnvParameters best_params = current_env;
    for (auto& obs : observations) {
        if (obs.objective_value > best_val) {
            best_val = obs.objective_value;
            best_params = obs.params;
        }
    }

    EnvOptimizationResult result;
    result.timestamp = now_s();
    result.zone_id = zone_id;
    result.optimal_temperature = best_params.temperature;
    result.optimal_humidity = best_params.humidity;
    result.optimal_light_filter = best_params.light_filter;

    float optimized_fading = objective_fn_ ? objective_fn_(best_params) : defaultObjective(best_params);
    result.predicted_lifespan_years = computeLifespan(optimized_fading);
    result.improvement_percent = current_fading_rate > 0.0f
        ? std::max(0.0f, (current_fading_rate - optimized_fading) / current_fading_rate * 100.0f)
        : 0.0f;
    result.current_temperature = current_env.temperature;
    result.current_humidity = current_env.humidity;
    return result;
}

AcquisitionResult BayesOpt::suggestNextPoint(
    const std::vector<ObservationPoint>& observations) {

    auto candidates = generateRandomCandidates(100);

    float y_best = std::numeric_limits<float>::lowest();
    for (auto& obs : observations) {
        if (obs.objective_value > y_best) {
            y_best = obs.objective_value;
        }
    }

    AcquisitionResult best_result;
    best_result.acquisition_value = std::numeric_limits<float>::lowest();

    if (observations.empty()) {
        if (!candidates.empty()) {
            best_result.params = candidates[0];
        }
        return best_result;
    }

    size_t n = observations.size();
    auto K = buildCovarianceMatrix(observations);
    for (size_t i = 0; i < n; ++i) {
        K[i][i] += gp_params_.noise_variance;
    }

    std::vector<float> y(n);
    for (size_t i = 0; i < n; ++i) {
        y[i] = observations[i].objective_value;
    }

    std::vector<float> alpha = solveLinearSystem(K, y);
    auto K_inv = K;
    (void)K_inv;

    for (auto& cand : candidates) {
        std::vector<float> k_star(n);
        for (size_t i = 0; i < n; ++i) {
            k_star[i] = rbfKernel(cand, observations[i].params);
        }

        float mean = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            mean += k_star[i] * alpha[i];
        }

        auto K_kstar = solveLinearSystem(K, k_star);
        float k_star_star = rbfKernel(cand, cand);
        float vTv = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            vTv += k_star[i] * K_kstar[i];
        }
        float variance = k_star_star - vTv;
        float std = std::sqrt(std::max(0.0f, variance));

        float ei = 0.0f;
        if (std >= 1e-9f) {
            float z = (mean - y_best) / std;
            ei = (mean - y_best) * normalCDF(z) + std * normalPDF(z);
        }

        if (ei > best_result.acquisition_value) {
            best_result.params = cand;
            best_result.acquisition_value = ei;
            best_result.predicted_mean = mean;
            best_result.predicted_std = std;
        }
    }
    return best_result;
}

std::pair<float, float> BayesOpt::predict(
    const EnvParameters& x,
    const std::vector<ObservationPoint>& observations) {

    if (observations.empty()) {
        return {0.0f, gp_params_.signal_variance};
    }

    size_t n = observations.size();
    auto K = buildCovarianceMatrix(observations);

    for (size_t i = 0; i < n; ++i) {
        K[i][i] += gp_params_.noise_variance;
    }

    std::vector<float> y(n);
    for (size_t i = 0; i < n; ++i) {
        y[i] = observations[i].objective_value;
    }

    std::vector<float> alpha = solveLinearSystem(K, y);

    std::vector<float> k_star(n);
    for (size_t i = 0; i < n; ++i) {
        k_star[i] = rbfKernel(x, observations[i].params);
    }

    float mean = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        mean += k_star[i] * alpha[i];
    }

    auto K_kstar = solveLinearSystem(K, k_star);

    float k_star_star = rbfKernel(x, x);
    float vTv = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        vTv += k_star[i] * K_kstar[i];
    }
    float variance = k_star_star - vTv;
    float std = std::sqrt(std::max(0.0f, variance));

    return {mean, std};
}

float BayesOpt::expectedImprovement(
    const EnvParameters& x,
    const std::vector<ObservationPoint>& observations,
    float y_best) {

    auto [mean, std] = predict(x, observations);
    if (std < 1e-9f) return 0.0f;

    float z = (mean - y_best) / std;
    return (mean - y_best) * normalCDF(z) + std * normalPDF(z);
}

float BayesOpt::upperConfidenceBound(
    const EnvParameters& x,
    const std::vector<ObservationPoint>& observations,
    float kappa) {

    auto [mean, std] = predict(x, observations);
    return mean + kappa * std;
}

float BayesOpt::probabilityOfImprovement(
    const EnvParameters& x,
    const std::vector<ObservationPoint>& observations,
    float y_best) {

    auto [mean, std] = predict(x, observations);
    if (std < 1e-9f) return 0.0f;

    float z = (mean - y_best) / std;
    return normalCDF(z);
}

float BayesOpt::computeLifespan(float fading_rate_monthly) const {
    if (fading_rate_monthly <= 0.0f) return 1000.0f;
    float max_acceptable_loss = 0.5f;
    float annual_rate = fading_rate_monthly * 12.0f / 100.0f;
    if (annual_rate <= 0.0f) return 1000.0f;
    return -std::log(1.0f - max_acceptable_loss) / annual_rate;
}

float BayesOpt::rbfKernel(const EnvParameters& x1, const EnvParameters& x2) const {
    float t_norm = (config_.temperature_max - config_.temperature_min) * 0.5f;
    float h_norm = (config_.humidity_max - config_.humidity_min) * 0.5f;
    float f_norm = (config_.light_filter_max - config_.light_filter_min) * 0.5f;

    float dt = (x1.temperature - x2.temperature) / t_norm;
    float dh = (x1.humidity - x2.humidity) / h_norm;
    float df = (x1.light_filter - x2.light_filter) / f_norm;
    float dist_sq = dt * dt + dh * dh + df * df;

    return gp_params_.signal_variance * std::exp(-dist_sq / (2.0f * gp_params_.length_scale * gp_params_.length_scale));
}

std::vector<std::vector<float>> BayesOpt::buildCovarianceMatrix(
    const std::vector<ObservationPoint>& observations) const {

    size_t n = observations.size();
    std::vector<std::vector<float>> K(n, std::vector<float>(n, 0.0f));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i; j < n; ++j) {
            float k = rbfKernel(observations[i].params, observations[j].params);
            K[i][j] = k;
            K[j][i] = k;
        }
    }
    return K;
}

std::vector<float> BayesOpt::solveLinearSystem(
    const std::vector<std::vector<float>>& A,
    const std::vector<float>& b) const {

    size_t n = A.size();
    std::vector<std::vector<float>> aug(n, std::vector<float>(n + 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            aug[i][j] = A[i][j];
        }
        aug[i][n] = b[i];
    }

    for (size_t col = 0; col < n; ++col) {
        size_t max_row = col;
        float max_val = std::abs(aug[col][col]);
        for (size_t row = col + 1; row < n; ++row) {
            if (std::abs(aug[row][col]) > max_val) {
                max_val = std::abs(aug[row][col]);
                max_row = row;
            }
        }
        if (max_row != col) {
            std::swap(aug[col], aug[max_row]);
        }

        float pivot = aug[col][col];
        if (std::abs(pivot) < 1e-12f) continue;

        for (size_t j = col; j <= n; ++j) {
            aug[col][j] /= pivot;
        }

        for (size_t row = 0; row < n; ++row) {
            if (row != col && std::abs(aug[row][col]) > 1e-12f) {
                float factor = aug[row][col];
                for (size_t j = col; j <= n; ++j) {
                    aug[row][j] -= factor * aug[col][j];
                }
            }
        }
    }

    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = aug[i][n];
    }
    return x;
}

EnvParameters BayesOpt::clampParams(const EnvParameters& p) const {
    EnvParameters r;
    r.temperature = std::max(config_.temperature_min, std::min(config_.temperature_max, p.temperature));
    r.humidity = std::max(config_.humidity_min, std::min(config_.humidity_max, p.humidity));
    r.light_filter = std::max(config_.light_filter_min, std::min(config_.light_filter_max, p.light_filter));
    return r;
}

std::vector<EnvParameters> BayesOpt::generateRandomCandidates(uint32_t n) const {
    std::vector<EnvParameters> candidates;
    candidates.reserve(n);

    std::uniform_real_distribution<float> temp_dist(config_.temperature_min, config_.temperature_max);
    std::uniform_real_distribution<float> hum_dist(config_.humidity_min, config_.humidity_max);
    std::uniform_real_distribution<float> filter_dist(config_.light_filter_min, config_.light_filter_max);

    std::mt19937 gen(rng_());
    for (uint32_t i = 0; i < n; ++i) {
        EnvParameters p;
        p.temperature = temp_dist(gen);
        p.humidity = hum_dist(gen);
        p.light_filter = filter_dist(gen);
        candidates.push_back(p);
    }
    return candidates;
}

float BayesOpt::defaultObjective(const EnvParameters& params) const {
    float T = params.temperature + 273.15f;
    float RH = params.humidity / 100.0f;
    float LF = 1.0f - params.light_filter;

    static constexpr float R_GAS = 8.314f;
    static constexpr float Ea1 = 45000.0f;
    static constexpr float A1 = 1.2e-8f;
    static constexpr float Ea2 = 55000.0f;
    static constexpr float A2 = 8.5e-7f;
    static constexpr float beta = 1.5f;

    float k1 = A1 * std::exp(-Ea1 / (R_GAS * T));
    float k2 = A2 * std::exp(-Ea2 / (R_GAS * T));

    float rh_factor = std::pow(std::max(0.01f, RH), beta);
    float rate_monthly = (k1 * LF * 50.0f + k2 * rh_factor * 0.15f) * 30.0f * 24.0f * 100.0f;

    return std::max(0.001f, rate_monthly);
}

}
