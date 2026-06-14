#include "test_stub.h"
#include "bayes_opt.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace haihunhou;

int main() {
    std::cout << "=== Bayesian Optimization Test ===" << std::endl;

    BayesOpt opt;

    BayesOptConfig cfg;
    cfg.max_iterations = 10;
    cfg.temperature_min = 15.0f;
    cfg.temperature_max = 30.0f;
    cfg.humidity_min = 40.0f;
    cfg.humidity_max = 70.0f;
    cfg.light_filter_min = 0.0f;
    cfg.light_filter_max = 1.0f;
    opt.setConfig(cfg);
    auto cfg_out = opt.getConfig();
    assert(cfg_out.max_iterations == 10);
    std::cout << "[PASS] setConfig/getConfig" << std::endl;

    GaussianParams gp;
    gp.length_scale = 1.0f;
    gp.signal_variance = 1.0f;
    gp.noise_variance = 0.01f;
    opt.setGaussianParams(gp);
    auto gp_out = opt.getGaussianParams();
    assert(std::abs(gp_out.length_scale - 1.0f) < 1e-6);
    std::cout << "[PASS] setGaussianParams/getGaussianParams" << std::endl;

    EnvParameters x1, x2;
    x1.temperature = 20.0f;
    x1.humidity = 50.0f;
    x1.light_filter = 0.3f;
    x2.temperature = 25.0f;
    x2.humidity = 60.0f;
    x2.light_filter = 0.5f;

    std::vector<ObservationPoint> obs;
    ObservationPoint p1, p2;
    p1.params = x1;
    p1.objective_value = -0.12f;
    p2.params = x2;
    p2.objective_value = -0.18f;
    obs.push_back(p1);
    obs.push_back(p2);

    auto [mean, std_dev] = opt.predict(x1, obs);
    assert(std_dev >= 0.0f);
    std::cout << "[PASS] predict mean=" << mean << " std=" << std_dev << std::endl;

    EnvParameters x_test;
    x_test.temperature = 22.0f;
    x_test.humidity = 55.0f;
    x_test.light_filter = 0.4f;

    float ei = opt.expectedImprovement(x_test, obs, -0.20f);
    assert(ei >= 0.0f);
    std::cout << "[PASS] expectedImprovement = " << ei << std::endl;

    float ucb = opt.upperConfidenceBound(x_test, obs, 2.576f);
    std::cout << "[PASS] upperConfidenceBound = " << ucb << std::endl;

    float pi = opt.probabilityOfImprovement(x_test, obs, -0.20f);
    assert(pi >= 0.0f && pi <= 1.0f);
    std::cout << "[PASS] probabilityOfImprovement = " << pi << std::endl;

    auto next = opt.suggestNextPoint(obs);
    assert(next.params.temperature >= cfg.temperature_min);
    assert(next.params.temperature <= cfg.temperature_max);
    assert(next.params.humidity >= cfg.humidity_min);
    assert(next.params.humidity <= cfg.humidity_max);
    assert(next.params.light_filter >= cfg.light_filter_min);
    assert(next.params.light_filter <= cfg.light_filter_max);
    std::cout << "[PASS] suggestNextPoint" << std::endl;
    std::cout << "  temp: " << next.params.temperature << std::endl;
    std::cout << "  hum: " << next.params.humidity << std::endl;
    std::cout << "  filter: " << next.params.light_filter << std::endl;
    std::cout << "  acquisition_value: " << next.acquisition_value << std::endl;

    float lifespan = opt.computeLifespan(0.1f);
    assert(lifespan > 0.0f);
    std::cout << "[PASS] computeLifespan (0.1%/month) = " << lifespan << " years" << std::endl;

    float lifespan_slow = opt.computeLifespan(0.01f);
    assert(lifespan_slow > lifespan);
    std::cout << "[PASS] slower fading = longer lifespan" << std::endl;

    EnvParameters current_env;
    current_env.temperature = 25.0f;
    current_env.humidity = 60.0f;
    current_env.light_filter = 0.3f;

    auto result = opt.optimize(1, current_env, 0.15f);
    assert(result.zone_id == 1);
    assert(result.optimal_temperature >= cfg.temperature_min);
    assert(result.optimal_temperature <= cfg.temperature_max);
    assert(result.optimal_humidity >= cfg.humidity_min);
    assert(result.optimal_humidity <= cfg.humidity_max);
    assert(result.optimal_light_filter >= cfg.light_filter_min);
    assert(result.optimal_light_filter <= cfg.light_filter_max);
    assert(result.predicted_lifespan_years > 0.0f);
    assert(result.improvement_percent >= 0.0f);
    std::cout << "[PASS] optimize" << std::endl;
    std::cout << "  optimal_temp: " << result.optimal_temperature << "°C" << std::endl;
    std::cout << "  optimal_hum: " << result.optimal_humidity << "%RH" << std::endl;
    std::cout << "  optimal_filter: " << result.optimal_light_filter << std::endl;
    std::cout << "  predicted_lifespan: " << result.predicted_lifespan_years << " years" << std::endl;
    std::cout << "  improvement: " << result.improvement_percent << "%" << std::endl;

    bool custom_called = false;
    opt.setObjectiveFunction([&](const EnvParameters& p) {
        custom_called = true;
        return 0.1f + 0.01f * (p.temperature - 20.0f);
    });
    auto result2 = opt.optimize(2, current_env, 0.15f);
    assert(custom_called);
    std::cout << "[PASS] custom objective function" << std::endl;

    std::cout << "\n=== All Bayesian Optimization tests passed ===" << std::endl;
    return 0;
}
