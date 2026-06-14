#include "notification.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <chrono>
#include <cmath>

namespace haihunhou {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

NotificationService::NotificationService() {
}

void NotificationService::setConfig(const AlertConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

std::string NotificationService::urlEncode(const std::string& str) const {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char)c);
            escaped << std::nouppercase;
        }
    }
    return escaped.str();
}

std::string NotificationService::base64Encode(const std::string& input) const {
    static const std::string b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            output.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) output.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (output.size() % 4) output.push_back('=');
    return output;
}

std::string NotificationService::hmacSha256Hex(const std::string& key, const std::string& data) const {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;

    HMAC(EVP_sha256(), key.c_str(), key.length(),
         (const unsigned char*)data.c_str(), data.length(),
         digest, &digest_len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < digest_len; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    }
    return oss.str();
}

std::string NotificationService::generateDingTalkSign() const {
    if (config_.dingtalk_secret.empty()) return "";

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::string string_to_sign = std::to_string(timestamp) + "\n" + config_.dingtalk_secret;
    std::string sign = hmacSha256Hex(config_.dingtalk_secret, string_to_sign);
    std::string encoded_sign = urlEncode(base64Encode(sign));

    return "&timestamp=" + std::to_string(timestamp) + "&sign=" + encoded_sign;
}

std::string NotificationService::buildDingTalkMarkdown(const Alert& alert) const {
    std::ostringstream oss;
    std::string severity_str = alert.severity == Alert::CRITICAL ? "严重" :
                               alert.severity == Alert::WARNING ? "警告" : "信息";
    std::string type_str = alert.alert_type == Alert::FADING ? "墨迹褪色告警" :
                          alert.alert_type == Alert::MOLD ? "霉菌告警" : "设备告警";

    oss << "{"
        << "\"msgtype\":\"markdown\","
        << "\"markdown\":{"
        << "\"title\":\"" << type_str << "\","
        << "\"text\":\""
        << "## 【" << severity_str << "】" << type_str << "\\n\\n"
        << "- **简牍编号**: #" << alert.slip_id << "\\n"
        << "- **告警时间**: " << std::asctime(std::localtime((time_t*)&alert.timestamp))
        << "- **当前值**: " << std::fixed << std::setprecision(2) << alert.current_value << "\\n"
        << "- **阈值**: " << alert.threshold << "\\n"
        << "- **描述**: " << alert.message
        << "\"},"
        << "\"at\":{\"isAtAll\":true}"
        << "}";

    return oss.str();
}

std::string NotificationService::buildEmailHtml(const Alert& alert) const {
    std::ostringstream oss;
    std::string severity_color = alert.severity == Alert::CRITICAL ? "#dc3545" :
                                 alert.severity == Alert::WARNING ? "#ffc107" : "#17a2b8";
    std::string severity_str = alert.severity == Alert::CRITICAL ? "严重" :
                               alert.severity == Alert::WARNING ? "警告" : "信息";
    std::string type_str = alert.alert_type == Alert::FADING ? "墨迹褪色告警" :
                          alert.alert_type == Alert::MOLD ? "霉菌告警" : "设备告警";

    oss << "<!DOCTYPE html><html><head><meta charset='UTF-8'></head><body>"
        << "<div style='padding:20px;font-family:Arial,sans-serif;'>"
        << "<h2 style='color:" << severity_color << ";'>【" << severity_str << "】" << type_str << "</h2>"
        << "<table style='border-collapse:collapse;border:1px solid #ddd;'>"
        << "<tr><td style='padding:10px;border:1px solid #ddd;background:#f5f5f5;'>简牍编号</td>"
        << "<td style='padding:10px;border:1px solid #ddd;'>#" << alert.slip_id << "</td></tr>"
        << "<tr><td style='padding:10px;border:1px solid #ddd;background:#f5f5f5;'>告警时间</td>"
        << "<td style='padding:10px;border:1px solid #ddd;'>" << std::asctime(std::localtime((time_t*)&alert.timestamp)) << "</td></tr>"
        << "<tr><td style='padding:10px;border:1px solid #ddd;background:#f5f5f5;'>当前值</td>"
        << "<td style='padding:10px;border:1px solid #ddd;color:" << severity_color << ";font-weight:bold;'>"
        << std::fixed << std::setprecision(2) << alert.current_value << "</td></tr>"
        << "<tr><td style='padding:10px;border:1px solid #ddd;background:#f5f5f5;'>阈值</td>"
        << "<td style='padding:10px;border:1px solid #ddd;'>" << alert.threshold << "</td></tr>"
        << "<tr><td style='padding:10px;border:1px solid #ddd;background:#f5f5f5;'>描述</td>"
        << "<td style='padding:10px;border:1px solid #ddd;'>" << alert.message << "</td></tr>"
        << "</table>"
        << "<p style='margin-top:20px;color:#666;font-size:12px;'>此邮件由系统自动发送，请勿直接回复。</p>"
        << "</div></body></html>";

    return oss.str();
}

