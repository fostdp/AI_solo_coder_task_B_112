#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace haihunhou {

using SpectralCallback = std::function<void(const std::vector<SpectralData>&)>;
using MicrobialCallback = std::function<void(const std::vector<MicrobialData>&)>;

class OpcUaClient {
public:
    OpcUaClient(const std::string& host = "127.0.0.1", uint16_t port = 4840);
    ~OpcUaClient();

    bool connect();
    void disconnect();
    bool isConnected() const;

    void setSpectralCallback(SpectralCallback callback);
    void setMicrobialCallback(MicrobialCallback callback);

    void startPolling(uint32_t interval_seconds = OPCUA_POLL_INTERVAL);
    void stopPolling();

    bool readSpectralData(uint16_t device_id, std::vector<SpectralData>& data);
    bool readMicrobialData(uint16_t device_id, std::vector<MicrobialData>& data);

    bool readAllDevices(std::vector<SpectralData>& spectral_data,
                        std::vector<MicrobialData>& microbial_data);

    void setEndpoint(const std::string& host, uint16_t port);

private:
    std::string host_;
    uint16_t port_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    std::thread polling_thread_;
    SpectralCallback spectral_callback_;
    MicrobialCallback microbial_callback_;
    mutable std::mutex mutex_;

    uint32_t poll_interval_;

    void pollingLoop();
    bool simulateReadSpectral(uint16_t device_id, std::vector<SpectralData>& data);
    bool simulateReadMicrobial(uint16_t device_id, std::vector<MicrobialData>& data);
};

}
