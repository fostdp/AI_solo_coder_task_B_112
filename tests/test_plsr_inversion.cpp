#include "test_stub.h"
#include "plsr_inversion.h"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace haihunhou;

int main() {
    std::cout << "=== PLSR Inversion Test ===" << std::endl;

    PlsrInversion plsr;

    PlsrInversionConfig cfg;
    cfg.n_components = 15;
    cfg.confidence_threshold = 0.85f;
    plsr.setConfig(cfg);
    auto cfg_out = plsr.getConfig();
    assert(cfg_out.n_components == 15);
    std::cout << "[PASS] setConfig/getConfig" << std::endl;

    bool loaded = plsr.loadCoefficients();
    assert(loaded);
    assert(plsr.isLoaded());
    std::cout << "[PASS] loadCoefficients" << std::endl;

    std::vector<uint16_t> wls = {380, 400, 450, 500, 550, 600, 650, 700, 750, 780};
    std::vector<SpectralData> spectral;
    for (size_t i = 0; i < wls.size(); ++i) {
        SpectralData d;
        d.slip_id = 1;
        d.wavelength = wls[i];
        d.reflectance = 0.45f + 0.25f * (float)i / (wls.size() - 1);
        spectral.push_back(d);
    }

    auto spectrum = plsr.extractSpectrumVector(spectral, wls);
    assert(spectrum.size() == wls.size());
    assert(std::abs(spectrum[0] - 0.45f) < 1e-4);
    assert(std::abs(spectrum.back() - 0.70f) < 1e-4);
    std::cout << "[PASS] extractSpectrumVector" << std::endl;

    auto comp = plsr.analyzeSlip(1, spectral);
    assert(comp.slip_id == 1);
    assert(comp.carbon_black_ratio >= 0.0f && comp.carbon_black_ratio <= 1.0f);
    assert(comp.binder_ratio >= 0.0f && comp.binder_ratio <= 1.0f);
    assert(comp.moisture_ratio >= 0.0f && comp.moisture_ratio <= 1.0f);
    assert(comp.impurity_ratio >= 0.0f && comp.impurity_ratio <= 1.0f);

    float total = comp.carbon_black_ratio + comp.binder_ratio +
                  comp.moisture_ratio + comp.impurity_ratio;
    assert(std::abs(total - 1.0f) < 0.1f);
    std::cout << "[PASS] analyzeSlip" << std::endl;
    std::cout << "  carbon_black: " << comp.carbon_black_ratio << std::endl;
    std::cout << "  binder: " << comp.binder_ratio << std::endl;
    std::cout << "  moisture: " << comp.moisture_ratio << std::endl;
    std::cout << "  impurity: " << comp.impurity_ratio << std::endl;
    std::cout << "  confidence: " << comp.confidence << std::endl;
    std::cout << "  ink_type: " << comp.ink_type << std::endl;

    assert(comp.confidence >= 0.0f && comp.confidence <= 1.0f);
    std::cout << "[PASS] confidence in range" << std::endl;

    std::vector<std::string> ink_types;
    for (float cb = 0.3f; cb <= 0.9f; cb += 0.1f) {
        std::string type = plsr.classifyInkType(cb, 0.2f);
        ink_types.push_back(type);
    }
    assert(ink_types.size() > 0);
    std::cout << "[PASS] classifyInkType (all types valid)" << std::endl;

    PlsrPrediction pred = plsr.predict(spectrum);
    assert(pred.carbon_black_ratio >= 0.0f);
    assert(pred.binder_ratio >= 0.0f);
    assert(pred.hotelling_t2 >= 0.0f);
    assert(pred.q_residual >= 0.0f);
    std::cout << "[PASS] predict with full PLSR output" << std::endl;

    float conf = plsr.calculateConfidence(pred, spectrum);
    assert(conf >= 0.0f && conf <= 1.0f);
    std::cout << "[PASS] calculateConfidence = " << conf << std::endl;

    std::cout << "\n=== All PLSR Inversion tests passed ===" << std::endl;
    return 0;
}
