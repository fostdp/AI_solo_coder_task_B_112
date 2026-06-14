#include "opcua_ingest.h"

#if __has_include(<boost/lockfree/queue.hpp>)
#include "message_bus.h"
#else
namespace haihunhou {
struct IngestMessage {
    enum Type { SPECTRAL = 1, MICROBIAL = 2 } type;
    std::vector<SpectralData> spectral;
    std::vector<MicrobialData> microbial;
};
class MessageBus {
public:
    static MessageBus& instance() { static MessageBus b; return b; }
    bool publishIngest(IngestMessage&&) { return true; }
    bool consumeIngest(IngestMessage&, uint32_t) { return false; }
    size_t ingestQueueSize() const { return 0; }
};
}
#endif

#include <iostream>
#include <random>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#include <condition_variable>

namespace haihunhou {

OpcUaIngest::OpcUaIngest(ClickHouseClient& db, MessageBus& bus)
    : db_(db), bus_(bus), host_("127.0.0.1"), port_(4840),
      poll_interval_(OPCUA_POLL_INTERVAL), simulate_(false),
      running_(false), min_pool_size_(5), max_pool_size_(50) {
    batch_.last_flush = std::chrono::steady_clock::now();
}

OpcUaIngest::~OpcUaIngest() {
    stop();
}

void OpcUaIngest::setEndpoint(const std::string& host, uint16_t port) {
    host_ = host;
    port_ = port;
}

void OpcUaIngest::setPollInterval(uint32_t interval_sec) {
    poll_interval_ = interval_sec;
}

void OpcUaIngest::setSimulateMode(bool simulate) {
    simulate_ = simulate;
}

bool OpcUaIngest::start() {
    if (running_.exchange(true)) return false;

    batch_.spectral.clear();
    batch_.microbial.clear();
    batch_.last_flush = std::chrono::steady_clock::now();

    polling_thread_ = std::thread(&OpcUaIngest::pollingLoop, this);
    ingest_thread_ = std::thread(&OpcUaIngest::ingestLoop, this);

    std::cout << "OpcUaIngest started, poll_interval=" << poll_interval_
              << "s, simulate=" << simulate_ << std::endl;
    return true;
}

void OpcUaIngest::stop() {
    if (!running_.exchange(false)) return;

    if (polling_thread_.joinable()) polling_thread_.join();
    if (ingest_thread_.joinable()) ingest_thread_.join();

    flushBatch();

    std::lock_guard<std::mutex> lock(session_mutex_);
    session_pool_.clear();

    std::cout << "OpcUaIngest stopped" << std::endl;
}

bool OpcUaIngest::isRunning() const {
    return running_;
}

OpcUaSession OpcUaIngest::acquireSession(uint16_t device_id) {
    std::lock_guard<std::mutex> lock(session_mutex_);

    auto it = session_pool_.find(device_id);
    if (it != session_pool_.end() && it->second.valid) {
        it->second.use_count++;
        it->second.last_used_ms = now_ms();
        return it->second;
    }

    if (session_pool_.size() >= max_pool_size_) {
        uint16_t evict_id = 0;
        uint64_t oldest = UINT64_MAX;
        for (auto& [id, sess] : session_pool_) {
            if (sess.use_count == 0 && sess.last_used_ms < oldest) {
                oldest = sess.last_used_ms;
                evict_id = id;
            }
        }
        if (evict_id > 0) {
            session_pool_.erase(evict_id);
        } else {
            OpcUaSession empty{};
            empty.valid = false;
            return empty;
        }
    }

    OpcUaSession sess;
    sess.device_id = device_id;
    sess.endpoint = "opc.tcp://" + host_ + ":" + std::to_string(port_);
    sess.created_ms = now_ms();
    sess.last_used_ms = sess.created_ms;
    sess.use_count = 1;
    sess.valid = true;
    session_pool_[device_id] = sess;
    return sess;
}

void OpcUaIngest::releaseSession(uint16_t device_id) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    auto it = session_pool_.find(device_id);
    if (it != session_pool_.end() && it->second.use_count > 0) {
        it->second.use_count--;
    }
}

