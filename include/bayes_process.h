#pragma once
#include "common.h"
#include "bayes_opt.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>

namespace haihunhou {

enum class ProcessExecMode {
    IN_PROCESS = 0,
    SUBPROCESS = 1
};

struct BayesOptProcessConfig {
    ProcessExecMode mode = ProcessExecMode::IN_PROCESS;
    std::string subprocess_path;
    uint32_t timeout_ms = 30000;
    bool enable_cross_process = false;
};

class BayesOptProcessExecutor {
public:
    BayesOptProcessExecutor();
    explicit BayesOptProcessExecutor(const BayesOptProcessConfig& cfg);
    ~BayesOptProcessExecutor();

    void setConfig(const BayesOptProcessConfig& cfg);
    BayesOptProcessConfig getConfig() const;

    EnvOptimizationResult runOptimization(
        uint32_t zone_id,
        const EnvParameters& current_env,
        float current_fading_rate,
        const BayesOptConfig& opt_cfg,
        const GaussianParams& gp_params,
        OptimizationDiagnostics* diag = nullptr);

    std::future<EnvOptimizationResult> runOptimizationAsync(
        uint32_t zone_id,
        const EnvParameters& current_env,
        float current_fading_rate,
        const BayesOptConfig& opt_cfg,
        const GaussianParams& gp_params,
        OptimizationDiagnostics* diag = nullptr);

    static std::string serializeEnvParams(const EnvParameters& p);
    static EnvParameters deserializeEnvParams(const std::string& s);

    static std::string serializeOptConfig(const BayesOptConfig& c);
    static BayesOptConfig deserializeOptConfig(const std::string& s);

    static std::string serializeGaussianParams(const GaussianParams& g);
    static GaussianParams deserializeGaussianParams(const std::string& s);

    static std::string serializeResult(const EnvOptimizationResult& r);
    static EnvOptimizationResult deserializeResult(const std::string& s);

private:
    BayesOptProcessConfig config_;
    mutable std::mutex mutex_;

    EnvOptimizationResult runInProcess(
        uint32_t zone_id,
        const EnvParameters& current_env,
        float current_fading_rate,
        const BayesOptConfig& opt_cfg,
        const GaussianParams& gp_params,
        OptimizationDiagnostics* diag);

    EnvOptimizationResult runInSubprocess(
        uint32_t zone_id,
        const EnvParameters& current_env,
        float current_fading_rate,
        const BayesOptConfig& opt_cfg,
        const GaussianParams& gp_params,
        OptimizationDiagnostics* diag);
};

}
