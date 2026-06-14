#include "bayes_process.h"
#include "thread_pool.h"
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#endif

namespace haihunhou {

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, delim)) {
        out.push_back(token);
    }
    return out;
}

BayesOptProcessExecutor::BayesOptProcessExecutor() = default;

BayesOptProcessExecutor::BayesOptProcessExecutor(const BayesOptProcessConfig& cfg)
    : config_(cfg) {}

BayesOptProcessExecutor::~BayesOptProcessExecutor() = default;

void BayesOptProcessExecutor::setConfig(const BayesOptProcessConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = cfg;
}

BayesOptProcessConfig BayesOptProcessExecutor::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

std::string BayesOptProcessExecutor::serializeEnvParams(const EnvParameters& p) {
    std::ostringstream ss;
    ss << p.temperature << "," << p.humidity << "," << p.light_filter;
    return ss.str();
}

EnvParameters BayesOptProcessExecutor::deserializeEnvParams(const std::string& s) {
    EnvParameters p{};
    auto parts = split(s, ',');
    if (parts.size() >= 1) p.temperature = std::atof(parts[0].c_str());
    if (parts.size() >= 2) p.humidity = std::atof(parts[1].c_str());
    if (parts.size() >= 3) p.light_filter = std::atof(parts[2].c_str());
    return p;
}

std::string BayesOptProcessExecutor::serializeOptConfig(const BayesOptConfig& c) {
    std::ostringstream ss;
    ss << c.max_iterations << ";"
       << c.temperature_min << "," << c.temperature_max << ";"
       << c.humidity_min << "," << c.humidity_max << ";"
       << c.light_filter_min << "," << c.light_filter_max << ";"
       << c.boundary_penalty_strength << "," << c.boundary_penalty_scale;
    return ss.str();
}

BayesOptConfig BayesOptProcessExecutor::deserializeOptConfig(const std::string& s) {
    BayesOptConfig c;
    auto parts = split(s, ';');
    if (parts.size() >= 1) c.max_iterations = (uint32_t)std::atoi(parts[0].c_str());
    if (parts.size() >= 2) {
        auto r = split(parts[1], ',');
        if (r.size() >= 2) { c.temperature_min = std::atof(r[0].c_str()); c.temperature_max = std::atof(r[1].c_str()); }
    }
    if (parts.size() >= 3) {
        auto r = split(parts[2], ',');
        if (r.size() >= 2) { c.humidity_min = std::atof(r[0].c_str()); c.humidity_max = std::atof(r[1].c_str()); }
    }
    if (parts.size() >= 4) {
        auto r = split(parts[3], ',');
        if (r.size() >= 2) { c.light_filter_min = std::atof(r[0].c_str()); c.light_filter_max = std::atof(r[1].c_str()); }
    }
    if (parts.size() >= 5) {
        auto r = split(parts[4], ',');
        if (r.size() >= 2) { c.boundary_penalty_strength = std::atof(r[0].c_str()); c.boundary_penalty_scale = std::atof(r[1].c_str()); }
    }
    return c;
}

std::string BayesOptProcessExecutor::serializeGaussianParams(const GaussianParams& g) {
    std::ostringstream ss;
    ss << g.length_scale << "," << g.signal_variance << "," << g.noise_variance;
    return ss.str();
}

GaussianParams BayesOptProcessExecutor::deserializeGaussianParams(const std::string& s) {
    GaussianParams g;
    auto parts = split(s, ',');
    if (parts.size() >= 1) g.length_scale = std::atof(parts[0].c_str());
    if (parts.size() >= 2) g.signal_variance = std::atof(parts[1].c_str());
    if (parts.size() >= 3) g.noise_variance = std::atof(parts[2].c_str());
    return g;
}

