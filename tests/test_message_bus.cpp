#include "common.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace haihunhou;

struct IngestMessage {
    enum Type { SPECTRAL = 1, MICROBIAL = 2 } type;
    std::vector<SpectralData> spectral;
    std::vector<MicrobialData> microbial;
};

struct AnalysisMessage {
    enum Type { FADING = 1, MOLD = 2 } type;
    std::vector<FadingAnalysis> fading;
    std::vector<MoldPrediction> mold;
};

template<typename T, size_t Capacity = 64>
class SimpleQueue {
public:
    bool push(const T& msg) {
        std::unique_lock<std::mutex> lock(mu_);
        if (q_.size() >= Capacity) return false;
        q_.push(msg);
        cv_.notify_one();
        return true;
    }
    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(mu_);
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }
    bool waitPop(T& out, uint32_t timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this]() { return !q_.empty(); })) {
            out = std::move(q_.front());
            q_.pop();
            return true;
        }
        return false;
    }
    size_t size() const {
        std::unique_lock<std::mutex> lock(mu_);
        return q_.size();
    }
private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::queue<T> q_;
};

class MessageBus {
public:
    static MessageBus& instance() { static MessageBus b; return b; }
    bool publishIngest(IngestMessage&& msg) { return ingest_q_.push(msg); }
    bool publishAnalysis(AnalysisMessage&& msg) { return analysis_q_.push(msg); }
    bool consumeIngest(IngestMessage& msg, uint32_t ms = 1000) {
        if (ingest_q_.pop(msg)) return true;
        return ingest_q_.waitPop(msg, ms);
    }
    bool consumeAnalysis(AnalysisMessage& msg, uint32_t ms = 1000) {
        if (analysis_q_.pop(msg)) return true;
        return analysis_q_.waitPop(msg, ms);
    }
private:
    SimpleQueue<IngestMessage, 64> ingest_q_;
    SimpleQueue<AnalysisMessage, 64> analysis_q_;
    MessageBus() = default;
};

int main() {
    std::cout << "=== MessageBus Test ===" << std::endl;

    auto& bus = MessageBus::instance();

    IngestMessage msg;
    msg.type = IngestMessage::SPECTRAL;
    SpectralData sd;
    sd.timestamp = 1000;
    sd.device_id = 1;
    sd.slip_id = 42;
    sd.wavelength = 450;
    sd.reflectance = 0.75f;
    sd.temperature = 22.0f;
    sd.humidity = 55.0f;
    sd.light_intensity = 50.0f;
    msg.spectral.push_back(sd);

    assert(bus.publishIngest(std::move(msg)));
    std::cout << "[PASS] publishIngest" << std::endl;

    IngestMessage received;
    assert(bus.consumeIngest(received, 1000));
    std::cout << "[PASS] consumeIngest" << std::endl;

    assert(received.type == IngestMessage::SPECTRAL);
    assert(received.spectral.size() == 1);
    assert(received.spectral[0].slip_id == 42);
    assert(received.spectral[0].wavelength == 450);
    std::cout << "[PASS] IngestMessage content verification" << std::endl;

    AnalysisMessage amsg;
    amsg.type = AnalysisMessage::FADING;
    FadingAnalysis fa;
    fa.timestamp = 2000;
    fa.slip_id = 42;
    fa.reflectance_450nm = 0.72f;
    fa.fading_rate_monthly = 5.3f;
    fa.risk_level = 2;
    amsg.fading.push_back(fa);

    assert(bus.publishAnalysis(std::move(amsg)));
    std::cout << "[PASS] publishAnalysis" << std::endl;

    AnalysisMessage areceived;
    assert(bus.consumeAnalysis(areceived, 1000));
    std::cout << "[PASS] consumeAnalysis" << std::endl;

    assert(areceived.type == AnalysisMessage::FADING);
    assert(areceived.fading.size() == 1);
    assert(areceived.fading[0].slip_id == 42);
    std::cout << "[PASS] AnalysisMessage content verification" << std::endl;

    std::cout << "\n=== All MessageBus tests passed ===" << std::endl;
    return 0;
}
