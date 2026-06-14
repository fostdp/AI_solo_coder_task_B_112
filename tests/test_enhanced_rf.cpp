#include "test_stub.h"
#include "rf_corrosion.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <array>

using namespace haihunhou;

static CorrosionFeatures make_features(
    float temperature,
    float humidity,
    float ochratoxin_a,
    float citrinin,
    float patulin,
    float fungi_concentration,
    float total_voc,
    float ph_value) {

    CorrosionFeatures f;
    f.temperature = temperature;
    f.humidity = humidity;
    f.ochratoxin_a = ochratoxin_a;
    f.citrinin = citrinin;
    f.patulin = patulin;
    f.fungi_concentration = fungi_concentration;
    f.total_voc = total_voc;
    f.ph_value = ph_value;
    for (int i = 0; i < 8; ++i) f.has_feature[i] = true;
    return f;
}

static CorrosionFeatures make_missing_features(
    float temperature,
    float humidity,
    float fungi_concentration) {

    CorrosionFeatures f;
    f.temperature = temperature;
    f.humidity = humidity;
    f.fungi_concentration = fungi_concentration;
    f.ochratoxin_a = 0;
    f.citrinin = 0;
    f.patulin = 0;
    f.total_voc = 0;
    f.ph_value = 0;
    f.has_feature[0] = true;   // temperature
    f.has_feature[1] = true;   // humidity
    f.has_feature[2] = false;  // ochratoxin_a (idx 3 actually 实际上的顺序为
    f.has_feature[3] = false;
    f.has_feature[4] = false;
    f.has_feature[5] = true;   // fungi (idx 2 fungi)
    f.has_feature[6] = false;
    f.has_feature[7] = false;
    return f;
}

