#pragma once
#include "common.h"
#include <string>
#include <vector>
#include <mutex>

namespace haihunhou {

class NotificationService {
public:
    NotificationService();

    void setConfig(const AlertConfig& config);

    bool sendDingTalkAlert(const Alert& alert);
    bool sendEmailAlert(const Alert& alert);
    bool sendAlert(const Alert& alert);

    bool sendDingTalkMessage(const std::string& title, const std::string& content,
                             bool at_all = false,
                             const std::vector<std::string>& at_mobiles = {});
    bool sendEmail(const std::string& subject, const std::string& html_content,
                   const std::vector<std::string>& to_addresses);

private:
    AlertConfig config_;
    mutable std::mutex mutex_;

    std::string generateDingTalkSign() const;
    std::string buildDingTalkMarkdown(const Alert& alert) const;
    std::string buildEmailHtml(const Alert& alert) const;
    std::string hmacSha256Hex(const std::string& key, const std::string& data) const;
    std::string urlEncode(const std::string& str) const;
    std::string base64Encode(const std::string& input) const;

    bool httpPostJson(const std::string& url, const std::string& json_body,
                      const std::vector<std::pair<std::string, std::string>>& headers = {});
    bool smtpSend(const std::string& from, const std::vector<std::string>& to,
                  const std::string& subject, const std::string& html_body);
};

}
