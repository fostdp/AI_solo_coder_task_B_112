#include "opcua/session_pool.h"
#include <random>
#include <algorithm>
#include <iostream>

namespace haihunhou {
namespace opcua {

std::atomic<uint32_t> OpcUaSession::next_session_id_{1};

OpcUaSession::OpcUaSession(const SessionConfig& config)
    : config_(config)
    , connected_(false)
    , valid_(false)
    , session_id_(next_session_id_++)
    , last_used_(0)
    , created_at_(0)
    , use_count_(0)
    , native_session_(nullptr) {
    created_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

OpcUaSession::~OpcUaSession() {
    disconnect();
}

bool OpcUaSession::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) return true;

    uint32_t retries = 0;
    while (retries < config_.max_retries) {
        connected_ = true;
        valid_ = true;
        last_used_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        return true;
    }

    return false;
}

void OpcUaSession::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    valid_ = false;
}

bool OpcUaSession::isConnected() const {
    return connected_;
}

bool OpcUaSession::isValid() const {
    if (!valid_ || !connected_) return false;
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return (now - last_used_) < static_cast<uint64_t>(config_.keepalive_interval_ms) * 10;
}

void OpcUaSession::touch() {
    use_count_++;
    last_used_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

bool OpcUaSession::readSpectralData(uint16_t device_id, std::vector<SpectralData>& data) {
    if (!isValid()) return false;
    touch();
    return simulateReadSpectral(device_id, data);
}

bool OpcUaSession::readMicrobialData(uint16_t device_id, std::vector<MicrobialData>& data) {
    if (!isValid()) return false;
    touch();
    return simulateReadMicrobial(device_id, data);
}

bool OpcUaSession::simulateReadSpectral(uint16_t device_id, std::vector<SpectralData>& data) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> reflectance_dist(0.65f, 0.9f);
    std::uniform_real_distribution<float> temp_dist(18.0f, 25.0f);
    std::uniform_real_distribution<float> humidity_dist(45.0f, 65.0f);
    std::uniform_real_distribution<float> light_dist(50.0f, 500.0f);

    uint32_t slips_per_device = 5000 / 20;
    uint32_t start_slip = (device_id - 1) * slips_per_device + 1;
    uint32_t end_slip = std::min(start_slip + slips_per_device, 5001u);

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::vector<uint16_t> wavelengths = {450, 550, 650, 750, 850};

    for (uint32_t slip_id = start_slip; slip_id < end_slip; slip_id++) {
        for (uint16_t wl : wavelengths) {
            SpectralData d;
            d.timestamp = now;
            d.device_id = device_id;
            d.slip_id = slip_id;
            d.wavelength = wl;
            float aging = std::min(0.3f, 0.001f * (slip_id % 100));
            d.reflectance = reflectance_dist(rng) * (1.0f - aging) * (1.0f - 0.0001f * (wl - 450));
            d.temperature = temp_dist(rng);
            d.humidity = humidity_dist(rng);
            d.light_intensity = light_dist(rng);
            data.push_back(d);
        }
    }

    return true;
}

bool OpcUaSession::simulateReadMicrobial(uint16_t device_id, std::vector<MicrobialData>& data) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> fungi_dist(5.0f, 80.0f);
    std::uniform_real_distribution<float> bacteria_dist(10.0f, 120.0f);
    std::uniform_real_distribution<float> temp_dist(18.0f, 25.0f);
    std::uniform_real_distribution<float> humidity_dist(45.0f, 65.0f);

    uint32_t slips_per_device = 5000 / 30;
    uint32_t start_slip = (device_id - 1) * slips_per_device + 1;
    uint32_t end_slip = std::min(start_slip + slips_per_device, 5001u);

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    for (uint32_t slip_id = start_slip; slip_id < end_slip; slip_id++) {
        MicrobialData d;
        d.timestamp = now;
        d.device_id = device_id;
        d.slip_id = slip_id;
        float humidity_factor = (humidity_dist(rng) - 45.0f) / 20.0f;
        d.fungi_concentration = fungi_dist(rng) * (1.0f + humidity_factor);
        d.bacteria_concentration = bacteria_dist(rng) * (1.0f + humidity_factor * 0.5f);
        d.temperature = temp_dist(rng);
        d.humidity = humidity_dist(rng);
        data.push_back(d);
    }

    return true;
}