int main() {
    std::cout << "=== Enhanced RF Corrosion Test (Normal/Boundary/Exception) ===" << std::endl;
    std::cout << std::fixed << std::setprecision(4);

    RfCorrosion rf;
    RfCorrosionConfig cfg;
    cfg.n_estimators = 200;
    cfg.min_corrosion_factor = 1.0f;
    cfg.max_corrosion_factor = 10.0f;
    rf.setConfig(cfg);
    rf.loadModel();

    // ============ NORMAL: High Aflatoxin / OTA concentration -> factor > 1.5 ============
    std::cout << "\n--- NORMAL CASE: High OTA -> factor > 1.5 ---" << std::endl;
    {
        CorrosionFeatures high_ota = make_features(22.0f, 60.0f, 25.0f, 2.0f, 1.0f, 50.0f, 80.0f, 5.5f);
        CorrosionDiagnostics diag;

        float factor = rf.predictCorrosionFactor(high_ota, &diag);

        std::cout << "  predicted_factor=" << factor
                  << " status=" << (int)diag.status
                  << " imputed=" << diag.imputed_feature_count
                  << " is_extreme=" << diag.is_extreme_concentration << std::endl;

        assert(factor > 1.5f);
        std::cout << "[PASS] High OTA (25 ppb) yields corrosion_factor > 1.5" << std::endl;

        CorrosionFeatures high_citrinin = make_features(24.0f, 65.0f, 5.0f, 35.0f, 1.0f, 40.0f, 60.0f, 5.0f);
        CorrosionDiagnostics diag2;
        float factor2 = rf.predictCorrosionFactor(high_citrinin, &diag2);
        std::cout << "  high_citrinin factor=" << factor2 << std::endl;
        assert(factor2 > 1.5f);
        std::cout << "[PASS] High citrinin (35 ppm) yields factor > 1.5" << std::endl;

        CorrosionFeatures extreme_all = make_features(24.0f, 70.0f, 20.0f, 25.0f, 10.0f, 1200.0f, 350.0f, 4.5f);
        CorrosionDiagnostics diag3;
        float factor3 = rf.predictCorrosionFactor(extreme_all, &diag3);
        std::cout << "  extreme_all factor=" << factor3
                  << " is_extreme=" << diag3.is_extreme_concentration
                  << " status=" << (int)diag3.status << std::endl;
        assert(factor3 > 1.5f);
        assert(diag3.is_extreme_concentration);
        assert(diag3.status == RF_WARN_EXTREME_CONCENTRATION);
        std::cout << "[PASS] Extreme concentrations: factor>1.5, is_extreme=true, status=RF_WARN_EXTREME_CONCENTRATION" << std::endl;
    }

    // ============ BOUNDARY: No mold (concentration <= 1.0) -> factor = 1.0 exactly ============
    std::cout << "\n--- BOUNDARY CASE: No mold -> factor = 1.0 exactly ---" << std::endl;
    {
        CorrosionFeatures no_mold = make_features(18.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 6.0f);
        CorrosionDiagnostics diag;

        float factor = rf.predictCorrosionFactor(no_mold, &diag);

        std::cout << "  fungi=0.5 factor=" << factor
                  << " status=" << (int)diag.status << std::endl;

        assert(std::abs(factor - 1.0f) < 1e-6f);
        assert(diag.status == RF_ERR_NO_MOLD);
        std::cout << "[PASS] fungi=0.5: factor exactly 1.0, status=RF_ERR_NO_MOLD" << std::endl;

        CorrosionFeatures edge_mold = make_features(20.0f, 55.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 6.5f);
        CorrosionDiagnostics diag2;
        float factor2 = rf.predictCorrosionFactor(edge_mold, &diag2);
        assert(std::abs(factor2 - 1.0f) < 1e-6f);
        assert(diag2.status == RF_ERR_NO_MOLD);
        std::cout << "[PASS] fungi=1.0 (boundary): factor exactly 1.0, status=RF_ERR_NO_MOLD" << std::endl;

        CorrosionFeatures tiny_mold = make_features(20.0f, 55.0f, 0.0f, 0.0f, 0.0f, 1.5f, 0.0f, 6.0f);
        CorrosionDiagnostics diag3;
        float factor3 = rf.predictCorrosionFactor(tiny_mold, &diag3);
        std::cout << "  fungi=1.5: factor=" << factor3
                  << " status=" << (int)diag3.status << std::endl;
        assert(factor3 >= 1.0f);
        assert(diag3.status == RF_OK);
        std::cout << "[PASS] fungi=1.5 (just above boundary): model prediction applied, status=RF_OK" << std::endl;
    }

    // ============ EXCEPTION: Missing features -> median imputation ============
    std::cout << "\n--- EXCEPTION CASE: Missing features -> median imputation ---" << std::endl;
    {
        auto medians = RfCorrosion::getFeatureMedians();
        std::cout << "  default medians: [";
        for (size_t i = 0; i < 8; ++i) {
            if (i) std::cout << ", ";
            std::cout << medians[i];
        }
        std::cout << "]" << std::endl;
        assert(std::abs(medians[0] - 22.0f) < 1e-6f);  // temperature
        assert(std::abs(medians[1] - 55.0f) < 1e-6f);  // humidity
        assert(std::abs(medians[2] - 30.0f) < 1e-6f);  // ochratoxin_a (index 2
        assert(std::abs(medians[3] - 1.0f) < 1e-6f);   // citrinin (3
        assert(std::abs(medians[4] - 2.0f) < 1e-6f);   // patulin 4
        assert(std::abs(medians[5] - 0.5f) < 1e-6f);   // fungi 5
        assert(std::abs(medians[6] - 40.0f) < 1e-6f);  // total_voc 6
        assert(std::abs(medians[7] - 6.0f) < 1e-6f);   // ph 7
        std::cout << "[PASS] getFeatureMedians returns correct 8 values via const float*" << std::endl;

        CorrosionFeatures partial;
        partial.temperature = 25.0f;
        partial.humidity = 65.0f;
        partial.fungi_concentration = 5.0f;
        partial.ochratoxin_a = 0.0f;  partial.has_feature[2] = false;
        partial.citrinin = 0.0f;        partial.has_feature[3] = false;
        partial.patulin = 0.0f;         partial.has_feature[4] = false;
        partial.total_voc = 0.0f;       partial.has_feature[6] = false;
        partial.ph_value = 0.0f;            partial.has_feature[7] = false;
        partial.has_feature[0] = true; partial.has_feature[1] = true; partial.has_feature[5] = true;

        CorrosionDiagnostics diag;
        float factor = rf.predictCorrosionFactor(partial, &diag);

        std::cout << "  5 features missing -> imputed count=" << diag.imputed_feature_count
                  << " status=" << (int)diag.status << std::endl;
        std::cout << "  imputed names: ";
        for (auto& n : diag.imputed_feature_names) std::cout << n << " ";
        std::cout << std::endl;
        std::cout << "  factor=" << factor << std::endl;
        assert(diag.imputed_feature_count == 5);
        assert(diag.status == RF_WARN_MISSING_FEATURES);
        assert(diag.imputed_feature_names.size() == 5);
        std::cout << "[PASS] 5 missing features imputed with median; status=RF_WARN_MISSING_FEATURES" << std::endl;

        CorrosionFeatures all_missing;
        all_missing.temperature = 0; all_missing.has_feature[0] = false;
        all_missing.humidity = 0; all_missing.has_feature[1] = false;
        all_missing.ochratoxin_a = 0; all_missing.has_feature[2] = false;
        all_missing.citrinin = 0; all_missing.has_feature[3] = false;
        all_missing.patulin = 0; all_missing.has_feature[4] = false;
        all_missing.fungi_concentration = 2.0f; all_missing.has_feature[5] = true;
        all_missing.total_voc = 0; all_missing.has_feature[6] = false;
        all_missing.ph_value = 0; all_missing.has_feature[7] = false;

        CorrosionDiagnostics diag2;
        float factor2 = rf.predictCorrosionFactor(all_missing, &diag2);
        std::cout << "  7 features missing -> imputed=" << diag2.imputed_feature_count
                  << " factor=" << factor2 << std::endl;
        assert(diag2.imputed_feature_count == 7);
        assert(factor2 >= 1.0f);
        std::cout << "[PASS] 7 features missing handled gracefully" << std::endl;

        CorrosionFeatures baseline_full = make_features(
            medians[0], medians[1],
            10.0f, 5.0f, 1.0f,
            5.0f,
            50.0f, medians[7]);
        CorrosionDiagnostics diag3;
        float factor3 = rf.predictCorrosionFactor(baseline_full, &diag3);
        std::cout << "  baseline (medians fungi=5) factor=" << factor3
                  << " status=" << (int)diag3.status << std::endl;
        assert(diag3.status == RF_OK);
        assert(diag3.imputed_feature_count == 0);
        std::cout << "[PASS] Complete baseline features -> RF_OK, 0 imputed" << std::endl;
    }

    // ============ BONUS: Direct imputeMissingFeatures ============
    std::cout << "\n--- BONUS: Direct imputeMissingFeatures API ---" << std::endl;
    {
        CorrosionFeatures f;
        f.temperature = 0; f.has_feature[0] = false;
        f.humidity = 0; f.has_feature[1] = false;
        f.ochratoxin_a = 0; f.has_feature[2] = true;
        f.citrinin = 0; f.has_feature[3] = true;
        f.patulin = 0; f.has_feature[4] = true;
        f.fungi_concentration = 2.0f; f.has_feature[5] = true;
        f.total_voc = 0; f.has_feature[6] = false;
        f.ph_value = 0; f.has_feature[7] = false;
        std::vector<std::string> names;
        auto imputed = rf.imputeMissingFeatures(f, &names);
        std::cout << "  imputed names: ";
        for (auto& n : names) std::cout << n << " ";
        std::cout << " (" << names.size() << " total)" << std::endl;
        assert(names.size() == 4);
        assert(imputed.has_feature[0]);
        assert(imputed.has_feature[1]);
        assert(imputed.has_feature[6]);
        assert(imputed.has_feature[7]);
        assert(std::abs(imputed.temperature - 22.0f) < 1e-6f);
        assert(std::abs(imputed.humidity - 55.0f) < 1e-6f);
        assert(std::abs(imputed.total_voc - 40.0f) < 1e-6f);
        assert(std::abs(imputed.ph_value - 6.0f) < 1e-6f);
        std::cout << "[PASS] imputeMissingFeatures correctly fills 4 missing features" << std::endl;
    }

    // ============ BONUS: isExtremeConcentration ============
    std::cout << "\n--- BONUS: isExtremeConcentration check ---" << std::endl;
    {
        CorrosionFeatures normal = make_features(22.0f, 55.0f, 5.0f, 5.0f, 1.0f, 10.0f, 50.0f, 6.0f);
        CorrosionFeatures big_ota = make_features(22.0f, 55.0f, 25.0f, 5.0f, 1.0f, 10.0f, 50.0f, 6.0f);
        CorrosionFeatures big_voc = make_features(22.0f, 55.0f, 5.0f, 5.0f, 1.0f, 10.0f, 400.0f, 6.0f);
        CorrosionFeatures big_fungi = make_features(22.0f, 55.0f, 5.0f, 5.0f, 1.0f, 2000.0f, 50.0f, 6.0f);

        assert(!rf.isExtremeConcentration(normal));
        assert(rf.isExtremeConcentration(big_ota));
        assert(rf.isExtremeConcentration(big_voc));
        assert(rf.isExtremeConcentration(big_fungi));
        std::cout << "[PASS] isExtremeConcentration classifies 4 cases correctly" << std::endl;
    }

    // ============ BONUS: Feature importance ============
    std::cout << "\n--- BONUS: Feature importance consistency ---" << std::endl;
    {
        auto fi = rf.getFeatureImportance();
        std::cout << "  feature importance count=" << fi.size() << std::endl;
        for (auto& [name, imp] : fi) {
            std::cout << "    " << name << ": " << imp << std::endl;
        }
        assert(fi.size() == 8);
        std::cout << "[PASS] 8 feature importances returned" << std::endl;
    }

    std::cout << "\n=== All Enhanced RF Corrosion tests passed ===" << std::endl;
    return 0;
}
