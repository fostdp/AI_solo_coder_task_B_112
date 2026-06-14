#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <memory>
#include <cctype>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

static std::atomic<bool> g_running(true);

struct SimulatorConfig {
    uint32_t total_slips = 5000;
    uint16_t msi_device_count = 20;
    uint16_t mc_device_count = 30;
    uint16_t msi_slips_per_device = 250;
    uint16_t mc_slips_per_device = 167;

    std::string server_host = "127.0.0.1";
    uint16_t server_port = 8080;
    uint32_t report_interval = 21600;
    bool fast_mode = false;

    bool inject_fading = false;
    float inject_rate = 100.0f;
    std::vector<uint32_t> inject_slip_ids;

    bool mqtt_enabled = false;
    std::string mqtt_broker = "emqx:1883";
    std::string mqtt_topic_template = "haihunhou/device/{device_id}/data";

    bool use_mqtt_webhook = false;
    std::string mqtt_webhook_url = "/mqtt/publish";
};

SimulatorConfig g_config;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

uint64_t now_s() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string getEnvStr(const char* name, const std::string& default_val) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : default_val;
}

int getEnvInt(const char* name, int default_val) {
    const char* val = std::getenv(name);
    if (!val) return default_val;
    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}

float getEnvFloat(const char* name, float default_val) {
    const char* val = std::getenv(name);
    if (!val) return default_val;
    try {
        return std::stof(val);
    } catch (...) {
        return default_val;
    }
}

