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

    // ── 用户信息变量 ─────────────────────────────────────────
    static std::string replace_user_vars(const std::string& text, const VarContext& ctx) {
        std::string result = text;
        auto uid = std::to_string(ctx.event_user_id);
        result = replace_all(result, "[qq]", uid);
        result = replace_all(result, "[QQ号]", uid);
        result = replace_all(result, "[name]", ctx.event_sender_name);
        result = replace_all(result, "[QQ名]", ctx.event_sender_name);
        result = replace_all(result, "[名字]", ctx.event_sender_name);
        result = replace_all(result, "[card]", ctx.event_sender_card);
        result = replace_all(result, "[群昵称]", ctx.event_sender_card);
        result = replace_all(result, "[id]", std::to_string(ctx.event_message_id));
        result = replace_all(result, "[消息id]", std::to_string(ctx.event_message_id));
        result = replace_all(result, "[ai]", std::to_string(ctx.bot_id));
        result = replace_all(result, "[AI号]", std::to_string(ctx.bot_id));
        result = replace_all(result, "[selfid]", std::to_string(ctx.event_self_id));
        result = replace_all(result, "[自身id]", std::to_string(ctx.event_self_id));
        return result;
    }

    // ── 环境变量 ─────────────────────────────────────────────
    static std::string replace_env_vars(const std::string& text, const VarContext& ctx) {
        std::string result = text;
        if (ctx.env == Env::Group) {
            result = replace_all(result, "[group]", std::to_string(ctx.env_id));
            result = replace_all(result, "[群号]", std::to_string(ctx.env_id));
        }
        result = replace_all(result, "[env]", ctx.env == Env::Group ? "group" : "private");
        result = replace_all(result, "[环境]", ctx.env == Env::Group ? "群聊" : "私聊");
        return result;
    }

    // ── 时间变量 ─────────────────────────────────────────────
    static std::string replace_time_vars(const std::string& text) {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm tm_now;
    #ifdef _WIN32
        localtime_s(&tm_now, &time_t_now);
    #else
        localtime_r(&time_t_now, &tm_now);
    #endif
        std::string result = text;
        result = replace_all(result, "(Y)", std::to_string(tm_now.tm_year + 1900));
        result = replace_all(result, "(M)", std::to_string(tm_now.tm_mon + 1));
        result = replace_all(result, "(D)", std::to_string(tm_now.tm_mday));
        result = replace_all(result, "(h)", std::to_string(tm_now.tm_hour));
        result = replace_all(result, "(m)", std::to_string(tm_now.tm_min));
        result = replace_all(result, "(s)", std::to_string(tm_now.tm_sec));
        result = replace_all(result, "[年]", std::to_string(tm_now.tm_year + 1900));
        result = replace_all(result, "[月]", std::to_string(tm_now.tm_mon + 1));
        result = replace_all(result, "[日]", std::to_string(tm_now.tm_mday));
        result = replace_all(result, "[时]", std::to_string(tm_now.tm_hour));
        result = replace_all(result, "[分]", std::to_string(tm_now.tm_min));
        result = replace_all(result, "[秒]", std::to_string(tm_now.tm_sec));

        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_now);
        result = replace_all(result, "[日期]", buf);
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_now);
        result = replace_all(result, "[时间]", buf);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
        result = replace_all(result, "[日期时间]", buf);

        const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
        result = replace_all(result, "[星期]", weekdays[tm_now.tm_wday]);
        result = replace_all(result, "[周几]", std::to_string(tm_now.tm_wday == 0 ? 7 : tm_now.tm_wday));
        result = replace_all(result, "[时间戳]", std::to_string(time_t_now));
        result = replace_all(result, "[年中第几天]", std::to_string(tm_now.tm_yday + 1));

        int year = tm_now.tm_year + 1900;
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        result = replace_all(result, "[闰年]", leap ? "是" : "否");
        int dim[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        result = replace_all(result, "[本月天数]", std::to_string(dim[tm_now.tm_mon]));
        result = replace_all(result, "[小时12]", std::to_string((tm_now.tm_hour % 12) == 0 ? 12 : (tm_now.tm_hour % 12)));
        result = replace_all(result, "[上午下午]", tm_now.tm_hour < 12 ? "上午" : "下午");
        result = replace_all(result, "[季度]", std::to_string(tm_now.tm_mon / 3 + 1));
        result = replace_all(result, "[本年第几周]", std::to_string(tm_now.tm_yday / 7 + 1));
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_now);
        result = replace_all(result, "[ISO日期]", buf);
        return result;
    }

    // ── 随机数变量 ──────────────────────────────────────────
    std::string replace_random_vars(const std::string& text) const {
        static const std::regex re(R"(\((\d+)-(\d+)\))");
        std::string result = text;
        std::smatch match;
        std::string::const_iterator searchStart = result.cbegin();
        while (std::regex_search(searchStart, result.cend(), match, re)) {
            int a = std::stoi(match[1].str());
            int b = std::stoi(match[2].str());
            std::uniform_int_distribution<int> dist(std::min(a, b), std::max(a, b));
            std::string repl = std::to_string(dist(m_rng));
            size_t pos = match.position() + (searchStart - result.cbegin());
            result.replace(pos, match.length(), repl);
            searchStart = result.cbegin() + pos + repl.length();
        }

        std::uniform_int_distribution<int> d025(0, 25);
        std::uniform_int_distribution<int> d09(0, 9);
        std::uniform_int_distribution<int> d015(0, 15);
        std::uniform_int_distribution<int> d01(0, 1);
        result = replace_all(result, "[随机字母]", std::string(1, static_cast<char>('a' + d025(m_rng))));
        result = replace_all(result, "[随机大写字母]", std::string(1, static_cast<char>('A' + d025(m_rng))));
        result = replace_all(result, "[随机数字]", std::to_string(d09(m_rng)));
        result = replace_all(result, "[随机布尔]", d01(m_rng) ? "true" : "false");
        result = replace_all(result, "[随机正负]", d01(m_rng) ? "+" : "-");
        result = replace_all(result, "[随机表情]", std::vector<std::string>{"(ฅ>ω<*ฅ)", "(｡･ω･｡)", "(≧▽≦)", "(*´∀`)~♥", "(つ≧▽≦)つ"}[std::uniform_int_distribution<size_t>(0, 4)(m_rng)]);
        result = replace_all(result, "[随机颜色]", std::vector<std::string>{"粉色", "薄荷绿", "天蓝", "薰衣草紫", "蜜桃橙", "奶油白"}[std::uniform_int_distribution<size_t>(0, 5)(m_rng)]);
        result = replace_all(result, "[随机动物]", std::vector<std::string>{"猫猫", "兔兔", "狐狸", "企鹅", "水獭", "熊猫", "小狗", "仓鼠"}[std::uniform_int_distribution<size_t>(0, 7)(m_rng)]);
        result = replace_all(result, "[随机天气]", std::vector<std::string>{"晴", "多云", "小雨", "雪", "微风", "彩虹"}[std::uniform_int_distribution<size_t>(0, 5)(m_rng)]);
        result = replace_all(result, "[随机方向]", std::vector<std::string>{"东", "南", "西", "北", "左", "右", "上", "下"}[std::uniform_int_distribution<size_t>(0, 7)(m_rng)]);
        result = replace_all(result, "[随机稀有度]", std::vector<std::string>{"N", "R", "SR", "SSR", "UR"}[std::uniform_int_distribution<size_t>(0, 4)(m_rng)]);
        result = replace_all(result, "[随机UUID段]", random_hex(8));

        if (result.find("[随机十六进制]") != std::string::npos) {
            int v = d015(m_rng);
            result = replace_all(result, "[随机十六进制]",
                std::string(1, static_cast<char>(v < 10 ? '0' + v : 'a' + v - 10)));
        }
        return result;
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
    static std::string replace_system_vars(const std::string& text, const VarContext& ctx) {
        std::string result = text;
    #ifdef _WIN32
        result = replace_all(result, "[平台]", "Windows");
    #else
        result = replace_all(result, "[平台]", "Linux");
    #endif
        (void)ctx;
        result = replace_all(result, "[变量数量]", "520+");
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

    // ── 实用别名与常量包：覆盖日期格式、布尔、HTTP、MIME、单位等高频场景 ───
    static std::string replace_alias_pack_vars(const std::string& text) {
        if (text.find('[') == std::string::npos) return text;
        std::string result = text;
        static const std::vector<std::pair<std::string, std::string>> aliases = {
            {"[布尔真]", "true"}, {"[布尔假]", "false"}, {"[开启]", "on"}, {"[关闭]", "off"},
            {"[成功]", "success"}, {"[失败]", "failed"}, {"[允许]", "allow"}, {"[拒绝]", "deny"},
            {"[空格]", " "}, {"[双引号]", "\""}, {"[单引号]", "'"}, {"[等号]", "="},
            {"[加号]", "+"}, {"[减号]", "-"}, {"[星号]", "*"}, {"[竖线]", "|"},
            {"[下划线]", "_"}, {"[短横线]", "-"}, {"[井号]", "#"}, {"[问号]", "?"},
            {"[感叹号]", "!"}, {"[百分号]", "%"}, {"[与号]", "&"}, {"[小于号]", "<"}, {"[大于号]", ">"},
            {"[HTTP200]", "OK"}, {"[HTTP201]", "Created"}, {"[HTTP204]", "No Content"},
            {"[HTTP301]", "Moved Permanently"}, {"[HTTP302]", "Found"}, {"[HTTP400]", "Bad Request"},
            {"[HTTP401]", "Unauthorized"}, {"[HTTP403]", "Forbidden"}, {"[HTTP404]", "Not Found"},
            {"[HTTP429]", "Too Many Requests"}, {"[HTTP500]", "Internal Server Error"}, {"[HTTP502]", "Bad Gateway"},
            {"[MIME文本]", "text/plain"}, {"[MIMEHTML]", "text/html"}, {"[MIMEJSON]", "application/json"},
            {"[MIME表单]", "application/x-www-form-urlencoded"}, {"[MIME二进制]", "application/octet-stream"},
            {"[单位B]", "B"}, {"[单位KB]", "KB"}, {"[单位MB]", "MB"}, {"[单位GB]", "GB"},
            {"[颜色红HEX]", "#ff4d4f"}, {"[颜色粉HEX]", "#ffadd2"}, {"[颜色橙HEX]", "#ffa940"},
            {"[颜色黄HEX]", "#fadb14"}, {"[颜色绿HEX]", "#52c41a"}, {"[颜色蓝HEX]", "#1677ff"},
            {"[颜色紫HEX]", "#722ed1"}, {"[颜色黑HEX]", "#000000"}, {"[颜色白HEX]", "#ffffff"},
            {"[可爱心]", "♡"}, {"[实心心]", "♥"}, {"[星星]", "☆"}, {"[实心星]", "★"},
            {"[音符]", "♪"}, {"[花花]", "🌸"}, {"[猫爪]", "🐾"}, {"[猫猫头]", "ฅ"},
            {"[早安]", "早安"}, {"[午安]", "午安"}, {"[晚安]", "晚安"}, {"[你好]", "你好"},
            {"[谢谢]", "谢谢"}, {"[抱歉]", "抱歉"}, {"[收到]", "收到"}, {"[完成]", "完成"},
            {"[状态正常]", "normal"}, {"[状态警告]", "warning"}, {"[状态错误]", "error"}, {"[状态未知]", "unknown"},
            {"[级别调试]", "debug"}, {"[级别信息]", "info"}, {"[级别警告]", "warn"}, {"[级别错误]", "error"},
            {"[方法GET]", "GET"}, {"[方法POST]", "POST"}, {"[方法PUT]", "PUT"}, {"[方法DELETE]", "DELETE"},
            {"[头JSON]", "Content-Type: application/json"}, {"[头文本]", "Content-Type: text/plain"},
            {"[本地地址]", "127.0.0.1"}, {"[全局监听]", "0.0.0.0"}, {"[默认端口]", "8080"},
            {"[UTF8]", "UTF-8"}, {"[CRLF]", "\r\n"}, {"[LF]", "\n"}, {"[TAB]", "\t"}
        };
        for (const auto& [key, value] : aliases) {
            if (result.find(key) != std::string::npos) result = replace_all(result, key, value);
        }
        return result;
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
        out = replace_all(out, "&", "&");
        out = replace_all(out, "[", "[");
        out = replace_all(out, "]", "]");
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

    // 生成型变量族本身提供 520+ 实用变量：数字/两位/百分/HTTP/端口/KB/MB/GB。
    static std::string replace_generated_utility_vars(const std::string& text) {
        if (text.find('[') == std::string::npos) return text;
        std::string result = text;
        static const std::regex numeric_re(R"(\[(数字|两位|百分|HTTP|端口)(\d{1,6})\])");
        result = replace_regex_two_args(result, numeric_re, [](const std::string& kind, const std::string& raw) {
            int value = to_int_or(raw);
            if (kind == "两位") return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
            if (kind == "百分") return std::to_string(value) + "%";
            return std::to_string(value);
        });

        static const std::regex bytes_re(R"(\[(KB|MB|GB)(\d{1,6})\])");
        result = replace_regex_two_args(result, bytes_re, [](const std::string& unit, const std::string& raw) {
            int64_t value = static_cast<int64_t>(to_int_or(raw));
            if (unit == "KB") value *= 1024LL;
            else if (unit == "MB") value *= 1024LL * 1024LL;
            else value *= 1024LL * 1024LL * 1024LL;
            return std::to_string(value);
        });
        return result;
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