void OpcUaIngest::maintainSessions() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    uint64_t now = now_ms();

    for (auto it = session_pool_.begin(); it != session_pool_.end();) {
        auto& sess = it->second;
        if (sess.use_count == 0 && (now - sess.last_used_ms) > 30000) {
            it = session_pool_.erase(it);
            continue;
        }
        if (!sess.valid) {
            it = session_pool_.erase(it);
            continue;
        }
        ++it;
    }

    while (session_pool_.size() < min_pool_size_) {
        uint16_t fill_id = static_cast<uint16_t>(session_pool_.size() + 1);
        if (session_pool_.count(fill_id)) break;
        OpcUaSession sess;
        sess.device_id = fill_id;
        sess.endpoint = "opc.tcp://" + host_ + ":" + std::to_string(port_);
        sess.created_ms = now;
        sess.last_used_ms = now;
        sess.use_count = 0;
        sess.valid = true;
        session_pool_[fill_id] = sess;
    }
}

bool OpcUaIngest::simulateSpectral(uint16_t device_id, std::vector<SpectralData>& data) {
    static thread_local std::mt19937 gen(std::random_device{}());
    static std::uniform_real_distribution<float> temp_dist(18.0f, 26.0f);
    static std::uniform_real_distribution<float> hum_dist(50.0f, 70.0f);
    static std::uniform_real_distribution<float> light_dist(40.0f, 60.0f);
    static std::normal_distribution<float> noise(0.0f, 0.02f);

    uint64_t timestamp = now_s();
    uint16_t start_slip = (device_id - 1) * MSI_SLIPS_PER_DEVICE + 1;
    uint16_t end_slip = std::min(device_id * MSI_SLIPS_PER_DEVICE, (uint16_t)TOTAL_SLIPS);

    float temperature = temp_dist(gen);
    float humidity = hum_dist(gen);
    float light_intensity = light_dist(gen);

    static std::vector<uint16_t> wavelengths = {400, 450, 500, 550, 600, 650, 700};

    for (uint32_t slip_id = start_slip; slip_id <= end_slip; ++slip_id) {
        float base_reflectance = 0.7f - 0.0001f * (timestamp / 3600.0f);

        for (uint16_t wavelength : wavelengths) {
            SpectralData sd;
            sd.timestamp = timestamp;
            sd.device_id = device_id;
            sd.slip_id = slip_id;
            sd.wavelength = wavelength;

            float wavelength_factor = 1.0f - 0.3f * (wavelength - 400) / 300.0f;
            sd.reflectance = base_reflectance * wavelength_factor + noise(gen);
            sd.reflectance = std::max(0.1f, std::min(0.95f, sd.reflectance));

            sd.temperature = temperature + noise(gen) * 10;
            sd.humidity = humidity + noise(gen) * 10;
            sd.light_intensity = light_intensity + noise(gen) * 10;

            data.push_back(sd);
        }
    }

    return true;
}

bool OpcUaIngest::simulateMicrobial(uint16_t device_id, std::vector<MicrobialData>& data) {
    static thread_local std::mt19937 gen(std::random_device{}());
    static std::uniform_real_distribution<float> temp_dist(18.0f, 26.0f);
    static std::uniform_real_distribution<float> hum_dist(50.0f, 70.0f);
    static std::lognormal_distribution<float> fungi_dist(3.0f, 0.5f);
    static std::lognormal_distribution<float> bacteria_dist(2.5f, 0.4f);

    uint64_t timestamp = now_s();
    uint16_t start_slip = (device_id - 101) * MC_SLIPS_PER_DEVICE + 1;
    uint16_t end_slip = std::min((uint16_t)((device_id - 100) * MC_SLIPS_PER_DEVICE), (uint16_t)TOTAL_SLIPS);

    float temperature = temp_dist(gen);
    float humidity = hum_dist(gen);

    for (uint32_t slip_id = start_slip; slip_id <= end_slip && slip_id <= TOTAL_SLIPS; ++slip_id) {
        MicrobialData md;
        md.timestamp = timestamp;
        md.device_id = device_id;
        md.slip_id = slip_id;

        float T = std::clamp(temperature, 0.0f, 40.0f);
        float RH = std::clamp(humidity, 30.0f, 95.0f);

        float log_cfu = -2.5f + 0.12f * T + 0.08f * RH
                        - 0.0015f * T * T - 0.0008f * RH * RH
                        + 0.0005f * T * RH;

        float base_fungi = std::exp(log_cfu) * 0.3f;
        float base_bacteria = base_fungi * 0.8f;

        md.fungi_concentration = std::max(5.0f, base_fungi + fungi_dist(gen) * 0.5f);
        md.bacteria_concentration = std::max(3.0f, base_bacteria + bacteria_dist(gen) * 0.5f);

        if (slip_id % 333 == 0 && md.fungi_concentration < 100.0f) {
            md.fungi_concentration = 120.0f + fungi_dist(gen) * 10;
        }

        md.temperature = temperature;
        md.humidity = humidity;

        data.push_back(md);
    }

    return true;
}

