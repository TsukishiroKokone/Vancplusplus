// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
// ─── VanBot Cooling System ──────────────────────────────────
// 指令冷却管理
#include "common.hpp"
#include "storage.hpp"
#include <chrono>
#include <ctime>
#include <sstream>
#include <vector>
#include <string>
#include <regex>

namespace vanbot {

class CoolingSystem {
public:
    explicit CoolingSystem(Storage& storage) : m_storage(storage) {}

    // ── 检查冷却 ─────────────────────────────────────────────
    // 返回: 0=无冷却, >0=剩余秒数
    int check(BotId bot_id, UserId user_id, GroupId group_id, int64_t lexicon_id) {
        auto content = m_storage.read(bot_id, "cooling/" + std::to_string(group_id) + ".txt");
        if (!content || content->empty()) return 0;

        auto now = std::chrono::system_clock::now();
        double now_ts = std::chrono::duration<double>(now.time_since_epoch()).count();

        std::istringstream stream(*content);
        std::string line;
        while (std::getline(stream, line)) {
            auto parts = split(line, '=');
            if (parts.size() == 3) {
                try {
                    if (std::stoll(parts[0]) == user_id && std::stoll(parts[1]) == lexicon_id) {
                        double expire = std::stod(parts[2]);
                        if (expire > now_ts) {
                            return static_cast<int>(expire - now_ts);
                        }
                        return 0;
                    }
                } catch (...) {}
            }
        }
        return 0;
    }

    // ── 设置冷却 ─────────────────────────────────────────────
    void set(BotId bot_id, UserId user_id, GroupId group_id,
             int64_t lexicon_id, int seconds) {
        auto content = m_storage.read(bot_id, "cooling/" + std::to_string(group_id) + ".txt");

        auto now = std::chrono::system_clock::now();
        double expire_ts;

        if (seconds == 0) {
            // 到明天0点
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            struct tm tm_now;
        #ifdef _WIN32
            localtime_s(&tm_now, &time_t_now);
        #else
            localtime_r(&time_t_now, &tm_now);
        #endif
            tm_now.tm_hour = 0;
            tm_now.tm_min = 0;
            tm_now.tm_sec = 0;
            tm_now.tm_mday += 1;
            expire_ts = static_cast<double>(mktime(&tm_now));
        } else {
            expire_ts = std::chrono::duration<double>(now.time_since_epoch()).count() + seconds;
        }

        std::vector<std::string> lines;
        bool found = false;

        if (content && !content->empty()) {
            std::istringstream stream(*content);
            std::string line;
            while (std::getline(stream, line)) {
                auto parts = split(line, '=');
                if (parts.size() == 3) {
                    try {
                        if (std::stoll(parts[0]) == user_id && std::stoll(parts[1]) == lexicon_id) {
                            lines.push_back(std::to_string(user_id) + "=" +
                                           std::to_string(lexicon_id) + "=" +
                                           std::to_string(expire_ts));
                            found = true;
                            continue;
                        }
                    } catch (...) {}
                }
                lines.push_back(line);
            }
        }

        if (!found) {
            lines.push_back(std::to_string(user_id) + "=" +
                           std::to_string(lexicon_id) + "=" +
                           std::to_string(expire_ts));
        }

        if (seconds == 0) {
            // 000 = 清除冷却
            m_storage.write(bot_id, "cooling/" + std::to_string(group_id) + ".txt", "");
        } else {
            std::string result;
            for (size_t i = 0; i < lines.size(); i++) {
                if (i) result += '\n';
                result += lines[i];
            }
            m_storage.write(bot_id, "cooling/" + std::to_string(group_id) + ".txt", result);
        }
    }

    // ── 从文本中提取并处理冷却标记 (seconds~) ──────────────
    // 返回: {处理后的文本, 冷却秒数(0=无冷却)}
    static std::pair<std::string, int> extract_cooling(const std::string& text) {
        static const std::regex re(R"(\((\d+)~\))");
        std::smatch match;
        if (std::regex_search(text, match, re)) {
            int seconds = std::stoi(match[1].str());
            std::string result = std::regex_replace(text, re, "");
            return {result, seconds};
        }
        return {text, 0};
    }

private:
    Storage& m_storage;

    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> tokens;
        std::istringstream stream(s);
        std::string token;
        while (std::getline(stream, token, delim)) {
            tokens.push_back(token);
        }
        return tokens;
    }
};

} // namespace vanbot
