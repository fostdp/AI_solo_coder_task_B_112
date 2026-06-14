#include "test_stub.h"
#include "bayes_opt.h"
#include "bayes_process.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <thread>
#include <future>
#include <random>
#include <numeric>

using namespace haihunhou;

static void test_boundary_penalty_direct() {
    std::cout << "\n--- computeBoundaryPenalty: Direct API ---" << std::endl;

    BayesOptConfig cfg;
    cfg.temperature_min = 15.0f;
    cfg.temperature_max = 30.0f;
    cfg.humidity_min = 30.0f;
    cfg.humidity_max = 70.0f;
    cfg.light_filter_min = 0.0f;
    cfg.light_filter_max = 1.0f;
    cfg.boundary_penalty_strength = 0.8f;
    cfg.boundary_penalty_scale = 0.15f;

    BayesOpt bo;
    bo.setConfig(cfg);

    EnvParameters center;
    center.temperature = 22.5f;
    center.humidity = 50.0f;
    center.light_filter = 0.5f;

    EnvParameters near_edge;
    near_edge.temperature = 22.5f;
    near_edge.humidity = 36.0f;
    near_edge.light_filter = 0.5f;

    EnvParameters low_hum;
    low_hum.temperature = 22.5f;
    low_hum.humidity = 30.0f;
    low_hum.light_filter = 0.5f;

    EnvParameters corner;
    corner.temperature = 15.0f;
    corner.humidity = 30.0f;
    corner.light_filter = 0.0f;

    float p_center = bo.computeBoundaryPenalty(center);
    float p_near = bo.computeBoundaryPenalty(near_edge);
    float p_low = bo.computeBoundaryPenalty(low_hum);
    float p_corner = bo.computeBoundaryPenalty(corner);

    std::cout << "  Center:     p=" << p_center << std::endl;
    std::cout << "  Near edge:  p=" << p_near << std::endl;
    std::cout << "  Low Hum:    p=" << p_low << std::endl;
    std::cout << "  Corner:     p=" << p_corner << std::endl;

    CHECK(p_center > 0.9f);
    CHECK(p_center > p_near);
    CHECK(p_near > p_low);
    CHECK(p_low >= p_corner);
    CHECK(p_corner >= 0.0f && p_corner <= 1.0f);
    CHECK(p_low < 0.5f);
    std::cout << "  Direct boundary penalty ordering correct [PASS]" << std::endl;

    BayesOptConfig cfg_no_penalty = cfg;
    cfg_no_penalty.boundary_penalty_strength = 0.0f;
    BayesOpt bo2;
    bo2.setConfig(cfg_no_penalty);
    float p_none = bo2.computeBoundaryPenalty(corner);
    CHECK(std::abs(p_none - 1.0f) < 1e-6f);
    std::cout << "  Strength=0 -> penalty=1.0 (no penalty) [PASS]" << std::endl;
}

static void test_penalty_monotonic() {
    std::cout << "\n--- Penalty monotonic along axis ---" << std::endl;

    BayesOptConfig cfg;
    cfg.temperature_min = 15.0f;
    cfg.temperature_max = 30.0f;
    cfg.humidity_min = 40.0f;
    cfg.humidity_max = 60.0f;
    cfg.light_filter_min = 0.0f;
    cfg.light_filter_max = 1.0f;
    cfg.boundary_penalty_strength = 0.9f;
    cfg.boundary_penalty_scale = 0.1f;

    BayesOpt bo;
    bo.setConfig(cfg);

    float prev_p = 0.0f;
    bool monotonic_up = true;
    for (int i = 0; i <= 10; ++i) {
        EnvParameters p;
        p.temperature = 22.5f;
        p.humidity = 40.0f + i * 2.0f;
        p.light_filter = 0.5f;
        float penal = bo.computeBoundaryPenalty(p);
        if (i > 0 && i <= 5 && penal < prev_p - 1e-6f) monotonic_up = false;
        if (i > 5 && penal > prev_p + 1e-6f) monotonic_up = false;
        std::cout << "  H=" << p.humidity << " -> p=" << penal << std::endl;
        prev_p = penal;
    }
    CHECK(monotonic_up);
    std::cout << "  Penalty bell-shaped (rises to center then falls) [PASS]" << std::endl;
}