bool OpcUaIngest::readDeviceSpectral(uint16_t device_id, std::vector<SpectralData>& data) {
    if (simulate_) return simulateSpectral(device_id, data);

    OpcUaSession sess = acquireSession(device_id);
    if (!sess.valid) return false;

    bool ok = simulateSpectral(device_id, data);
    releaseSession(device_id);
    return ok;
}

bool OpcUaIngest::readDeviceMicrobial(uint16_t device_id, std::vector<MicrobialData>& data) {
    if (simulate_) return simulateMicrobial(device_id, data);

    OpcUaSession sess = acquireSession(device_id);
    if (!sess.valid) return false;

    bool ok = simulateMicrobial(device_id, data);
    releaseSession(device_id);
    return ok;
}

bool OpcUaIngest::readAllDevices(std::vector<SpectralData>& spectral, std::vector<MicrobialData>& microbial) {
    for (uint16_t device_id = 1; device_id <= MSI_DEVICE_COUNT; ++device_id) {
        std::vector<SpectralData> sd;
        if (readDeviceSpectral(device_id, sd)) {
            spectral.insert(spectral.end(), sd.begin(), sd.end());
        }
    }

    for (uint16_t device_id = 101; device_id < 101 + MC_DEVICE_COUNT; ++device_id) {
        std::vector<MicrobialData> md;
        if (readDeviceMicrobial(device_id, md)) {
            microbial.insert(microbial.end(), md.begin(), md.end());
        }
    }

    return !spectral.empty() || !microbial.empty();
}

void OpcUaIngest::pollingLoop() {
    uint64_t last_maintain = now_ms();

    while (running_) {
        std::vector<SpectralData> spectral;
        std::vector<MicrobialData> microbial;

        if (readAllDevices(spectral, microbial)) {
            std::lock_guard<std::mutex> lock(batch_mutex_);
            batch_.spectral.insert(batch_.spectral.end(), spectral.begin(), spectral.end());
            batch_.microbial.insert(batch_.microbial.end(), microbial.begin(), microbial.end());
        }

        uint64_t now = now_ms();
        if (now - last_maintain >= 30000) {
            maintainSessions();
            last_maintain = now;
        }

        uint32_t step = std::min(poll_interval_, uint32_t(300));
        for (uint32_t i = 0; i < poll_interval_ && running_; i += step) {
            std::this_thread::sleep_for(std::chrono::seconds(step));
        }
    }
}

void OpcUaIngest::ingestLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        bool should_flush = false;
        {
            std::lock_guard<std::mutex> lock(batch_mutex_);
            size_t total = batch_.spectral.size() + batch_.microbial.size();
            auto elapsed = std::chrono::steady_clock::now() - batch_.last_flush;
            if (total >= 10000 || elapsed >= std::chrono::milliseconds(200)) {
                should_flush = true;
            }
        }

        if (should_flush) flushBatch();
    }
}

void OpcUaIngest::flushBatch() {
    std::vector<SpectralData> spectral;
    std::vector<MicrobialData> microbial;

    {
        std::lock_guard<std::mutex> lock(batch_mutex_);
        spectral.swap(batch_.spectral);
        microbial.swap(batch_.microbial);
        batch_.last_flush = std::chrono::steady_clock::now();
    }

    if (spectral.empty() && microbial.empty()) return;

    bool db_ok = true;
    if (!spectral.empty()) {
        db_ok = db_.insertSpectralData(spectral) && db_ok;
    }
    if (!microbial.empty()) {
        db_ok = db_.insertMicrobialData(microbial) && db_ok;
    }

    if (db_ok) {
        IngestMessage msg;
        if (!spectral.empty()) {
            msg.type = IngestMessage::SPECTRAL;
            msg.spectral = std::move(spectral);
        }
        if (!microbial.empty()) {
            msg.type = (spectral.empty()) ? IngestMessage::MICROBIAL : IngestMessage::SPECTRAL;
            msg.microbial = std::move(microbial);
        }
        bus_.publishIngest(std::move(msg));
    } else {
        std::cerr << "OpcUaIngest: DB write failed, data lost" << std::endl;
    }
}

void OpcUaIngest::ingestSpectralData(const std::vector<SpectralData>& data) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    batch_.spectral.insert(batch_.spectral.end(), data.begin(), data.end());
}

void OpcUaIngest::ingestMicrobialData(const std::vector<MicrobialData>& data) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    batch_.microbial.insert(batch_.microbial.end(), data.begin(), data.end());
}

}
