#include "opcua_client.h"
#include <iostream>
#include <random>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>

namespace haihunhou {

OpcUaClient::OpcUaClient(const std::string& host, uint16_t port)
    : host_(host), port_(port), connected_(false), running_(false),
      poll_interval_(OPCUA_POLL_INTERVAL) {
}

OpcUaClient::~OpcUaClient() {
    stopPolling();
    disconnect();
}

void OpcUaClient::setEndpoint(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    host_ = host;
    port_ = port;
}

bool OpcUaClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
    std::cout << "OPC UA Client connected to " << host_ << ":" << port_ << std::endl;
    return true;
}

void OpcUaClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    std::cout << "OPC UA Client disconnected" << std::endl;
}

bool OpcUaClient::isConnected() const {
    return connected_;
}

void OpcUaClient::setSpectralCallback(SpectralCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    spectral_callback_ = callback;
}

void OpcUaClient::setMicrobialCallback(MicrobialCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    microbial_callback_ = callback;
}

bool OpcUaClient::simulateReadSpectral(uint16_t device_id, std::vector<SpectralData>& data) {
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

bool OpcUaClient::simulateReadMicrobial(uint16_t device_id, std::vector<MicrobialData>& data) {
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

bool OpcUaClient::readSpectralData(uint16_t device_id, std::vector<SpectralData>& data) {
    if (!connected_) return false;
    return simulateReadSpectral(device_id, data);
}

bool OpcUaClient::readMicrobialData(uint16_t device_id, std::vector<MicrobialData>& data) {
    if (!connected_) return false;
    return simulateReadMicrobial(device_id, data);
}

bool OpcUaClient::readAllDevices(std::vector<SpectralData>& spectral_data,
                                 std::vector<MicrobialData>& microbial_data) {
    if (!connected_) return false;

    std::cout << "Reading data from all devices..." << std::endl;

    for (uint16_t device_id = 1; device_id <= MSI_DEVICE_COUNT; ++device_id) {
        std::vector<SpectralData> sd;
        if (readSpectralData(device_id, sd)) {
            spectral_data.insert(spectral_data.end(), sd.begin(), sd.end());
        }
    }

    for (uint16_t device_id = 101; device_id < 101 + MC_DEVICE_COUNT; ++device_id) {
        std::vector<MicrobialData> md;
        if (readMicrobialData(device_id, md)) {
            microbial_data.insert(microbial_data.end(), md.begin(), md.end());
        }
    }

    std::cout << "Read " << spectral_data.size() << " spectral records, "
              << microbial_data.size() << " microbial records" << std::endl;

    return !spectral_data.empty() || !microbial_data.empty();
}

void OpcUaClient::pollingLoop() {
    while (running_) {
        if (connected_) {
            std::vector<SpectralData> spectral_data;
            std::vector<MicrobialData> microbial_data;

            if (readAllDevices(spectral_data, microbial_data)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (spectral_callback_ && !spectral_data.empty()) {
                    spectral_callback_(spectral_data);
                }
                if (microbial_callback_ && !microbial_data.empty()) {
                    microbial_callback_(microbial_data);
                }
            }
        }

        uint32_t sleep_interval = poll_interval_;
        if (sleep_interval > 300) {
            sleep_interval = 300;
        }

        for (uint32_t i = 0; i < poll_interval_ && running_; i += sleep_interval) {
            std::this_thread::sleep_for(std::chrono::seconds(sleep_interval));
        }
    }
}

void OpcUaClient::startPolling(uint32_t interval_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;

    poll_interval_ = interval_seconds;
    running_ = true;
    polling_thread_ = std::thread(&OpcUaClient::pollingLoop, this);
    std::cout << "OPC UA polling started, interval: " << interval_seconds << "s" << std::endl;
}

void OpcUaClient::stopPolling() {
    running_ = false;
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
    std::cout << "OPC UA polling stopped" << std::endl;
}

}
