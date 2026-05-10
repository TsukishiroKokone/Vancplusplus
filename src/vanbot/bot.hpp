// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
// ─── VanBot Core Bot ────────────────────────────────────────
// 事件处理编排 + 消息解码/编码 + 请求转发
// 多适配器架构：AdapterManager 管理多条连接
#include "common.hpp"
#include "storage.hpp"
#include "lexicon_engine.hpp"
#include "variable_engine.hpp"
#include "http_client.hpp"
#include "onebot_adapter.hpp"
#include "cooling.hpp"
#include "coins.hpp"
#include "fun_plugins.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <random>
#include <algorithm>
#include <ctime>
#include <chrono>
#include <unordered_set>

namespace vanbot {

using json = nlohmann::json;

class Bot {
public:
    struct Stats {
        std::atomic<uint64_t> recv_count{0};
        std::atomic<uint64_t> send_count{0};
        std::atomic<uint64_t> lexicon_count{0};
        std::atomic<int64_t>  lexicon_id{0};
        std::string last_message;
        std::string last_response;
        std::mutex  log_mutex;
        std::vector<std::string> recent_logs;
    };

    Bot(const Config& config)
        : m_config(config)
        , m_storage(config.data_dir, config.storage_backend, config.sqlite_path)
        , m_lexicon(m_storage)
        , m_variables(m_storage)
        , m_cooling(m_storage)
        , m_coins(m_storage)
        , m_fun(m_storage, m_coins)
        , m_http(60, 256)
    {
        // 初始化存储
        m_storage.init();

        // 设置适配器管理器事件回调
        m_adapter_mgr.on_event([this](const Event& event, BotId bot_id) {
            handle_event(event);
        });

        // 注册所有适配器
        for (const auto& adapter_cfg : config.adapters) {
            m_adapter_mgr.add_adapter(adapter_cfg);
        }
    }

    ~Bot() {
        stop();
    }

    // ── 启动 ─────────────────────────────────────────────────
    void start() {
        spdlog::info("🌸 VanBot v3.0.0 正在启动...");

        if (m_config.adapters.empty()) {
            spdlog::warn("⚠️ 未配置任何适配器！请通过 --add-adapter 或配置文件添加");
        }

        // 启动所有适配器连接
        m_adapter_mgr.start_all();

        spdlog::info("✅ VanBot 已启动！共 {} 个适配器", m_adapter_mgr.connection_count());
    }

    // ── 停止 ─────────────────────────────────────────────────
    void stop() {
        m_adapter_mgr.stop_all();
        spdlog::info("🛑 VanBot 已停止");
    }

    // ── 获取统计信息 ─────────────────────────────────────────
    Stats& stats() { return m_stats; }
    const Config& config() const { return m_config; }
    AdapterManager& adapter_mgr() { return m_adapter_mgr; }
    Storage& storage() { return m_storage; }

    // ── 添加日志 ─────────────────────────────────────────────
    void add_log(const std::string& msg) {
        std::lock_guard lock(m_stats.log_mutex);
        m_stats.recent_logs.push_back(msg);
        if (m_stats.recent_logs.size() > 200)
            m_stats.recent_logs.erase(m_stats.recent_logs.begin(),
                                       m_stats.recent_logs.begin() + 100);
    }

private:
    Config m_config;
    Storage m_storage;
    LexiconEngine m_lexicon;
    VariableEngine m_variables;
    CoolingSystem m_cooling;
    CoinsSystem m_coins;
    FunPlugins m_fun;
    HttpClient m_http;
    AdapterManager m_adapter_mgr;
    Stats m_stats;

