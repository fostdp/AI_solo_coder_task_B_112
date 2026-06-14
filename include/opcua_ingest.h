#pragma once
#include "common.h"
#include "clickhouse_client.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>

namespace haihunhou {

class MessageBus;

struct OpcUaSession {
    uint16_t device_id;
    std::string endpoint;
    uint64_t last_used_ms;
    uint64_t created_ms;
    uint32_t use_count;
    bool valid;
};

class OpcUaIngest {
public:
    OpcUaIngest(ClickHouseClient& db, MessageBus& bus);
    ~OpcUaIngest();

    void setEndpoint(const std::string& host, uint16_t port);
    void setPollInterval(uint32_t interval_sec);
    void setSimulateMode(bool simulate);

    bool start();
    void stop();
    bool isRunning() const;

    void ingestSpectralData(const std::vector<SpectralData>& data);
    void ingestMicrobialData(const std::vector<MicrobialData>& data);

private:
    ClickHouseClient& db_;
    MessageBus& bus_;
    std::string host_;
    uint16_t port_;
    uint32_t poll_interval_;
    bool simulate_;
    std::atomic<bool> running_;
    std::thread polling_thread_;
    std::thread ingest_thread_;
    std::mutex session_mutex_;

    std::unordered_map<uint16_t, OpcUaSession> session_pool_;
    uint32_t min_pool_size_;
    uint32_t max_pool_size_;

    void pollingLoop();
    void ingestLoop();
    OpcUaSession acquireSession(uint16_t device_id);
    void releaseSession(uint16_t device_id);
    void maintainSessions();
    bool readDeviceSpectral(uint16_t device_id, std::vector<SpectralData>& data);
    bool readDeviceMicrobial(uint16_t device_id, std::vector<MicrobialData>& data);
    bool readAllDevices(std::vector<SpectralData>& spectral, std::vector<MicrobialData>& microbial);
    bool simulateSpectral(uint16_t device_id, std::vector<SpectralData>& data);
    bool simulateMicrobial(uint16_t device_id, std::vector<MicrobialData>& data);

    struct BatchBuffer {
        std::vector<SpectralData> spectral;
        std::vector<MicrobialData> microbial;
        std::chrono::steady_clock::time_point last_flush;
    } batch_;
    std::mutex batch_mutex_;
    void flushBatch();
};

}
