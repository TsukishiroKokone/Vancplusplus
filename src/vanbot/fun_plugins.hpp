// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
// ─── VanBot Fun Plugins ──────────────────────────────────────
// ZeroBot-plugin 风格趣味功能：签到 / 今日运势 / 抽签 / 塔罗 / 骰子 / 硬币 / 老婆 / 戳一戳
#include "common.hpp"
#include "storage.hpp"
#include "coins.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace vanbot {

using json = nlohmann::json;

class FunPlugins {
public:
    FunPlugins(Storage& storage, CoinsSystem& coins)
        : m_storage(storage), m_coins(coins) {}

    std::optional<std::string> handle(BotId bot_id, Env env, int64_t env_id, const Event& event, const std::string& message) {
        if (event.type == Event::Poke) {
            return handle_poke(event);
        }

        std::string cmd = trim(message);
        if (cmd.empty()) return std::nullopt;

        if (cmd == "签到" || cmd == "/签到" || cmd == "打卡" || cmd == "/checkin") {
            return checkin(bot_id, env, env_id, event.user_id, display_name(event));
        }
        if (cmd == "今日运势" || cmd == "运势" || cmd == "/fortune" || cmd == "今日人品") {
            return daily_fortune(event.user_id, display_name(event));
        }
        if (cmd == "抽签" || cmd == "/lottery" || cmd == "求签") {
            return lottery(event.user_id, display_name(event));
        }
        if (cmd == "塔罗" || cmd == "塔罗牌" || cmd == "/tarot") {
            return tarot(event.user_id, display_name(event));
        }
        if (cmd == "骰子" || cmd == "投骰子" || cmd == "/dice" || cmd == "roll") {
            return dice(event.user_id, 6);
        }
        if (starts_with(cmd, "骰子")) {
            int sides = to_int_or(cmd.substr(std::string("骰子").size()), 6);
            return dice(event.user_id, sides);
        }
        if (starts_with(cmd, "d") || starts_with(cmd, "D")) {
            int sides = to_int_or(cmd.substr(1), 6);
            return dice(event.user_id, sides);
        }
        if (cmd == "硬币" || cmd == "抛硬币" || cmd == "/coin") {
            return coin(event.user_id);
        }
        if (cmd == "老虎机" || cmd == "拉霸" || cmd == "/slot") {
            return slot(event.user_id);
        }
        if (cmd == "今日老婆" || cmd == "老婆" || cmd == "/waifu") {
            return waifu(event.user_id, display_name(event));
        }
        if (cmd == "菜单" || cmd == "功能" || cmd == "/fun") {
            return menu();
        }

        return std::nullopt;
    }

private:
    Storage& m_storage;
    CoinsSystem& m_coins;

    std::optional<std::string> handle_poke(const Event& event) const {
        if (event.group_id == 0) return std::nullopt;
        if (event.target_id != 0 && event.target_id != event.self_id) return std::nullopt;
        const auto& replies = poke_replies();
        return replies[seeded_index(event.user_id + event.group_id + event.self_id, replies.size())];
    }

    std::string checkin(BotId bot_id, Env env, int64_t env_id, UserId user_id, const std::string& name) {
        const std::string today = date_key();
        json data = m_storage.read_json(bot_id, "fun_plugins.json").value_or(json::object());
        if (!data.contains("checkin") || !data["checkin"].is_object()) data["checkin"] = json::object();

        const std::string scope = std::to_string(static_cast<int>(env)) + ":" + std::to_string(env_id) + ":" + std::to_string(user_id);
        json& checkin = data["checkin"];
        json& record = checkin[scope];
        const std::string last_date = record.value("date", "");
        int streak = record.value("streak", 0);

        if (last_date == today) {
            int reward = record.value("reward", 0);
            int total = record.value("total", 0);
            return "🌸 " + name + " 今天已经签到过啦~\n连续签到：" + std::to_string(streak) +
                   " 天\n今日获得：" + std::to_string(reward) + " 枚樱花币\n累计签到奖励：" + std::to_string(total) + " 枚";
        }

        streak = is_yesterday(last_date, today) ? streak + 1 : 1;
        const int reward = 10 + static_cast<int>(seeded_index(user_id + env_id + ymd_number(), 31)) + (std::min)(streak, 30);
        const int total = record.value("total", 0) + reward;
        record["date"] = today;
        record["streak"] = streak;
        record["reward"] = reward;
        record["total"] = total;
        m_storage.write_json(bot_id, "fun_plugins.json", data);
        if (env == Env::Group) {
            m_coins.operate(bot_id, env_id, user_id, "+" + std::to_string(reward), "sakura");
        }

        return "✅ 签到成功，" + name + "！\n获得 " + std::to_string(reward) + " 枚樱花币 🌸\n连续签到：" +
               std::to_string(streak) + " 天\n今日小贴士：" + pick_seeded(tips(), user_id + ymd_number());
    }

