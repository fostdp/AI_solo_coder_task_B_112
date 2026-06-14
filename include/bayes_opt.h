#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <random>

namespace haihunhou {

struct GaussianParams {
    float length_scale = 1.0f;
    float signal_variance = 1.0f;
    float noise_variance = 0.01f;
};

struct EnvParameters {
    float temperature;
    float humidity;
    float light_filter;
};

struct ObservationPoint {
    EnvParameters params;
    float objective_value;
};

struct AcquisitionResult {
    EnvParameters params;
    float acquisition_value;
    float predicted_mean;
    float predicted_std;
};

enum BayesOptStatus {
    BOPT_OK = 0,
    BOPT_WARN_TIMEOUT = 1,
    BOPT_WARN_TPE_FALLBACK = 2,
    BOPT_ERR_INVALID_PARAMS = 3,
    BOPT_WARN_NO_CONVERGENCE = 4
};

struct OptimizationDiagnostics {
    BayesOptStatus status = BOPT_OK;
    uint32_t iterations_performed = 0;
    uint32_t time_elapsed_ms = 0;
    bool used_tpe_fallback = false;
    float convergence_gap = 0.0f;
    std::string message;
};

class BayesOpt {
public:
    using ObjectiveFunction = std::function<float(const EnvParameters&)>;

    BayesOpt();
    ~BayesOpt();

    void setConfig(const BayesOptConfig& config);
    BayesOptConfig getConfig() const;

    void setGaussianParams(const GaussianParams& params);
    GaussianParams getGaussianParams() const;

    void setObjectiveFunction(ObjectiveFunction fn);

    EnvOptimizationResult optimize(
        uint32_t zone_id,
        const EnvParameters& current_env,
        float current_fading_rate,
        OptimizationDiagnostics* diag = nullptr,
        uint32_t timeout_ms = 0);

    AcquisitionResult suggestNextPoint(
        const std::vector<ObservationPoint>& observations,
        const std::string& acquisition = "ei",
        bool use_tpe = false);

    std::pair<float, float> predict(
        const EnvParameters& x,
        const std::vector<ObservationPoint>& observations);

    float expectedImprovement(
        const EnvParameters& x,
        const std::vector<ObservationPoint>& observations,
        float y_best);

    float upperConfidenceBound(
        const EnvParameters& x,
        const std::vector<ObservationPoint>& observations,
        float kappa = 2.576f);

    float probabilityOfImprovement(
        const EnvParameters& x,
        const std::vector<ObservationPoint>& observations,
        float y_best);

    float computeLifespan(float fading_rate_monthly) const;

    AcquisitionResult suggestNextPointTPE(
        const std::vector<ObservationPoint>& observations,
        uint32_t top_n_percent = 25);

    bool checkConvergence(
        const std::vector<ObservationPoint>& observations,
        float tol = 1e-3f,
        uint32_t window = 5) const;

    float computeBoundaryPenalty(const EnvParameters& p) const;

private:
    BayesOptConfig config_;
    GaussianParams gp_params_;
    ObjectiveFunction objective_fn_;
    mutable std::mutex mutex_;
    mutable std::mt19937 rng_;

    float rbfKernel(const EnvParameters& x1, const EnvParameters& x2) const;

    std::vector<std::vector<float>> buildCovarianceMatrix(
        const std::vector<ObservationPoint>& observations) const;

    std::vector<float> solveLinearSystem(
        const std::vector<std::vector<float>>& A,
        const std::vector<float>& b) const;

    EnvParameters clampParams(const EnvParameters& p) const;

    std::vector<EnvParameters> generateRandomCandidates(uint32_t n) const;

    float defaultObjective(const EnvParameters& params) const;
};

}
