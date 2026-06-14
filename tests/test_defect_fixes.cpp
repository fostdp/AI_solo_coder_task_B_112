#include "test_stub.h"
#include "cnn_matcher.h"
#include "plsr_inversion.h"
#include "bayes_opt.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <thread>
#include <chrono>
#include <future>
#include <random>
#include <numeric>

using namespace haihunhou;

static std::vector<SpectralData> make_spectrum(size_t n_points = 20) {
    std::vector<SpectralData> data;
    for (size_t i = 0; i < n_points; ++i) {
        SpectralData d;
        d.wavelength = (uint16_t)(380 + i * (780 - 380) / (n_points - 1));
        d.reflectance = 0.4f + 0.2f * std::sin(i * 0.3f);
        data.push_back(d);
    }
    return data;
}

static void test_cnn_async_and_threads() {
    std::cout << "\n=== DEFECT FIX 1: CNN Matcher Async + Thread Limit ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    CnnMatcher matcher;
    matcher.loadModel();

    auto query = make_spectrum(30);
    std::vector<uint32_t> cand_ids = {101, 102, 103, 104, 105};
    std::unordered_map<uint32_t, std::vector<SpectralData>> cand_spec;
    for (auto id : cand_ids) {
        cand_spec[id] = make_spectrum(30);
    }

    // ---- Test thread count API ----
    std::cout << "  [Thread control] Default num_threads = " << matcher.getNumThreads() << std::endl;
    assert(matcher.getNumThreads() == 2);

    matcher.setNumThreads(4);
    assert(matcher.getNumThreads() == 4);
    std::cout << "  [Thread control] setNumThreads(4) -> " << matcher.getNumThreads() << " OK" << std::endl;
    matcher.setNumThreads(2);

    // ---- Test simulated latency: 0-thread (auto/many) vs 2-thread ----
    matcher.setSimulatedLatencyMs(2000);
    matcher.setNumThreads(0);

    auto t_start = std::chrono::steady_clock::now();
    MatchStatus st_sync;
    auto r_sync = matcher.findMatches(1, cand_ids, query, cand_spec, &st_sync);
    auto t_end = std::chrono::steady_clock::now();
    auto t_sync_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    std::cout << "  [Sync] 0 threads (default/many): " << t_sync_ms << "ms (simulated ~2000ms)" << std::endl;
    assert(t_sync_ms >= 1500);

    matcher.setNumThreads(2);
    t_start = std::chrono::steady_clock::now();
    auto r2 = matcher.findMatches(1, cand_ids, query, cand_spec, nullptr);
    t_end = std::chrono::steady_clock::now();
    auto t_2thread_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    std::cout << "  [Sync] 2 threads (limited): " << t_2thread_ms << "ms (simulated ~300ms)" << std::endl;
    assert(t_2thread_ms < 800);
    assert(t_2thread_ms < t_sync_ms / 2);
    std::cout << "  [Verify] Thread limiting reduces latency: PASS" << std::endl;

    // ---- Test async inference ----
    matcher.setNumThreads(2);
    matcher.setSimulatedLatencyMs(1500);

    auto t_before = std::chrono::steady_clock::now();
    MatchStatus st_async;
    auto future = matcher.findMatchesAsync(1, cand_ids, query, cand_spec, &st_async);
    auto t_after_call = std::chrono::steady_clock::now();
    auto call_return_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_after_call - t_before).count();

    bool immediate = (call_return_ms < 200);
    std::cout << "  [Async] findMatchesAsync returns in " << call_return_ms << "ms (immediate="
              << (immediate ? "YES" : "NO") << ")" << std::endl;
    assert(immediate);

    auto t_before_wait = std::chrono::steady_clock::now();
    auto r_async = future.get();
    auto t_after_wait = std::chrono::steady_clock::now();
    auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_after_wait - t_before_wait).count();
    std::cout << "  [Async] future.get() waits " << wait_ms << "ms for result" << std::endl;
    assert(wait_ms > 150);

    assert(r_async.size() == r2.size());
    std::cout << "  [Verify] Async returns same number of matches: PASS" << std::endl;

    matcher.setSimulatedLatencyMs(0);
    matcher.setNumThreads(2);
    std::cout << "  >>> DEFECT FIX 1: PASSED <<<" << std::endl;
}

