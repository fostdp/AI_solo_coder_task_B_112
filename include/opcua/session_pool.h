#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <thread>

namespace haihunhou {
namespace opcua {

struct SessionConfig {
    std::string host;
    uint16_t port;
    uint32_t timeout_ms;
    uint32_t max_retries;
    uint32_t keepalive_interval_ms;
    std::string application_uri;
};

class OpcUaSession {
public:
    explicit OpcUaSession(const SessionConfig& config);
    ~OpcUaSession();

    OpcUaSession(const OpcUaSession&) = delete;
    OpcUaSession& operator=(const OpcUaSession&) = delete;

    bool connect();
    void disconnect();
    bool isConnected() const;
    bool isValid() const;

    uint32_t sessionId() const { return session_id_; }
    uint64_t lastUsed() const { return last_used_; }
    uint64_t createdAt() const { return created_at_; }
    uint32_t useCount() const { return use_count_; }

    void touch();

    bool readSpectralData(uint16_t device_id, std::vector<SpectralData>& data);
    bool readMicrobialData(uint16_t device_id, std::vector<MicrobialData>& data);

private:
    SessionConfig config_;
    std::atomic<bool> connected_;
    std::atomic<bool> valid_;
    uint32_t session_id_;
    uint64_t last_used_;
    uint64_t created_at_;
    uint32_t use_count_;
    mutable std::mutex mutex_;
    void* native_session_;

    bool simulateReadSpectral(uint16_t device_id, std::vector<SpectralData>& data);
    bool simulateReadMicrobial(uint16_t device_id, std::vector<MicrobialData>& data);

    static std::atomic<uint32_t> next_session_id_;
};

using OpcUaSessionPtr = std::shared_ptr<OpcUaSession>;

class SessionPool {
public:
    explicit SessionPool(const SessionConfig& config,
                         size_t min_pool_size = 5,
                         size_t max_pool_size = 50);
    ~SessionPool();

    SessionPool(const SessionPool&) = delete;
    SessionPool& operator=(const SessionPool&) = delete;

    OpcUaSessionPtr acquire(uint32_t timeout_ms = 5000);
    void release(OpcUaSessionPtr session);

    void start();
    void stop();

    size_t available() const;
    size_t total() const;
    size_t borrowed() const;

    void setMinPoolSize(size_t size);
    void setMaxPoolSize(size_t size);
    void setIdleTimeoutMs(uint32_t ms);

private:
    SessionConfig config_;
    size_t min_pool_size_;
    size_t max_pool_size_;
    uint32_t idle_timeout_ms_;

    std::queue<OpcUaSessionPtr> available_;
    std::unordered_map<uint32_t, OpcUaSessionPtr> all_sessions_;
    std::atomic<size_t> borrowed_count_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_;

    std::thread maintenance_thread_;

    OpcUaSessionPtr createSession();
    void destroySession(OpcUaSessionPtr session);
    void maintenanceLoop();
};

}
}
