// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
// ─── VanBot Web API ───────────────────────────────────────────
// 轻量 Web API：状态、配置、词库查询与新增词条
#include "bot.hpp"
#include "config.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace vanbot {

class WebApiServer {
public:
    WebApiServer(Bot& bot, const Config& config)
        : m_bot(bot), m_config(config) {}

    ~WebApiServer() { stop(); }

    void start() {
        if (!m_config.web_api_enabled || m_running.exchange(true)) return;
        setup_routes();
        m_thread = std::thread([this] {
            m_server.listen(m_config.web_api_host, m_config.web_api_port);
            m_running = false;
        });
    }

    void stop() {
        if (!m_running.exchange(false)) return;
        m_server.stop();
        if (m_thread.joinable()) m_thread.join();
    }

private:
    Bot& m_bot;
    Config m_config;
    httplib::Server m_server;
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    bool authorized(const httplib::Request& req, httplib::Response& res) const {
        if (m_config.web_api_token.empty()) return true;
        auto auth = req.get_header_value("Authorization");
        auto token = req.get_header_value("X-Van-Token");
        if (auth == "Bearer " + m_config.web_api_token || token == m_config.web_api_token) return true;
        res.status = 401;
        res.set_content(R"({"ok":false,"error":"unauthorized"})", "application/json; charset=utf-8");
        return false;
    }

    void setup_routes() {
        using json = nlohmann::json;

        m_server.Get("/api/status", [this](const httplib::Request& req, httplib::Response& res) {
            if (!authorized(req, res)) return;
            auto& st = m_bot.stats();
            json adapters = json::array();
            for (auto& [name, connected] : m_bot.adapter_mgr().get_all_status()) {
                adapters.push_back({{"name", name}, {"connected", connected}});
            }
            json body = {
                {"ok", true},
                {"recv_count", st.recv_count.load()},
                {"send_count", st.send_count.load()},
                {"lexicon_count", st.lexicon_count.load()},
                {"lexicon_id", st.lexicon_id.load()},
                {"last_message", st.last_message},
                {"last_response", st.last_response},
                {"adapters", adapters},
            };
            res.set_content(body.dump(2), "application/json; charset=utf-8");
        });

        m_server.Get("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
            if (!authorized(req, res)) return;
            json adapters = json::array();
            for (const auto& adapter : m_config.adapters) {
                adapters.push_back({
                    {"name", adapter.name},
                    {"type", IniConfig::adapter_type_to_string(adapter.type)},
                    {"url", adapter.url},
                    {"port", adapter.port},
                    {"reconnect_interval", adapter.reconnect_interval},
                    {"heartbeat_interval", adapter.heartbeat_interval},
                });
            }
            json body = {
                {"ok", true},
                {"data_dir", m_config.data_dir},
                {"self_trigger", m_config.self_trigger},
                {"storage_backend", IniConfig::storage_backend_to_string(m_config.storage_backend)},
                {"sqlite_path", m_config.sqlite_path},
                {"web_api", { {"enabled", m_config.web_api_enabled}, {"host", m_config.web_api_host}, {"port", m_config.web_api_port} }},
                {"adapters", adapters},
            };
            res.set_content(body.dump(2), "application/json; charset=utf-8");
        });

        m_server.Get(R"(/api/lexicon/(\d+)/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
            if (!authorized(req, res)) return;
            BotId bot_id = std::stoll(req.matches[1]);
            std::string data_id = req.matches[2];
            auto data = m_bot.storage().read_json(bot_id, "lexicon/" + data_id + ".json");
            json body = {{"ok", static_cast<bool>(data)}, {"bot_id", bot_id}, {"data_id", data_id}, {"data", data.value_or(json::object())}};
            res.set_content(body.dump(2), "application/json; charset=utf-8");
        });

        m_server.Post(R"(/api/lexicon/(\d+)/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
            if (!authorized(req, res)) return;
            BotId bot_id = std::stoll(req.matches[1]);
            std::string data_id = req.matches[2];
            try {
                auto body = json::parse(req.body);
                std::string keyword = body.value("keyword", "");
                std::string response = body.value("response", "");
                int mode = body.value("mode", 0);
                if (keyword.empty() || response.empty()) {
                    res.status = 400;
                    res.set_content(R"({"ok":false,"error":"keyword/response required"})", "application/json; charset=utf-8");
                    return;
                }
                auto data = m_bot.storage().read_json(bot_id, "lexicon/" + data_id + ".json").value_or(json{{"work", json::array()}});
                if (!data.contains("work")) data["work"] = json::array();
                data["work"].push_back(json{{keyword, {{"r", {response}}, {"s", mode}}}});
                bool ok = m_bot.storage().write_json(bot_id, "lexicon/" + data_id + ".json", data);
                res.set_content(json{{"ok", ok}, {"message", ok ? "added" : "write failed"}}.dump(2), "application/json; charset=utf-8");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content(json{{"ok", false}, {"error", e.what()}}.dump(2), "application/json; charset=utf-8");
            }
        });

        m_server.Get("/api/logs", [this](const httplib::Request& req, httplib::Response& res) {
            if (!authorized(req, res)) return;
            std::lock_guard lock(m_bot.stats().log_mutex);
            res.set_content(json{{"ok", true}, {"logs", m_bot.stats().recent_logs}}.dump(2), "application/json; charset=utf-8");
        });
    }
};

} // namespace vanbot