bool getEnvBool(const char* name, bool default_val) {
    const char* val = std::getenv(name);
    if (!val) return default_val;
    std::string s(val);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

std::vector<uint32_t> parseSlipIds(const std::string& str) {
    std::vector<uint32_t> ids;
    if (str.empty()) return ids;
    std::istringstream iss(str);
    std::string token;
    while (std::getline(iss, token, ',')) {
        try {
            ids.push_back(static_cast<uint32_t>(std::stoul(token)));
        } catch (...) {
        }
    }
    return ids;
}

void loadConfigFromEnv() {
    g_config.total_slips = static_cast<uint32_t>(getEnvInt("TOTAL_SLIPS", 5000));
    g_config.msi_device_count = static_cast<uint16_t>(getEnvInt("MSI_DEVICES", 20));
    g_config.mc_device_count = static_cast<uint16_t>(getEnvInt("MC_DEVICES", 30));

    if (g_config.msi_device_count > 0) {
        g_config.msi_slips_per_device = static_cast<uint16_t>(
            (g_config.total_slips + g_config.msi_device_count - 1) / g_config.msi_device_count);
    }
    if (g_config.mc_device_count > 0) {
        g_config.mc_slips_per_device = static_cast<uint16_t>(
            (g_config.total_slips + g_config.mc_device_count - 1) / g_config.mc_device_count);
    }

    g_config.report_interval = static_cast<uint32_t>(getEnvInt("REPORT_INTERVAL", 21600));
    g_config.fast_mode = getEnvBool("FAST_MODE", false);
    if (g_config.fast_mode) {
        g_config.report_interval = 30;
    }

    g_config.inject_fading = getEnvBool("INJECT_FADING", false);
    g_config.inject_rate = getEnvFloat("INJECT_RATE", 100.0f);
    g_config.inject_slip_ids = parseSlipIds(getEnvStr("INJECT_SLIP_ID", ""));

    g_config.mqtt_enabled = getEnvBool("MQTT_ENABLED", false);
    g_config.mqtt_broker = getEnvStr("MQTT_BROKER", "emqx:1883");
    g_config.mqtt_topic_template = getEnvStr("MQTT_TOPIC", "haihunhou/device/{device_id}/data");

    g_config.use_mqtt_webhook = getEnvBool("MQTT_USE_WEBHOOK", true);
    g_config.mqtt_webhook_url = getEnvStr("MQTT_WEBHOOK_PATH", "/mqtt/publish");
}

std::string httpPostJson(const std::string& host, uint16_t port,
                         const std::string& path, const std::string& json_body) {
    try {
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve(host, std::to_string(port));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::post, path, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(http::field::content_type, "application/json");
        req.body() = json_body;
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return res.body();
    } catch (std::exception const& e) {
        std::cerr << "HTTP POST error: " << e.what() << std::endl;
        return "";
    }
}

float getFadingMultiplier(uint32_t slip_id) {
    float multiplier = 1.0f;

    if (g_config.inject_fading) {
        multiplier *= g_config.inject_rate;

        if (slip_id % 100 == 0) {
            static thread_local std::mt19937 gen(std::random_device{}());
            static std::uniform_real_distribution<float> extra_dist(0.05f, 0.10f);
            multiplier *= (1.0f + extra_dist(gen));
        }

        for (uint32_t id : g_config.inject_slip_ids) {
            if (id == slip_id) {
                multiplier *= 2.0f;
                break;
            }
        }
    }

    return multiplier;
}

void generateSpectralData(uint16_t device_id, std::ostringstream& json) {
    static thread_local std::mt19937 gen(std::random_device{}());
    static std::uniform_real_distribution<float> temp_dist(18.0f, 26.0f);
    static std::uniform_real_distribution<float> hum_dist(50.0f, 70.0f);
    static std::uniform_real_distribution<float> light_dist(40.0f, 60.0f);
    static std::normal_distribution<float> noise(0.0f, 0.02f);

    uint64_t timestamp = now_s();
    uint32_t start_slip = (device_id - 1) * g_config.msi_slips_per_device + 1;
    uint32_t end_slip = std::min(
        static_cast<uint32_t>(device_id * g_config.msi_slips_per_device),
        g_config.total_slips);

    float temperature = temp_dist(gen);
    float humidity = hum_dist(gen);
    float light_intensity = light_dist(gen);

    std::vector<uint16_t> wavelengths = {400, 450, 500, 550, 600, 650, 700};

    bool first = true;
    for (uint32_t slip_id = start_slip; slip_id <= end_slip; ++slip_id) {
        float fade_mult = getFadingMultiplier(slip_id);
        float base_reflectance = 0.7f - 0.0001f * fade_mult * (timestamp / 3600.0f);
        base_reflectance = std::max(0.1f, base_reflectance);

        for (uint16_t wavelength : wavelengths) {
            float wavelength_factor = 1.0f - 0.3f * (wavelength - 400) / 300.0f;
            float reflectance = base_reflectance * wavelength_factor + noise(gen);
            reflectance = std::max(0.1f, std::min(0.95f, reflectance));

            if (!first) json << ",";
            json << "{"
                 << "\"timestamp\":" << timestamp << ","
                 << "\"device_id\":" << device_id << ","
                 << "\"slip_id\":" << slip_id << ","
                 << "\"wavelength\":" << wavelength << ","
                 << "\"reflectance\":" << std::fixed << std::setprecision(6) << reflectance << ","
                 << "\"temperature\":" << std::fixed << std::setprecision(2) << (temperature + noise(gen) * 10) << ","
                 << "\"humidity\":" << std::fixed << std::setprecision(2) << (humidity + noise(gen) * 10) << ","
                 << "\"light_intensity\":" << std::fixed << std::setprecision(2) << (light_intensity + noise(gen) * 10)
                 << "}";
            first = false;
        }
    }
}

void generateMicrobialData(uint16_t device_id, std::ostringstream& json) {
    static thread_local std::mt19937 gen(std::random_device{}());
    static std::uniform_real_distribution<float> temp_dist(18.0f, 26.0f);
    static std::uniform_real_distribution<float> hum_dist(50.0f, 70.0f);
    static std::lognormal_distribution<float> fungi_dist(3.0f, 0.5f);
    static std::lognormal_distribution<float> bacteria_dist(2.5f, 0.4f);

    uint64_t timestamp = now_s();
    uint32_t start_slip = (device_id - 101) * g_config.mc_slips_per_device + 1;
    uint32_t end_slip = std::min(
        static_cast<uint32_t>((device_id - 100) * g_config.mc_slips_per_device),
        g_config.total_slips);

    float temperature = temp_dist(gen);
    float humidity = hum_dist(gen);

    bool first = true;
    for (uint32_t slip_id = start_slip; slip_id <= end_slip && slip_id <= g_config.total_slips; ++slip_id) {
        float fade_mult = getFadingMultiplier(slip_id);

        float T = std::clamp(temperature, 0.0f, 40.0f);
        float RH = std::clamp(humidity, 30.0f, 95.0f);

        float log_cfu = -2.5f + 0.12f * T + 0.08f * RH
                        - 0.0015f * T * T - 0.0008f * RH * RH
                        + 0.0005f * T * RH;

        float base_fungi = std::exp(log_cfu) * 0.3f * (1.0f + 0.001f * fade_mult * (timestamp / 3600.0f));
        float base_bacteria = base_fungi * 0.8f;

        float fungi = std::max(5.0f, base_fungi + fungi_dist(gen) * 0.5f);
        float bacteria = std::max(3.0f, base_bacteria + bacteria_dist(gen) * 0.5f);

        if (slip_id % 333 == 0 && fungi < 100.0f) {
            fungi = 120.0f + fungi_dist(gen) * 10;
        }

        if (!first) json << ",";
        json << "{"
             << "\"timestamp\":" << timestamp << ","
             << "\"device_id\":" << device_id << ","
             << "\"slip_id\":" << slip_id << ","
             << "\"fungi_concentration\":" << std::fixed << std::setprecision(2) << fungi << ","
             << "\"bacteria_concentration\":" << std::fixed << std::setprecision(2) << bacteria << ","
             << "\"temperature\":" << std::fixed << std::setprecision(2) << temperature << ","
             << "\"humidity\":" << std::fixed << std::setprecision(2) << humidity
             << "}";
        first = false;
    }
}

class IMqttSender {
public:
    virtual ~IMqttSender() = default;
    virtual bool publish(const std::string& topic, const std::string& payload) = 0;
    virtual bool isConnected() const = 0;
};

class MqttWebhookSender : public IMqttSender {
public:
    MqttWebhookSender(const std::string& host, uint16_t port, const std::string& webhook_path)
        : host_(host), port_(port), webhook_path_(webhook_path) {}

    bool publish(const std::string& topic, const std::string& payload) override {
        std::ostringstream json;
        json << "{"
             << "\"topic\":\"" << topic << "\","
             << "\"payload\":" << payload << ","
             << "\"qos\":0,"
             << "\"retain\":false"
             << "}";

        std::string response = httpPostJson(host_, port_, webhook_path_, json.str());
        return !response.empty();
    }

    bool isConnected() const override { return true; }

private:
    std::string host_;
    uint16_t port_;
    std::string webhook_path_;
};

std::string buildTopic(uint16_t device_id, const std::string& type) {
    std::string topic = g_config.mqtt_topic_template;
    size_t pos = topic.find("{device_id}");
    if (pos != std::string::npos) {
        topic.replace(pos, 11, std::to_string(device_id));
    }
    if (!type.empty()) {
        topic += "/" + type;
    }
    return topic;
}

void sendSpectralDataHttp(const std::string& host, uint16_t port) {
    std::ostringstream json;
    json << "[";

    for (uint16_t device_id = 1; device_id <= g_config.msi_device_count; ++device_id) {
        if (device_id > 1) json << ",";
        generateSpectralData(device_id, json);
    }

    json << "]";

    std::string response = httpPostJson(host, port, "/api/v1/ingest/spectral", json.str());
    if (!response.empty()) {
        std::cout << "[" << now_s() << "] Spectral data sent via HTTP successfully" << std::endl;
    }
}

void sendMicrobialDataHttp(const std::string& host, uint16_t port) {
    std::ostringstream json;
    json << "[";

    for (uint16_t device_id = 101; device_id < 101 + g_config.mc_device_count; ++device_id) {
        if (device_id > 101) json << ",";
        generateMicrobialData(device_id, json);
    }

    json << "]";

    std::string response = httpPostJson(host, port, "/api/v1/ingest/microbial", json.str());
    if (!response.empty()) {
        std::cout << "[" << now_s() << "] Microbial data sent via HTTP successfully" << std::endl;
    }
}

void sendSpectralDataMqtt(IMqttSender& sender) {
    int sent_count = 0;
    for (uint16_t device_id = 1; device_id <= g_config.msi_device_count; ++device_id) {
        std::ostringstream json;
        json << "[";
        generateSpectralData(device_id, json);
        json << "]";

        std::string topic = buildTopic(device_id, "spectral");
        if (sender.publish(topic, json.str())) {
            sent_count++;
        }
    }
    std::cout << "[" << now_s() << "] Spectral data sent via MQTT: " << sent_count << " devices" << std::endl;
}

void sendMicrobialDataMqtt(IMqttSender& sender) {
    int sent_count = 0;
    for (uint16_t device_id = 101; device_id < 101 + g_config.mc_device_count; ++device_id) {
        std::ostringstream json;
        json << "[";
        generateMicrobialData(device_id, json);
        json << "]";

        std::string topic = buildTopic(device_id, "microbial");
        if (sender.publish(topic, json.str())) {
            sent_count++;
        }
    }
    std::cout << "[" << now_s() << "] Microbial data sent via MQTT: " << sent_count << " devices" << std::endl;
}

void printConfig() {
    std::cout << "============================================\n";
    std::cout << "海昏侯简牍监测系统 - OPC UA 模拟器\n";
    std::cout << "============================================\n";
    std::cout << "Server: " << g_config.server_host << ":" << g_config.server_port << "\n";
    std::cout << "Interval: " << g_config.report_interval << "s";
    if (g_config.fast_mode) std::cout << " (FAST_MODE)";
    std::cout << "\n";
    std::cout << "MSI Devices: " << g_config.msi_device_count << "\n";
    std::cout << "MC Devices: " << g_config.mc_device_count << "\n";
    std::cout << "Total Slips: " << g_config.total_slips << "\n";
    std::cout << "MSI Slips/Device: " << g_config.msi_slips_per_device << "\n";
    std::cout << "MC Slips/Device: " << g_config.mc_slips_per_device << "\n";
    std::cout << "--------------------------------------------\n";
    std::cout << "Inject Fading: " << (g_config.inject_fading ? "ON" : "OFF") << "\n";
    if (g_config.inject_fading) {
        std::cout << "  Rate: " << g_config.inject_rate << "x\n";
        if (!g_config.inject_slip_ids.empty()) {
            std::cout << "  Target Slip IDs: ";
            for (size_t i = 0; i < g_config.inject_slip_ids.size(); i++) {
                if (i > 0) std::cout << ",";
                std::cout << g_config.inject_slip_ids[i];
            }
            std::cout << "\n";
        }
    }
    std::cout << "--------------------------------------------\n";
    std::cout << "MQTT: " << (g_config.mqtt_enabled ? "ON" : "OFF") << "\n";
    if (g_config.mqtt_enabled) {
        std::cout << "  Broker: " << g_config.mqtt_broker << "\n";
        std::cout << "  Topic Template: " << g_config.mqtt_topic_template << "\n";
        std::cout << "  Mode: " << (g_config.use_mqtt_webhook ? "HTTP Webhook" : "Native MQTT") << "\n";
    }
    std::cout << "============================================\n\n";
}

int main(int argc, char* argv[]) {
    loadConfigFromEnv();

    g_config.server_host = getEnvStr("SIM_HOST", "127.0.0.1");
    g_config.server_port = static_cast<uint16_t>(getEnvInt("SIM_PORT", 8080));
    bool single_shot = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            g_config.server_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            g_config.server_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--interval" && i + 1 < argc) {
            g_config.report_interval = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--once") {
            single_shot = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --host <host>      Server host (default: 127.0.0.1)\n"
                      << "  --port <port>      Server port (default: 8080)\n"
                      << "  --interval <s>     Report interval in seconds (default: 21600)\n"
                      << "  --once             Send data once and exit\n"
                      << "  --help             Show this help\n"
                      << "\nEnvironment Variables:\n"
                      << "  MSI_DEVICES        Number of MSI devices (default: 20)\n"
                      << "  MC_DEVICES         Number of MC devices (default: 30)\n"
                      << "  TOTAL_SLIPS        Total number of slips (default: 5000)\n"
                      << "  REPORT_INTERVAL    Report interval in seconds (default: 21600)\n"
                      << "  FAST_MODE          Fast demo mode, 30s interval (default: 0)\n"
                      << "  INJECT_FADING      Enable fading injection (default: 0)\n"
                      << "  INJECT_RATE        Fading rate multiplier (default: 100)\n"
                      << "  INJECT_SLIP_ID     Target slip IDs (comma-separated)\n"
                      << "  MQTT_ENABLED       Enable MQTT sending (default: 0)\n"
                      << "  MQTT_BROKER        MQTT broker address (default: emqx:1883)\n"
                      << "  MQTT_TOPIC         MQTT topic template (default: haihunhou/device/{device_id}/data)\n"
                      << "  MQTT_USE_WEBHOOK   Use HTTP webhook mode (default: 1)\n";
            return 0;
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printConfig();

    std::unique_ptr<IMqttSender> mqtt_sender;
    if (g_config.mqtt_enabled && g_config.use_mqtt_webhook) {
        size_t colon_pos = g_config.mqtt_broker.find(':');
        std::string mqtt_host = g_config.mqtt_broker;
        uint16_t mqtt_port = 1883;
        if (colon_pos != std::string::npos) {
            mqtt_host = g_config.mqtt_broker.substr(0, colon_pos);
            try {
                mqtt_port = static_cast<uint16_t>(std::stoi(g_config.mqtt_broker.substr(colon_pos + 1)));
            } catch (...) {}
        }
        mqtt_sender = std::make_unique<MqttWebhookSender>(mqtt_host, mqtt_port, g_config.mqtt_webhook_url);
        std::cout << "MQTT Webhook sender initialized: " << mqtt_host << ":" << mqtt_port << g_config.mqtt_webhook_url << "\n\n";
    }

    if (single_shot) {
        std::cout << "Sending single batch of data...\n";
        sendSpectralDataHttp(g_config.server_host, g_config.server_port);
        sendMicrobialDataHttp(g_config.server_host, g_config.server_port);
        if (mqtt_sender) {
            sendSpectralDataMqtt(*mqtt_sender);
            sendMicrobialDataMqtt(*mqtt_sender);
        }
        std::cout << "Done.\n";
        return 0;
    }

    std::cout << "Simulator running. Press Ctrl+C to stop.\n\n";

    while (g_running) {
        try {
            sendSpectralDataHttp(g_config.server_host, g_config.server_port);
            sendMicrobialDataHttp(g_config.server_host, g_config.server_port);
            if (mqtt_sender) {
                sendSpectralDataMqtt(*mqtt_sender);
                sendMicrobialDataMqtt(*mqtt_sender);
            }
        } catch (std::exception const& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }

        uint32_t sleep_interval = std::min(g_config.report_interval, 10u);
        for (uint32_t i = 0; i < g_config.report_interval && g_running; i += sleep_interval) {
            std::this_thread::sleep_for(std::chrono::seconds(sleep_interval));
        }
    }

    std::cout << "\nSimulator stopped.\n";
    return 0;
}