bool NotificationService::httpPostJson(const std::string& url, const std::string& json_body,
                                       const std::vector<std::pair<std::string, std::string>>& headers) {
    try {
        std::string host;
        std::string path;
        bool use_ssl = false;

        size_t pos = url.find("://");
        if (pos != std::string::npos) {
            std::string protocol = url.substr(0, pos);
            use_ssl = (protocol == "https");
            std::string rest = url.substr(pos + 3);
            size_t slash_pos = rest.find('/');
            if (slash_pos != std::string::npos) {
                host = rest.substr(0, slash_pos);
                path = rest.substr(slash_pos);
            } else {
                host = rest;
                path = "/";
            }
        }

        size_t port_pos = host.find(':');
        uint16_t port = use_ssl ? 443 : 80;
        if (port_pos != std::string::npos) {
            port = std::stoi(host.substr(port_pos + 1));
            host = host.substr(0, port_pos);
        }

        asio::io_context ioc;

        if (use_ssl) {
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_verify_mode(ssl::verify_none);

            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(host, std::to_string(port));
            beast::get_lowest_layer(stream).connect(results);
            stream.handshake(ssl::stream_base::client);

            http::request<http::string_body> req{http::verb::post, path, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set(http::field::content_type, "application/json");
            for (const auto& h : headers) {
                req.set(h.first, h.second);
            }
            req.body() = json_body;
            req.prepare_payload();

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);

            return res.result() == http::status::ok;
        } else {
            beast::tcp_stream stream(ioc);
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(host, std::to_string(port));
            stream.connect(results);

            http::request<http::string_body> req{http::verb::post, path, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set(http::field::content_type, "application/json");
            for (const auto& h : headers) {
                req.set(h.first, h.second);
            }
            req.body() = json_body;
            req.prepare_payload();

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);

            return res.result() == http::status::ok;
        }
    } catch (std::exception const& e) {
        std::cerr << "HTTP POST error: " << e.what() << std::endl;
        return false;
    }
}

bool NotificationService::sendDingTalkAlert(const Alert& alert) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (config_.dingtalk_webhook.empty() ||
        config_.dingtalk_webhook.find("YOUR_TOKEN") != std::string::npos) {
        std::cout << "DingTalk webhook not configured, skipping alert: " << alert.message << std::endl;
        return true;
    }

    std::string url = config_.dingtalk_webhook + generateDingTalkSign();
    std::string body = buildDingTalkMarkdown(alert);

    return httpPostJson(url, body);
}

bool NotificationService::sendEmailAlert(const Alert& alert) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (config_.smtp_host.empty() || config_.smtp_host.find("example.com") != std::string::npos) {
        std::cout << "SMTP not configured, skipping email alert: " << alert.message << std::endl;
        return true;
    }

    std::string type_str = alert.alert_type == Alert::FADING ? "墨迹褪色告警" :
                          alert.alert_type == Alert::MOLD ? "霉菌告警" : "设备告警";
    std::string subject = "[海昏侯简牍监测系统] " + type_str + " - 简牍#" + std::to_string(alert.slip_id);
    std::string html = buildEmailHtml(alert);

    return smtpSend(config_.smtp_from, config_.smtp_to, subject, html);
}

bool NotificationService::sendAlert(const Alert& alert) {
    bool success = true;
    success &= sendDingTalkAlert(alert);
    success &= sendEmailAlert(alert);
    return success;
}

bool NotificationService::sendDingTalkMessage(const std::string& title, const std::string& content,
                                              bool at_all, const std::vector<std::string>& at_mobiles) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (config_.dingtalk_webhook.empty()) return false;

    std::ostringstream oss;
    oss << "{\"msgtype\":\"markdown\",\"markdown\":{"
        << "\"title\":\"" << title << "\","
        << "\"text\":\"" << content << "\"},"
        << "\"at\":{\"isAtAll\":" << (at_all ? "true" : "false") << ","
        << "\"atMobiles\":[";
    for (size_t i = 0; i < at_mobiles.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << at_mobiles[i] << "\"";
    }
    oss << "]}}";

    std::string url = config_.dingtalk_webhook + generateDingTalkSign();
    return httpPostJson(url, oss.str());
}

bool NotificationService::sendEmail(const std::string& subject, const std::string& html_content,
                                    const std::vector<std::string>& to_addresses) {
    std::lock_guard<std::mutex> lock(mutex_);
    return smtpSend(config_.smtp_from, to_addresses, subject, html_content);
}

bool NotificationService::smtpSend(const std::string& from, const std::vector<std::string>& to,
                                   const std::string& subject, const std::string& html_body) {
    try {
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        asio::ip::tcp::socket socket(ioc);

        auto endpoints = resolver.resolve(config_.smtp_host, std::to_string(config_.smtp_port));
        asio::connect(socket, endpoints);

        std::ostringstream response;
        auto read_response = [&]() {
            asio::streambuf buf;
            asio::read_until(socket, buf, "\r\n");
            std::istream is(&buf);
            std::string line;
            std::getline(is, line);
            response.str(line);
        };

        auto send_command = [&](const std::string& cmd) {
            asio::write(socket, asio::buffer(cmd + "\r\n"));
            read_response();
        };

        read_response();

        send_command("EHLO haihunhou-server");
        send_command("AUTH LOGIN");
        send_command(base64Encode(config_.smtp_user));
        send_command(base64Encode(config_.smtp_password));
        send_command("MAIL FROM:<" + from + ">");

        for (const auto& recipient : to) {
            send_command("RCPT TO:<" + recipient + ">");
        }

        send_command("DATA");

        std::ostringstream mime_message;
        mime_message << "From: <" << from << ">\r\n";
        mime_message << "To: ";
        for (size_t i = 0; i < to.size(); ++i) {
            if (i > 0) mime_message << ", ";
            mime_message << "<" << to[i] << ">";
        }
        mime_message << "\r\n";
        mime_message << "Subject: =?UTF-8?B?" << base64Encode(subject) << "?=\r\n";
        mime_message << "MIME-Version: 1.0\r\n";
        mime_message << "Content-Type: text/html; charset=UTF-8\r\n";
        mime_message << "\r\n";
        mime_message << html_body << "\r\n.\r\n";

        asio::write(socket, asio::buffer(mime_message.str()));
        read_response();

        send_command("QUIT");

        socket.close();
        return true;
    } catch (std::exception const& e) {
        std::cerr << "SMTP error: " << e.what() << std::endl;
        return false;
    }
}

}