    // ── 核心：事件处理 ──────────────────────────────────────
    void handle_event(const Event& event) {
        // 生命周期事件
        if (event.type == Event::Lifecycle) {
            auto masters = m_storage.read_id_list("master.txt", 0);
            if (!masters.empty()) {
                std::string msg = "嘿，我上线啦~\nBot Id: " + std::to_string(event.self_id);
                auto conn = m_adapter_mgr.get_connection(event.self_id);
                if (conn) conn->send_private_msg(std::stoll(masters[0]), msg);
            }
            add_log("🌟 Bot 上线: " + std::to_string(event.self_id));
            return;
        }

        // 心跳事件
        if (event.type == Event::Heartbeat) {
            if (!event.online) {
                spdlog::warn("💔 Bot {} 掉线！", event.self_id);
                add_log("💔 掉线: " + std::to_string(event.self_id));
            }
            return;
        }

        BotId bot_id = event.self_id;
        m_storage.init(bot_id);

        // 判断环境
        Env env;
        int64_t env_id = 0;
        if (event.group_id != 0) {
            env = Env::Group;
            env_id = event.group_id;
        } else {
            env = Env::Private;
            env_id = event.user_id;
        }

        // 将事件转换为匹配消息
        std::string message = event_to_message(event);
        if (message.empty()) return;

        spdlog::debug("📨 消息: {}", message.substr(0, 200));
        add_log("📨 " + message.substr(0, 100));
        m_stats.recv_count++;
        m_stats.last_message = message.substr(0, 200);

        // ZeroBot-plugin 风格趣味功能优先处理（签到 / 运势 / 抽签 / 塔罗 / 骰子 / 戳一戳等）
        if (auto fun_response = m_fun.handle(bot_id, env, env_id, event, message)) {
            if (!fun_response->empty()) {
                auto conn = m_adapter_mgr.get_connection(bot_id);
                if (conn) {
                    conn->send_msg(env, env_id, fun_response->substr(0, 3000));
                    m_stats.send_count++;
                    m_stats.last_response = fun_response->substr(0, 200);
                    add_log("🎀 " + fun_response->substr(0, 100));
                }
            }
            return;
        }

        // 构建词库ID数组
        std::vector<std::string> data_ids = {"common"};
        if (env == Env::Group) {
            data_ids.push_back(std::to_string(env_id));
        }

        // 获取群/用户使用的词库
        auto user_lexicon = get_user_lexicon(bot_id, env, env_id);
        if (!user_lexicon.empty()) data_ids.push_back(user_lexicon);

        // 词库匹配
        auto match = m_lexicon.lookup(bot_id, data_ids, message);
        if (!match) return;

        auto& [response, captures] = *match;
        m_stats.lexicon_id = m_lexicon.current_id();
        m_stats.lexicon_count = m_lexicon.count(bot_id);

        // 构建变量上下文（事件数据拷贝，线程安全）
        VarContext ctx = VarContext::from_event(bot_id, env, env_id, event);
        ctx.lexicon_id = m_stats.lexicon_id.load();
        ctx.lexicon_n  = m_stats.lexicon_count.load();
        ctx.recv_count = m_stats.recv_count.load();
        ctx.send_count = m_stats.send_count.load();
        ctx.user_lexicon = user_lexicon;
        ctx.captures   = captures;

        // 变量替换（嵌套解析）
        std::string processed = m_variables.process(response, ctx);

        // 冷却检查
        auto [text_after_cooling, cooling_seconds] = CoolingSystem::extract_cooling(processed);
        if (cooling_seconds > 0) {
            int remaining = m_cooling.check(bot_id, event.user_id,
                                            env == Env::Group ? env_id : 0,
                                            m_stats.lexicon_id.load());
            if (remaining > 0) {
                // 冷却中，不回复
                return;
            }
            // 设置冷却
            m_cooling.set(bot_id, event.user_id,
                         env == Env::Group ? env_id : 0,
                         m_stats.lexicon_id.load(), cooling_seconds);
            processed = text_after_cooling;
        }

        // 处理分段延迟发送 (-秒数-)
        if (processed.find("(-") != std::string::npos) {
            handle_clause_send(processed, bot_id, env, env_id, ctx);
            return;
        }

        // 处理 [get.url] 请求变量
        processed = handle_get_variable(processed, bot_id);

        // 限制长度
        if (processed.size() > 3000) processed = processed.substr(0, 3000);

        // 发送消息
        if (!processed.empty()) {
            auto conn = m_adapter_mgr.get_connection(bot_id);
            if (conn) {
                conn->send_msg(env, env_id, processed);
                m_stats.send_count++;
                m_stats.last_response = processed.substr(0, 200);
                add_log("📤 " + processed.substr(0, 100));
            } else {
                spdlog::warn("⚠️ 未找到 bot_id={} 的连接，无法发送", bot_id);
            }
        }
    }

    // ── 事件→匹配消息转换 ───────────────────────────────────
    static std::string event_to_message(const Event& event) {
        switch (event.type) {
            case Event::GroupMessage:
            case Event::PrivateMessage:
                return transcoding(event.raw_message);
            case Event::Poke:
                return "[poke." + std::to_string(event.target_id) + "." +
                       std::to_string(event.group_id) + "]";
            case Event::GroupIncrease:
                return "[groupin." + std::to_string(event.user_id) + "." +
                       std::to_string(event.group_id) + "]";
            case Event::GroupDecrease:
                return "[groupout." + std::to_string(event.user_id) + "." +
                       std::to_string(event.group_id) + "]";
            case Event::GroupRecall:
                return "[recall." + std::to_string(event.operator_id) + "." +
                       std::to_string(event.group_id) + "]";
            case Event::GroupAdminSet:
                return "[adminset." + std::to_string(event.user_id) + "." +
                       std::to_string(event.group_id) + "]";
            case Event::GroupAdminUnset:
                return "[adminunset." + std::to_string(event.user_id) + "." +
                       std::to_string(event.group_id) + "]";
            case Event::GroupBan:
                return "[ban." + std::to_string(event.operator_id) + "." +
                       std::to_string(event.group_id) + "]";
            case Event::GroupUnBan:
                return "[unban." + std::to_string(event.operator_id) + "." +
                       std::to_string(event.group_id) + "]";
            case Event::FriendAdd:
                return "[friendadd." + std::to_string(event.user_id) + "." +
                       std::to_string(event.user_id) + "]";
            default:
                return "";
        }
    }

