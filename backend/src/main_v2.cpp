#include "config_loader.h"
#include "clickhouse_client.h"
#include "message_bus.h"
#include "opcua_ingest.h"
#include "fading_predictor.h"
#include "mold_engine.h"
#include "alert_broker.h"
#include "http_server.h"
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
    std::string config_path = "config/config.yaml";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --config <path>  Config file path (default: config/config.yaml)\n"
                      << "  --help           Show this help\n";
            return 0;
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "============================================\n";
    std::cout << "海昏侯简牍监测系统 - 模块化后端\n";
    std::cout << "============================================\n";
    std::cout << "Config: " << config_path << "\n";
    std::cout << "============================================\n\n";

    ConfigLoader cfg_loader(config_path);
    const auto& cfg = cfg_loader.config();

    std::cout << "[1/6] Initializing MessageBus... OK\n";

    auto& bus = MessageBus::instance();

    std::cout << "[2/6] Connecting to ClickHouse... ";
    ClickHouseClient db(cfg.clickhouse.host, cfg.clickhouse.port, cfg.clickhouse.database);
    if (!db.connect()) {
        std::cout << "FAILED\n";
        std::cerr << "Warning: Could not connect to ClickHouse.\n";
    } else {
        std::cout << "OK\n";
    }

    std::cout << "[3/6] Starting OpcUaIngest module... ";
    OpcUaIngest ingest(db, bus);
    ingest.setSimulateMode(cfg.opcua.simulation_mode);
    ingest.setPollInterval(cfg.opcua.polling_interval_sec);
    if (!ingest.start()) {
        std::cout << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";

    std::cout << "[4/6] Starting FadingPredictor module... ";
    FadingPredictor fading(db, bus);
    fading.setParams(cfg.fading_params);
    if (!fading.start()) {
        std::cout << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";

    std::cout << "[5/6] Starting MoldEngine module... ";
    MoldEngine mold(db, bus);
    mold.setParams(cfg.mold_params);
    mold.loadMaterialCoefficients();
    if (!mold.start()) {
        std::cout << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n";

    std::cout << "[6/6] Starting AlertBroker module... ";
    AlertBroker alert_broker(db, bus);
    alert_broker.setConfig(cfg.alert_config);
    alert_broker.setNotificationConfig(cfg.notification_config);
    if (!alert_broker.start()) {
        std::cout << "FAILED\n";
        return 1;
    }
    std::cout << "OK\n\n";

    FadingModel fading_model_compat;
    fading_model_compat.setParams(ModelParams{
        cfg.fading_params.A1, cfg.fading_params.Ea1, cfg.fading_params.alpha,
        cfg.fading_params.A2, cfg.fading_params.Ea2, cfg.fading_params.beta,
        cfg.mold_params.beta0, cfg.mold_params.beta1, cfg.mold_params.beta2,
        cfg.mold_params.beta3, cfg.mold_params.beta4, cfg.mold_params.beta5
    });
    MoldModel mold_model_compat;
    AlertEngine alert_engine_compat(db);
    AlertConfig ac;
    ac.fading_threshold = cfg.alert_config.fading_threshold;
    ac.mold_threshold = cfg.alert_config.mold_threshold;
    alert_engine_compat.setConfig(ac);

    HttpServer http_server(db, alert_engine_compat, fading_model_compat,
                           mold_model_compat, cfg.http_server.port,
                           cfg.http_server.frontend_dir);

    if (!http_server.start()) {
        std::cerr << "Failed to start HTTP server\n";
        return 1;
    }

    std::cout << "All modules running. Press Ctrl+C to stop.\n\n";

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        cfg_loader.checkAndReload();
    }

    std::cout << "\nShutting down...\n";

    std::cout << "Stopping AlertBroker... ";
    alert_broker.stop();
    std::cout << "OK\n";

    std::cout << "Stopping MoldEngine... ";
    mold.stop();
    std::cout << "OK\n";

    std::cout << "Stopping FadingPredictor... ";
    fading.stop();
    std::cout << "OK\n";

    std::cout << "Stopping OpcUaIngest... ";
    ingest.stop();
    std::cout << "OK\n";

    std::cout << "Stopping HTTP server... ";
    http_server.stop();
    std::cout << "OK\n";

    std::cout << "Disconnecting ClickHouse... ";
    db.disconnect();
    std::cout << "OK\n";

    std::cout << "\nShutdown complete.\n";
    return 0;
}