    static std::string daily_fortune(UserId user_id, const std::string& name) {
        const uint64_t seed = static_cast<uint64_t>(user_id) * 1315423911ULL + static_cast<uint64_t>(ymd_number());
        int score = 1 + static_cast<int>(seeded_index(seed, 100));
        return "🔮 " + name + " 的今日运势\n运势指数：" + std::to_string(score) + "/100\n等级：" +
               fortune_level(score) + "\n幸运色：" + pick_seeded(colors(), seed + 1) +
               "\n幸运数字：" + std::to_string(1 + seeded_index(seed + 2, 99)) +
               "\n建议：" + pick_seeded(advices(), seed + 3);
    }

    static std::string lottery(UserId user_id, const std::string& name) {
        const uint64_t seed = static_cast<uint64_t>(user_id) + static_cast<uint64_t>(std::time(nullptr));
        return "🎋 " + name + " 抽到了「" + pick_seeded(lottery_levels(), seed) + "」\n签文：" + pick_seeded(lottery_texts(), seed + 7);
    }

    static std::string tarot(UserId user_id, const std::string& name) {
        const uint64_t seed = static_cast<uint64_t>(user_id) * 11400714819323198485ULL + static_cast<uint64_t>(std::time(nullptr) / 3600);
        bool reversed = seeded_index(seed + 1, 2) == 1;
        std::string card = pick_seeded(tarot_cards(), seed);
        return "🃏 " + name + " 抽到塔罗牌：" + card + (reversed ? "（逆位）" : "（正位）") +
               "\n提示：" + pick_seeded(tarot_hints(), seed + 2);
    }

    static std::string dice(UserId user_id, int sides) {
        sides = (std::clamp)(sides, 2, 1000000);
        const uint64_t seed = static_cast<uint64_t>(user_id) + static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        return "🎲 D" + std::to_string(sides) + " = " + std::to_string(1 + seeded_index(seed, static_cast<size_t>(sides)));
    }

    static std::string coin(UserId user_id) {
        const uint64_t seed = static_cast<uint64_t>(user_id) + static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        return std::string("🪙 硬币落下：") + (seeded_index(seed, 2) ? "反面" : "正面");
    }

    static std::string slot(UserId user_id) {
        const uint64_t seed = static_cast<uint64_t>(user_id) + static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto& icons = slot_icons();
        std::string a = icons[seeded_index(seed, icons.size())];
        std::string b = icons[seeded_index(seed + 1, icons.size())];
        std::string c = icons[seeded_index(seed + 2, icons.size())];
        bool win = a == b && b == c;
        return "🎰 | " + a + " | " + b + " | " + c + " |\n" + (win ? "大奖！今天被幸运砸中啦~" : "差一点点喵，再试试吧~");
    }

    static std::string waifu(UserId user_id, const std::string& name) {
        const uint64_t seed = static_cast<uint64_t>(user_id) * 2654435761ULL + static_cast<uint64_t>(ymd_number());
        return "💞 " + name + " 今天的二次元老婆是：" + pick_seeded(waifus(), seed) + "\n羁绊值：" + std::to_string(1 + seeded_index(seed + 9, 100)) + "%";
    }

    static std::string menu() {
        return "🌸 趣味功能菜单\n签到 / 今日运势 / 抽签 / 塔罗 / 骰子 或 D20 / 硬币 / 老婆 / 老虎机\n戳一戳机器人也会有可爱回应哦~";
    }

    static std::string display_name(const Event& event) {
        if (!event.sender_card.empty()) return event.sender_card;
        if (!event.sender_name.empty()) return event.sender_name;
        return std::to_string(event.user_id);
    }

    static bool starts_with(const std::string& s, const std::string& prefix) {
        return s.rfind(prefix, 0) == 0;
    }

    static int to_int_or(const std::string& s, int fallback) {
        try {
            size_t idx = 0;
            int v = std::stoi(trim(s), &idx);
            return idx == trim(s).size() ? v : fallback;
        } catch (...) {
            return fallback;
        }
    }

