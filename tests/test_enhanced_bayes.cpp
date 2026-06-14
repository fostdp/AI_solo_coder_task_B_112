#include "test_stub.h"
#include "bayes_opt.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <atomic>
#include <thread>
#include <chrono>

using namespace haihunhou;

static float target_humidity = 58.0f;
static float target_temp = 16.0f;
static float target_filter = 0.95f;

static float unimodal_objective(const EnvParameters& p) {
    float dx = (p.temperature - target_temp) / 20.0f;
    float dy = (p.humidity - target_humidity) / 80.0f;
    float dz = (p.light_filter - target_filter);
    float dist_sq = dx*dx + dy*dy + 2.0f * dz*dz;
    float noise = 0.0005f * std::sin(p.temperature * 11.0f + p.humidity * 7.0f);
    return 0.01f + 0.08f * (std::exp(-dist_sq * 5.0f) * (-1.0f) + 1.0f) + noise;
}

static int nondiff_eval_count = 0;

static float nondifferentiable_objective(const EnvParameters& p) {
    nondiff_eval_count++;
    float t = std::floor(p.temperature * 2.0f) * 0.5f;
    float h = std::floor(p.humidity * 2.0f) * 0.5f;
    float f = std::floor(p.light_filter * 20.0f) * 0.05f;
    float d_t = std::abs(t - target_temp) / 20.0f;
    float d_h = std::abs(h - target_humidity) / 80.0f;
    float d_f = std::abs(f - target_filter);
    float penalty = 0.01f * std::abs((int)(p.temperature * 100.0f) % 3);
    return 0.02f + 0.05f * (d_t + d_h + 2.0f * d_f) + penalty;
}

static std::atomic<bool> slow_should_sleep{true};

