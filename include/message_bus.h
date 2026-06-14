#pragma once
#include "common.h"
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <atomic>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>

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
    static MessageBus& instance() {
        static MessageBus bus;
        return bus;
    }

    bool publishIngest(IngestMessage&& msg) {
        bool ok = ingest_queue_.push(std::move(msg));
        if (ok) ingest_cv_.notify_one();
        return ok;
    }

    bool publishAnalysis(AnalysisMessage&& msg) {
        bool ok = analysis_queue_.push(std::move(msg));
        if (ok) analysis_cv_.notify_one();
        return ok;
    }

    bool publishAlert(AlertMessage&& msg) {
        bool ok = alert_queue_.push(std::move(msg));
        if (ok) alert_cv_.notify_one();
        return ok;
    }

    bool consumeIngest(IngestMessage& msg, uint32_t timeout_ms = 1000) {
        if (ingest_queue_.pop(msg)) return true;
        std::unique_lock<std::mutex> lock(ingest_mutex_);
        return ingest_cv_.wait_for(lock,
            std::chrono::milliseconds(timeout_ms),
            [this]() { return ingest_queue_.pop(msg); });
    }

    bool consumeAnalysis(AnalysisMessage& msg, uint32_t timeout_ms = 1000) {
        if (analysis_queue_.pop(msg)) return true;
        std::unique_lock<std::mutex> lock(analysis_mutex_);
        return analysis_cv_.wait_for(lock,
            std::chrono::milliseconds(timeout_ms),
            [this]() { return analysis_queue_.pop(msg); });
    }

    bool consumeAlert(AlertMessage& msg, uint32_t timeout_ms = 1000) {
        if (alert_queue_.pop(msg)) return true;
        std::unique_lock<std::mutex> lock(alert_mutex_);
        return alert_cv_.wait_for(lock,
            std::chrono::milliseconds(timeout_ms),
            [this]() { return alert_queue_.pop(msg); });
    }

    size_t ingestQueueSize() const { return ingest_queue_.read_available(); }
    size_t analysisQueueSize() const { return analysis_queue_.read_available(); }
    size_t alertQueueSize() const { return alert_queue_.read_available(); }

private:
    MessageBus() = default;

    boost::lockfree::queue<IngestMessage, boost::lockfree::capacity<64>> ingest_queue_;
    boost::lockfree::queue<AnalysisMessage, boost::lockfree::capacity<64>> analysis_queue_;
    boost::lockfree::queue<AlertMessage, boost::lockfree::capacity<256>> alert_queue_;

    std::mutex ingest_mutex_;
    std::condition_variable ingest_cv_;
    std::mutex analysis_mutex_;
    std::condition_variable analysis_cv_;
    std::mutex alert_mutex_;
    std::condition_variable alert_cv_;
};

}