static void test_bayes_boundary_penalty() {
    std::cout << "\n=== DEFECT FIX 2: Bayes Opt Boundary Penalty ===" << std::endl;
    std::cout << std::fixed << std::setprecision(3);

    GaussianParams gp_params;
    gp_params.length_scale = 0.15f;
    gp_params.signal_variance = 0.01f;
    gp_params.noise_variance = 1e-4f;

    // ---- Test boundary penalty function directly ----
    {
        BayesOptConfig cfg;
        cfg.temperature_min = 15.0f;
        cfg.temperature_max = 30.0f;
        cfg.humidity_min = 40.0f;
        cfg.humidity_max = 70.0f;
        cfg.light_filter_min = 0.0f;
        cfg.light_filter_max = 1.0f;
        cfg.boundary_penalty_strength = 0.8f;
        cfg.boundary_penalty_scale = 0.15f;

        BayesOpt bo;
        bo.setConfig(cfg);
        bo.setGaussianParams(gp_params);

        EnvParameters center;
        center.temperature = 22.5f;
        center.humidity = 55.0f;
        center.light_filter = 0.5f;

        EnvParameters boundary;
        boundary.temperature = 15.0f;
        boundary.humidity = 40.0f;
        boundary.light_filter = 0.0f;

        float pen_center = bo.computeBoundaryPenalty(center);
        float pen_boundary = bo.computeBoundaryPenalty(boundary);

        std::cout << "  [Direct] Center point penalty: " << pen_center << std::endl;
        std::cout << "  [Direct] Boundary point penalty: " << pen_boundary << std::endl;

        assert(pen_center > 0.8f);
        assert(pen_boundary < 0.5f);
        assert(pen_center > pen_boundary);
        std::cout << "  [Verify] Boundary penalty weaker at center: PASS" << std::endl;
    }

    // ---- Test with strong penalty: results stay in 40-60% RH range ----
    {
        BayesOptConfig cfg;
        cfg.max_iterations = 30;
        cfg.temperature_min = 15.0f;
        cfg.temperature_max = 30.0f;
        cfg.humidity_min = 20.0f;
        cfg.humidity_max = 80.0f;
        cfg.light_filter_min = 0.0f;
        cfg.light_filter_max = 1.0f;
        cfg.boundary_penalty_strength = 0.9f;
        cfg.boundary_penalty_scale = 0.12f;

        auto flat_obj = [](const EnvParameters& p) -> float {
            return 0.05f + 0.001f * p.humidity * 0.0f;
        };

        BayesOpt bo;
        bo.setConfig(cfg);
        bo.setGaussianParams(gp_params);
        bo.setObjectiveFunction(flat_obj);

        EnvParameters current;
        current.temperature = 22.0f;
        current.humidity = 50.0f;
        current.light_filter = 0.5f;

        OptimizationDiagnostics diag;
        auto result = bo.optimize(1, current, 0.05f, &diag);

        std::cout << "  [Optimization] Optimal humidity: " << result.optimal_humidity << "% RH" << std::endl;
        std::cout << "  [Optimization] Optimal temperature: " << result.optimal_temperature << " C" << std::endl;
        std::cout << "  [Optimization] Optimal filter: " << result.optimal_light_filter << std::endl;

        assert(result.optimal_humidity >= 35.0f);
        assert(result.optimal_humidity <= 65.0f);
        assert(result.optimal_temperature >= 18.0f);
        assert(result.optimal_temperature <= 27.0f);
        std::cout << "  [Verify] Optimal stays away from boundaries: PASS" << std::endl;
    }

    // ---- Compare: penalty OFF vs ON (small sample -> more extreme without penalty) ----
    {
        BayesOptConfig cfg_on;
        cfg_on.max_iterations = 20;
        cfg_on.temperature_min = 15.0f;
        cfg_on.temperature_max = 30.0f;
        cfg_on.humidity_min = 20.0f;
        cfg_on.humidity_max = 80.0f;
        cfg_on.light_filter_min = 0.0f;
        cfg_on.light_filter_max = 1.0f;
        cfg_on.boundary_penalty_strength = 0.9f;
        cfg_on.boundary_penalty_scale = 0.1f;

        BayesOptConfig cfg_off = cfg_on;
        cfg_off.boundary_penalty_strength = 0.0f;

        auto edge_bias_obj = [](const EnvParameters& p) -> float {
            float h_norm = (p.humidity - 20.0f) / 60.0f;
            return 0.1f - 0.08f * h_norm;
        };

        BayesOpt bo_on, bo_off;
        bo_on.setConfig(cfg_on);
        bo_on.setGaussianParams(gp_params);
        bo_on.setObjectiveFunction(edge_bias_obj);
        bo_off.setConfig(cfg_off);
        bo_off.setGaussianParams(gp_params);
        bo_off.setObjectiveFunction(edge_bias_obj);

        EnvParameters current;
        current.temperature = 22.0f;
        current.humidity = 50.0f;
        current.light_filter = 0.5f;

        auto r_on = bo_on.optimize(1, current, 0.06f);
        auto r_off = bo_off.optimize(2, current, 0.06f);

        std::cout << "  [Compare] With penalty: H=" << r_on.optimal_humidity
                  << "%, Without penalty: H=" << r_off.optimal_humidity << "%" << std::endl;

        assert(r_on.optimal_humidity > 30.0f);
        std::cout << "  [Verify] Penalty prevents extreme boundary values: PASS" << std::endl;
    }

    std::cout << "  >>> DEFECT FIX 2: PASSED <<<" << std::endl;
}

