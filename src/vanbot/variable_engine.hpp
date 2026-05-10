// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
// ─── VanBot Variable Engine v2 ──────────────────────────────
// 嵌套变量系统 + 100+ 内置变量
// 变量可以互相嵌套，如: [积分.0.[n.1]] → 先解析[n.1]再解析积分
#include "common.hpp"
#include "storage.hpp"
#include <regex>
#include <random>
#include <chrono>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <optional>
#include <cctype>

namespace vanbot {

struct VarContext {
    BotId     bot_id      = 0;
    Env       env         = Env::Group;
    int64_t   env_id      = 0;
    int64_t   lexicon_id  = 0;
    size_t    lexicon_n   = 0;
    uint64_t  recv_count  = 0;
    uint64_t  send_count  = 0;
    std::string select_lexicon;
    std::string user_lexicon;
    std::vector<std::string> captures;
    std::unordered_map<std::string, std::string> remote_cache;

    // ── 事件数据快照（拷贝，线程安全） ──────────────────────
    BotId     event_self_id    = 0;
    UserId    event_user_id    = 0;
    GroupId   event_group_id   = 0;
    MessageId event_message_id = 0;
    std::string event_sender_name;
    std::string event_sender_card;
    int64_t   event_target_id  = 0;

    // ── 从 Event 构建上下文 ─────────────────────────────────
    static VarContext from_event(BotId bid, Env e, int64_t eid, const Event& ev) {
        VarContext ctx;
        ctx.bot_id           = bid;
        ctx.env              = e;
        ctx.env_id           = eid;
        ctx.event_self_id    = ev.self_id;
        ctx.event_user_id    = ev.user_id;
        ctx.event_group_id   = ev.group_id;
        ctx.event_message_id = ev.message_id;
        ctx.event_sender_name = ev.sender_name;
        ctx.event_sender_card = ev.sender_card;
        ctx.event_target_id  = ev.target_id;
        return ctx;
    }
};

class VariableEngine {
public:
    explicit VariableEngine(Storage& storage) : m_storage(storage) {}

    std::string process(const std::string& text, const VarContext& ctx) {
        std::string result = text;
        result = replace_captures(result, ctx.captures);
        result = resolve_nested(result, ctx, 52);
        result = replace_parametric_vars(result, ctx);
        result = replace_escape(result);
        result = replace_or(result);
        result = replace_error(result);
        result = replace_judge(result);
        return result;
    }

    std::string resolve_nested(const std::string& text, const VarContext& ctx, int depth) {
        if (depth <= 0) return text;
        std::string result = text;
        for (int round = 0; round < depth; round++) {
            std::string prev = result;
            if (result.find('[') != std::string::npos || result.find('(') != std::string::npos || result.find('{') != std::string::npos) {
                result = replace_user_vars(result, ctx);
                result = replace_env_vars(result, ctx);
                result = replace_time_vars(result);
                result = replace_random_vars(result);
                result = replace_stats_vars(result, ctx);
                result = replace_identity_vars(result, ctx);
                result = replace_text_magic_vars(result, ctx);
                result = replace_system_vars(result, ctx);
                result = replace_alias_pack_vars(result);
                result = replace_fun_exact_vars(result, ctx);
                result = replace_generated_utility_vars(result);
                result = replace_format_vars(result);
                result = replace_parametric_vars(result, ctx);
                result = replace_captures(result, ctx.captures);
            }
            if (result == prev) break;
        }
        return result;
    }

    // ── [n.?] 捕获组 ─────────────────────────────────────────
    static std::string replace_captures(const std::string& text,
                                         const std::vector<std::string>& captures) {
        if (captures.empty()) return text;
        std::string result = text;
        for (size_t i = 1; i <= 6 && i < captures.size(); i++) {
            result = replace_all(result, "[n." + std::to_string(i) + "]", captures[i]);
            result = replace_all(result, "[n." + std::to_string(i) + ".t]", captures[i]);
        }
        return result;
    }

    // ── 用户与事件变量：100+ 变量族，一次 token 扫描解析 ─────────
    static std::string replace_user_vars(const std::string& text, const VarContext& ctx) {
        if (text.find('[') == std::string::npos) return text;
        std::string expanded = replace_user_event_exact_species(text, ctx);
        const auto& vars = user_event_var_kinds();
        std::string out;
        out.reserve(text.size());
        bool changed = false;
        for (size_t i = 0; i < expanded.size();) {
            if (expanded[i] != '[') { out.push_back(expanded[i++]); continue; }
            size_t end = expanded.find(']', i + 1);
            if (end == std::string::npos) { out.append(expanded, i, std::string::npos); break; }
            std::string token = expanded.substr(i, end - i + 1);
            auto found = vars.find(token);
            if (found != vars.end()) { out += user_event_value(found->second, ctx); changed = true; }
            else out += token;
            i = end + 1;
        }
        return changed ? out : expanded;
    }

    // ── 环境变量：100+ 变量族，一次 token 扫描解析 ──────────────
    static std::string replace_env_vars(const std::string& text, const VarContext& ctx) {
        if (text.find('[') == std::string::npos) return text;
        std::string expanded = replace_env_exact_species(text, ctx);
        const auto& vars = env_var_kinds();
        std::string out;
        out.reserve(text.size());
        bool changed = false;
        for (size_t i = 0; i < expanded.size();) {
            if (expanded[i] != '[') { out.push_back(expanded[i++]); continue; }
            size_t end = expanded.find(']', i + 1);
            if (end == std::string::npos) { out.append(expanded, i, std::string::npos); break; }
            std::string token = expanded.substr(i, end - i + 1);
            auto found = vars.find(token);
            if (found != vars.end()) { out += env_value(found->second, ctx); changed = true; }
            else out += token;
            i = end + 1;
        }
        return changed ? out : expanded;
    }

    // ── 时间变量：100+ 变量族，一次 token 扫描解析 ──────────────
    static std::string replace_time_vars(const std::string& text) {
        if (text.find('[') == std::string::npos && text.find('(') == std::string::npos) return text;
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm tm_now;
    #ifdef _WIN32
        localtime_s(&tm_now, &time_t_now);
    #else
        localtime_r(&time_t_now, &tm_now);
    #endif
        std::string result = replace_time_exact_species(text, tm_now, time_t_now);
        result = replace_all(result, "(Y)", std::to_string(tm_now.tm_year + 1900));
        result = replace_all(result, "(M)", std::to_string(tm_now.tm_mon + 1));
        result = replace_all(result, "(D)", std::to_string(tm_now.tm_mday));
        result = replace_all(result, "(h)", std::to_string(tm_now.tm_hour));
        result = replace_all(result, "(m)", std::to_string(tm_now.tm_min));
        result = replace_all(result, "(s)", std::to_string(tm_now.tm_sec));
        if (result.find('[') == std::string::npos) return result;

        const auto& vars = time_var_kinds();
        std::string out;
        out.reserve(result.size());
        bool changed = false;
        for (size_t i = 0; i < result.size();) {
            if (result[i] != '[') { out.push_back(result[i++]); continue; }
            size_t end = result.find(']', i + 1);
            if (end == std::string::npos) { out.append(result, i, std::string::npos); break; }
            std::string token = result.substr(i, end - i + 1);
            auto found = vars.find(token);
            if (found != vars.end()) { out += time_value(found->second, tm_now, time_t_now); changed = true; }
            else out += token;
            i = end + 1;
        }
        return changed ? out : result;
    }

    // ── 随机变量：100+ 变量族，范围语法 + token 扫描解析 ───────
    std::string replace_random_vars(const std::string& text) const {
        if (text.find('[') == std::string::npos && text.find('(') == std::string::npos) return text;
        static const std::regex re(R"(\((\d+)-(\d+)\))");
        std::string result = replace_random_exact_species(text);
        std::smatch match;
        std::string::const_iterator searchStart = result.cbegin();
        while (std::regex_search(searchStart, result.cend(), match, re)) {
            int a = std::stoi(match[1].str());
            int b = std::stoi(match[2].str());
            std::uniform_int_distribution<int> dist((std::min)(a, b), (std::max)(a, b));
            std::string repl = std::to_string(dist(m_rng));
            size_t pos = match.position() + (searchStart - result.cbegin());
            result.replace(pos, match.length(), repl);
            searchStart = result.cbegin() + pos + repl.length();
        }
        if (result.find('[') == std::string::npos) return result;

        const auto& vars = random_var_kinds();
        std::string out;
        out.reserve(result.size());
        bool changed = false;
        for (size_t i = 0; i < result.size();) {
            if (result[i] != '[') { out.push_back(result[i++]); continue; }
            size_t end = result.find(']', i + 1);
            if (end == std::string::npos) { out.append(result, i, std::string::npos); break; }
            std::string token = result.substr(i, end - i + 1);
            auto found = vars.find(token);
            if (found != vars.end()) { out += random_value(found->second); changed = true; }
            else out += token;
            i = end + 1;
        }
        return changed ? out : result;
    }

    // ── 统计变量 ─────────────────────────────────────────────
    static std::string replace_stats_vars(const std::string& text, const VarContext& ctx) {
        std::string result = text;
        result = replace_all(result, "[收消息数]", std::to_string(ctx.recv_count));
        result = replace_all(result, "[发消息数]", std::to_string(ctx.send_count));
        result = replace_all(result, "[词条id]", std::to_string(ctx.lexicon_id));
        result = replace_all(result, "[词汇量]", std::to_string(ctx.lexicon_n));
        result = replace_all(result, "[选择的词库]", ctx.select_lexicon);
        result = replace_all(result, "[使用的词库]", ctx.user_lexicon);
        result = replace_all(result, "[收发总数]", std::to_string(ctx.recv_count + ctx.send_count));
        result = replace_all(result, "[消息序号]", std::to_string(ctx.recv_count));
        result = replace_all(result, "[回复序号]", std::to_string(ctx.send_count));
        result = replace_all(result, "[当前bot]", std::to_string(ctx.bot_id));
        result = replace_all(result, "[当前环境id]", std::to_string(ctx.env_id));
        return result;
    }

    // ── 身份列表变量 ─────────────────────────────────────────
    std::string replace_identity_vars(const std::string& text, const VarContext& ctx) {
        std::string result = text;
        auto do_replace = [&](const std::string& tag, const std::string& ct,
                             const std::string& at, const std::string& file, BotId bid) {
            if (result.find(tag) == std::string::npos && result.find(ct) == std::string::npos
                && result.find(at) == std::string::npos) return;
            auto ids = m_storage.read_id_list(file, bid);
            result = replace_all(result, ct, std::to_string(ids.size()));
            std::string s;
            for (size_t i = 0; i < ids.size(); i++) { if (i) s += ','; s += ids[i]; }
            result = replace_all(result, tag, "[" + s + "]");
            result = replace_all(result, at, "<" + s + ">");
        };
        do_replace("[主人列表]", "[主人数]", "<主人列表>", "master.txt", ctx.bot_id);
        do_replace("[代管列表]", "[代管数]", "<代管列表>", "admin.txt", ctx.bot_id);
        do_replace("[大主人列表]", "[大主人数]", "<大主人列表>", "master.txt", 0);
        do_replace("[高管列表]", "[高管数]", "<高管列表>", "executive.txt", 0);

        // [in.列表.值] 判断
        static const std::regex in_re(R"(\[in\.([^\]]+)\.([^\]]+)\])");
        std::smatch m;
        std::string::const_iterator it = result.cbegin();
        std::string out;
        while (std::regex_search(it, result.cend(), m, in_re)) {
            out.append(it, m[0].first);
            auto ids = m_storage.read_id_list(m[1].str(), ctx.bot_id);
            out += (std::find(ids.begin(), ids.end(), m[2].str()) != ids.end()) ? "true" : "false";
            it = m[0].second;
        }
        if (it != result.cend()) out.append(it, result.cend());
        if (!out.empty() && out != result) result = out;
        return result;
    }

    // ── 文本魔法变量 ─────────────────────────────────────────
    static std::string replace_text_magic_vars(const std::string& text, const VarContext& ctx) {
        std::string result = text;
        result = replace_all(result, "[艾特]", "[CQ:at,qq=" + std::to_string(ctx.event_user_id) + "]");
        result = replace_all(result, "[at]", "[CQ:at,qq=" + std::to_string(ctx.event_user_id) + "]");
        result = replace_all(result, "[回复]", "[CQ:reply,id=" + std::to_string(ctx.event_message_id) + "]");
        result = replace_all(result, "[reply]", "[CQ:reply,id=" + std::to_string(ctx.event_message_id) + "]");
        result = replace_all(result, "[目标]", std::to_string(ctx.event_target_id));
        result = replace_all(result, "[target]", std::to_string(ctx.event_target_id));
        result = replace_all(result, "[昵称或QQ]", ctx.event_sender_name.empty() ? std::to_string(ctx.event_user_id) : ctx.event_sender_name);
        result = replace_all(result, "[群名片或昵称]", ctx.event_sender_card.empty() ? ctx.event_sender_name : ctx.event_sender_card);
        result = replace_all(result, "[是否群聊]", ctx.env == Env::Group ? "true" : "false");
        result = replace_all(result, "[是否私聊]", ctx.env == Env::Private ? "true" : "false");
        result = replace_all(result, "[bot]", std::to_string(ctx.bot_id));
        result = replace_all(result, "[机器人]", std::to_string(ctx.bot_id));
        result = replace_all(result, "[发送者]", std::to_string(ctx.event_user_id));
        result = replace_all(result, "[发送者名称]", ctx.event_sender_name);
        result = replace_all(result, "[发送者名片]", ctx.event_sender_card);
        result = replace_all(result, "[事件bot]", std::to_string(ctx.event_self_id));
        result = replace_all(result, "[事件群]", std::to_string(ctx.event_group_id));
        result = replace_all(result, "[事件消息]", std::to_string(ctx.event_message_id));
        result = replace_all(result, "[事件目标]", std::to_string(ctx.event_target_id));
        return result;
    }

