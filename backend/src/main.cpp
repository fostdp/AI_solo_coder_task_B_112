#include "common.h"
#include "clickhouse_client.h"
#include "fading_model.h"
#include "mold_model.h"
#include "alert_engine.h"
#include "opcua_client.h"
#include "http_server.h"
#include "notification.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace haihunhou;

static std::atomic<bool> g_running(true);

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::string clickhouse_host = "127.0.0.1";
    uint16_t clickhouse_port = 8123;
    uint16_t http_port = 8080;
    uint32_t poll_interval = 300;
    bool simulate_data = true;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--ch-host" && i + 1 < argc) {
            clickhouse_host = argv[++i];
        } else if (arg == "--ch-port" && i + 1 < argc) {
            clickhouse_port = std::stoi(argv[++i]);
        } else if (arg == "--http-port" && i + 1 < argc) {
            http_port = std::stoi(argv[++i]);
        } else if (arg == "--poll-interval" && i + 1 < argc) {
            poll_interval = std::stoi(argv[++i]);
        } else if (arg == "--no-simulate") {
            simulate_data = false;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --ch-host <host>      ClickHouse host (default: 127.0.0.1)\n"
                      << "  --ch-port <port>      ClickHouse HTTP port (default: 8123)\n"
                      << "  --http-port <port>    HTTP server port (default: 8080)\n"
                      << "  --poll-interval <s>   OPC UA poll interval in seconds (default: 300)\n"
                      << "  --no-simulate         Disable simulated data mode\n"
                      << "  --help                Show this help\n";
            return 0;
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "============================================\n";
    std::cout << "海昏侯简牍监测系统 - 后端服务\n";
    std::cout << "============================================\n";
    std::cout << "ClickHouse: " << clickhouse_host << ":" << clickhouse_port << "\n";
    std::cout << "HTTP Port: " << http_port << "\n";
    std::cout << "Poll Interval: " << poll_interval << "s\n";
    std::cout << "Simulate Mode: " << (simulate_data ? "ON" : "OFF") << "\n";
    std::cout << "============================================\n\n";

    ClickHouseClient db(clickhouse_host, clickhouse_port, "haihunhou_slips");
    FadingModel fading_model;
    MoldModel mold_model;
    AlertEngine alert_engine(db);
    OpcUaClient opcua_client;
    HttpServer http_server(db, alert_engine, fading_model, mold_model, http_port, "frontend");

    std::cout << "[1/5] Connecting to ClickHouse... ";
    if (!db.connect()) {
        std::cout << "FAILED\n";
        std::cerr << "Warning: Could not connect to ClickHouse. "
                  << "Please make sure ClickHouse is running and initialized.\n";
        std::cerr << "Run: clickhouse-client --queries-file clickhouse/init.sql\n";
        std::cerr << "     clickhouse-client --queries-file clickhouse/seed_data.sql\n";
    } else {
        std::cout << "OK\n";
    }

    std::cout << "[2/5] Loading configuration... ";
    ModelParams model_params;
    AlertConfig alert_config;
    if (db.getConfig(model_params, alert_config)) {
        fading_model.setParams(model_params);
        mold_model.setParams(model_params);
        alert_engine.setConfig(alert_config);
        std::cout << "OK\n";
    } else {
        std::cout << "using defaults\n";
    }

    std::cout << "[3/5] Starting HTTP server... ";
    if (!http_server.start()) {
        std::cout << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";

    std::cout << "[4/5] Setting up OPC UA client... ";
    opcua_client.connect();
    std::cout << "OK\n";

    auto on_spectral_data = [&](const std::vector<SpectralData>& data) {
        if (!db.insertSpectralData(data)) {
            std::cerr << "Failed to insert spectral data\n";
            return;
        }
        alert_engine.processNewSpectralData(data);

        std::ostringstream oss;
        oss << "{\"type\":\"spectral_update\",\"count\":" << data.size() << "}";
        http_server.broadcastWebSocket(oss.str());
    };

    auto on_microbial_data = [&](const std::vector<MicrobialData>& data) {
        if (!db.insertMicrobialData(data)) {
            std::cerr << "Failed to insert microbial data\n";
            return;
        }
        alert_engine.processNewMicrobialData(data);

        std::ostringstream oss;
        oss << "{\"type\":\"microbial_update\",\"count\":" << data.size() << "}";
        http_server.broadcastWebSocket(oss.str());
    };

    opcua_client.setSpectralCallback(on_spectral_data);
    opcua_client.setMicrobialCallback(on_microbial_data);

    std::cout << "[5/5] Starting OPC UA polling... ";
    if (simulate_data) {
        opcua_client.startPolling(poll_interval);
    }
    std::cout << "OK\n\n";

    std::cout << "Server is running. Press Ctrl+C to stop.\n\n";

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nShutting down...\n";

    std::cout << "Stopping OPC UA polling... ";
    opcua_client.stopPolling();
    std::cout << "OK\n";

    std::cout << "Disconnecting OPC UA client... ";
    opcua_client.disconnect();
    std::cout << "OK\n";

    std::cout << "Stopping HTTP server... ";
    http_server.stop();
    std::cout << "OK\n";

    std::cout << "Disconnecting from ClickHouse... ";
    db.disconnect();
    std::cout << "OK\n";

    std::cout << "\nShutdown complete.\n";
    return 0;
}