static void test_optimization_with_penalty() {
    std::cout << "\n--- Optimization with penalty stabilizes humidity ---" << std::endl;

    GaussianParams gp;
    gp.length_scale = 0.15f;
    gp.signal_variance = 0.01f;
    gp.noise_variance = 1e-4f;

    auto edge_obj = [](const EnvParameters& p) -> float {
        float bias = (p.humidity - 30.0f) * 0.0001f;
        return 0.02f + 0.00001f * bias;
    };

    {
        BayesOptConfig cfg_on;
        cfg_on.max_iterations = 25;
        cfg_on.temperature_min = 15.0f;
        cfg_on.temperature_max = 30.0f;
        cfg_on.humidity_min = 30.0f;
        cfg_on.humidity_max = 80.0f;
        cfg_on.light_filter_min = 0.0f;
        cfg_on.light_filter_max = 1.0f;
        cfg_on.boundary_penalty_strength = 0.9f;
        cfg_on.boundary_penalty_scale = 0.1f;

        BayesOpt bo_on;
        bo_on.setConfig(cfg_on);
        bo_on.setGaussianParams(gp);
        bo_on.setObjectiveFunction(edge_obj);

        EnvParameters cur;
        cur.temperature = 22.0f;
        cur.humidity = 50.0f;
        cur.light_filter = 0.5f;

        auto r_on = bo_on.optimize(1, cur, 0.05f);
        std::cout << "  With penalty: H_opt=" << r_on.optimal_humidity << "% RH" << std::endl;

        CHECK(r_on.optimal_humidity >= cfg_on.humidity_min - 1e-4f);
        CHECK(r_on.optimal_humidity <= cfg_on.humidity_max + 1e-4f);
        std::cout << "  Optimal humidity stays within feasible bounds [PASS]" << std::endl;
    }

    {
        BayesOptConfig cfg_off;
        cfg_off.max_iterations = 25;
        cfg_off.temperature_min = 15.0f;
        cfg_off.temperature_max = 30.0f;
        cfg_off.humidity_min = 30.0f;
        cfg_off.humidity_max = 80.0f;
        cfg_off.light_filter_min = 0.0f;
        cfg_off.light_filter_max = 1.0f;
        cfg_off.boundary_penalty_strength = 0.0f;

        BayesOpt bo_off;
        bo_off.setConfig(cfg_off);
        bo_off.setGaussianParams(gp);
        bo_off.setObjectiveFunction(edge_obj);

        EnvParameters cur;
        cur.temperature = 22.0f;
        cur.humidity = 50.0f;
        cur.light_filter = 0.5f;

        auto r_off = bo_off.optimize(1, cur, 0.05f);
        std::cout << "  Without penalty: H_opt=" << r_off.optimal_humidity << "% RH" << std::endl;

        CHECK(r_off.optimal_humidity >= cfg_off.humidity_min - 1e-4f);
        CHECK(r_off.optimal_humidity <= cfg_off.humidity_max + 1e-4f);
    }
}

static void test_penalty_acquisition_applied() {
    std::cout << "\n--- Penalty actually affects EI acquisition ---" << std::endl;

    BayesOptConfig cfg;
    cfg.temperature_min = 15.0f;
    cfg.temperature_max = 30.0f;
    cfg.humidity_min = 40.0f;
    cfg.humidity_max = 70.0f;
    cfg.light_filter_min = 0.0f;
    cfg.light_filter_max = 1.0f;
    cfg.boundary_penalty_strength = 0.9f;
    cfg.boundary_penalty_scale = 0.1f;

    GaussianParams gp;
    gp.length_scale = 0.3f;
    gp.signal_variance = 0.05f;
    gp.noise_variance = 1e-3f;

    BayesOpt bo;
    bo.setConfig(cfg);
    bo.setGaussianParams(gp);

    auto uni_obj = [](const EnvParameters&) { return 0.05f; };
    bo.setObjectiveFunction(uni_obj);

    EnvParameters cur;
    cur.temperature = 22.0f;
    cur.humidity = 55.0f;
    cur.light_filter = 0.5f;

    OptimizationDiagnostics diag;
    auto res = bo.optimize(1, cur, 0.05f, &diag);

    std::cout << "  Opt T=" << res.optimal_temperature
              << " H=" << res.optimal_humidity
              << " F=" << res.optimal_light_filter << std::endl;
    std::cout << "  Status=" << diag.status << " iters=" << diag.iterations_performed << std::endl;

    CHECK(res.optimal_humidity >= cfg.humidity_min - 1e-4f);
    CHECK(res.optimal_humidity <= cfg.humidity_max + 1e-4f);
    CHECK(res.optimal_temperature >= cfg.temperature_min - 1e-4f);
    CHECK(res.optimal_temperature <= cfg.temperature_max + 1e-4f);
    std::cout << "  Optimizer stays within feasible bounds [PASS]" << std::endl;
}