    // ── CQ码转码 ─────────────────────────────────────────────
    static std::string transcoding(const std::string& text) {
        std::string result = text;
        static const std::regex cq_re(R"(\[CQ:(\w+),(.*?)\])");
        static const std::unordered_map<std::string, std::string> keep_params = {
            {"reply", "id"}, {"at", "qq"}, {"face", "id"},
            {"image", "url"}, {"video", "url"}, {"record", "url"},
            {"forward", "id"}, {"file", "file_id"}, {"json", "data"}
        };

        std::smatch match;
        std::string::const_iterator it = result.cbegin();
        std::string out;

        while (std::regex_search(it, result.cend(), match, cq_re)) {
            out.append(it, match[0].first);
            std::string cq_type = match[1].str();
            std::string params_str = match[2].str();

            // 解析参数
            std::unordered_map<std::string, std::string> params;
            static const std::regex param_re(R"((\w+)=([^,]+))");
            auto pit = std::sregex_iterator(params_str.begin(), params_str.end(), param_re);
            for (auto it2 = pit; it2 != std::sregex_iterator(); ++it2) {
                params[(*it2)[1].str()] = (*it2)[2].str();
            }

            auto kp_it = keep_params.find(cq_type);
            if (kp_it != keep_params.end()) {
                auto param_it = params.find(kp_it->second);
                if (param_it != params.end()) {
                    out += "[" + cq_type + "." + param_it->second + "]";
                } else {
                    out += match[0].str();
                }
            } else {
                out += match[0].str();
            }

            it = match[0].second;
        }
        out.append(it, result.cend());

        // 还原 HTML 实体
        out = replace_all(out, "[", "[");
        out = replace_all(out, "]", "]");
        out = replace_all(out, "&", "&");

        return out;
    }

    // ── 获取群/用户使用的词库名 ──────────────────────────────
    std::string get_user_lexicon(BotId bot_id, Env env, int64_t env_id) {
        std::string lexicon_name;
        if (env == Env::Group) {
            lexicon_name = std::to_string(env_id);
        } else {
            lexicon_name = "private";
        }

        auto kv = m_storage.read_kv(bot_id, "switch.txt");
        auto it = kv.find(lexicon_name);
        if (it != kv.end() && !it->second.empty()) return it->second;
        return lexicon_name;
    }

    // ── 分段延迟发送 ─────────────────────────────────────────
    void handle_clause_send(const std::string& text, BotId bot_id,
                            Env env, int64_t env_id, VarContext ctx) {
        static const std::regex sep_re(R"(\(-(\d+)-\))");
        std::vector<int> delays;
        std::vector<std::string> parts;

        std::sregex_token_iterator it(text.begin(), text.end(), sep_re, -1);
        std::sregex_token_iterator end;
        for (; it != end; ++it) {
            if (!it->str().empty()) parts.push_back(it->str());
        }

        std::sregex_iterator dit(text.begin(), text.end(), sep_re);
        for (; dit != std::sregex_iterator(); ++dit) {
            delays.push_back(std::stoi((*dit)[1].str()));
        }

        // 异步延迟发送
        std::thread([this, parts, delays, bot_id, env, env_id, ctx]() {
            for (size_t i = 0; i < parts.size() && i <= delays.size(); i++) {
                std::string processed = m_variables.process(parts[i], ctx);
                if (!processed.empty()) {
                    auto conn = m_adapter_mgr.get_connection(bot_id);
                    if (conn) {
                        conn->send_msg(env, env_id, processed);
                        m_stats.send_count++;
                    }
                }
                if (i < delays.size()) {
                    std::this_thread::sleep_for(std::chrono::seconds(delays[i]));
                }
            }
        }).detach();
    }

    // ── 处理 [get.url] 请求变量 ──────────────────────────────
    std::string handle_get_variable(const std::string& text, BotId bot_id) {
        static const std::regex get_re(R"(\[get\.(.*?)\])");
        std::string result = text;
        std::smatch match;
        std::string::const_iterator searchStart = result.cbegin();

        while (std::regex_search(searchStart, result.cend(), match, get_re)) {
            std::string url = match[1].str();
            auto response = m_http.get(url);
            size_t pos = match.position() + (searchStart - result.cbegin());
            result.replace(pos, match.length(), response);
            searchStart = result.cbegin() + pos + response.length();
        }

        return result;
    }

    static std::string replace_all(std::string str, const std::string& from,
                                    const std::string& to) {
        if (from.empty()) return str;
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
        return str;
    }
};

} // namespace vanbot