static void test_plsr_sg_smoothing() {
    std::cout << "\n=== DEFECT FIX 3: PLSR Savitzky-Golay Smoothing ===" << std::endl;
    std::cout << std::fixed << std::setprecision(4);

    PlsrInversion plsr;
    plsr.loadCoefficients();

    // ---- Test smoothSpectrumSavitzkyGolay directly ----
    {
        std::vector<float> signal(20);
        for (size_t i = 0; i < signal.size(); ++i) {
            signal[i] = 0.5f + 0.15f * std::sin(i * 0.15f);
        }

        std::mt19937 rng(42);
        std::normal_distribution<float> noise(0.0f, 0.02f);
        auto noisy = signal;
        for (auto& v : noisy) v += noise(rng);

        auto smoothed = plsr.smoothSpectrumSavitzkyGolay(noisy, 7, 2);

        float noise_err = 0.0f, smooth_err = 0.0f;
        for (size_t i = 0; i < signal.size(); ++i) {
            float ne = noisy[i] - signal[i];
            float se = smoothed[i] - signal[i];
            noise_err += ne * ne;
            smooth_err += se * se;
        }
        noise_err = std::sqrt(noise_err / signal.size());
        smooth_err = std::sqrt(smooth_err / signal.size());

        std::cout << "  [Direct] Noisy RMSE: " << noise_err << std::endl;
        std::cout << "  [Direct] Smoothed RMSE: " << smooth_err << std::endl;

        assert(smooth_err < noise_err * 0.85f);
        std::cout << "  [Verify] Smoothing reduces noise: PASS" << std::endl;

        assert(smoothed.size() == signal.size());
        std::cout << "  [Verify] Output length matches input: PASS" << std::endl;
    }

    // ---- Test noisy spectrum inversion: with smoothing vs without ----
    {
        std::vector<uint16_t> wl = {380, 400, 450, 500, 550, 600, 650, 700, 750, 780};

        PlsrInversionConfig cfg_on;
        cfg_on.enable_smoothing = true;
        cfg_on.smooth_window_size = 5;
        cfg_on.smooth_poly_order = 2;

        PlsrInversionConfig cfg_off;
        cfg_off.enable_smoothing = false;

        std::vector<float> clean_spec(wl.size());
        for (size_t i = 0; i < wl.size(); ++i) {
            float t = (float)i / (wl.size() - 1);
            clean_spec[i] = 0.4f + 0.3f * t + 0.15f * std::sin(t * 3.14159f);
        }

        // Test 1: Small noise (σ=0.02) - verify error < 3% (user acceptance)
        {
            std::mt19937 rng(123);
            std::normal_distribution<float> noise(0.0f, 0.02f);
            auto noisy_spec = clean_spec;
            for (auto& v : noisy_spec) v += noise(rng);

            PlsrInversion plsr_on, plsr_off;
            plsr_on.setConfig(cfg_on);
            plsr_on.loadCoefficients();
            plsr_off.setConfig(cfg_off);
            plsr_off.loadCoefficients();

            auto pred_clean = plsr_off.predict(clean_spec);
            auto pred_smooth = plsr_on.predict(noisy_spec);

            float smooth_err = std::abs(pred_smooth.carbon_black_ratio - pred_clean.carbon_black_ratio)
                             / std::max(0.001f, pred_clean.carbon_black_ratio) * 100.0f;

            std::cout << "  [Inversion σ=0.02] Clean CB: " << pred_clean.carbon_black_ratio
                      << ", Smooth CB: " << pred_smooth.carbon_black_ratio
                      << ", err=" << smooth_err << "%" << std::endl;

            assert(smooth_err < 3.0f);
            std::cout << "  [Verify] Inversion error < 3% with noise σ=0.02: PASS" << std::endl;
        }

        // Test 2: Larger noise - verify smoothing reduces error
        {
            std::mt19937 rng(456);
            std::normal_distribution<float> noise(0.0f, 0.08f);
            auto noisy_spec = clean_spec;
            for (auto& v : noisy_spec) v += noise(rng);

            PlsrInversion plsr_on, plsr_off;
            plsr_on.setConfig(cfg_on);
            plsr_on.loadCoefficients();
            plsr_off.setConfig(cfg_off);
            plsr_off.loadCoefficients();

            auto pred_clean = plsr_off.predict(clean_spec);
            auto pred_noisy = plsr_off.predict(noisy_spec);
            auto pred_smooth = plsr_on.predict(noisy_spec);

            float noisy_err = std::abs(pred_noisy.carbon_black_ratio - pred_clean.carbon_black_ratio)
                            / std::max(0.001f, pred_clean.carbon_black_ratio) * 100.0f;
            float smooth_err = std::abs(pred_smooth.carbon_black_ratio - pred_clean.carbon_black_ratio)
                             / std::max(0.001f, pred_clean.carbon_black_ratio) * 100.0f;

            std::cout << "  [Inversion σ=0.08] No smooth err: " << noisy_err
                      << "%, With smooth err: " << smooth_err << "%" << std::endl;

            assert(smooth_err < noisy_err);
            std::cout << "  [Verify] Smoothing reduces inversion error at high noise: PASS" << std::endl;
        }
    }

    // ---- Boundary: smoothing on constant signal should preserve value ----
    {
        std::vector<float> constant(10, 0.6f);
        auto smoothed = plsr.smoothSpectrumSavitzkyGolay(constant, 5, 2);
        float max_dev = 0.0f;
        for (auto v : smoothed) {
            max_dev = std::max(max_dev, std::abs(v - 0.6f));
        }
        std::cout << "  [Boundary] Constant signal max deviation after smoothing: " << max_dev << std::endl;
        assert(max_dev < 0.01f);
        std::cout << "  [Verify] Constant signal preserved: PASS" << std::endl;
    }

    // ---- Edge case: very short spectrum ----
    {
        std::vector<float> short_sig(3);
        short_sig[0] = 0.3f;
        short_sig[1] = 0.5f;
        short_sig[2] = 0.4f;
        auto smoothed = plsr.smoothSpectrumSavitzkyGolay(short_sig, 5, 2);
        assert(smoothed.size() == 3);
        std::cout << "  [Edge] Short spectrum (3pt) smoothed OK: length=" << smoothed.size() << std::endl;
    }

    std::cout << "  >>> DEFECT FIX 3: PASSED <<<" << std::endl;
}

int main() {
    std::cout << "========== DEFECT FIX VERIFICATION TEST ==========" << std::endl;
    std::cout << std::fixed << std::setprecision(4);

    test_cnn_async_and_threads();
    test_bayes_boundary_penalty();
    test_plsr_sg_smoothing();

    std::cout << "\n========== ALL DEFECT FIX TESTS PASSED ==========" << std::endl;
    return 0;
}
