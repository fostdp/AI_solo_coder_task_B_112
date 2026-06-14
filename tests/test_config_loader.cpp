#include "config_loader.h"
#include <iostream>
#include <cassert>
#include <fstream>

using namespace haihunhou;

int main() {
    std::cout << "=== ConfigLoader Test ===" << std::endl;

    const std::string test_path = "test_config.yaml";

    {
        std::ofstream f(test_path);
        f << "clickhouse:\n"
          << "  host: 192.168.1.100\n"
          << "  port: 9000\n"
          << "  database: test_db\n"
          << "http_server:\n"
          << "  port: 9090\n"
          << "opcua:\n"
          << "  simulation_mode: true\n"
          << "  polling_interval_sec: 3600\n"
          << "alert:\n"
          << "  fading_threshold: 15.0\n"
          << "  mold_threshold: 80.0\n"
          << "algorithm:\n"
          << "  fading_model:\n"
          << "    A1: 2.0e-8\n"
          << "    Ea1: 48.0\n"
          << "    alpha: 1.2\n"
          << "    A2: 5.0e-7\n"
          << "    Ea2: 58.0\n"
          << "    beta: 1.8\n"
          << "  mold_model:\n"
          << "    beta0: -3.0\n"
          << "    beta1: 0.15\n"
          << "    beta2: 0.10\n";
    }

    ConfigLoader loader(test_path);
    const auto& cfg = loader.config();

    assert(cfg.clickhouse.host == "192.168.1.100");
    std::cout << "[PASS] clickhouse.host" << std::endl;

    assert(cfg.clickhouse.port == 9000);
    std::cout << "[PASS] clickhouse.port" << std::endl;

    assert(cfg.clickhouse.database == "test_db");
    std::cout << "[PASS] clickhouse.database" << std::endl;

    assert(cfg.http_server.port == 9090);
    std::cout << "[PASS] http_server.port" << std::endl;

    assert(cfg.opcua.simulation_mode == true);
    std::cout << "[PASS] opcua.simulation_mode" << std::endl;

    assert(cfg.opcua.polling_interval_sec == 3600);
    std::cout << "[PASS] opcua.polling_interval_sec" << std::endl;

    assert(cfg.alert_config.fading_threshold == 15.0f);
    std::cout << "[PASS] alert.fading_threshold" << std::endl;

    assert(cfg.alert_config.mold_threshold == 80.0f);
    std::cout << "[PASS] alert.mold_threshold" << std::endl;

    assert(cfg.fading_params.A1 == 2.0e-8f);
    std::cout << "[PASS] fading_model.A1" << std::endl;

    assert(cfg.fading_params.Ea1 == 48.0f);
    std::cout << "[PASS] fading_model.Ea1" << std::endl;

    assert(cfg.fading_params.alpha == 1.2f);
    std::cout << "[PASS] fading_model.alpha" << std::endl;

    assert(cfg.mold_params.beta0 == -3.0f);
    std::cout << "[PASS] mold_model.beta0" << std::endl;

    assert(cfg.mold_params.beta1 == 0.15f);
    std::cout << "[PASS] mold_model.beta1" << std::endl;

    assert(loader.getFloat("algorithm.fading_model.A1") == 2.0e-8f);
    std::cout << "[PASS] getFloat()" << std::endl;

    assert(loader.getInt("clickhouse.port") == 9000);
    std::cout << "[PASS] getInt()" << std::endl;

    assert(loader.getString("clickhouse.host") == "192.168.1.100");
    std::cout << "[PASS] getString()" << std::endl;

    std::remove(test_path.c_str());

    std::cout << "\n=== All ConfigLoader tests passed ===" << std::endl;
    return 0;
}