    static std::string trim(const std::string& s) {
        auto b = std::find_if_not(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
        auto e = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) { return std::isspace(c); }).base();
        return b >= e ? std::string{} : std::string(b, e);
    }

    static std::string date_key() {
        std::time_t now = std::time(nullptr);
        struct tm t;
#ifdef _WIN32
        localtime_s(&t, &now);
#else
        localtime_r(&now, &t);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
        return buf;
    }

    static int ymd_number() {
        std::time_t now = std::time(nullptr);
        struct tm t;
#ifdef _WIN32
        localtime_s(&t, &now);
#else
        localtime_r(&now, &t);
#endif
        return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
    }

    static bool is_yesterday(const std::string& last, const std::string& today) {
        if (last.empty()) return false;
        auto parse = [](const std::string& s) {
            struct tm t{};
            if (s.size() >= 10) {
                t.tm_year = std::stoi(s.substr(0, 4)) - 1900;
                t.tm_mon = std::stoi(s.substr(5, 2)) - 1;
                t.tm_mday = std::stoi(s.substr(8, 2));
                t.tm_isdst = -1;
            }
            return std::mktime(&t);
        };
        try {
            return static_cast<long long>((parse(today) - parse(last)) / 86400) == 1;
        } catch (...) {
            return false;
        }
    }

    static size_t seeded_index(uint64_t seed, size_t size) {
        if (size == 0) return 0;
        seed ^= seed >> 33;
        seed *= 0xff51afd7ed558ccdULL;
        seed ^= seed >> 33;
        seed *= 0xc4ceb9fe1a85ec53ULL;
        seed ^= seed >> 33;
        return static_cast<size_t>(seed % size);
    }

    static std::string pick_seeded(const std::vector<std::string>& v, uint64_t seed) {
        return v[seeded_index(seed, v.size())];
    }

    static std::string fortune_level(int score) {
        if (score >= 95) return "大吉·SSR";
        if (score >= 80) return "大吉";
        if (score >= 65) return "中吉";
        if (score >= 45) return "小吉";
        if (score >= 25) return "末吉";
        return "需要奶茶续命";
    }

    static const std::vector<std::string>& poke_replies() { static const std::vector<std::string> v = {"戳、戳回来啦！(ฅ>ω<*ฅ)", "不要一直戳啦，会害羞的喵~", "检测到戳一戳：发送一朵小樱花 🌸", "诶嘿，被发现啦！", "再戳就把你加入今日幸运名单哦~"}; return v; }
    static const std::vector<std::string>& colors() { static const std::vector<std::string> v = {"樱花粉", "薄荷绿", "奶油白", "天空蓝", "薰衣草紫", "蜜桃橙", "月光银", "海盐蓝"}; return v; }
    static const std::vector<std::string>& advices() { static const std::vector<std::string> v = {"适合整理词库，让灵感变得闪闪发光。", "适合早点休息，给明天留一点甜。", "适合大胆表达，会遇见温柔回应。", "适合写代码，但记得保存和提交。", "适合喝奶茶，不要空腹战斗。", "适合学习新协议，OneBot 与 Milky 都会眷顾你。"}; return v; }
    static const std::vector<std::string>& lottery_levels() { static const std::vector<std::string> v = {"大吉", "中吉", "小吉", "吉", "末吉", "凶转吉", "SSR签", "猫猫签"}; return v; }
    static const std::vector<std::string>& lottery_texts() { static const std::vector<std::string> v = {"花会沿着你选择的方向开放。", "先做最小的一步，剩下的路会变亮。", "今天的 bug 会在第二杯水后投降。", "有人正在悄悄支持你。", "把复杂的事情拆小，就会变得可爱。", "别急，月亮也不是一天就圆的。"}; return v; }
    static const std::vector<std::string>& tarot_cards() { static const std::vector<std::string> v = {"愚者", "魔术师", "女祭司", "女皇", "皇帝", "教皇", "恋人", "战车", "力量", "隐者", "命运之轮", "正义", "倒吊人", "死神", "节制", "恶魔", "塔", "星星", "月亮", "太阳", "审判", "世界"}; return v; }
    static const std::vector<std::string>& tarot_hints() { static const std::vector<std::string> v = {"新的旅程正在加载中。", "把主动权拿回来。", "听听直觉给你的 whisper。", "暂停不是失败，是蓄力。", "旧结构会崩塌，但新窗口也会打开。", "答案可能藏在你忽略的小细节里。"}; return v; }
    static const std::vector<std::string>& tips() { static const std::vector<std::string> v = {"变量可以嵌套 52 层哦。", "非 HTTP 变量会尽量走哈希分派。", "签到奖励会写入 fun_plugins.json。", "D20 可以快速投二十面骰。", "塔罗每小时都会换一点点手感。"}; return v; }
    static const std::vector<std::string>& slot_icons() { static const std::vector<std::string> v = {"🌸", "🍒", "⭐", "🐾", "💎", "🍀"}; return v; }
    static const std::vector<std::string>& waifus() { static const std::vector<std::string> v = {"樱花魔法师", "薄荷猫娘", "星空旅人", "月白歌姬", "代码妖精", "奶茶店看板娘", "机械天使", "图书馆幽灵", "海盐偶像", "梦境占卜师"}; return v; }
};

} // namespace vanbot