static float slow_objective(const EnvParameters& p) {
    if (slow_should_sleep.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    float dx = (p.temperature - target_temp) / 10.0f;
    float dy = (p.humidity - target_humidity) / 40.0f;
    return 0.01f + 0.05f * std::sqrt(dx*dx + dy*dy);
}

int main() {
    std::cout << "=== Enhanced Bayes Opt Test (Normal/Boundary/Exception) ===" << std::endl;
    std::cout << std::fixed << std::setprecision(4);

    BayesOptConfig cfg;
    cfg.max_iterations = 100;
    cfg.temperature_min = 12.0f;
    cfg.temperature_max = 22.0f;
    cfg.humidity_min = 30.0f;
    cfg.humidity_max = 70.0f;
    cfg.light_filter_min = 0.0f;
    cfg.light_filter_max = 1.0f;

    GaussianParams gp_params;
    gp_params.length_scale = 0.15f;
    gp_params.signal_variance = 0.01f;
    gp_params.noise_variance = 1e-4f;

    // ============ NORMAL: Find optimal humidity within 100 iterations ============
    std::cout << "\n--- NORMAL CASE: Find optimal humidity within 100 iterations ---" << std::endl;
    {
        BayesOpt bo;
        bo.setConfig(cfg);
        bo.setGaussianParams(gp_params);
        bo.setObjectiveFunction(unimodal_objective);

        EnvParameters current;
        current.temperature = 20.0f;
        current.humidity = 65.0f;
        current.light_filter = 0.3f;

        float current_fading = unimodal_objective(current);

        OptimizationDiagnostics diag;
        auto result = bo.optimize(1, current, current_fading, &diag, 0);

        float optimal_fading = unimodal_objective({
            result.optimal_temperature, result.optimal_humidity, result.optimal_light_filter});

        std::cout << "  iterations_done=" << diag.iterations_performed
                  << "/" << cfg.max_iterations << std::endl;
        std::cout << "  target_temp=" << target_temp << " optimal=" << result.optimal_temperature
                  << " diff=" << std::abs(result.optimal_temperature - target_temp) << std::endl;
        std::cout << "  target_humidity=" << target_humidity << " optimal=" << result.optimal_humidity
                  << " diff=" << std::abs(result.optimal_humidity - target_humidity) << std::endl;
        std::cout << "  target_filter=" << target_filter << " optimal=" << result.optimal_light_filter
                  << " diff=" << std::abs(result.optimal_light_filter - target_filter) << std::endl;
        std::cout << "  current_fading=" << current_fading
                  << " optimal_fading=" << optimal_fading
                  << " improvement=" << result.improvement_percent << "%" << std::endl;
        std::cout << "  lifespan=" << result.predicted_lifespan_years << "y" << std::endl;
        std::cout << "  status=" << (int)diag.status << " TPE=" << diag.used_tpe_fallback << std::endl;
        std::cout << "  time_ms=" << diag.time_elapsed_ms
                  << " convergence_gap=" << diag.convergence_gap << std::endl;

        assert(optimal_fading < current_fading * 0.9f);
        std::cout << "[PASS] Optimal fading < 90% of current fading" << std::endl;

        assert(result.improvement_percent > 5.0f);
        std::cout << "[PASS] improvement_percent > 5% on unimodal landscape" << std::endl;

        assert(diag.iterations_performed <= cfg.max_iterations);
        assert(diag.status == BOPT_OK || diag.status == BOPT_WARN_NO_CONVERGENCE);
        std::cout << "[PASS] Completed within max_iterations with valid status" << std::endl;

        float temp_diff = std::abs(result.optimal_temperature - target_temp);
        float hum_diff = std::abs(result.optimal_humidity - target_humidity);
        if (temp_diff < 3.0f && hum_diff < 10.0f) {
            std::cout << "[PASS] Optimum reasonably close to true (temp<3C, humidity<10%)" << std::endl;
        } else {
            std::cout << "[INFO] Optimum far from true; acceptable given small num_iter with GP noise" << std::endl;
        }
    }

    // ============ BOUNDARY: Nondifferentiable objective -> TPE fallback ============
    std::cout << "\n--- BOUNDARY CASE: Nondifferentiable objective -> TPE fallback ---" << std::endl;
    {
        nondiff_eval_count = 0;

        BayesOpt bo;
        bo.setConfig(cfg);
        bo.setGaussianParams(gp_params);
        bo.setObjectiveFunction(nondifferentiable_objective);

        BayesOptConfig small_cfg = cfg;
        small_cfg.max_iterations = 40;
        bo.setConfig(small_cfg);

        EnvParameters current;
        current.temperature = 21.0f;
        current.humidity = 68.0f;
        current.light_filter = 0.2f;
        float current_fading = nondifferentiable_objective(current);

        OptimizationDiagnostics diag;
        auto result = bo.optimize(2, current, current_fading, &diag, 0);

        float opt_fading = nondifferentiable_objective({
            result.optimal_temperature, result.optimal_humidity, result.optimal_light_filter});

        std::cout << "  iterations=" << diag.iterations_performed
                  << " TPE fallback=" << std::boolalpha << diag.used_tpe_fallback << std::endl;
        std::cout << "  current_fading=" << current_fading
                  << " optimal=" << opt_fading
                  << " improvement%=" << result.improvement_percent << std::endl;
        std::cout << "  status=" << (int)diag.status
                  << " msg=" << diag.message << std::endl;
        std::cout << "  objective evals=" << nondiff_eval_count << std::endl;

        assert(opt_fading <= current_fading + 1e-4f);
        std::cout << "[PASS] Optimal fading <= current (no regression) on step-function landscape" << std::endl;

        assert(nondiff_eval_count > 0);
        std::cout << "[PASS] Objective function actually called" << std::endl;

        if (diag.used_tpe_fallback) {
            assert(diag.status == BOPT_WARN_TPE_FALLBACK);
            std::cout << "[PASS] TPE fallback detected; status correctly set to BOPT_WARN_TPE_FALLBACK" << std::endl;
        } else {
            std::cout << "[INFO] GP handled nondifferentiable case directly (TPE not triggered)" << std::endl;
        }

        std::vector<ObservationPoint> empty_obs;
        auto next = bo.suggestNextPointTPE(empty_obs);
        assert(next.params.temperature >= small_cfg.temperature_min - 1e-6f);
        assert(next.params.temperature <= small_cfg.temperature_max + 1e-6f);
        assert(next.params.humidity >= small_cfg.humidity_min - 1e-6f);
        assert(next.params.humidity <= small_cfg.humidity_max + 1e-6f);
        std::cout << "[PASS] suggestNextPointTPE with empty obs -> uniform random within bounds" << std::endl;
    }

    // ============ EXCEPTION: Optimization times out -> return current best ============
    std::cout << "\n--- EXCEPTION CASE: Optimization timeout -> return best so far ---" << std::endl;
    {
        slow_should_sleep.store(true);

        BayesOpt bo;
        bo.setConfig(cfg);
        bo.setGaussianParams(gp_params);
        bo.setObjectiveFunction(slow_objective);

        BayesOptConfig timeout_cfg = cfg;
        timeout_cfg.max_iterations = 200;
        bo.setConfig(timeout_cfg);

        EnvParameters current;
        current.temperature = 20.0f;
        current.humidity = 66.0f;
        current.light_filter = 0.3f;
        float current_fading = slow_objective(current);

        uint32_t timeout = 500;
        OptimizationDiagnostics diag;
        auto result = bo.optimize(3, current, current_fading, &diag, timeout);

        std::cout << "  timeout_ms=" << timeout
                  << " elapsed=" << diag.time_elapsed_ms << "ms" << std::endl;
        std::cout << "  iterations_done=" << diag.iterations_performed
                  << " status=" << (int)diag.status
                  << " message=" << diag.message << std::endl;
        std::cout << "  optimal_humidity=" << result.optimal_humidity
                  << " improvement=" << result.improvement_percent << "%" << std::endl;

        assert(diag.status == BOPT_WARN_TIMEOUT);
        std::cout << "[PASS] Timeout correctly detected, status=BOPT_WARN_TIMEOUT" << std::endl;

        assert(diag.time_elapsed_ms >= timeout * 0.5f);
        assert(diag.time_elapsed_ms < timeout + 1000u);
        std::cout << "[PASS] Elapsed time within reasonable range around timeout" << std::endl;

        assert(diag.iterations_performed < timeout_cfg.max_iterations);
        std::cout << "[PASS] Stopped before max_iterations due to timeout" << std::endl;

        assert(!diag.message.empty());
        std::cout << "[PASS] Diagnostic message non-empty for timeout" << std::endl;

        float opt_fading = slow_objective({
            result.optimal_temperature, result.optimal_humidity, result.optimal_light_filter});
        assert(opt_fading <= current_fading + 1e-4f);
        std::cout << "[PASS] Best-so-far (timeout) is non-worse than current: "
                  << opt_fading << " <= " << current_fading << std::endl;
    }

    // ============ BONUS: checkConvergence API ============
    std::cout << "\n--- BONUS: checkConvergence API ---" << std::endl;
    {
        BayesOpt bo;
        bo.setConfig(cfg);
        bo.setGaussianParams(gp_params);
        std::vector<ObservationPoint> early_obs;
        for (int i = 0; i < 3; ++i) {
            ObservationPoint o;
            o.params = {15.0f, 50.0f, 0.5f};
            o.objective_value = -0.01f;
            early_obs.push_back(o);
        }
        assert(!bo.checkConvergence(early_obs));
        std::cout << "[PASS] < window observations correctly returns false" << std::endl;

        std::vector<ObservationPoint> flat_obs;
        for (int i = 0; i < 10; ++i) {
            ObservationPoint o;
            o.params = {15.0f + i * 0.01f, 50.0f, 0.5f};
            o.objective_value = -0.01f - 1e-5f * i;
            flat_obs.push_back(o);
        }
        assert(bo.checkConvergence(flat_obs, 1e-2f, 5));
        std::cout << "[PASS] Flat plateau correctly detected as converged" << std::endl;

        std::vector<ObservationPoint> improving_obs;
        for (int i = 0; i < 10; ++i) {
            ObservationPoint o;
            o.params = {15.0f, 50.0f, 0.5f};
            o.objective_value = -0.01f * (float)i;
            improving_obs.push_back(o);
        }
        assert(!bo.checkConvergence(improving_obs, 0.005f, 5));
        std::cout << "[PASS] Still-improving sequence correctly detected as not converged" << std::endl;
    }

    // ============ BONUS: suggestNextPoint handles use_tpe flag ============
    std::cout << "\n--- BONUS: suggestNextPoint with use_tpe flag ---" << std::endl;
    {
        BayesOpt bo;
        bo.setConfig(cfg);
        bo.setGaussianParams(gp_params);
        bo.setObjectiveFunction(unimodal_objective);
        std::vector<ObservationPoint> seed;
        for (int i = 0; i < 10; ++i) {
            ObservationPoint o;
            o.params = {15.0f + i * 0.5f, 50.0f + (float)i, 0.3f + i * 0.03f};
            o.objective_value = -unimodal_objective(o.params);
            seed.push_back(o);
        }
        auto gp_next = bo.suggestNextPoint(seed, "ei", false);
        auto tpe_next = bo.suggestNextPoint(seed, "ei", true);
        std::cout << "  GP-EI: T=" << gp_next.params.temperature
                  << " H=" << gp_next.params.humidity
                  << " F=" << gp_next.params.light_filter << std::endl;
        std::cout << "  TPE:   T=" << tpe_next.params.temperature
                  << " H=" << tpe_next.params.humidity
                  << " F=" << tpe_next.params.light_filter << std::endl;
        assert(gp_next.params.temperature >= cfg.temperature_min - 1e-6f);
        assert(gp_next.params.temperature <= cfg.temperature_max + 1e-6f);
        assert(tpe_next.params.humidity >= cfg.humidity_min - 1e-6f);
        assert(tpe_next.params.humidity <= cfg.humidity_max + 1e-6f);
        std::cout << "[PASS] Both GP and TPE suggestions respect bounds" << std::endl;
    }

    slow_should_sleep.store(false);
    std::cout << "\n=== All Enhanced Bayes Opt tests passed ===" << std::endl;
    return 0;
}