static void test_serialization_roundtrip() {
    std::cout << "\n--- BayesOptProcessExecutor serialization ---" << std::endl;

    EnvParameters p;
    p.temperature = 18.5f;
    p.humidity = 52.0f;
    p.light_filter = 0.75f;

    std::string s = BayesOptProcessExecutor::serializeEnvParams(p);
    auto p2 = BayesOptProcessExecutor::deserializeEnvParams(s);
    CHECK(std::abs(p2.temperature - p.temperature) < 0.01f);
    CHECK(std::abs(p2.humidity - p.humidity) < 0.01f);
    CHECK(std::abs(p2.light_filter - p.light_filter) < 0.01f);
    std::cout << "  EnvParameters roundtrip OK [PASS]" << std::endl;

    BayesOptConfig cfg;
    cfg.max_iterations = 42;
    cfg.temperature_min = 10;
    cfg.temperature_max = 35;
    cfg.humidity_min = 20;
    cfg.humidity_max = 80;
    cfg.light_filter_min = 0.2f;
    cfg.light_filter_max = 0.95f;
    cfg.boundary_penalty_strength = 0.7f;
    cfg.boundary_penalty_scale = 0.12f;

    std::string cs = BayesOptProcessExecutor::serializeOptConfig(cfg);
    auto cfg2 = BayesOptProcessExecutor::deserializeOptConfig(cs);
    CHECK(cfg2.max_iterations == 42);
    CHECK(std::abs(cfg2.temperature_min - 10.0f) < 0.01f);
    CHECK(std::abs(cfg2.humidity_max - 80.0f) < 0.01f);
    CHECK(std::abs(cfg2.boundary_penalty_strength - 0.7f) < 0.01f);
    std::cout << "  BayesOptConfig roundtrip OK [PASS]" << std::endl;

    GaussianParams gp;
    gp.length_scale = 0.25f;
    gp.signal_variance = 0.08f;
    gp.noise_variance = 0.002f;
    std::string gs = BayesOptProcessExecutor::serializeGaussianParams(gp);
    auto gp2 = BayesOptProcessExecutor::deserializeGaussianParams(gs);
    CHECK(std::abs(gp2.length_scale - 0.25f) < 1e-4f);
    CHECK(std::abs(gp2.signal_variance - 0.08f) < 1e-4f);
    std::cout << "  GaussianParams roundtrip OK [PASS]" << std::endl;

    EnvOptimizationResult r{};
    r.zone_id = 7;
    r.optimal_temperature = 16.5f;
    r.optimal_humidity = 58.0f;
    r.optimal_light_filter = 0.88f;
    r.predicted_lifespan_years = 150.0f;
    r.improvement_percent = 42.5f;
    r.current_temperature = 22.0f;
    r.current_humidity = 65.0f;
    r.timestamp = 1234567890ULL;
    std::string rs = BayesOptProcessExecutor::serializeResult(r);
    auto r2 = BayesOptProcessExecutor::deserializeResult(rs);
    CHECK(r2.zone_id == 7);
    CHECK(std::abs(r2.optimal_temperature - 16.5f) < 0.01f);
    CHECK(std::abs(r2.optimal_humidity - 58.0f) < 0.01f);
    CHECK(std::abs(r2.predicted_lifespan_years - 150.0f) < 0.1f);
    CHECK(std::abs(r2.improvement_percent - 42.5f) < 0.1f);
    CHECK(r2.timestamp == 1234567890ULL);
    std::cout << "  EnvOptimizationResult roundtrip OK [PASS]" << std::endl;
}

static void test_process_executor_inprocess() {
    std::cout << "\n--- BayesOptProcessExecutor in-process execution ---" << std::endl;

    BayesOptProcessConfig pcfg;
    pcfg.mode = ProcessExecMode::IN_PROCESS;
    pcfg.enable_cross_process = false;

    BayesOptProcessExecutor exec(pcfg);

    BayesOptConfig cfg;
    cfg.max_iterations = 15;
    cfg.temperature_min = 15;
    cfg.temperature_max = 30;
    cfg.humidity_min = 40;
    cfg.humidity_max = 70;
    cfg.light_filter_min = 0;
    cfg.light_filter_max = 1;
    cfg.boundary_penalty_strength = 0.7f;

    GaussianParams gp;
    gp.length_scale = 0.15f;
    gp.signal_variance = 0.01f;
    gp.noise_variance = 1e-4f;

    EnvParameters cur;
    cur.temperature = 22.0f;
    cur.humidity = 55.0f;
    cur.light_filter = 0.5f;

    auto r = exec.runOptimization(3, cur, 0.05f, cfg, gp);
    std::cout << "  Sync optimal: T=" << r.optimal_temperature
              << " H=" << r.optimal_humidity << std::endl;
    CHECK(r.zone_id == 3);
    CHECK(r.optimal_humidity >= cfg.humidity_min - 1e-4f);
    CHECK(r.optimal_humidity <= cfg.humidity_max + 1e-4f);
    std::cout << "  Sync in-process optimization OK [PASS]" << std::endl;

    auto fut = exec.runOptimizationAsync(4, cur, 0.05f, cfg, gp);
    auto r2 = fut.get();
    CHECK(r2.zone_id == 4);
    std::cout << "  Async in-process optimization OK [PASS]" << std::endl;
}

int main() {
    std::cout << "========== test_bayes_penalty ==========" << std::endl;
    std::cout << std::fixed << std::setprecision(4);

    test_boundary_penalty_direct();
    test_penalty_monotonic();
    test_optimization_with_penalty();
    test_penalty_acquisition_applied();
    test_serialization_roundtrip();
    test_process_executor_inprocess();

    std::cout << "\n========== test_bayes_penalty: ALL PASSED ==========" << std::endl;
    return 0;
}