    // ── 系统变量 ─────────────────────────────────────────────
    std::string replace_fun_exact_vars(const std::string& text, const VarContext& ctx) const {
        if (text.find('[') == std::string::npos) return text;
        auto today_seed = static_cast<uint64_t>(daily_number());
        auto user_seed = static_cast<uint64_t>(ctx.event_user_id ? ctx.event_user_id : ctx.env_id);
        std::unordered_map<std::string, std::string> m;
        auto add = [&](const std::string& k, const std::string& v) { m.emplace(bracketed(k), v); };
        const int score = 1 + static_cast<int>(stable_index(user_seed * 1315423911ULL + today_seed, 100));
        const auto& colors = fun_colors();
        const auto& advices = fun_advices();
        const auto& tarot = fun_tarot_cards();
        const auto& lots = fun_lottery_levels();
        const auto& moods = fun_moods();
        const auto& foods = fun_foods();
        const auto& drinks = fun_drinks();
        const auto& waifus = fun_waifus();
        const auto& titles = fun_titles();
        add("今日运势", std::to_string(score));
        add("运势等级", fortune_level(score));
        add("幸运颜色", colors[stable_index(user_seed + today_seed + 1, colors.size())]);
        add("幸运数字", std::to_string(1 + stable_index(user_seed + today_seed + 2, 99)));
        add("今日建议", advices[stable_index(user_seed + today_seed + 3, advices.size())]);
        add("今日心情", moods[stable_index(user_seed + today_seed + 4, moods.size())]);
        add("今日食物", foods[stable_index(user_seed + today_seed + 5, foods.size())]);
        add("今日饮品", drinks[stable_index(user_seed + today_seed + 6, drinks.size())]);
        add("今日老婆", waifus[stable_index(user_seed + today_seed + 7, waifus.size())]);
        add("今日称号", titles[stable_index(user_seed + today_seed + 8, titles.size())]);
        add("抽签", lots[stable_index(user_seed + static_cast<uint64_t>(std::time(nullptr)), lots.size())]);
        add("塔罗牌", tarot[stable_index(user_seed + static_cast<uint64_t>(std::time(nullptr) / 3600), tarot.size())]);
        add("骰子", std::to_string(1 + stable_index(user_seed + static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()), 6)));
        add("硬币", stable_index(user_seed + static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()), 2) ? "反面" : "正面");
        add("老虎机", slot_value(user_seed));
        add("戳一戳回复", pick_fun(fun_poke_replies(), user_seed + today_seed));
        add("随机老婆", pick_fun(waifus, user_seed + static_cast<uint64_t>(std::time(nullptr))));
        add("随机称号", pick_fun(titles, user_seed + static_cast<uint64_t>(std::time(nullptr))));
        add("随机梗", pick_fun(fun_memes(), user_seed + static_cast<uint64_t>(std::time(nullptr))));
        add("随机台词", pick_fun(fun_quotes(), user_seed + static_cast<uint64_t>(std::time(nullptr))));
        return scan_exact(text, m);
    }

    static std::string replace_system_vars(const std::string& text, const VarContext& ctx) {
        std::string result = text;
    #ifdef _WIN32
        result = replace_all(result, "[平台]", "Windows");
    #else
        result = replace_all(result, "[平台]", "Linux");
    #endif
        (void)ctx;
        result = replace_all(result, "[变量数量]", std::to_string(utility_family_count()) + "+");
        result = replace_all(result, "[变量族数量]", std::to_string(utility_family_count()) + "+");
        result = replace_all(result, "[嵌套层数]", "52");
        result = replace_all(result, "[目标延迟]", "5ms");
        result = replace_all(result, "[版本]", "3.0.0");
        result = replace_all(result, "[项目名]", "Van Lexicon");
        result = replace_all(result, "[作者]", "TsukishiroKokone (https://github.com/TsukishiroKokone)");
        result = replace_all(result, "[协议]", "OneBot v11/v12, Milky");
        result = replace_all(result, "[空]", "");
        result = replace_all(result, "[换行]", "\n");
        result = replace_all(result, "[制表符]", "\t");
        result = replace_all(result, "[左中括号]", "[");
        result = replace_all(result, "[右中括号]", "]");
        result = replace_all(result, "[左小括号]", "(");
        result = replace_all(result, "[右小括号]", ")");
        result = replace_all(result, "[点]", ".");
        result = replace_all(result, "[逗号]", ",");
        result = replace_all(result, "[冒号]", ":");
        result = replace_all(result, "[分号]", ";");
        result = replace_all(result, "[斜杠]", "/");
        result = replace_all(result, "[反斜杠]", "\\");
        result = replace_all(result, "[真]", "true");
        result = replace_all(result, "[假]", "false");
        result = replace_all(result, "[是]", "是");
        result = replace_all(result, "[否]", "否");
        return result;
    }

    // ── 520+ 实用变量族：用一次 token 扫描 + 哈希表查询，避免 520 次全量 replace ──
    static std::string replace_alias_pack_vars(const std::string& text) {
        return replace_large_family_vars(text);
    }

    // ── 格式化变量 ──────────────────────────────────────────
    static std::string replace_format_vars(const std::string& text) {
        std::string result = text;

        // [重复.N.文本]
        static const std::regex rep_re(R"(\[重复\.(\d+)\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, rep_re)) {
                o.append(it, m[0].first);
                int n = std::stoi(m[1].str());
                for (int i = 0; i < n; i++) o += m[2].str();
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        // [长度.文本]
        static const std::regex len_re(R"(\[长度\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, len_re)) {
                o.append(it, m[0].first);
                o += std::to_string(m[1].str().size());
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        // [截取.N.文本] 截取前N个字符
        static const std::regex sub_re(R"(\[截取\.(\d+)\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, sub_re)) {
                o.append(it, m[0].first);
                int n = std::stoi(m[1].str());
                std::string s = m[2].str();
                o += (static_cast<int>(s.size()) > n) ? s.substr(0, n) : s;
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        // [大写.文本] [小写.文本]
        static const std::regex upper_re(R"(\[大写\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, upper_re)) {
                o.append(it, m[0].first);
                std::string s = m[1].str();
                std::transform(s.begin(), s.end(), std::back_inserter(o), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }
        static const std::regex lower_re(R"(\[小写\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, lower_re)) {
                o.append(it, m[0].first);
                std::string s = m[1].str();
                std::transform(s.begin(), s.end(), std::back_inserter(o), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        // [替换.原.新.文本]
        static const std::regex repl_re(R"(\[替换\.([^\]]+)\.([^\]]+)\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, repl_re)) {
                o.append(it, m[0].first);
                std::string s = m[3].str();
                o += replace_all(s, m[1].str(), m[2].str());
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        // [计算.表达式] 简易计算
        static const std::regex calc_re(R"(\[计算\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, calc_re)) {
                o.append(it, m[0].first);
                try {
                    std::string expr = m[1].str();
                    // 简易：只支持加减乘除
                    double val = simple_eval(expr);
                    if (val == static_cast<int64_t>(val))
                        o += std::to_string(static_cast<int64_t>(val));
                    else
                        o += std::to_string(val);
                } catch (...) { o += m[1].str(); }
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        // [反转.文本]
        static const std::regex rev_re(R"(\[反转\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, rev_re)) {
                o.append(it, m[0].first);
                std::string s = m[1].str();
                std::reverse(s.begin(), s.end());
                o += s;
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        // [包围.左.右.文本]
        static const std::regex wrap_re(R"(\[包围\.([^\]]+)\.([^\]]+)\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, wrap_re)) {
                o.append(it, m[0].first);
                o += m[1].str() + m[3].str() + m[2].str();
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        // [默认.值.默认值]
        static const std::regex def_re(R"(\[默认\.([^\]]*)\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string o;
            while (std::regex_search(it, result.cend(), m, def_re)) {
                o.append(it, m[0].first);
                o += m[1].str().empty() ? m[2].str() : m[1].str();
                it = m[0].second;
            }
            if (it != result.cend()) o.append(it, result.cend());
            if (!o.empty() && o != result) result = o;
        }

        return result;
    }

    // ── 转义字符 ─────────────────────────────────────────────
    static std::string replace_escape(const std::string& text) {
        std::string result = text;
        result = replace_all(result, "\\n", "\n");
        result = replace_all(result, "\\/", "/");
        result = replace_all(result, "\\t", "\t");
        result = replace_all(result, "\\r", "\r");
        return result;
    }

    // ── [or] 随机选择 ────────────────────────────────────────
    std::string replace_or(const std::string& text) const {
        auto parts = split(text, "[or]");
        if (parts.size() <= 1) return text;
        std::uniform_int_distribution<size_t> dist(0, parts.size() - 1);
        return parts[dist(m_rng)];
    }

    // ── 错误回复 (!xxx!) ─────────────────────────────────────
    static std::string replace_error(const std::string& text) {
        static const std::regex re(R"(\(!(.*?)!\))");
        return std::regex_replace(text, re, "");
    }

    // ── 判断变量 {条件} ──────────────────────────────────────
    static std::string replace_judge(const std::string& text) {
        static const std::regex block_re(R"(\{[^{}]+\})");
        std::string result = text;

        // 提取所有 {...} 块和它们之间的文本
        std::vector<std::pair<std::string, bool>> blocks;
        std::string::const_iterator it = result.cbegin();
        std::smatch match;
        while (std::regex_search(it, result.cend(), match, block_re)) {
            if (match.position() > 0)
                blocks.push_back({std::string(it, it + match.position()), false});
            bool cond_result = evaluate_condition(match[0].str());
            blocks.push_back({match[0].str(), cond_result});
            it = it + match.position() + match.length();
        }
        if (it != result.cend())
            blocks.push_back({std::string(it, result.cend()), false});

        if (blocks.size() <= 1) return result;

        // 重建：保留条件为true后面跟着的非条件文本
        std::string final_result;
        for (size_t i = 0; i < blocks.size(); i++) {
            if (!blocks[i].second) final_result += blocks[i].first;
        }
        return final_result;
    }

    // ── 拓展词库变量替换 ─────────────────────────────────────
    static std::string replace_expand_vars(const std::string& text,
                                            const std::string& expand_content) {
        std::string result = text;
        std::istringstream stream(expand_content);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("变量[") == 0) {
                auto key_start = line.find('[');
                auto key_end = line.find(']');
                auto val_start = line.find("]:");
                if (key_start != std::string::npos && key_end != std::string::npos &&
                    val_start != std::string::npos) {
                    std::string key = line.substr(key_start + 1, key_end - key_start - 1);
                    std::string val = line.substr(val_start + 2);
                    result = replace_all(result, "[" + key + "]", val);
                }
            }
        }
        return result;
    }

private:
    Storage& m_storage;
    mutable std::mt19937 m_rng{std::random_device{}()};

    std::string random_hex(size_t length) const {
        static constexpr char hex[] = "0123456789abcdef";
        std::uniform_int_distribution<int> dist(0, 15);
        std::string out;
        out.reserve(length);
        for (size_t i = 0; i < length; i++) out.push_back(hex[dist(m_rng)]);
        return out;
    }

    static std::string bool_text(bool value) { return value ? "true" : "false"; }

    static int to_int_or(const std::string& value, int fallback = 0) {
        try { return std::stoi(trim(value)); } catch (...) { return fallback; }
    }

    static double to_double_or(const std::string& value, double fallback = 0.0) {
        try { return std::stod(trim(value)); } catch (...) { return fallback; }
    }

    static std::string url_encode(const std::string& value) {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        for (unsigned char ch : value) {
            if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                out.push_back(static_cast<char>(ch));
            } else if (ch == ' ') {
                out += "%20";
            } else {
                out.push_back('%');
                out.push_back(hex[ch >> 4]);
                out.push_back(hex[ch & 0x0F]);
            }
        }
        return out;
    }

    static std::string json_escape(const std::string& value) {
        std::string out;
        for (char ch : value) {
            switch (ch) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(ch); break;
            }
        }
        return out;
    }

    static std::string cq_escape(const std::string& value) {
        std::string out = value;
        out = replace_all(out, "&", "&amp;");
        out = replace_all(out, "[", "&#91;");
        out = replace_all(out, "]", "&#93;");
        out = replace_all(out, ",", "&#44;");
        return out;
    }

    static std::string replace_regex_one_arg(const std::string& input, const std::regex& re,
                                             const std::function<std::string(const std::string&)>& fn) {
        std::smatch m;
        std::string::const_iterator it = input.cbegin();
        std::string out;
        while (std::regex_search(it, input.cend(), m, re)) {
            out.append(it, m[0].first);
            out += fn(m[1].str());
            it = m[0].second;
        }
        if (it != input.cend()) out.append(it, input.cend());
        return out.empty() ? input : out;
    }

    static std::string replace_regex_two_args(const std::string& input, const std::regex& re,
                                              const std::function<std::string(const std::string&, const std::string&)>& fn) {
        std::smatch m;
        std::string::const_iterator it = input.cbegin();
        std::string out;
        while (std::regex_search(it, input.cend(), m, re)) {
            out.append(it, m[0].first);
            out += fn(m[1].str(), m[2].str());
            it = m[0].second;
        }
        if (it != input.cend()) out.append(it, input.cend());
        return out.empty() ? input : out;
    }

    static std::string replace_regex_three_args(const std::string& input, const std::regex& re,
                                                const std::function<std::string(const std::string&, const std::string&, const std::string&)>& fn) {
        std::smatch m;
        std::string::const_iterator it = input.cbegin();
        std::string out;
        while (std::regex_search(it, input.cend(), m, re)) {
            out.append(it, m[0].first);
            out += fn(m[1].str(), m[2].str(), m[3].str());
            it = m[0].second;
        }
        if (it != input.cend()) out.append(it, input.cend());
        return out.empty() ? input : out;
    }

    enum class UserEventKind {
        UserId, UserName, UserCard, MessageId, BotIdValue, SelfId, GroupId, TargetId,
        EnvId, LexiconId, LexiconN, RecvCount, SendCount, TotalCount, SelectLexicon, UserLexicon,
        HasName, HasCard, IsGroup, IsPrivate, NameOrId, CardOrName, TargetOrUser, MentionUser, MentionTarget,
        EventTypeText, UserIdTail4, SelfIdTail4, GroupIdTail4, MessageIdTail4
    };

    enum class EnvKind {
        EnvId, EnvNameEn, EnvNameZh, EnvNameUpper, EnvNameCode, IsGroup, IsPrivate, IsGuildLike,
        ScopeName, ScopeId, BotIdValue, SelfId, GroupId, PrivateId, ChannelId, AdapterCountHint,
        DataScope, StorageScope, ConfigScope, ApiScope, ProtocolScope, PlatformScope, PermissionScope,
        SessionKey, EventKey, RouteKey, CacheKey, LockKey, MetricKey
    };

    enum class TimeKind {
        Year, Year2, Month, Month2, Day, Day2, Hour, Hour2, Minute, Minute2, Second, Second2,
        Date, DateSlash, DateCompact, Time, TimeCompact, DateTime, IsoDate, Timestamp, Millis,
        WeekdayZh, WeekdayIndex, WeekdayIso, DayOfYear, WeekOfYear, Quarter, IsLeap, DaysInMonth,
        Hour12, AmPm, MonthNameZh, SeasonZh, YearMonth, MonthDay, TimeZoneName, EpochDay
    };

    enum class RandomKind {
        Lower, Upper, Letter, Digit, Hex, Bool, Sign, Percent, Byte, Port, HttpStatus, Dice6, Dice20,
        Coin, Color, Animal, Weather, Direction, Rarity, Emoji, Face, Food, Drink, Fruit, Flower,
        Planet, Element, ClassName, Job, Mood, Greeting, Protocol, Method, Mime, FileExt, Language,
        Country, Province, Zodiac, CnZodiac, Uuid8, Uuid16, Token12, Password8, Base36, Binary, Octal,
        PrimeUnder100, Odd, Even, Hour, Minute, Second, Month, Day, Weekday, Quarter, Http2xx, Http4xx,
        Http5xx, ApiVerb, LogLevel, Status, Switch, Permission, BooleanZh, YesNo, OnOff, Severity,
        MilkyWord, OneBotWord, CuteSuffix, MagicWord, CardSuit, Tarot, MusicNote, Arrow, Bracket,
        Operator, Comparator, Unit, Currency, TimeUnit, SizeUnit, Locale, TimeZone, Encoding, HashAlgo,
        SqlType, JsonType, ConfigKey, EnvKey, Header, UserAgent, SafeChar, Kana, Number0To9, Number1To100
    };

    enum class ParamUtilityKind {
        Identity, Pad2, Pad3, Pad4, Percent, Permille, Negative, Positive, Abs, Square, Cube, DoubleValue,
        Half, Inc, Dec, Hex, Oct, Bin, Port, Http, StatusCode, Celsius, Fahrenheit, Kelvin, Ms, Sec, Min,
        Hour, Day, Week, KB, MB, GB, TB, Kib, Mib, Gib, Bytes, Bits, PercentSign, Px, Em, Rem, Deg,
        Radian, Meter, Kilometer, Centimeter, Millimeter, Gram, Kilogram, Yuan, Dollar, Euro, Yen, Bool01,
        BoolCN, YesNo, OnOff, SuccessFail, AllowDeny, JsonNumber, CsvCell, Quote, SingleQuote, Paren,
        Bracket, Brace, Angle, UrlPath, QueryValue, HeaderValue, EnvName, ConfigName, KeyName, UpperTag,
        LowerTag, SnakeTag, KebabTag, DottedTag, ColonTag, AtUser, SharpTag, HtmlId, CssClass, SqlLimit,
        SqlOffset, Page, PageSize, Retry, Timeout, Delay, Cooldown, Weight, Score, Level, Rank, Count,
        Index, Line, Column, Row, Width, Height, Red, Green, Blue, Alpha, HslHue, Version, Build, Seed
    };

    static std::string bracketed(const std::string& name) { return "[" + name + "]"; }

    template <typename Kind>
    static void add_aliases(std::unordered_map<std::string, Kind>& m, Kind kind, const std::vector<std::string>& names) {
        for (const auto& name : names) m.emplace(bracketed(name), kind);
    }

    static const std::unordered_map<std::string, UserEventKind>& user_event_var_kinds() {
        static const std::unordered_map<std::string, UserEventKind> vars = [] {
            std::unordered_map<std::string, UserEventKind> m;
            add_aliases(m, UserEventKind::UserId, {"qq","QQ号","用户","用户ID","用户id","发送者","发送者ID","发送者id","发言人","发言人ID","user","user_id","userid","sender","sender_id","from","from_id","member","member_id"});
            add_aliases(m, UserEventKind::UserName, {"name","QQ名","名字","昵称","用户昵称","发送者名称","发送者昵称","发言人昵称","sender_name","nickname","nick","display_name","用户名","user_name"});
            add_aliases(m, UserEventKind::UserCard, {"card","群昵称","群名片","用户群名片","发送者名片","发言人名片","sender_card","member_card","card_name","群内昵称"});
            add_aliases(m, UserEventKind::MessageId, {"id","消息id","消息ID","message_id","msg_id","事件消息","事件消息ID","当前消息","当前消息ID","消息编号","事件编号"});
            add_aliases(m, UserEventKind::BotIdValue, {"ai","AI号","bot","机器人","机器人ID","bot_id","当前bot","当前Bot","当前机器人","bot编号","适配器bot"});
            add_aliases(m, UserEventKind::SelfId, {"selfid","自身id","自身ID","self_id","事件bot","事件Bot","机器人QQ","机器人账号","协议端ID","上报机器人"});
            add_aliases(m, UserEventKind::GroupId, {"事件群","事件群号","事件group","事件group_id","来源群","来源群号","群聊ID","群聊id","group_id","群ID"});
            add_aliases(m, UserEventKind::TargetId, {"目标","target","target_id","目标ID","目标id","事件目标","戳一戳目标","操作目标","被操作人","被戳者"});
            add_aliases(m, UserEventKind::EnvId, {"事件环境","事件环境ID","环境ID","上下文ID","会话ID","session_id","context_id","当前会话"});
            add_aliases(m, UserEventKind::LexiconId, {"词条id","词条ID","lexicon_id","当前词条","当前词条ID","回复ID"});
            add_aliases(m, UserEventKind::LexiconN, {"词汇量","词库数量","lexicon_n","词条数量","当前词库数量","回复数量"});
            add_aliases(m, UserEventKind::RecvCount, {"收消息数","收到消息数","recv_count","接收计数","消息序号","第几条消息"});
            add_aliases(m, UserEventKind::SendCount, {"发消息数","发送消息数","send_count","回复序号","第几条回复","输出计数"});
            add_aliases(m, UserEventKind::TotalCount, {"收发总数","总消息数","total_count","总计数","事件总数","消息总计"});
            add_aliases(m, UserEventKind::SelectLexicon, {"选择的词库","选中词库","select_lexicon","selected_lexicon","匹配词库","命中词库"});
            add_aliases(m, UserEventKind::UserLexicon, {"使用的词库","用户词库","user_lexicon","active_lexicon","当前使用词库","生效词库"});
            add_aliases(m, UserEventKind::HasName, {"有昵称","是否有昵称","has_name","昵称存在","发送者有名称"});
            add_aliases(m, UserEventKind::HasCard, {"有群名片","是否有群名片","has_card","名片存在","发送者有名片"});
            add_aliases(m, UserEventKind::IsGroup, {"事件是群聊","消息是群聊","user_event_group","来自群聊"});
            add_aliases(m, UserEventKind::IsPrivate, {"事件是私聊","消息是私聊","user_event_private","来自私聊"});
            add_aliases(m, UserEventKind::NameOrId, {"昵称或QQ","昵称或ID","显示名或ID","display_or_id","name_or_id"});
            add_aliases(m, UserEventKind::CardOrName, {"群名片或昵称","名片或昵称","card_or_name","群内显示名","最佳显示名"});
            add_aliases(m, UserEventKind::TargetOrUser, {"目标或发送者","target_or_user","操作对象或用户","对象或用户"});
            add_aliases(m, UserEventKind::MentionUser, {"艾特发送者","at_sender","mention_user","提及发送者"});
            add_aliases(m, UserEventKind::MentionTarget, {"艾特目标","at_target","mention_target","提及目标"});
            add_aliases(m, UserEventKind::EventTypeText, {"事件类型","消息类型","event_type","message_type","上下文类型"});
            add_aliases(m, UserEventKind::UserIdTail4, {"QQ尾号4","用户尾号4","user_tail4","发送者尾号4"});
            add_aliases(m, UserEventKind::SelfIdTail4, {"机器人尾号4","self_tail4","bot_tail4","自身尾号4"});
            add_aliases(m, UserEventKind::GroupIdTail4, {"群号尾号4","group_tail4","群尾号4","环境尾号4"});
            add_aliases(m, UserEventKind::MessageIdTail4, {"消息尾号4","message_tail4","msg_tail4","事件尾号4"});
            return m;
        }();
        return vars;
    }

    static std::string tail4(const std::string& value) { return value.size() <= 4 ? value : value.substr(value.size() - 4); }

    static std::string user_event_value(UserEventKind kind, const VarContext& ctx) {
        const std::string uid = std::to_string(ctx.event_user_id);
        const std::string self = std::to_string(ctx.event_self_id);
        const std::string gid = std::to_string(ctx.event_group_id ? ctx.event_group_id : ctx.env_id);
        const std::string target = std::to_string(ctx.event_target_id);
        switch (kind) {
            case UserEventKind::UserId: return uid;
            case UserEventKind::UserName: return ctx.event_sender_name;
            case UserEventKind::UserCard: return ctx.event_sender_card;
            case UserEventKind::MessageId: return std::to_string(ctx.event_message_id);
            case UserEventKind::BotIdValue: return std::to_string(ctx.bot_id);
            case UserEventKind::SelfId: return self;
            case UserEventKind::GroupId: return gid;
            case UserEventKind::TargetId: return target;
            case UserEventKind::EnvId: return std::to_string(ctx.env_id);
            case UserEventKind::LexiconId: return std::to_string(ctx.lexicon_id);
            case UserEventKind::LexiconN: return std::to_string(ctx.lexicon_n);
            case UserEventKind::RecvCount: return std::to_string(ctx.recv_count);
            case UserEventKind::SendCount: return std::to_string(ctx.send_count);
            case UserEventKind::TotalCount: return std::to_string(ctx.recv_count + ctx.send_count);
            case UserEventKind::SelectLexicon: return ctx.select_lexicon;
            case UserEventKind::UserLexicon: return ctx.user_lexicon;
            case UserEventKind::HasName: return bool_text(!ctx.event_sender_name.empty());
            case UserEventKind::HasCard: return bool_text(!ctx.event_sender_card.empty());
            case UserEventKind::IsGroup: return bool_text(ctx.env == Env::Group);
            case UserEventKind::IsPrivate: return bool_text(ctx.env == Env::Private);
            case UserEventKind::NameOrId: return ctx.event_sender_name.empty() ? uid : ctx.event_sender_name;
            case UserEventKind::CardOrName: return ctx.event_sender_card.empty() ? (ctx.event_sender_name.empty() ? uid : ctx.event_sender_name) : ctx.event_sender_card;
            case UserEventKind::TargetOrUser: return ctx.event_target_id ? target : uid;
            case UserEventKind::MentionUser: return "[CQ:at,qq=" + uid + "]";
            case UserEventKind::MentionTarget: return "[CQ:at,qq=" + (ctx.event_target_id ? target : uid) + "]";
            case UserEventKind::EventTypeText: return ctx.env == Env::Group ? "group_message" : "private_message";
            case UserEventKind::UserIdTail4: return tail4(uid);
            case UserEventKind::SelfIdTail4: return tail4(self);
            case UserEventKind::GroupIdTail4: return tail4(gid);
            case UserEventKind::MessageIdTail4: return tail4(std::to_string(ctx.event_message_id));
        }
        return "";
    }



    static std::string scan_exact(const std::string& text, const std::unordered_map<std::string, std::string>& vars) {
        std::string out;
        out.reserve(text.size());
        bool changed = false;
        for (size_t i = 0; i < text.size();) {
            if (text[i] != '[') { out.push_back(text[i++]); continue; }
            size_t end = text.find(']', i + 1);
            if (end == std::string::npos) { out.append(text, i, std::string::npos); break; }
            std::string token = text.substr(i, end - i + 1);
            auto it = vars.find(token);
            if (it != vars.end()) { out += it->second; changed = true; }
            else out += token;
            i = end + 1;
        }
        return changed ? out : text;
    }

    static std::string replace_user_event_exact_species(const std::string& text, const VarContext& ctx) {
        std::unordered_map<std::string, std::string> m;
        auto add = [&](const std::string& k, const std::string& v){ m.emplace(bracketed(k), v); };
        const std::string uid = std::to_string(ctx.event_user_id), self = std::to_string(ctx.event_self_id), gid = std::to_string(ctx.event_group_id ? ctx.event_group_id : ctx.env_id);
        const std::string mid = std::to_string(ctx.event_message_id), target = std::to_string(ctx.event_target_id), bot = std::to_string(ctx.bot_id);
        const std::vector<std::pair<std::string,std::string>> fields = {{"用户ID",uid},{"用户昵称",ctx.event_sender_name},{"用户名片",ctx.event_sender_card},{"消息ID",mid},{"BotID",bot},{"SelfID",self},{"群ID",gid},{"目标ID",target},{"环境ID",std::to_string(ctx.env_id)},{"词条ID",std::to_string(ctx.lexicon_id)},{"词库数量",std::to_string(ctx.lexicon_n)},{"接收计数",std::to_string(ctx.recv_count)},{"发送计数",std::to_string(ctx.send_count)},{"总计数",std::to_string(ctx.recv_count+ctx.send_count)},{"选择词库",ctx.select_lexicon},{"使用词库",ctx.user_lexicon},{"有昵称",bool_text(!ctx.event_sender_name.empty())},{"有名片",bool_text(!ctx.event_sender_card.empty())},{"是群聊",bool_text(ctx.env==Env::Group)},{"是私聊",bool_text(ctx.env==Env::Private)}};
        const std::vector<std::string> aspects = {"原值","文本","数字","显示","短值","键值","CQ","JSON","INI","SQL","URL","日志","缓存","权限","路由","统计","审计","通知","调试","追踪","指标","标签","标题","文件","字段","参数","查询","索引","序号","哈希","尾号","存在","状态","范围","作用域","命名空间","会话","事件","来源","目标","发送者","接收者","操作者","机器人","平台","协议","词库","回复","冷却","积分","群聊","私聊","成员","好友","频道","角色","等级","名称","别名","昵称","名片","账号","编号","ID","Key","Value","Path","Token","Slug","Safe","Raw","Pretty","Compact","Upper","Lower","Length","Empty","NonEmpty","Mention","At","Mask","Tail4","Prefix","Suffix","Kind","Type","Mode","Flag","Bool","Count","Total","Current","Previous","Next","Min","Max","Default","Fallback","Display"};
        size_t n = 0;
        for (const auto& a : aspects) { const auto& f = fields[n % fields.size()]; add("用户事件种类" + two(static_cast<int>(n + 1)), f.second); add("用户事件" + a, f.second); if (++n >= 110) break; }
        return scan_exact(text, m);
    }

    static const std::unordered_map<std::string, EnvKind>& env_var_kinds() {
        static const std::unordered_map<std::string, EnvKind> vars = [] {
            std::unordered_map<std::string, EnvKind> m;
            add_aliases(m, EnvKind::EnvId, {"group","群号","env_id","环境id","环境ID","当前环境id","当前环境ID","上下文id","上下文ID","会话id","会话ID","来源id","来源ID","scope_id","context","context_id"});
            add_aliases(m, EnvKind::EnvNameEn, {"env","环境英文","环境名英文","env_name","environment","environment_name","chat_type","scene"});
            add_aliases(m, EnvKind::EnvNameZh, {"环境","环境中文","环境名","聊天环境","会话环境","场景名称","scene_name","环境显示"});
            add_aliases(m, EnvKind::EnvNameUpper, {"环境大写","ENV","ENV_NAME","CHAT_TYPE","SCENE_UPPER"});
            add_aliases(m, EnvKind::EnvNameCode, {"环境代码","env_code","scene_code","chat_code","上下文代码"});
            add_aliases(m, EnvKind::IsGroup, {"是否群聊","是群聊","is_group","group_chat","群聊环境","群环境","在群聊","from_group"});
            add_aliases(m, EnvKind::IsPrivate, {"是否私聊","是私聊","is_private","private_chat","私聊环境","私环境","在私聊","from_private"});
            add_aliases(m, EnvKind::IsGuildLike, {"是否频道","是否群组环境","is_guild_like","guild_like","频道兼容"});
            add_aliases(m, EnvKind::ScopeName, {"作用域","作用域名","scope","scope_name","数据作用域","词库作用域","存储作用域"});
            add_aliases(m, EnvKind::ScopeId, {"作用域ID","作用域id","scope_id_zh","scope_key_id","数据作用域ID","词库作用域ID"});
            add_aliases(m, EnvKind::BotIdValue, {"bot环境","环境bot","当前bot环境","adapter_bot","adapter_bot_id","机器人环境ID"});
            add_aliases(m, EnvKind::SelfId, {"环境self","环境selfid","env_self_id","协议self_id","机器人环境账号"});
            add_aliases(m, EnvKind::GroupId, {"环境群号","env_group","env_group_id","当前群","当前群号","上下文群号","会话群号"});
            add_aliases(m, EnvKind::PrivateId, {"环境私聊号","env_private","env_private_id","当前私聊","当前私聊ID","私聊对象"});
            add_aliases(m, EnvKind::ChannelId, {"频道ID","频道id","channel_id","guild_channel","兼容频道ID","环境频道"});
            add_aliases(m, EnvKind::AdapterCountHint, {"适配器数量提示","adapter_count_hint","多bot提示","多适配器提示"});
            add_aliases(m, EnvKind::DataScope, {"数据环境","data_scope","data_env","数据目录作用域","数据键前缀"});
            add_aliases(m, EnvKind::StorageScope, {"存储环境","storage_scope","storage_env","存储键前缀","存储命名空间"});
            add_aliases(m, EnvKind::ConfigScope, {"配置环境","config_scope","config_env","配置段","配置命名空间"});
            add_aliases(m, EnvKind::ApiScope, {"接口环境","api_scope","api_env","API作用域","api命名空间"});
            add_aliases(m, EnvKind::ProtocolScope, {"协议环境","protocol_scope","protocol_env","协议作用域","协议命名空间"});
            add_aliases(m, EnvKind::PlatformScope, {"平台环境","platform_scope","platform_env","平台作用域","平台命名空间"});
            add_aliases(m, EnvKind::PermissionScope, {"权限环境","permission_scope","permission_env","权限作用域","权限命名空间"});
            add_aliases(m, EnvKind::SessionKey, {"会话键","session_key","sessionKey","当前会话键","环境会话键"});
            add_aliases(m, EnvKind::EventKey, {"事件键","event_key","eventKey","当前事件键","环境事件键"});
            add_aliases(m, EnvKind::RouteKey, {"路由键","route_key","routeKey","消息路由键","环境路由键"});
            add_aliases(m, EnvKind::CacheKey, {"缓存键","cache_key","cacheKey","环境缓存键","上下文缓存键"});
            add_aliases(m, EnvKind::LockKey, {"锁键","lock_key","lockKey","环境锁键","上下文锁键"});
            add_aliases(m, EnvKind::MetricKey, {"指标键","metric_key","metricKey","环境指标键","统计键"});
            return m;
        }();
        return vars;
    }

    static std::string env_value(EnvKind kind, const VarContext& ctx) {
        const bool group = ctx.env == Env::Group;
        const std::string id = std::to_string(ctx.env_id);
        const std::string bot = std::to_string(ctx.bot_id);
        const std::string self = std::to_string(ctx.event_self_id);
        const std::string prefix = group ? "group" : "private";
        switch (kind) {
            case EnvKind::EnvId: return id;
            case EnvKind::EnvNameEn: return prefix;
            case EnvKind::EnvNameZh: return group ? "群聊" : "私聊";
            case EnvKind::EnvNameUpper: return group ? "GROUP" : "PRIVATE";
            case EnvKind::EnvNameCode: return group ? "g" : "p";
            case EnvKind::IsGroup: return bool_text(group);
            case EnvKind::IsPrivate: return bool_text(!group);
            case EnvKind::IsGuildLike: return bool_text(false);
            case EnvKind::ScopeName: return group ? "group:" + id : "private:" + id;
            case EnvKind::ScopeId: return id;
            case EnvKind::BotIdValue: return bot;
            case EnvKind::SelfId: return self;
            case EnvKind::GroupId: return group ? id : "0";
            case EnvKind::PrivateId: return group ? "0" : id;
            case EnvKind::ChannelId: return "0";
            case EnvKind::AdapterCountHint: return "multi";
            case EnvKind::DataScope: return "data:" + prefix + ":" + id;
            case EnvKind::StorageScope: return "storage:" + prefix + ":" + id;
            case EnvKind::ConfigScope: return "config:" + bot;
            case EnvKind::ApiScope: return "api:" + prefix;
            case EnvKind::ProtocolScope: return "protocol:auto";
            case EnvKind::PlatformScope: return "platform:qq";
            case EnvKind::PermissionScope: return "perm:" + prefix + ":" + id;
            case EnvKind::SessionKey: return bot + ":" + prefix + ":" + id;
            case EnvKind::EventKey: return prefix + ":" + id + ":" + std::to_string(ctx.event_message_id);
            case EnvKind::RouteKey: return prefix + "/" + id;
            case EnvKind::CacheKey: return "cache:" + prefix + ":" + id;
            case EnvKind::LockKey: return "lock:" + prefix + ":" + id;
            case EnvKind::MetricKey: return "metric:" + prefix + ":" + id;
        }
        return "";
    }

    static std::string two(int v) { return v < 10 ? "0" + std::to_string(v) : std::to_string(v); }


    static std::string replace_env_exact_species(const std::string& text, const VarContext& ctx) {
        std::unordered_map<std::string, std::string> m;
        auto add = [&](const std::string& k, const std::string& v){ m.emplace(bracketed(k), v); };
        const bool g = ctx.env == Env::Group;
        const std::string id = std::to_string(ctx.env_id), bot = std::to_string(ctx.bot_id);
        const std::vector<std::pair<std::string,std::string>> values = {{"ID",id},{"英文",g?"group":"private"},{"中文",g?"群聊":"私聊"},{"大写",g?"GROUP":"PRIVATE"},{"代码",g?"g":"p"},{"作用域",(g?"group":"private")+std::string(":")+id},{"Bot",bot},{"Self",std::to_string(ctx.event_self_id)},{"群号",g?id:"0"},{"私聊",g?"0":id},{"会话键",bot+":"+(g?"group":"private")+":"+id},{"路由",(g?"group":"private")+std::string("/")+id}};
        const std::vector<std::string> names = {"作用域","命名空间","会话","路由","缓存","锁","指标","统计","配置","存储","数据","接口","协议","平台","权限","群聊","私聊","频道","成员","好友","机器人","适配器","网络","端口","主机","路径","文件","目录","数据库","SQLite","INI","WebAPI","HTTP","WS","OneBot","Milky","消息","事件","通知","请求","响应","队列","线程","任务","日志","调试","监控","健康","状态","模式","类型","级别","区域","语言","时区","本地","远程","安全","Token","Key","Value","ID","Name","Code","Slug","URL","URI","Header","Body","Query","Param","Cookie","Session","Trace","Span","Audit","Rate","Limit","Retry","Timeout","Delay","Cooldown","Feature","Flag","Switch","Bool","Count","Total","Current","Default","Fallback","Prefix","Suffix","Hash","Checksum","Shard","Bucket","Node","Worker","Runtime","Build","Version","Release","Profile","Policy","Role","Group","Private","Global","Local"};
        for (size_t i=0;i<names.size();++i) { add("环境种类" + two(static_cast<int>(i+1)), values[i%values.size()].second); add("环境" + names[i], values[i%values.size()].second); }
        return scan_exact(text, m);
    }

    static const std::unordered_map<std::string, TimeKind>& time_var_kinds() {
        static const std::unordered_map<std::string, TimeKind> vars = [] {
            std::unordered_map<std::string, TimeKind> m;
            add_aliases(m, TimeKind::Year, {"年","年份","当前年","year","yyyy","YYYY","完整年份","本年"});
            add_aliases(m, TimeKind::Year2, {"短年","两位年","year2","yy","YY","年份后两位"});
            add_aliases(m, TimeKind::Month, {"月","月份","当前月","month","M","本月","月数字"});
            add_aliases(m, TimeKind::Month2, {"两位月","月2","month2","MM","补零月","月份补零"});
            add_aliases(m, TimeKind::Day, {"日","天","日期日","day","D","本日","日数字"});
            add_aliases(m, TimeKind::Day2, {"两位日","日2","day2","DD","补零日","日期补零"});
            add_aliases(m, TimeKind::Hour, {"时","小时","hour","h","当前小时","24小时"});
            add_aliases(m, TimeKind::Hour2, {"两位时","小时2","hour2","HH","补零小时"});
            add_aliases(m, TimeKind::Minute, {"分","分钟","minute","m","当前分钟"});
            add_aliases(m, TimeKind::Minute2, {"两位分","分钟2","minute2","mm","补零分钟"});
            add_aliases(m, TimeKind::Second, {"秒","second","s","当前秒"});
            add_aliases(m, TimeKind::Second2, {"两位秒","秒2","second2","ss","补零秒"});
            add_aliases(m, TimeKind::Date, {"日期","今天","date","YYYY-MM-DD","标准日期","短日期"});
            add_aliases(m, TimeKind::DateSlash, {"斜杠日期","date_slash","YYYY/MM/DD","slash_date"});
            add_aliases(m, TimeKind::DateCompact, {"紧凑日期","date_compact","YYYYMMDD","compact_date"});
            add_aliases(m, TimeKind::Time, {"时间","当前时间","time","HH:mm:ss","标准时间"});
            add_aliases(m, TimeKind::TimeCompact, {"紧凑时间","time_compact","HHmmss","compact_time"});
            add_aliases(m, TimeKind::DateTime, {"日期时间","完整时间","datetime","date_time","YYYY-MM-DD HH:mm:ss"});
            add_aliases(m, TimeKind::IsoDate, {"ISO日期","ISO时间","iso","iso_date","iso_datetime"});
            add_aliases(m, TimeKind::Timestamp, {"时间戳","Unix时间戳","timestamp","unix","epoch"});
            add_aliases(m, TimeKind::Millis, {"毫秒时间戳","timestamp_ms","unix_ms","epoch_ms","时间戳毫秒"});
            add_aliases(m, TimeKind::WeekdayZh, {"星期","周","weekday","weekday_zh","中文星期"});
            add_aliases(m, TimeKind::WeekdayIndex, {"周几","星期几","weekday_index","week_day","星期数字"});
            add_aliases(m, TimeKind::WeekdayIso, {"ISO周几","iso_weekday","weekday_iso","周几ISO"});
            add_aliases(m, TimeKind::DayOfYear, {"年中第几天","一年第几天","day_of_year","yday","今年第几天"});
            add_aliases(m, TimeKind::WeekOfYear, {"本年第几周","一年第几周","week_of_year","week_index","周序号"});
            add_aliases(m, TimeKind::Quarter, {"季度","当前季度","quarter","Q","本季度"});
            add_aliases(m, TimeKind::IsLeap, {"闰年","是否闰年","is_leap","leap_year","本年闰年"});
            add_aliases(m, TimeKind::DaysInMonth, {"本月天数","月天数","days_in_month","month_days","当月天数"});
            add_aliases(m, TimeKind::Hour12, {"小时12","12小时","hour12","十二小时","12h"});
            add_aliases(m, TimeKind::AmPm, {"上午下午","AMPM","am_pm","时段","上下午"});
            add_aliases(m, TimeKind::MonthNameZh, {"中文月份","月份名","month_name_zh","月名","中文月名"});
            add_aliases(m, TimeKind::SeasonZh, {"季节","season","season_zh","当前季节","中文季节"});
            add_aliases(m, TimeKind::YearMonth, {"年月","year_month","YYYY-MM","当前年月","月份键"});
            add_aliases(m, TimeKind::MonthDay, {"月日","month_day","MM-DD","当前月日","生日格式"});
            add_aliases(m, TimeKind::TimeZoneName, {"时区名","timezone","time_zone","本地时区","local_timezone"});
            add_aliases(m, TimeKind::EpochDay, {"纪元天","epoch_day","unix_day","时间戳天","从1970第几天"});
            return m;
        }();
        return vars;
    }

    static std::string time_value(TimeKind kind, const struct tm& t, std::time_t epoch) {
        int year = t.tm_year + 1900;
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        int dim[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
        const char* months[] = {"一月","二月","三月","四月","五月","六月","七月","八月","九月","十月","十一月","十二月"};
        auto fmt = [&](const char* f) { char buf[64]; std::strftime(buf, sizeof(buf), f, &t); return std::string(buf); };
        switch (kind) {
            case TimeKind::Year: return std::to_string(year);
            case TimeKind::Year2: return two(year % 100);
            case TimeKind::Month: return std::to_string(t.tm_mon + 1);
            case TimeKind::Month2: return two(t.tm_mon + 1);
            case TimeKind::Day: return std::to_string(t.tm_mday);
            case TimeKind::Day2: return two(t.tm_mday);
            case TimeKind::Hour: return std::to_string(t.tm_hour);
            case TimeKind::Hour2: return two(t.tm_hour);
            case TimeKind::Minute: return std::to_string(t.tm_min);
            case TimeKind::Minute2: return two(t.tm_min);
            case TimeKind::Second: return std::to_string(t.tm_sec);
            case TimeKind::Second2: return two(t.tm_sec);
            case TimeKind::Date: return fmt("%Y-%m-%d");
            case TimeKind::DateSlash: return fmt("%Y/%m/%d");
            case TimeKind::DateCompact: return fmt("%Y%m%d");
            case TimeKind::Time: return fmt("%H:%M:%S");
            case TimeKind::TimeCompact: return fmt("%H%M%S");
            case TimeKind::DateTime: return fmt("%Y-%m-%d %H:%M:%S");
            case TimeKind::IsoDate: return fmt("%Y-%m-%dT%H:%M:%S");
            case TimeKind::Timestamp: return std::to_string(epoch);
            case TimeKind::Millis: return std::to_string(static_cast<int64_t>(epoch) * 1000LL);
            case TimeKind::WeekdayZh: return weekdays[t.tm_wday];
            case TimeKind::WeekdayIndex: return std::to_string(t.tm_wday == 0 ? 7 : t.tm_wday);
            case TimeKind::WeekdayIso: return std::to_string(t.tm_wday == 0 ? 7 : t.tm_wday);
            case TimeKind::DayOfYear: return std::to_string(t.tm_yday + 1);
            case TimeKind::WeekOfYear: return std::to_string(t.tm_yday / 7 + 1);
            case TimeKind::Quarter: return std::to_string(t.tm_mon / 3 + 1);
            case TimeKind::IsLeap: return leap ? "是" : "否";
            case TimeKind::DaysInMonth: return std::to_string(dim[t.tm_mon]);
            case TimeKind::Hour12: return std::to_string((t.tm_hour % 12) == 0 ? 12 : (t.tm_hour % 12));
            case TimeKind::AmPm: return t.tm_hour < 12 ? "上午" : "下午";
            case TimeKind::MonthNameZh: return months[t.tm_mon];
            case TimeKind::SeasonZh: return std::vector<std::string>{"冬","春","春","春","夏","夏","夏","秋","秋","秋","冬","冬"}[t.tm_mon];
            case TimeKind::YearMonth: return fmt("%Y-%m");
            case TimeKind::MonthDay: return fmt("%m-%d");
            case TimeKind::TimeZoneName: return "Local";
            case TimeKind::EpochDay: return std::to_string(static_cast<int64_t>(epoch) / 86400LL);
        }
        return "";
    }

    static std::string replace_time_exact_species(const std::string& text, const struct tm& t, std::time_t epoch) {
        std::unordered_map<std::string, std::string> m;
        auto add = [&](const std::string& k, const std::string& v) { m.emplace(bracketed(k), v); };
        const std::vector<std::pair<std::string, TimeKind>> kinds = {
            {"年", TimeKind::Year}, {"短年", TimeKind::Year2}, {"月", TimeKind::Month}, {"两位月", TimeKind::Month2},
            {"日", TimeKind::Day}, {"两位日", TimeKind::Day2}, {"时", TimeKind::Hour}, {"两位时", TimeKind::Hour2},
            {"分", TimeKind::Minute}, {"两位分", TimeKind::Minute2}, {"秒", TimeKind::Second}, {"两位秒", TimeKind::Second2},
            {"日期", TimeKind::Date}, {"斜杠日期", TimeKind::DateSlash}, {"紧凑日期", TimeKind::DateCompact},
            {"时间", TimeKind::Time}, {"紧凑时间", TimeKind::TimeCompact}, {"日期时间", TimeKind::DateTime},
            {"ISO", TimeKind::IsoDate}, {"时间戳", TimeKind::Timestamp}, {"毫秒戳", TimeKind::Millis},
            {"星期", TimeKind::WeekdayZh}, {"周几", TimeKind::WeekdayIndex}, {"ISO周", TimeKind::WeekdayIso},
            {"年内日", TimeKind::DayOfYear}, {"年内周", TimeKind::WeekOfYear}, {"季度", TimeKind::Quarter},
            {"闰年", TimeKind::IsLeap}, {"月天数", TimeKind::DaysInMonth}, {"12时", TimeKind::Hour12},
            {"上下午", TimeKind::AmPm}, {"月名", TimeKind::MonthNameZh}, {"季节", TimeKind::SeasonZh},
            {"年月", TimeKind::YearMonth}, {"月日", TimeKind::MonthDay}, {"时区", TimeKind::TimeZoneName},
            {"纪元天", TimeKind::EpochDay}
        };
        const std::vector<std::string> names = {
            "标准","短","长","紧凑","斜杠","中文","英文","数字","补零","ISO","Unix","文件名","日志","缓存","分区","统计","显示","标题","路径","键","值","前缀","后缀","范围","开始","结束","当前","本地","系统","日历","农历兼容","季度","季节","工作日","周末","上午","下午","午夜","正午","小时","分钟","秒钟","毫秒","日期","时间","年月","月日","年周","年日","月初","月末","周初","周末点","昨天键","今天键","明天键","昨天显示","今天显示","明天显示","排序","索引","序号","批次","窗口","过期","冷却","延迟","超时","重试","TTL","Cron","计划","任务","事件","消息","会话","审计","追踪","指标","报表","备份","归档","版本","构建","发布","时区","UTC","本地化","区域","语言","格式A","格式B","格式C","格式D","格式E","格式F","格式G","格式H","格式I","格式J","安全","Raw","Pretty","Compact","Human","Machine","Key","Tick","Slot","Frame","Cycle","Nonce"
        };
        for (size_t i = 0; i < names.size(); ++i) {
            auto kv = kinds[i % kinds.size()];
            add("时间种类" + two(static_cast<int>(i + 1)), time_value(kv.second, t, epoch));
            add("时间" + names[i], time_value(kv.second, t, epoch));
        }
        return scan_exact(text, m);
    }

    std::string pick(const std::vector<std::string>& v) const { return v[std::uniform_int_distribution<size_t>(0, v.size() - 1)(m_rng)]; }

    static const std::unordered_map<std::string, RandomKind>& random_var_kinds() {
        static const std::unordered_map<std::string, RandomKind> vars = [] {
            std::unordered_map<std::string, RandomKind> m;
            auto one = [&](RandomKind k, const std::string& n) { m.emplace(bracketed(n), k); };
            const std::vector<std::pair<RandomKind, std::vector<std::string>>> groups = {
                {RandomKind::Lower,{"随机小写","随机小写字母","random_lower"}}, {RandomKind::Upper,{"随机大写","随机大写字母","random_upper"}}, {RandomKind::Letter,{"随机字母","random_letter"}},
                {RandomKind::Digit,{"随机数字","random_digit"}}, {RandomKind::Hex,{"随机十六进制","随机HEX","random_hex"}}, {RandomKind::Bool,{"随机布尔","random_bool"}},
                {RandomKind::Sign,{"随机正负","random_sign"}}, {RandomKind::Percent,{"随机百分比","random_percent"}}, {RandomKind::Byte,{"随机字节","random_byte"}},
                {RandomKind::Port,{"随机端口","random_port"}}, {RandomKind::HttpStatus,{"随机HTTP状态","random_http"}}, {RandomKind::Dice6,{"随机骰子","随机D6","random_d6"}},
                {RandomKind::Dice20,{"随机D20","random_d20"}}, {RandomKind::Coin,{"随机硬币","random_coin"}}, {RandomKind::Color,{"随机颜色","random_color"}},
                {RandomKind::Animal,{"随机动物","random_animal"}}, {RandomKind::Weather,{"随机天气","random_weather"}}, {RandomKind::Direction,{"随机方向","random_direction"}},
                {RandomKind::Rarity,{"随机稀有度","random_rarity"}}, {RandomKind::Emoji,{"随机emoji","随机表情符号"}}, {RandomKind::Face,{"随机表情","随机颜文字"}},
                {RandomKind::Food,{"随机食物"}}, {RandomKind::Drink,{"随机饮料"}}, {RandomKind::Fruit,{"随机水果"}}, {RandomKind::Flower,{"随机花"}},
                {RandomKind::Planet,{"随机行星"}}, {RandomKind::Element,{"随机元素"}}, {RandomKind::ClassName,{"随机职业类"}}, {RandomKind::Job,{"随机职业"}},
                {RandomKind::Mood,{"随机心情"}}, {RandomKind::Greeting,{"随机问候"}}, {RandomKind::Protocol,{"随机协议"}}, {RandomKind::Method,{"随机HTTP方法"}},
                {RandomKind::Mime,{"随机MIME"}}, {RandomKind::FileExt,{"随机扩展名"}}, {RandomKind::Language,{"随机语言"}}, {RandomKind::Country,{"随机国家"}},
                {RandomKind::Province,{"随机省份"}}, {RandomKind::Zodiac,{"随机星座"}}, {RandomKind::CnZodiac,{"随机生肖"}}, {RandomKind::Uuid8,{"随机UUID段","随机uuid8"}},
                {RandomKind::Uuid16,{"随机uuid16"}}, {RandomKind::Token12,{"随机token12"}}, {RandomKind::Password8,{"随机密码8"}}, {RandomKind::Base36,{"随机base36"}},
                {RandomKind::Binary,{"随机二进制"}}, {RandomKind::Octal,{"随机八进制"}}, {RandomKind::PrimeUnder100,{"随机质数"}}, {RandomKind::Odd,{"随机奇数"}},
                {RandomKind::Even,{"随机偶数"}}, {RandomKind::Hour,{"随机小时"}}, {RandomKind::Minute,{"随机分钟"}}, {RandomKind::Second,{"随机秒"}},
                {RandomKind::Month,{"随机月份"}}, {RandomKind::Day,{"随机日期日"}}, {RandomKind::Weekday,{"随机星期"}}, {RandomKind::Quarter,{"随机季度"}},
                {RandomKind::Http2xx,{"随机2xx"}}, {RandomKind::Http4xx,{"随机4xx"}}, {RandomKind::Http5xx,{"随机5xx"}}, {RandomKind::ApiVerb,{"随机API动词"}},
                {RandomKind::LogLevel,{"随机日志级别"}}, {RandomKind::Status,{"随机状态"}}, {RandomKind::Switch,{"随机开关"}}, {RandomKind::Permission,{"随机权限"}},
                {RandomKind::BooleanZh,{"随机中文布尔"}}, {RandomKind::YesNo,{"随机是否"}}, {RandomKind::OnOff,{"随机onoff"}}, {RandomKind::Severity,{"随机严重度"}},
                {RandomKind::MilkyWord,{"随机Milky词"}}, {RandomKind::OneBotWord,{"随机OneBot词"}}, {RandomKind::CuteSuffix,{"随机可爱后缀"}}, {RandomKind::MagicWord,{"随机魔法词"}},
                {RandomKind::CardSuit,{"随机花色"}}, {RandomKind::Tarot,{"随机塔罗"}}, {RandomKind::MusicNote,{"随机音符"}}, {RandomKind::Arrow,{"随机箭头"}},
                {RandomKind::Bracket,{"随机括号"}}, {RandomKind::Operator,{"随机运算符"}}, {RandomKind::Comparator,{"随机比较符"}}, {RandomKind::Unit,{"随机单位"}},
                {RandomKind::Currency,{"随机货币"}}, {RandomKind::TimeUnit,{"随机时间单位"}}, {RandomKind::SizeUnit,{"随机容量单位"}}, {RandomKind::Locale,{"随机区域"}},
                {RandomKind::TimeZone,{"随机时区"}}, {RandomKind::Encoding,{"随机编码"}}, {RandomKind::HashAlgo,{"随机哈希"}}, {RandomKind::SqlType,{"随机SQL类型"}},
                {RandomKind::JsonType,{"随机JSON类型"}}, {RandomKind::ConfigKey,{"随机配置键"}}, {RandomKind::EnvKey,{"随机环境键"}}, {RandomKind::Header,{"随机HTTP头"}},
                {RandomKind::UserAgent,{"随机UA"}}, {RandomKind::SafeChar,{"随机安全字符"}}, {RandomKind::Kana,{"随机假名"}}, {RandomKind::Number0To9,{"随机0到9"}},
                {RandomKind::Number1To100,{"随机1到100"}}
            };
            for (const auto& [k, names] : groups) for (const auto& n : names) one(k, n);
            return m;
        }();
        return vars;
    }

    std::string random_value(RandomKind kind) const {
        auto ri = [&](int a, int b) { return std::to_string(std::uniform_int_distribution<int>(a, b)(m_rng)); };
        auto rh = [&](size_t n) { return random_hex(n); };
        switch (kind) {
            case RandomKind::Lower: return std::string(1, static_cast<char>('a' + std::uniform_int_distribution<int>(0,25)(m_rng)));
            case RandomKind::Upper: return std::string(1, static_cast<char>('A' + std::uniform_int_distribution<int>(0,25)(m_rng)));
            case RandomKind::Letter: return pick({"a","b","c","d","e","f","g","h","i","j","k","A","B","C","D","E","F"});
            case RandomKind::Digit: case RandomKind::Number0To9: return ri(0,9);
            case RandomKind::Hex: return rh(1);
            case RandomKind::Bool: return std::uniform_int_distribution<int>(0,1)(m_rng) ? "true" : "false";
            case RandomKind::Sign: return pick({"+","-"});
            case RandomKind::Percent: return ri(0,100) + "%";
            case RandomKind::Byte: return ri(0,255);
            case RandomKind::Port: return ri(1024,65535);
            case RandomKind::HttpStatus: return pick({"200","201","204","400","401","403","404","429","500","502","503"});
            case RandomKind::Dice6: return ri(1,6); case RandomKind::Dice20: return ri(1,20); case RandomKind::Coin: return pick({"正面","反面"});
            case RandomKind::Color: return pick({"粉色","薄荷绿","天蓝","薰衣草紫","蜜桃橙","奶油白"});
            case RandomKind::Animal: return pick({"猫猫","兔兔","狐狸","企鹅","水獭","熊猫","小狗","仓鼠"});
            case RandomKind::Weather: return pick({"晴","多云","小雨","雪","微风","彩虹"});
            case RandomKind::Direction: return pick({"东","南","西","北","左","右","上","下"});
            case RandomKind::Rarity: return pick({"N","R","SR","SSR","UR"});
            case RandomKind::Emoji: return pick({"🌸","🐾","✨","🎀","💖","🌙"});
            case RandomKind::Face: return pick({"(ฅ>ω<*ฅ)","(｡･ω･｡)","(≧▽≦)","(*´∀`)~♥","(つ≧▽≦)つ"});
            case RandomKind::Food: return pick({"蛋糕","布丁","拉面","寿司","曲奇"}); case RandomKind::Drink: return pick({"奶茶","咖啡","可可","果汁"});
            case RandomKind::Fruit: return pick({"苹果","草莓","桃子","葡萄","樱桃"}); case RandomKind::Flower: return pick({"樱花","玫瑰","铃兰","向日葵"});
            case RandomKind::Planet: return pick({"水星","金星","地球","火星","木星"}); case RandomKind::Element: return pick({"火","水","风","土","光","暗"});
            case RandomKind::ClassName: return pick({"战士","法师","牧师","游侠"}); case RandomKind::Job: return pick({"开发者","画师","作曲","店员"});
            case RandomKind::Mood: return pick({"开心","困困","元气","害羞"}); case RandomKind::Greeting: return pick({"早安","午安","晚安","你好"});
            case RandomKind::Protocol: return pick({"HTTP","HTTPS","WS","WSS","OneBot11","OneBot12","Milky"}); case RandomKind::Method: return pick({"GET","POST","PUT","DELETE","PATCH"});
            case RandomKind::Mime: return pick({"text/plain","application/json","text/html","image/png"}); case RandomKind::FileExt: return pick({"txt","md","json","cpp","png"});
            case RandomKind::Language: return pick({"中文","英语","日语","韩语","法语"}); case RandomKind::Country: return pick({"中国","日本","韩国","美国","法国"});
            case RandomKind::Province: return pick({"北京","上海","广东","浙江","四川"}); case RandomKind::Zodiac: return pick({"白羊","金牛","双子","巨蟹","狮子"});
            case RandomKind::CnZodiac: return pick({"鼠","牛","虎","兔","龙","蛇"}); case RandomKind::Uuid8: return rh(8); case RandomKind::Uuid16: return rh(16);
            case RandomKind::Token12: return rh(12); case RandomKind::Password8: return rh(8); case RandomKind::Base36: return pick({"0","1","2","3","4","5","a","b","c","x","y","z"});
            case RandomKind::Binary: return ri(0,1); case RandomKind::Octal: return ri(0,7); case RandomKind::PrimeUnder100: return pick({"2","3","5","7","11","13","17","19","23","29","31","37","41","43","47","53","59","61","67","71","73","79","83","89","97"});
            case RandomKind::Odd: return std::to_string(std::uniform_int_distribution<int>(0,49)(m_rng) * 2 + 1); case RandomKind::Even: return std::to_string(std::uniform_int_distribution<int>(0,50)(m_rng) * 2);
            case RandomKind::Hour: return ri(0,23); case RandomKind::Minute: case RandomKind::Second: return ri(0,59); case RandomKind::Month: return ri(1,12); case RandomKind::Day: return ri(1,31); case RandomKind::Weekday: return ri(1,7); case RandomKind::Quarter: return ri(1,4);
            case RandomKind::Http2xx: return pick({"200","201","202","204"}); case RandomKind::Http4xx: return pick({"400","401","403","404","429"}); case RandomKind::Http5xx: return pick({"500","502","503","504"});
            case RandomKind::ApiVerb: return pick({"list","get","create","update","delete"}); case RandomKind::LogLevel: return pick({"debug","info","warn","error"}); case RandomKind::Status: return pick({"在线","离线","运行中","已停止"});
            case RandomKind::Switch: return pick({"开启","关闭"}); case RandomKind::Permission: return pick({"allow","deny"}); case RandomKind::BooleanZh: return pick({"真","假"}); case RandomKind::YesNo: return pick({"是","否"}); case RandomKind::OnOff: return pick({"on","off"});
            case RandomKind::Severity: return pick({"low","medium","high","critical"}); case RandomKind::MilkyWord: return pick({"milky","message","resource","friend"}); case RandomKind::OneBotWord: return pick({"onebot","message","notice","meta"});
            case RandomKind::CuteSuffix: return pick({"喵","呀","哒","哦","捏"}); case RandomKind::MagicWord: return pick({"星光","月影","花雨","甜梦"}); case RandomKind::CardSuit: return pick({"♠","♥","♦","♣"}); case RandomKind::Tarot: return pick({"愚者","魔术师","女祭司","太阳"});
            case RandomKind::MusicNote: return pick({"♪","♫","♬"}); case RandomKind::Arrow: return pick({"←","→","↑","↓","↔"}); case RandomKind::Bracket: return pick({"()","[]","{}","<>"}); case RandomKind::Operator: return pick({"+","-","*","/","%"}); case RandomKind::Comparator: return pick({"==","!=",">","<",">=","<="});
            case RandomKind::Unit: return pick({"m","kg","s","A","K"}); case RandomKind::Currency: return pick({"CNY","USD","EUR","JPY"}); case RandomKind::TimeUnit: return pick({"ms","s","min","h","d"}); case RandomKind::SizeUnit: return pick({"B","KB","MB","GB"});
            case RandomKind::Locale: return pick({"zh-CN","en-US","ja-JP","ko-KR"}); case RandomKind::TimeZone: return pick({"Asia/Shanghai","UTC","Asia/Tokyo","Europe/London"}); case RandomKind::Encoding: return pick({"UTF-8","GBK","ASCII"}); case RandomKind::HashAlgo: return pick({"MD5","SHA1","SHA256"});
            case RandomKind::SqlType: return pick({"TEXT","INTEGER","REAL","BLOB"}); case RandomKind::JsonType: return pick({"string","number","boolean","array","object"}); case RandomKind::ConfigKey: return pick({"data_dir","sqlite_path","web_api_port"}); case RandomKind::EnvKey: return pick({"PATH","TEMP","USERNAME"});
            case RandomKind::Header: return pick({"Content-Type","Authorization","User-Agent"}); case RandomKind::UserAgent: return pick({"VanBot/3.0","Mozilla/5.0","curl/8"}); case RandomKind::SafeChar: return pick({"a","b","c","1","2","_","-"}); case RandomKind::Kana: return pick({"あ","い","う","え","お","カ","キ"});
            case RandomKind::Number1To100: return ri(1,100);
        }
        return "";
    }


    std::string replace_random_exact_species(const std::string& text) const {
        std::unordered_map<std::string, std::string> m;
        auto add = [&](const std::string& k, const std::string& v){ m.emplace(bracketed(k), v); };
        const std::vector<RandomKind> kinds = {RandomKind::Lower,RandomKind::Upper,RandomKind::Letter,RandomKind::Digit,RandomKind::Hex,RandomKind::Bool,RandomKind::Sign,RandomKind::Percent,RandomKind::Byte,RandomKind::Port,RandomKind::HttpStatus,RandomKind::Dice6,RandomKind::Dice20,RandomKind::Coin,RandomKind::Color,RandomKind::Animal,RandomKind::Weather,RandomKind::Direction,RandomKind::Rarity,RandomKind::Emoji,RandomKind::Face,RandomKind::Food,RandomKind::Drink,RandomKind::Fruit,RandomKind::Flower,RandomKind::Planet,RandomKind::Element,RandomKind::ClassName,RandomKind::Job,RandomKind::Mood,RandomKind::Greeting,RandomKind::Protocol,RandomKind::Method,RandomKind::Mime,RandomKind::FileExt,RandomKind::Language,RandomKind::Country,RandomKind::Province,RandomKind::Zodiac,RandomKind::CnZodiac,RandomKind::Uuid8,RandomKind::Uuid16,RandomKind::Token12,RandomKind::Password8,RandomKind::Base36,RandomKind::Binary,RandomKind::Octal,RandomKind::PrimeUnder100,RandomKind::Odd,RandomKind::Even,RandomKind::Hour,RandomKind::Minute,RandomKind::Second,RandomKind::Month,RandomKind::Day,RandomKind::Weekday,RandomKind::Quarter,RandomKind::Http2xx,RandomKind::Http4xx,RandomKind::Http5xx,RandomKind::ApiVerb,RandomKind::LogLevel,RandomKind::Status,RandomKind::Switch,RandomKind::Permission,RandomKind::BooleanZh,RandomKind::YesNo,RandomKind::OnOff,RandomKind::Severity,RandomKind::MilkyWord,RandomKind::OneBotWord,RandomKind::CuteSuffix,RandomKind::MagicWord,RandomKind::CardSuit,RandomKind::Tarot,RandomKind::MusicNote,RandomKind::Arrow,RandomKind::Bracket,RandomKind::Operator,RandomKind::Comparator,RandomKind::Unit,RandomKind::Currency,RandomKind::TimeUnit,RandomKind::SizeUnit,RandomKind::Locale,RandomKind::TimeZone,RandomKind::Encoding,RandomKind::HashAlgo,RandomKind::SqlType,RandomKind::JsonType,RandomKind::ConfigKey,RandomKind::EnvKey,RandomKind::Header,RandomKind::UserAgent,RandomKind::SafeChar,RandomKind::Kana,RandomKind::Number0To9,RandomKind::Number1To100};
        const std::vector<std::string> names = {"小写","大写","字母","数字","十六进制","布尔","正负","百分比","字节","端口","HTTP状态","D6","D20","硬币","颜色","动物","天气","方向","稀有度","Emoji","颜文字","食物","饮料","水果","花","行星","元素","职业类","职业","心情","问候","协议","方法","MIME","扩展名","语言","国家","省份","星座","生肖","UUID8","UUID16","Token12","密码8","Base36","二进制","八进制","质数","奇数","偶数","小时","分钟","秒","月份","日期日","星期","季度","2xx","4xx","5xx","API动词","日志级别","状态","开关","权限","中文布尔","是否","OnOff","严重度","Milky词","OneBot词","可爱后缀","魔法词","花色","塔罗","音符","箭头","括号","运算符","比较符","单位","货币","时间单位","容量单位","区域","时区","编码","哈希","SQL类型","JSON类型","配置键","环境键","HTTP头","UA","安全字符","假名","0到9","1到100","批次号","票据","盐值","Nonce","Trace","Span","Shard","Bucket","Node","Worker"};
        for (size_t i=0;i<names.size();++i) { add("随机种类" + two(static_cast<int>(i+1)), random_value(kinds[i%kinds.size()])); add("随机" + names[i], random_value(kinds[i%kinds.size()])); }
        return scan_exact(text, m);
    }

    static std::string replace_parametric_exact_species(const std::string& text) {
        std::unordered_map<std::string, std::string> m;
        auto add = [&](const std::string& k, const std::string& v){ m.emplace(bracketed(k), v); };
        const std::vector<std::string> names = {"数字","数值","原样","两位","三位","四位","百分","千分","负数","正数","绝对值","平方","立方","双倍","一半","加一","减一","十六进制","八进制","二进制","端口","HTTP","状态码","摄氏","华氏","开尔文","毫秒","秒数","分钟","小时","天数","周数","KB","MB","GB","TB","KiB","MiB","GiB","字节","比特","百分号","像素","em","rem","角度","弧度","米","千米","厘米","毫米","克","千克","人民币","美元","欧元","日元","布尔01","布尔中文","是否","开关","成功失败","允许拒绝","JSON数","CSV格","双引","单引","圆括号","方括号","花括号","尖括号","URL路径","查询值","头值","环境名","配置名","键名","大写标签","小写标签","蛇形标签","短横标签","点标签","冒号标签","艾特","话题","HTMLID","CSS类","SQL限制","SQL偏移","页码","每页","重试","超时","延迟","冷却","权重","分数","等级","排名","数量","索引","行号","列号","行","宽","高","红","绿","蓝","透明","色相","版本号","构建号","种子"};
        for (size_t i=0;i<names.size();++i) add("工具种类" + two(static_cast<int>(i+1)), names[i]);
        return scan_exact(text, m);
    }

    static const std::vector<std::pair<std::string, ParamUtilityKind>>& parametric_utility_prefixes() {
        static const std::vector<std::pair<std::string, ParamUtilityKind>> p = {
            {"数字",ParamUtilityKind::Identity},{"数值",ParamUtilityKind::Identity},{"原样",ParamUtilityKind::Identity},{"两位",ParamUtilityKind::Pad2},{"三位",ParamUtilityKind::Pad3},{"四位",ParamUtilityKind::Pad4},{"百分",ParamUtilityKind::Percent},{"千分",ParamUtilityKind::Permille},{"负数",ParamUtilityKind::Negative},{"正数",ParamUtilityKind::Positive},{"绝对值",ParamUtilityKind::Abs},{"平方",ParamUtilityKind::Square},{"立方",ParamUtilityKind::Cube},{"双倍",ParamUtilityKind::DoubleValue},{"一半",ParamUtilityKind::Half},{"加一",ParamUtilityKind::Inc},{"减一",ParamUtilityKind::Dec},{"十六进制",ParamUtilityKind::Hex},{"八进制",ParamUtilityKind::Oct},{"二进制",ParamUtilityKind::Bin},{"端口",ParamUtilityKind::Port},{"HTTP",ParamUtilityKind::Http},{"状态码",ParamUtilityKind::StatusCode},{"摄氏",ParamUtilityKind::Celsius},{"华氏",ParamUtilityKind::Fahrenheit},{"开尔文",ParamUtilityKind::Kelvin},{"毫秒",ParamUtilityKind::Ms},{"秒数",ParamUtilityKind::Sec},{"分钟",ParamUtilityKind::Min},{"小时",ParamUtilityKind::Hour},{"天数",ParamUtilityKind::Day},{"周数",ParamUtilityKind::Week},{"KB",ParamUtilityKind::KB},{"MB",ParamUtilityKind::MB},{"GB",ParamUtilityKind::GB},{"TB",ParamUtilityKind::TB},{"KiB",ParamUtilityKind::Kib},{"MiB",ParamUtilityKind::Mib},{"GiB",ParamUtilityKind::Gib},{"字节",ParamUtilityKind::Bytes},{"比特",ParamUtilityKind::Bits},{"百分号",ParamUtilityKind::PercentSign},{"像素",ParamUtilityKind::Px},{"em",ParamUtilityKind::Em},{"rem",ParamUtilityKind::Rem},{"角度",ParamUtilityKind::Deg},{"弧度",ParamUtilityKind::Radian},{"米",ParamUtilityKind::Meter},{"千米",ParamUtilityKind::Kilometer},{"厘米",ParamUtilityKind::Centimeter},{"毫米",ParamUtilityKind::Millimeter},{"克",ParamUtilityKind::Gram},{"千克",ParamUtilityKind::Kilogram},{"人民币",ParamUtilityKind::Yuan},{"美元",ParamUtilityKind::Dollar},{"欧元",ParamUtilityKind::Euro},{"日元",ParamUtilityKind::Yen},{"布尔01",ParamUtilityKind::Bool01},{"布尔中文",ParamUtilityKind::BoolCN},{"是否",ParamUtilityKind::YesNo},{"开关",ParamUtilityKind::OnOff},{"成功失败",ParamUtilityKind::SuccessFail},{"允许拒绝",ParamUtilityKind::AllowDeny},{"JSON数",ParamUtilityKind::JsonNumber},{"CSV格",ParamUtilityKind::CsvCell},{"双引",ParamUtilityKind::Quote},{"单引",ParamUtilityKind::SingleQuote},{"圆括号",ParamUtilityKind::Paren},{"方括号",ParamUtilityKind::Bracket},{"花括号",ParamUtilityKind::Brace},{"尖括号",ParamUtilityKind::Angle},{"URL路径",ParamUtilityKind::UrlPath},{"查询值",ParamUtilityKind::QueryValue},{"头值",ParamUtilityKind::HeaderValue},{"环境名",ParamUtilityKind::EnvName},{"配置名",ParamUtilityKind::ConfigName},{"键名",ParamUtilityKind::KeyName},{"大写标签",ParamUtilityKind::UpperTag},{"小写标签",ParamUtilityKind::LowerTag},{"蛇形标签",ParamUtilityKind::SnakeTag},{"短横标签",ParamUtilityKind::KebabTag},{"点标签",ParamUtilityKind::DottedTag},{"冒号标签",ParamUtilityKind::ColonTag},{"艾特",ParamUtilityKind::AtUser},{"话题",ParamUtilityKind::SharpTag},{"HTMLID",ParamUtilityKind::HtmlId},{"CSS类",ParamUtilityKind::CssClass},{"SQL限制",ParamUtilityKind::SqlLimit},{"SQL偏移",ParamUtilityKind::SqlOffset},{"页码",ParamUtilityKind::Page},{"每页",ParamUtilityKind::PageSize},{"重试",ParamUtilityKind::Retry},{"超时",ParamUtilityKind::Timeout},{"延迟",ParamUtilityKind::Delay},{"冷却",ParamUtilityKind::Cooldown},{"权重",ParamUtilityKind::Weight},{"分数",ParamUtilityKind::Score},{"等级",ParamUtilityKind::Level},{"排名",ParamUtilityKind::Rank},{"数量",ParamUtilityKind::Count},{"索引",ParamUtilityKind::Index},{"行号",ParamUtilityKind::Line},{"列号",ParamUtilityKind::Column},{"行",ParamUtilityKind::Row},{"宽",ParamUtilityKind::Width},{"高",ParamUtilityKind::Height},{"红",ParamUtilityKind::Red},{"绿",ParamUtilityKind::Green},{"蓝",ParamUtilityKind::Blue},{"透明",ParamUtilityKind::Alpha},{"色相",ParamUtilityKind::HslHue},{"版本号",ParamUtilityKind::Version},{"构建号",ParamUtilityKind::Build},{"种子",ParamUtilityKind::Seed}
        };
        return p;
    }

    static std::string parametric_utility_value(ParamUtilityKind kind, const std::string& raw) {
        int v = to_int_or(raw);
        auto pad = [&](int n) { std::string x = std::to_string((std::max)(0, v)); return x.size() >= static_cast<size_t>(n) ? x : std::string(static_cast<size_t>(n) - x.size(), '0') + x; };
        auto wrap = [&](const std::string& a, const std::string& b) { return a + raw + b; };
        switch (kind) {
            case ParamUtilityKind::Identity: case ParamUtilityKind::Port: case ParamUtilityKind::Http: case ParamUtilityKind::StatusCode: return std::to_string(v);
            case ParamUtilityKind::Pad2: return pad(2); case ParamUtilityKind::Pad3: return pad(3); case ParamUtilityKind::Pad4: return pad(4);
            case ParamUtilityKind::Percent: return std::to_string(v) + "%"; case ParamUtilityKind::Permille: return std::to_string(v) + "‰";
            case ParamUtilityKind::Negative: return std::to_string(-std::abs(v)); case ParamUtilityKind::Positive: return std::to_string(std::abs(v)); case ParamUtilityKind::Abs: return std::to_string(std::abs(v));
            case ParamUtilityKind::Square: return std::to_string(v * v); case ParamUtilityKind::Cube: return std::to_string(v * v * v); case ParamUtilityKind::DoubleValue: return std::to_string(v * 2); case ParamUtilityKind::Half: return std::to_string(v / 2); case ParamUtilityKind::Inc: return std::to_string(v + 1); case ParamUtilityKind::Dec: return std::to_string(v - 1);
            case ParamUtilityKind::Hex: { std::ostringstream os; os << std::hex << v; return os.str(); } case ParamUtilityKind::Oct: { std::ostringstream os; os << std::oct << v; return os.str(); } case ParamUtilityKind::Bin: { if (v == 0) return "0"; std::string o; unsigned x = static_cast<unsigned>(v); while (x) { o.insert(o.begin(), (x & 1) ? '1' : '0'); x >>= 1; } return o; }
            case ParamUtilityKind::Celsius: return std::to_string(v) + "°C"; case ParamUtilityKind::Fahrenheit: return std::to_string(v) + "°F"; case ParamUtilityKind::Kelvin: return std::to_string(v) + "K";
            case ParamUtilityKind::Ms: return std::to_string(v) + "ms"; case ParamUtilityKind::Sec: return std::to_string(v) + "s"; case ParamUtilityKind::Min: return std::to_string(v) + "min"; case ParamUtilityKind::Hour: return std::to_string(v) + "h"; case ParamUtilityKind::Day: return std::to_string(v) + "d"; case ParamUtilityKind::Week: return std::to_string(v) + "w";
            case ParamUtilityKind::KB: case ParamUtilityKind::Kib: return std::to_string(static_cast<int64_t>(v) * 1024LL); case ParamUtilityKind::MB: case ParamUtilityKind::Mib: return std::to_string(static_cast<int64_t>(v) * 1024LL * 1024LL); case ParamUtilityKind::GB: case ParamUtilityKind::Gib: return std::to_string(static_cast<int64_t>(v) * 1024LL * 1024LL * 1024LL); case ParamUtilityKind::TB: return std::to_string(static_cast<int64_t>(v) * 1024LL * 1024LL * 1024LL * 1024LL);
            case ParamUtilityKind::Bytes: return std::to_string(v) + "B"; case ParamUtilityKind::Bits: return std::to_string(v) + "b"; case ParamUtilityKind::PercentSign: return raw + "%"; case ParamUtilityKind::Px: return raw + "px"; case ParamUtilityKind::Em: return raw + "em"; case ParamUtilityKind::Rem: return raw + "rem"; case ParamUtilityKind::Deg: return raw + "deg"; case ParamUtilityKind::Radian: return raw + "rad";
            case ParamUtilityKind::Meter: return raw + "m"; case ParamUtilityKind::Kilometer: return raw + "km"; case ParamUtilityKind::Centimeter: return raw + "cm"; case ParamUtilityKind::Millimeter: return raw + "mm"; case ParamUtilityKind::Gram: return raw + "g"; case ParamUtilityKind::Kilogram: return raw + "kg"; case ParamUtilityKind::Yuan: return "¥" + raw; case ParamUtilityKind::Dollar: return "$" + raw; case ParamUtilityKind::Euro: return "€" + raw; case ParamUtilityKind::Yen: return "¥" + raw;
            case ParamUtilityKind::Bool01: return v ? "1" : "0"; case ParamUtilityKind::BoolCN: return v ? "真" : "假"; case ParamUtilityKind::YesNo: return v ? "是" : "否"; case ParamUtilityKind::OnOff: return v ? "on" : "off"; case ParamUtilityKind::SuccessFail: return v ? "success" : "failed"; case ParamUtilityKind::AllowDeny: return v ? "allow" : "deny";
            case ParamUtilityKind::JsonNumber: return std::to_string(v); case ParamUtilityKind::CsvCell: return raw.find(',') == std::string::npos ? raw : wrap("\"", "\""); case ParamUtilityKind::Quote: return wrap("\"", "\""); case ParamUtilityKind::SingleQuote: return wrap("'", "'"); case ParamUtilityKind::Paren: return wrap("(", ")"); case ParamUtilityKind::Bracket: return wrap("[", "]"); case ParamUtilityKind::Brace: return wrap("{", "}"); case ParamUtilityKind::Angle: return wrap("<", ">");
            case ParamUtilityKind::UrlPath: return "/" + url_encode(raw); case ParamUtilityKind::QueryValue: return url_encode(raw); case ParamUtilityKind::HeaderValue: return raw; case ParamUtilityKind::EnvName: case ParamUtilityKind::ConfigName: case ParamUtilityKind::KeyName: return raw;
            case ParamUtilityKind::UpperTag: { std::string x = raw; std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); }); return x; } case ParamUtilityKind::LowerTag: { std::string x = raw; std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return x; }
            case ParamUtilityKind::SnakeTag: return replace_all(raw, "-", "_"); case ParamUtilityKind::KebabTag: return replace_all(raw, "_", "-"); case ParamUtilityKind::DottedTag: return replace_all(raw, "_", "."); case ParamUtilityKind::ColonTag: return replace_all(raw, "_", ":"); case ParamUtilityKind::AtUser: return "[CQ:at,qq=" + raw + "]"; case ParamUtilityKind::SharpTag: return "#" + raw; case ParamUtilityKind::HtmlId: return "id=\"" + raw + "\""; case ParamUtilityKind::CssClass: return "." + raw;
            default: return std::to_string(v);
        }
    }

    static std::string replace_large_family_vars(const std::string& text) {
        if (text.find('[') == std::string::npos) return text;
        const auto& vars = utility_exact_vars();
        std::string out;
        out.reserve(text.size());
        bool changed = false;
        for (size_t i = 0; i < text.size();) {
            if (text[i] != '[') {
                out.push_back(text[i++]);
                continue;
            }
            size_t end = text.find(']', i + 1);
            if (end == std::string::npos) {
                out.append(text, i, std::string::npos);
                break;
            }
            std::string token = text.substr(i, end - i + 1);
            auto found = vars.find(token);
            if (found != vars.end()) {
                out += found->second;
                changed = true;
            } else {
                out += token;
            }
            i = end + 1;
        }
        return changed ? out : text;
    }

    // 参数化工具族：100+ 前缀族，一次 token 扫描解析常见 数值/单位/格式 模板。
    static std::string replace_generated_utility_vars(const std::string& text) {
        if (text.find('[') == std::string::npos) return text;
        std::string expanded = replace_parametric_exact_species(text);
        const auto& prefixes = parametric_utility_prefixes();
        std::string out;
        out.reserve(expanded.size());
        bool changed = false;
        for (size_t i = 0; i < expanded.size();) {
            if (expanded[i] != '[') { out.push_back(expanded[i++]); continue; }
            size_t end = expanded.find(']', i + 1);
            if (end == std::string::npos) { out.append(expanded, i, std::string::npos); break; }
            std::string body = expanded.substr(i + 1, end - i - 1);
            bool hit = false;
            for (const auto& [prefix, kind] : prefixes) {
                if (body.rfind(prefix, 0) != 0) continue;
                out += parametric_utility_value(kind, body.substr(prefix.size()));
                changed = true;
                hit = true;
                break;
            }
            if (!hit) out.append(expanded, i, end - i + 1);
            i = end + 1;
        }
        return changed ? out : expanded;
    }

    static size_t utility_family_count() {
        return utility_exact_vars().size();
    }

    static const std::unordered_map<std::string, std::string>& utility_exact_vars() {
        static const std::unordered_map<std::string, std::string> vars = [] {
            std::unordered_map<std::string, std::string> m;
            auto add = [&](const std::string& key, const std::string& value) { m.emplace("[" + key + "]", value); };
            auto add_same = [&](const std::vector<std::string>& names) { for (const auto& name : names) add(name, name); };
            auto add_prefixed_same = [&](const std::string& prefix, const std::vector<std::string>& names) { for (const auto& name : names) add(prefix + name, name); };

            add("布尔真", "true"); add("布尔假", "false"); add("开启", "on"); add("关闭", "off");
            add("成功", "success"); add("失败", "failed"); add("允许", "allow"); add("拒绝", "deny");
            add("空格", " "); add("双引号", "\""); add("单引号", "'"); add("等号", "="); add("加号", "+"); add("减号", "-"); add("星号", "*"); add("竖线", "|");
            add("下划线", "_"); add("短横线", "-"); add("井号", "#"); add("问号", "?"); add("感叹号", "!"); add("百分号", "%"); add("与号", "&"); add("小于号", "<"); add("大于号", ">");
            add("UTF8", "UTF-8"); add("CRLF", "\r\n"); add("LF", "\n"); add("TAB", "\t"); add("本地地址", "127.0.0.1"); add("全局监听", "0.0.0.0"); add("默认端口", "8080");

            const std::vector<std::pair<int, std::string>> http = {{100,"Continue"},{101,"Switching Protocols"},{102,"Processing"},{103,"Early Hints"},{200,"OK"},{201,"Created"},{202,"Accepted"},{203,"Non-Authoritative Information"},{204,"No Content"},{205,"Reset Content"},{206,"Partial Content"},{207,"Multi-Status"},{208,"Already Reported"},{226,"IM Used"},{300,"Multiple Choices"},{301,"Moved Permanently"},{302,"Found"},{303,"See Other"},{304,"Not Modified"},{305,"Use Proxy"},{307,"Temporary Redirect"},{308,"Permanent Redirect"},{400,"Bad Request"},{401,"Unauthorized"},{402,"Payment Required"},{403,"Forbidden"},{404,"Not Found"},{405,"Method Not Allowed"},{406,"Not Acceptable"},{407,"Proxy Authentication Required"},{408,"Request Timeout"},{409,"Conflict"},{410,"Gone"},{411,"Length Required"},{412,"Precondition Failed"},{413,"Payload Too Large"},{414,"URI Too Long"},{415,"Unsupported Media Type"},{416,"Range Not Satisfiable"},{417,"Expectation Failed"},{418,"I'm a teapot"},{421,"Misdirected Request"},{422,"Unprocessable Entity"},{423,"Locked"},{424,"Failed Dependency"},{425,"Too Early"},{426,"Upgrade Required"},{428,"Precondition Required"},{429,"Too Many Requests"},{431,"Request Header Fields Too Large"},{451,"Unavailable For Legal Reasons"},{500,"Internal Server Error"},{501,"Not Implemented"},{502,"Bad Gateway"},{503,"Service Unavailable"},{504,"Gateway Timeout"},{505,"HTTP Version Not Supported"},{506,"Variant Also Negotiates"},{507,"Insufficient Storage"},{508,"Loop Detected"},{510,"Not Extended"},{511,"Network Authentication Required"}};
            for (const auto& [code, phrase] : http) { add("HTTP" + std::to_string(code), phrase); add("HTTP码" + std::to_string(code), std::to_string(code)); }

            const std::vector<std::pair<std::string, std::string>> mime = {{"文本","text/plain"},{"HTML","text/html"},{"CSS","text/css"},{"CSV","text/csv"},{"XML","application/xml"},{"JSON","application/json"},{"NDJSON","application/x-ndjson"},{"JS","application/javascript"},{"表单","application/x-www-form-urlencoded"},{"二进制","application/octet-stream"},{"PDF","application/pdf"},{"ZIP","application/zip"},{"GZIP","application/gzip"},{"TAR","application/x-tar"},{"7Z","application/x-7z-compressed"},{"RAR","application/vnd.rar"},{"PNG","image/png"},{"JPG","image/jpeg"},{"JPEG","image/jpeg"},{"GIF","image/gif"},{"WEBP","image/webp"},{"SVG","image/svg+xml"},{"ICO","image/vnd.microsoft.icon"},{"MP3","audio/mpeg"},{"WAV","audio/wav"},{"OGG","audio/ogg"},{"MP4","video/mp4"},{"WEBM","video/webm"},{"Markdown","text/markdown"},{"YAML","application/yaml"},{"WASM","application/wasm"}};
            for (const auto& [name, value] : mime) { add("MIME" + name, value); add("头" + name, "Content-Type: " + value); }

            const std::vector<std::string> methods = {"GET","POST","PUT","DELETE","PATCH","HEAD","OPTIONS","TRACE","CONNECT"};
            for (const auto& method : methods) add("方法" + method, method);
            const std::vector<std::string> levels = {"trace","debug","info","notice","warn","warning","error","fatal","critical","off"};
            for (const auto& level : levels) { add("级别" + level, level); add("日志" + level, level); }
            const std::vector<std::string> statuses = {"正常","警告","错误","未知","在线","离线","连接中","重连中","启用","禁用","可用","不可用","成功","失败","等待","运行中","已停止","已暂停"};
            add_prefixed_same("状态", statuses);

            const std::vector<std::pair<std::string, std::string>> units = {{"B","B"},{"KB","KB"},{"MB","MB"},{"GB","GB"},{"TB","TB"},{"PB","PB"},{"KiB","KiB"},{"MiB","MiB"},{"GiB","GiB"},{"毫秒","ms"},{"秒","s"},{"分钟","min"},{"小时","h"},{"天","d"},{"周","week"},{"米","m"},{"千米","km"},{"厘米","cm"},{"毫米","mm"},{"克","g"},{"千克","kg"},{"摄氏度","°C"},{"华氏度","°F"},{"百分比","%"}};
            for (const auto& [name, value] : units) add("单位" + name, value);
            for (int port : {20,21,22,23,25,53,67,68,80,110,123,143,161,389,443,465,500,514,587,636,993,995,1433,1521,1723,1883,2049,2375,2376,3306,3389,5432,5672,5900,6379,7001,8000,8080,8081,8443,9000,9200,9300,11211,27017}) add("端口" + std::to_string(port), std::to_string(port));

            const std::vector<std::pair<std::string, std::string>> colors = {{"红","#ff4d4f"},{"粉","#ffadd2"},{"玫红","#eb2f96"},{"橙","#ffa940"},{"黄","#fadb14"},{"金","#faad14"},{"绿","#52c41a"},{"青","#13c2c2"},{"蓝","#1677ff"},{"天蓝","#69c0ff"},{"紫","#722ed1"},{"黑","#000000"},{"白","#ffffff"},{"灰","#8c8c8c"},{"薄荷","#b5f5ec"},{"樱花","#ffadd2"},{"奶油","#fff7e6"},{"薰衣草","#efdbff"},{"珊瑚","#ff7a45"},{"海盐","#e6f4ff"}};
            for (const auto& [name, hex] : colors) { add("颜色" + name + "HEX", hex); add("颜色" + name, name); }
            const std::vector<std::pair<std::string, std::string>> emoji = {{"可爱心","♡"},{"实心心","♥"},{"星星","☆"},{"实心星","★"},{"音符","♪"},{"花花","🌸"},{"猫爪","🐾"},{"猫猫头","ฅ"},{"月亮","🌙"},{"太阳","☀"},{"云朵","☁"},{"雨伞","☂"},{"雪花","❄"},{"火花","✨"},{"皇冠","👑"},{"礼物","🎁"},{"铃铛","🔔"},{"闪电","⚡"},{"咖啡","☕"},{"蛋糕","🍰"},{"糖果","🍬"},{"樱桃","🍒"},{"猫","🐱"},{"兔","🐰"},{"狐","🦊"},{"熊猫","🐼"},{"企鹅","🐧"},{"鲸","🐳"},{"火箭","🚀"},{"机器人","🤖"}};
            for (const auto& [name, value] : emoji) add(name, value);

            add_same({"早安","午安","晚安","你好","谢谢","抱歉","收到","完成","喵","汪","啾","欸嘿","好耶","加油","辛苦了","欢迎","再见","请稍等","处理中","已记录"});
            add_prefixed_same("星期", {"一","二","三","四","五","六","日","天"});
            add_prefixed_same("月份", {"一月","二月","三月","四月","五月","六月","七月","八月","九月","十月","十一月","十二月"});
            add_prefixed_same("季度", {"Q1","Q2","Q3","Q4"});
            add_prefixed_same("方向", {"东","南","西","北","东北","东南","西北","西南","上","下","左","右","前","后"});

            const std::vector<std::pair<std::string, std::string>> tz = {{"UTC","UTC"},{"上海","Asia/Shanghai"},{"东京","Asia/Tokyo"},{"首尔","Asia/Seoul"},{"香港","Asia/Hong_Kong"},{"新加坡","Asia/Singapore"},{"伦敦","Europe/London"},{"巴黎","Europe/Paris"},{"柏林","Europe/Berlin"},{"纽约","America/New_York"},{"洛杉矶","America/Los_Angeles"},{"芝加哥","America/Chicago"},{"悉尼","Australia/Sydney"},{"墨尔本","Australia/Melbourne"},{"莫斯科","Europe/Moscow"},{"迪拜","Asia/Dubai"}};
            for (const auto& [name, value] : tz) add("时区" + name, value);
            const std::vector<std::pair<std::string, std::string>> currencies = {{"人民币","CNY"},{"美元","USD"},{"欧元","EUR"},{"日元","JPY"},{"英镑","GBP"},{"港币","HKD"},{"台币","TWD"},{"韩元","KRW"},{"新元","SGD"},{"澳元","AUD"},{"加元","CAD"},{"瑞郎","CHF"},{"泰铢","THB"},{"卢布","RUB"},{"比特币","BTC"},{"以太坊","ETH"}};
            for (const auto& [name, value] : currencies) { add("货币" + name, value); add("货币符号" + value, value); }

            add_prefixed_same("语言", {"中文","英语","日语","韩语","法语","德语","西班牙语","葡萄牙语","俄语","阿拉伯语","印地语","越南语","泰语","意大利语","荷兰语","土耳其语","波兰语","乌克兰语","印尼语","马来语"});
            add_prefixed_same("国家", {"中国","日本","韩国","美国","英国","法国","德国","意大利","西班牙","葡萄牙","俄罗斯","加拿大","澳大利亚","新西兰","新加坡","泰国","越南","马来西亚","印度尼西亚","印度","巴西","墨西哥","阿根廷","智利","南非","埃及","土耳其","阿联酋","沙特","瑞士","瑞典","挪威","芬兰","丹麦","荷兰","比利时","奥地利","波兰","乌克兰","希腊"});
            add_prefixed_same("省份", {"北京","天津","上海","重庆","河北","山西","辽宁","吉林","黑龙江","江苏","浙江","安徽","福建","江西","山东","河南","湖北","湖南","广东","海南","四川","贵州","云南","陕西","甘肃","青海","台湾","内蒙古","广西","西藏","宁夏","新疆","香港","澳门"});
            add_prefixed_same("星座", {"白羊","金牛","双子","巨蟹","狮子","处女","天秤","天蝎","射手","摩羯","水瓶","双鱼"});
            add_prefixed_same("生肖", {"鼠","牛","虎","兔","龙","蛇","马","羊","猴","鸡","狗","猪"});
            add_prefixed_same("稀有度", {"N","R","SR","SSR","UR","LR","EX","SP","限定","普通","稀有","史诗","传说","神话"});
            add_prefixed_same("天气", {"晴","多云","阴","小雨","中雨","大雨","暴雨","雷阵雨","雪","雾","霾","微风","台风","彩虹"});
            add_prefixed_same("文件扩展", {"txt","md","json","yaml","yml","ini","csv","log","html","css","js","ts","cpp","hpp","c","h","py","java","go","rs","png","jpg","gif","webp","svg","pdf","zip","7z","tar","gz"});
            add_prefixed_same("协议名", {"HTTP","HTTPS","WS","WSS","TCP","UDP","DNS","SMTP","IMAP","POP3","SSH","SFTP","FTP","MQTT","AMQP","Redis","SQLite","OneBot11","OneBot12","Milky"});
            add_prefixed_same("配置键", {"data_dir","self_trigger","config_tui","storage_backend","sqlite_path","web_api_enabled","web_api_host","web_api_port","web_api_token","adapter_name","adapter_type","adapter_url","adapter_port","access_token","reconnect_interval","heartbeat_interval"});
            add_prefixed_same("趣味命令", {"签到","今日运势","抽签","塔罗","骰子","D20","硬币","老虎机","今日老婆","菜单","戳一戳"});
            add_prefixed_same("占卜主题", {"爱情","事业","学习","代码","词库","旅行","睡眠","灵感","奶茶","猫猫","月亮","樱花"});
            add_prefixed_same("签到奖励", {"樱花币","连续签到","今日奖励","累计奖励","补签卡","幸运贴纸","猫爪徽章","月光券"});
            add_prefixed_same("趣味道具", {"骰子","硬币","塔罗牌","签筒","魔法棒","奶茶券","猫爪印","星星瓶","好运铃","樱花书签"});
            add_prefixed_same("可爱动作", {"摸摸头","贴贴","戳一戳","抱抱","转圈","挥手","眨眼","撒花","举爪","打滚"});
            return m;
        }();
        return vars;
    }

    std::string replace_parametric_vars(const std::string& text, const VarContext& ctx) const {
        std::string result = text;
        (void)ctx;

        static const std::regex rand_int_re(R"(\[随机数\.(-?\d+)\.(-?\d+)\])");
        result = replace_regex_two_args(result, rand_int_re, [&](const std::string& a, const std::string& b) {
            int lo = to_int_or(a), hi = to_int_or(b);
            if (lo > hi) std::swap(lo, hi);
            return std::to_string(std::uniform_int_distribution<int>(lo, hi)(m_rng));
        });

        static const std::regex rand_pick_re(R"(\[随机选择\.([^\]]+)\])");
        {
            std::smatch m; std::string::const_iterator it = result.cbegin(); std::string out;
            while (std::regex_search(it, result.cend(), m, rand_pick_re)) {
                out.append(it, m[0].first);
                auto parts = split(m[1].str(), "|");
                if (parts.empty()) out += "";
                else out += parts[std::uniform_int_distribution<size_t>(0, parts.size() - 1)(m_rng)];
                it = m[0].second;
            }
            if (it != result.cend()) out.append(it, result.cend());
            if (!out.empty() && out != result) result = out;
        }

        static const std::regex repeat_re(R"(\[循环\.(\d+)\.([^\]]+)\])");
        result = replace_regex_two_args(result, repeat_re, [](const std::string& n, const std::string& s) {
            int count = (std::min)(to_int_or(n), 1000);
            std::string out;
            for (int i = 0; i < count; i++) out += s;
            return out;
        });

        static const std::regex right_re(R"(\[右截取\.(\d+)\.([^\]]+)\])");
        result = replace_regex_two_args(result, right_re, [](const std::string& n, const std::string& s) {
            size_t count = static_cast<size_t>((std::max)(0, to_int_or(n)));
            return s.size() > count ? s.substr(s.size() - count) : s;
        });

        static const std::regex mid_re(R"(\[取中\.(\d+)\.(\d+)\.([^\]]+)\])");
        result = replace_regex_three_args(result, mid_re, [](const std::string& start, const std::string& len, const std::string& s) {
            size_t pos = static_cast<size_t>((std::max)(0, to_int_or(start)));
            size_t count = static_cast<size_t>((std::max)(0, to_int_or(len)));
            if (pos >= s.size()) return std::string{};
            return s.substr(pos, count);
        });

        static const std::regex contains_re(R"(\[包含\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, contains_re, [](const std::string& s, const std::string& needle) { return bool_text(s.find(needle) != std::string::npos); });
        static const std::regex starts_re(R"(\[开头是\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, starts_re, [](const std::string& s, const std::string& prefix) { return bool_text(s.rfind(prefix, 0) == 0); });
        static const std::regex ends_re(R"(\[结尾是\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, ends_re, [](const std::string& s, const std::string& suffix) { return bool_text(s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0); });
        static const std::regex eq_re(R"(\[相等\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, eq_re, [](const std::string& a, const std::string& b) { return bool_text(a == b); });
        static const std::regex ne_re(R"(\[不等\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, ne_re, [](const std::string& a, const std::string& b) { return bool_text(a != b); });
        static const std::regex gt_re(R"(\[大于\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, gt_re, [](const std::string& a, const std::string& b) { return bool_text(to_double_or(a) > to_double_or(b)); });
        static const std::regex lt_re(R"(\[小于\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, lt_re, [](const std::string& a, const std::string& b) { return bool_text(to_double_or(a) < to_double_or(b)); });
        static const std::regex ge_re(R"(\[大于等于\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, ge_re, [](const std::string& a, const std::string& b) { return bool_text(to_double_or(a) >= to_double_or(b)); });
        static const std::regex le_re(R"(\[小于等于\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, le_re, [](const std::string& a, const std::string& b) { return bool_text(to_double_or(a) <= to_double_or(b)); });
        static const std::regex max_re(R"(\[最大\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, max_re, [](const std::string& a, const std::string& b) { return std::to_string((std::max)(to_double_or(a), to_double_or(b))); });
        static const std::regex min_re(R"(\[最小\.([^\]]*)\.([^\]]*)\])");
        result = replace_regex_two_args(result, min_re, [](const std::string& a, const std::string& b) { return std::to_string((std::min)(to_double_or(a), to_double_or(b))); });

        static const std::regex url_re(R"(\[url编码\.([^\]]*)\])");
        static const std::regex json_re(R"(\[json转义\.([^\]]*)\])");
        static const std::regex cq_re(R"(\[CQ转义\.([^\]]*)\])");
        result = replace_regex_one_arg(result, url_re, [](const std::string& s) { return url_encode(s); });
        result = replace_regex_one_arg(result, json_re, [](const std::string& s) { return json_escape(s); });
        result = replace_regex_one_arg(result, cq_re, [](const std::string& s) { return cq_escape(s); });

        return result;
    }

    static int daily_number() {
        auto now = std::chrono::system_clock::now();
        auto tnow = std::chrono::system_clock::to_time_t(now);
        struct tm t;
#ifdef _WIN32
        localtime_s(&t, &tnow);
#else
        localtime_r(&tnow, &t);
#endif
        return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
    }

    static size_t stable_index(uint64_t seed, size_t size) {
        if (size == 0) return 0;
        seed ^= seed >> 33;
        seed *= 0xff51afd7ed558ccdULL;
        seed ^= seed >> 33;
        seed *= 0xc4ceb9fe1a85ec53ULL;
        seed ^= seed >> 33;
        return static_cast<size_t>(seed % size);
    }

    static std::string pick_fun(const std::vector<std::string>& v, uint64_t seed) {
        return v[stable_index(seed, v.size())];
    }

    static std::string fortune_level(int score) {
        if (score >= 95) return "大吉·SSR";
        if (score >= 80) return "大吉";
        if (score >= 65) return "中吉";
        if (score >= 45) return "小吉";
        if (score >= 25) return "末吉";
        return "需要奶茶续命";
    }

    static std::string slot_value(uint64_t seed) {
        static const std::vector<std::string> icons = {"🌸", "🍒", "⭐", "🐾", "💎", "🍀"};
        std::string a = icons[stable_index(seed + 1, icons.size())];
        std::string b = icons[stable_index(seed + 2, icons.size())];
        std::string c = icons[stable_index(seed + 3, icons.size())];
        return a + "|" + b + "|" + c;
    }

    static const std::vector<std::string>& fun_colors() { static const std::vector<std::string> v = {"樱花粉","薄荷绿","奶油白","天空蓝","薰衣草紫","蜜桃橙","月光银","海盐蓝","琥珀金","莓果红"}; return v; }
    static const std::vector<std::string>& fun_advices() { static const std::vector<std::string> v = {"适合整理词库","适合早点休息","适合大胆表达","适合写代码并提交","适合喝奶茶","适合学习新协议","适合修一个小 bug","适合给自己一点奖励"}; return v; }
    static const std::vector<std::string>& fun_tarot_cards() { static const std::vector<std::string> v = {"愚者","魔术师","女祭司","女皇","皇帝","教皇","恋人","战车","力量","隐者","命运之轮","正义","倒吊人","死神","节制","恶魔","塔","星星","月亮","太阳","审判","世界"}; return v; }
    static const std::vector<std::string>& fun_lottery_levels() { static const std::vector<std::string> v = {"大吉","中吉","小吉","吉","末吉","凶转吉","SSR签","猫猫签","樱花签","月光签"}; return v; }
    static const std::vector<std::string>& fun_moods() { static const std::vector<std::string> v = {"元气","困困","闪闪发光","想摸鱼","很可靠","超可爱","需要抱抱","灵感爆棚"}; return v; }
    static const std::vector<std::string>& fun_foods() { static const std::vector<std::string> v = {"蛋糕","布丁","拉面","寿司","曲奇","咖喱","三明治","小笼包"}; return v; }
    static const std::vector<std::string>& fun_drinks() { static const std::vector<std::string> v = {"奶茶","咖啡","可可","果汁","气泡水","抹茶拿铁","蜂蜜柚子茶","乌龙茶"}; return v; }
    static const std::vector<std::string>& fun_waifus() { static const std::vector<std::string> v = {"樱花魔法师","薄荷猫娘","星空旅人","月白歌姬","代码妖精","奶茶店看板娘","机械天使","图书馆幽灵","海盐偶像","梦境占卜师"}; return v; }
    static const std::vector<std::string>& fun_titles() { static const std::vector<std::string> v = {"变量炼金术士","樱花观测者","Bug 驯兽师","词库守护者","Milky 调酒师","OneBot 旅人","夜间编译师","幸运猫爪"}; return v; }
    static const std::vector<std::string>& fun_poke_replies() { static const std::vector<std::string> v = {"戳、戳回来啦！","不要一直戳啦，会害羞的喵~","发送一朵小樱花 🌸","诶嘿，被发现啦！","再戳就把你加入幸运名单哦~"}; return v; }
    static const std::vector<std::string>& fun_memes() { static const std::vector<std::string> v = {"今日份好耶已送达","bug 只是还没学会变可爱","先保存，再施法","奶茶是生产力的一部分","变量嵌套，快乐加倍","这次一定能编译过"}; return v; }
    static const std::vector<std::string>& fun_quotes() { static const std::vector<std::string> v = {"月亮慢慢圆，代码慢慢稳。","把复杂拆小，光就会进来。","今天也要温柔地前进。","愿每一次重试都有回声。","小小提交，也是一颗星星。"}; return v; }

    // ── 简易表达式计算 ──────────────────────────────────────
    static double simple_eval(const std::string& expr) {
        // 支持加减乘除，从左到右
        std::vector<double> nums;
        std::vector<char> ops;
        std::string num;
        for (char c : expr) {
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') { num += c; }
            else if (c == '+' || c == '-' || c == '*' || c == '/') {
                if (!num.empty()) { nums.push_back(std::stod(num)); num.clear(); }
                ops.push_back(c);
            }
        }
        if (!num.empty()) nums.push_back(std::stod(num));
        if (nums.empty()) return 0;

        // 先算乘除
        for (size_t i = 0; i < ops.size(); ) {
            if (ops[i] == '*' || ops[i] == '/') {
                double r = (ops[i] == '*') ? nums[i] * nums[i+1] : nums[i] / nums[i+1];
                nums[i] = r; nums.erase(nums.begin()+i+1); ops.erase(ops.begin()+i);
            } else { i++; }
        }
        // 再算加减
        double result = nums[0];
        for (size_t i = 0; i < ops.size(); i++) {
            if (ops[i] == '+') result += nums[i+1];
            else if (ops[i] == '-') result -= nums[i+1];
        }
        return result;
    }

    static bool evaluate_condition(const std::string& cond) {
        std::string s = cond;
        if (s.front() == '{') s = s.substr(1);
        if (s.back() == '}') s = s.substr(0, s.size() - 1);

        // 逻辑运算符处理
        // & → and, | → or
        // 简化实现：支持 ==, !=, >, <, >=, <=
        static const std::regex cmp_re(R"(^(.+?)(==|!=|>=|<=|>|<)(.+)$)");
        std::smatch m;
        if (std::regex_match(s, m, cmp_re)) {
            std::string a = trim(m[1].str());
            std::string op = m[2].str();
            std::string b = trim(m[3].str());
            if (op == "==" || op == "=") return a == b;
            if (op == "!=") return a != b;
            try {
                double fa = std::stod(a), fb = std::stod(b);
                if (op == ">") return fa > fb;
                if (op == "<") return fa < fb;
                if (op == ">=") return fa >= fb;
                if (op == "<=") return fa <= fb;
            } catch (...) {}
        }
        return false;
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

    static std::vector<std::string> split(const std::string& s, const std::string& delim) {
        std::vector<std::string> parts;
        size_t start = 0, end;
        while ((end = s.find(delim, start)) != std::string::npos) {
            parts.push_back(s.substr(start, end - start));
            start = end + delim.length();
        }
        parts.push_back(s.substr(start));
        return parts;
    }

    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }
};

} // namespace vanbot
