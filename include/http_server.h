#pragma once
#include "common.h"
#include "clickhouse_client.h"
#include "alert_engine.h"
#include "fading_model.h"
#include "mold_model.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>

namespace haihunhou {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

using WsMessageCallback = std::function<void(const std::string&)>;

class HttpServer {
public:
    HttpServer(ClickHouseClient& db, AlertEngine& alert_engine,
               FadingModel& fading_model, MoldModel& mold_model,
               uint16_t port = 8080, const std::string& doc_root = "frontend");
    ~HttpServer();

    bool start();
    void stop();
    bool isRunning() const;

    void broadcastWebSocket(const std::string& message);
    void setWsMessageCallback(WsMessageCallback callback);

private:
    ClickHouseClient& db_;
    AlertEngine& alert_engine_;
    FadingModel& fading_model_;
    MoldModel& mold_model_;
    uint16_t port_;
    std::string doc_root_;
    std::atomic<bool> running_;
    std::thread server_thread_;
    std::mutex ws_mutex_;
    std::vector<std::shared_ptr<websocket::stream<tcp::socket>>> ws_connections_;
    WsMessageCallback ws_callback_;

    void serverLoop();
    void handleSession(tcp::socket socket);
    void handleWebSocket(tcp::socket socket);
    void handleHttpRequest(const http::request<http::string_body>& req,
                           http::response<http::string_body>& res);

    void routeApi(const std::string& path, const std::string& method,
                  const std::string& body,
                  http::response<http::string_body>& res);

    std::string jsonResponse(bool success, const std::string& message = "",
                             const std::string& data = "");
    std::string escapeJson(const std::string& s) const;

    void apiGetSlips(const std::unordered_map<std::string, std::string>& params,
                     std::string& response);
    void apiGetSlipById(uint32_t slip_id, std::string& response);
    void apiGetSlipSpectral(uint32_t slip_id,
                            const std::unordered_map<std::string, std::string>& params,
                            std::string& response);
    void apiGetSlipMicrobial(uint32_t slip_id,
                             const std::unordered_map<std::string, std::string>& params,
                             std::string& response);
    void apiGetSlipFading(uint32_t slip_id,
                          const std::unordered_map<std::string, std::string>& params,
                          std::string& response);
    void apiGetSlipMold(uint32_t slip_id,
                        const std::unordered_map<std::string, std::string>& params,
                        std::string& response);
    void apiGetAlerts(const std::unordered_map<std::string, std::string>& params,
                      std::string& response);
    void apiPostAlertAcknowledge(const std::string& alert_id,
                                 const std::string& body, std::string& response);
    void apiPostAlertResolve(const std::string& alert_id, std::string& response);
    void apiGetDevices(std::string& response);
    void apiGetDashboardStats(std::string& response);
    void apiGetSlipsStatus(std::string& response);
    void apiPostIngestSpectral(const std::string& body, std::string& response);
    void apiPostIngestMicrobial(const std::string& body, std::string& response);

    bool serveStaticFile(const std::string& path,
                         http::response<http::string_body>& res);

    std::unordered_map<std::string, std::string> parseQueryString(
        const std::string& query) const;
};

}