std::string BayesOptProcessExecutor::serializeResult(const EnvOptimizationResult& r) {
    std::ostringstream ss;
    ss << r.zone_id << ";"
       << r.optimal_temperature << "," << r.optimal_humidity << "," << r.optimal_light_filter << ";"
       << r.predicted_lifespan_years << ";"
       << r.improvement_percent << ";"
       << r.current_temperature << "," << r.current_humidity << ";"
       << r.timestamp;
    return ss.str();
}

EnvOptimizationResult BayesOptProcessExecutor::deserializeResult(const std::string& s) {
    EnvOptimizationResult r{};
    auto parts = split(s, ';');
    if (parts.size() >= 1) r.zone_id = (uint32_t)std::atoi(parts[0].c_str());
    if (parts.size() >= 2) {
        auto p = split(parts[1], ',');
        if (p.size() >= 1) r.optimal_temperature = std::atof(p[0].c_str());
        if (p.size() >= 2) r.optimal_humidity = std::atof(p[1].c_str());
        if (p.size() >= 3) r.optimal_light_filter = std::atof(p[2].c_str());
    }
    if (parts.size() >= 3) r.predicted_lifespan_years = std::atof(parts[2].c_str());
    if (parts.size() >= 4) r.improvement_percent = std::atof(parts[3].c_str());
    if (parts.size() >= 5) {
        auto p = split(parts[4], ',');
        if (p.size() >= 1) r.current_temperature = std::atof(p[0].c_str());
        if (p.size() >= 2) r.current_humidity = std::atof(p[1].c_str());
    }
    if (parts.size() >= 6) r.timestamp = (uint64_t)std::atoll(parts[5].c_str());
    return r;
}

EnvOptimizationResult BayesOptProcessExecutor::runInProcess(
    uint32_t zone_id,
    const EnvParameters& current_env,
    float current_fading_rate,
    const BayesOptConfig& opt_cfg,
    const GaussianParams& gp_params,
    OptimizationDiagnostics* diag) {

    BayesOpt bo;
    bo.setConfig(opt_cfg);
    bo.setGaussianParams(gp_params);
    return bo.optimize(zone_id, current_env, current_fading_rate, diag);
}

EnvOptimizationResult BayesOptProcessExecutor::runInSubprocess(
    uint32_t zone_id,
    const EnvParameters& current_env,
    float current_fading_rate,
    const BayesOptConfig& opt_cfg,
    const GaussianParams& gp_params,
    OptimizationDiagnostics* diag) {

    (void)zone_id;
    (void)current_env;
    (void)current_fading_rate;
    (void)opt_cfg;
    (void)gp_params;
    (void)diag;
    return runInProcess(zone_id, current_env, current_fading_rate, opt_cfg, gp_params, diag);
}

EnvOptimizationResult BayesOptProcessExecutor::runOptimization(
    uint32_t zone_id,
    const EnvParameters& current_env,
    float current_fading_rate,
    const BayesOptConfig& opt_cfg,
    const GaussianParams& gp_params,
    OptimizationDiagnostics* diag) {

    std::lock_guard<std::mutex> lock(mutex_);
    if (config_.enable_cross_process && config_.mode == ProcessExecMode::SUBPROCESS) {
        return runInSubprocess(zone_id, current_env, current_fading_rate, opt_cfg, gp_params, diag);
    }
    return runInProcess(zone_id, current_env, current_fading_rate, opt_cfg, gp_params, diag);
}

std::future<EnvOptimizationResult> BayesOptProcessExecutor::runOptimizationAsync(
    uint32_t zone_id,
    const EnvParameters& current_env,
    float current_fading_rate,
    const BayesOptConfig& opt_cfg,
    const GaussianParams& gp_params,
    OptimizationDiagnostics* diag) {

    return ThreadPool::instance().submit([this, zone_id, current_env, current_fading_rate, opt_cfg, gp_params, diag]() {
        return this->runOptimization(zone_id, current_env, current_fading_rate, opt_cfg, gp_params, diag);
    });
}

}
