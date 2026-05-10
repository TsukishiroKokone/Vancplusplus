// ─── VanBot HTTP Client Implementation ───────────────────────
#include "http_client.hpp"
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <regex>

namespace vanbot {

std::string HttpClient::do_get(const std::string& url) const {
    try {
        // 解析 URL
        std::regex url_re(R"(^(https?)://([^/:]+)(?::(\d+))?(/.*)$)", std::regex::icase);
        std::smatch m;
        if (!std::regex_match(url, m, url_re)) {
            spdlog::warn("Invalid URL: {}", url);
            return "";
        }

        std::string scheme = m[1].str();
        std::string host = m[2].str();
        int port = m[3].matched ? std::stoi(m[3].str()) : (scheme == "https" ? 443 : 80);
        std::string path = m[4].str();

        httplib::Client cli(scheme + "://" + host + ":" + std::to_string(port));
        cli.set_connection_timeout(m_timeout);
        cli.set_read_timeout(m_timeout);
        cli.set_follow_location(true);
        auto res = cli.Get(path);
        if (res && res->status == 200) {
            // trim
            std::string body = res->body;
            auto end = body.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) body = body.substr(0, end + 1);
            return body;
        }
        spdlog::warn("HTTP GET failed: {} - status {}", url, res ? res->status : 0);
        return "";
    } catch (const std::exception& e) {
        spdlog::error("HTTP GET exception: {} - {}", url, e.what());
        return "";
    }
}

} // namespace vanbot
