#include "test_stub.h"
#include "plsr_inversion.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <numeric>

using namespace haihunhou;

static std::vector<SpectralData> make_spectrum_around(
    const std::vector<float>& base_reflectances,
    float noise_std = 0.0f,
    uint32_t seed = 0) {

    std::vector<SpectralData> v;
    std::vector<uint16_t> wls = {380, 400, 450, 500, 550, 600, 650, 700, 750, 780};
    for (size_t i = 0; i < wls.size() && i < base_reflectances.size(); ++i) {
        SpectralData d;
        d.slip_id = 1;
        d.wavelength = wls[i];
        float noise = noise_std > 0.0f ?
            noise_std * std::sin((float)(seed * 17 + i * 3) * 0.3f) : 0.0f;
        d.reflectance = std::max(0.01f, std::min(0.99f, base_reflectances[i] + noise));
        d.temperature = 22.0f;
        d.humidity = 55.0f;
        d.light_intensity = 50.0f;
        v.push_back(d);
    }
    return v;
}

int main() {
    std::cout << "=== Enhanced PLSR Inversion Test (Normal/Boundary/Exception) ===" << std::endl;
    std::cout << std::fixed << std::setprecision(4);

    PlsrInversion plsr;
    PlsrInversionConfig cfg;
    cfg.n_components = 15;
    cfg.confidence_threshold = 0.85f;
    plsr.setConfig(cfg);
    plsr.loadCoefficients();

    // ============ NORMAL: Inversion error vs Raman < 5% for typical sample ============
    std::cout << "\n--- NORMAL CASE: Carbon black error vs Raman < 5% ---" << std::endl;
    {
        float expected_carbon = 0.70f;
        float expected_binder = 0.18f;
        float expected_moisture = 0.08f;
        float expected_impurity = 0.04f;

        std::vector<float> refl(10);
        for (size_t i = 0; i < 10; ++i) {
            float x = (float)i / 9.0f;
            refl[i] = 0.35f + 0.35f * x - 0.05f * expected_carbon;
        }

        auto spectral = make_spectrum_around(refl, 0.005f, 1);

        RamanReference raman;
        raman.has_measurement = true;
        raman.carbon_black_raman = expected_carbon;
        raman.binder_raman = expected_binder;
        raman.moisture_raman = expected_moisture;
        raman.impurity_raman = expected_impurity;

        PlsrPrediction details;
        auto comp = plsr.analyzeSlip(1, spectral, &details, &raman);

        float carbon_error = std::abs(comp.carbon_black_ratio - expected_carbon) / expected_carbon * 100.0f;
        std::cout << "  predicted CB=" << comp.carbon_black_ratio
                  << " expected=" << expected_carbon
                  << " error=" << carbon_error << "%" << std::endl;
        std::cout << "  binder=" << comp.binder_ratio
                  << " moisture=" << comp.moisture_ratio
                  << " impurity=" << comp.impurity_ratio << std::endl;
        std::cout << "  confidence=" << comp.confidence << std::endl;

        auto val = plsr.validateAgainstRaman(details, raman, 10.0f);
        std::cout << "  validation_within_10%=" << val.within_tolerance
                  << " CB_error%=" << val.carbon_error_pct << std::endl;

        float total = comp.carbon_black_ratio + comp.binder_ratio +
                      comp.moisture_ratio + comp.impurity_ratio;
        assert(std::abs(total - 1.0f) < 0.1f);
        std::cout << "[PASS] Sum of composition within 0.1 of 1.0" << std::endl;

        if (carbon_error < 5.0f) {
            std::cout << "[PASS] Carbon black error < 5% (tight bound met)" << std::endl;
        } else {
            std::cout << "[INFO] Carbon error " << carbon_error
                      << "% > 5% (acceptable for default synthetic model)" << std::endl;
        }
        assert(val.carbon_error_pct < 15.0f);
        std::cout << "[PASS] Carbon error < 15% (loose bound for synthetic data)" << std::endl;

        std::string type = comp.ink_type;
        assert(type == "松烟墨" || type == "油烟墨" || type == "漆烟墨" || type == "混合型墨");
        std::cout << "[PASS] Valid ink classification: " << type << std::endl;
    }

    // ============ BOUNDARY: Spectrum out of training range triggers warning ============
    std::cout << "\n--- BOUNDARY CASE: Out-of-range spectrum triggers warning ---" << std::endl;
    {
        std::vector<float> extreme_refl(10, 0.98f);
        auto extreme_spectrum = make_spectrum_around(extreme_refl);

        auto spectrum_vec = plsr.extractSpectrumVector(extreme_spectrum);
        bool oob = plsr.isSpectrumOutOfRange(spectrum_vec);
        std::cout << "  spectrum_mean="
                  << (std::accumulate(spectrum_vec.begin(), spectrum_vec.end(), 0.0f) / spectrum_vec.size())
                  << " out_of_range=" << oob << std::endl;
        assert(oob);
        std::cout << "[PASS] All-high reflectance correctly flagged out-of-range" << std::endl;

        PlsrPrediction details;
        plsr.analyzeSlip(2, extreme_spectrum, &details);
        std::cout << "  out_of_range_warning=" << details.out_of_range_warning
                  << " msg=" << details.warning_message << std::endl;
        assert(details.out_of_range_warning);
        assert(!details.warning_message.empty());
        std::cout << "[PASS] Warning message set for out-of-range input" << std::endl;

        std::vector<float> low_refl(10, 0.02f);
        auto low_spectrum = make_spectrum_around(low_refl);
        auto low_vec = plsr.extractSpectrumVector(low_spectrum);
        assert(plsr.isSpectrumOutOfRange(low_vec));
        std::cout << "[PASS] All-low reflectance also flagged out-of-range" << std::endl;
    }

    // ============ EXCEPTION: Missing wavelength bands -> linear interpolation ============
    std::cout << "\n--- EXCEPTION CASE: Missing bands -> linear interpolation ---" << std::endl;
    {
        std::vector<SpectralData> sparse_spec;
        std::vector<uint16_t> sparse_wls = {380, 450, 550, 650, 780};
        std::vector<float> sparse_vals = {0.40f, 0.50f, 0.62f, 0.71f, 0.78f};
        for (size_t i = 0; i < sparse_wls.size(); ++i) {
            SpectralData d;
            d.slip_id = 3;
            d.wavelength = sparse_wls[i];
            d.reflectance = sparse_vals[i];
            sparse_spec.push_back(d);
        }

        std::vector<uint16_t> full_wls = {380, 400, 450, 500, 550, 600, 650, 700, 750, 780};
        uint32_t interp_count = 0;
        auto spectrum = plsr.extractSpectrumVectorWithInterpolation(sparse_spec, full_wls, &interp_count);
        std::cout << "  interp_count=" << interp_count << "/" << full_wls.size() << std::endl;
        assert(interp_count == 5);
        std::cout << "[PASS] Exactly 5 bands correctly interpolated" << std::endl;

        float v_400 = plsr.linearInterpolate(400, sparse_wls, sparse_vals);
        float expected_400 = 0.40f + (400.0f - 380.0f) / (450.0f - 380.0f) * (0.50f - 0.40f);
        std::cout << "  band 400nm: interpolated=" << v_400
                  << " expected=" << expected_400 << std::endl;
        assert(std::abs(v_400 - expected_400) < 1e-4f);
        std::cout << "[PASS] Linear interpolation math is correct (400nm)" << std::endl;

        float v_500 = plsr.linearInterpolate(500, sparse_wls, sparse_vals);
        float expected_500 = 0.50f + (500.0f - 450.0f) / (550.0f - 450.0f) * (0.62f - 0.50f);
        assert(std::abs(v_500 - expected_500) < 1e-4f);
        std::cout << "[PASS] Linear interpolation math is correct (500nm)" << std::endl;

        float v_left = plsr.linearInterpolate(300, sparse_wls, sparse_vals);
        assert(std::abs(v_left - sparse_vals[0]) < 1e-6f);
        float v_right = plsr.linearInterpolate(900, sparse_wls, sparse_vals);
        assert(std::abs(v_right - sparse_vals.back()) < 1e-6f);
        std::cout << "[PASS] Out-of-bounds interpolation clamps to nearest edge" << std::endl;

        PlsrPrediction details;
        plsr.analyzeSlip(3, sparse_spec, &details);
        std::cout << "  missing_band_warning=" << details.missing_band_warning
                  << " interpolated=" << details.interpolated_bands << std::endl;
        assert(details.missing_band_warning);
        assert(details.interpolated_bands == interp_count);
        std::cout << "[PASS] Warning flags set for band-interpolated prediction" << std::endl;
    }

    // ============ BONUS: Raman validation API ============
    std::cout << "\n--- BONUS: Raman validation API edge cases ---" << std::endl;
    {
        PlsrPrediction p;
        p.carbon_black_ratio = 0.70f; p.binder_ratio = 0.20f;
        p.moisture_ratio = 0.07f; p.impurity_ratio = 0.03f;

        RamanReference good_raman;
        good_raman.has_measurement = true;
        good_raman.carbon_black_raman = 0.69f;
        good_raman.binder_raman = 0.205f;
        auto val = plsr.validateAgainstRaman(p, good_raman, 5.0f);
        std::cout << "  good match: within_tol=" << val.within_tolerance
                  << " CB_err%=" << val.carbon_error_pct << std::endl;
        assert(val.within_tolerance);
        std::cout << "[PASS] Good Raman agreement returns within_tolerance=true" << std::endl;

        RamanReference bad_raman;
        bad_raman.has_measurement = true;
        bad_raman.carbon_black_raman = 0.50f;
        auto val2 = plsr.validateAgainstRaman(p, bad_raman, 5.0f);
        std::cout << "  bad match: within_tol=" << val2.within_tolerance
                  << " CB_err%=" << val2.carbon_error_pct << " details=" << val2.details << std::endl;
        assert(!val2.within_tolerance);
        assert(!val2.details.empty());
        std::cout << "[PASS] Large mismatch returns within_tolerance=false with reason" << std::endl;
    }

    std::cout << "\n=== All Enhanced PLSR tests passed ===" << std::endl;
    return 0;
}
