#pragma once
#include "common.h"

namespace haihunhou {

struct IngestMessage {
    enum Type { SPECTRAL = 1, MICROBIAL = 2 } type;
    std::vector<SpectralData> spectral;
    std::vector<MicrobialData> microbial;
};

struct AnalysisMessage {
    enum Type { FADING = 1, MOLD = 2 } type;
    std::vector<FadingAnalysis> fading;
    std::vector<MoldPrediction> mold;
    std::vector<SpectralData> raw_spectral;
    std::vector<MicrobialData> raw_microbial;
};

struct AlertMessage {
    Alert alert;
};

class MessageBus {
public:
    static MessageBus& instance() { static MessageBus b; return b; }
    bool publishIngest(IngestMessage&&) { return true; }
    bool publishAnalysis(AnalysisMessage&&) { return true; }
    bool publishAlert(AlertMessage&&) { return true; }
    bool consumeIngest(IngestMessage&, uint32_t) { return false; }
    bool consumeAnalysis(AnalysisMessage&, uint32_t) { return false; }
    bool consumeAlert(AlertMessage&, uint32_t) { return false; }
    size_t ingestQueueSize() const { return 0; }
    size_t analysisQueueSize() const { return 0; }
    size_t alertQueueSize() const { return 0; }
};

}