SessionPool::SessionPool(const SessionConfig& config,
                         size_t min_pool_size,
                         size_t max_pool_size)
    : config_(config)
    , min_pool_size_(min_pool_size)
    , max_pool_size_(max_pool_size)
    , idle_timeout_ms_(300000)
    , borrowed_count_(0)
    , running_(false) {
}

SessionPool::~SessionPool() {
    stop();
}

void SessionPool::start() {
    running_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (all_sessions_.size() < min_pool_size_) {
            auto session = createSession();
            if (session) {
                available_.push(session);
                all_sessions_[session->sessionId()] = session;
            }
        }
    }

    maintenance_thread_ = std::thread(&SessionPool::maintenanceLoop, this);
}

void SessionPool::stop() {
    running_ = false;
    cv_.notify_all();

    if (maintenance_thread_.joinable()) {
        maintenance_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    while (!available_.empty()) {
        available_.pop();
    }
    all_sessions_.clear();
}

OpcUaSessionPtr SessionPool::acquire(uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
        return !available_.empty() || (all_sessions_.size() < max_pool_size_);
    })) {
        return nullptr;
    }

    if (!available_.empty()) {
        auto session = available_.front();
        available_.pop();

        if (session->isValid()) {
            borrowed_count_++;
            return session;
        } else {
            all_sessions_.erase(session->sessionId());
            destroySession(session);
        }
    }

    if (all_sessions_.size() < max_pool_size_) {
        auto session = createSession();
        if (session) {
            all_sessions_[session->sessionId()] = session;
            borrowed_count_++;
            return session;
        }
    }

    return nullptr;
}

void SessionPool::release(OpcUaSessionPtr session) {
    if (!session) return;

    std::lock_guard<std::mutex> lock(mutex_);
    borrowed_count_--;

    if (session->isValid() && running_) {
        available_.push(session);
        cv_.notify_one();
    } else {
        all_sessions_.erase(session->sessionId());
        destroySession(session);
    }
}

size_t SessionPool::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_.size();
}

size_t SessionPool::total() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_sessions_.size();
}

size_t SessionPool::borrowed() const {
    return borrowed_count_.load();
}

void SessionPool::setMinPoolSize(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_pool_size_ = size;
}

void SessionPool::setMaxPoolSize(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_pool_size_ = size;
}

void SessionPool::setIdleTimeoutMs(uint32_t ms) {
    idle_timeout_ms_ = ms;
}

OpcUaSessionPtr SessionPool::createSession() {
    auto session = std::make_shared<OpcUaSession>(config_);
    if (session->connect()) {
        return session;
    }
    return nullptr;
}

void SessionPool::destroySession(OpcUaSessionPtr session) {
    if (session) {
        session->disconnect();
    }
}

void SessionPool::maintenanceLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(30));

        if (!running_) break;

        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        std::queue<OpcUaSessionPtr> valid_sessions;
        while (!available_.empty()) {
            auto session = available_.front();
            available_.pop();

            bool idle_too_long = (now - session->lastUsed()) > idle_timeout_ms_;
            bool too_many_sessions = all_sessions_.size() > min_pool_size_;

            if (!session->isValid() || (idle_too_long && too_many_sessions)) {
                all_sessions_.erase(session->sessionId());
                destroySession(session);
            } else {
                valid_sessions.push(session);
            }
        }
        available_ = std::move(valid_sessions);

        while (all_sessions_.size() < min_pool_size_) {
            auto session = createSession();
            if (session) {
                available_.push(session);
                all_sessions_[session->sessionId()] = session;
            } else {
                break;
            }
        }
    }
}

}
}
