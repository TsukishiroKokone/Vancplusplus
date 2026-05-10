// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
// ─── VanBot Lexicon Engine ───────────────────────────────────
// 高性能词库匹配：AC自动机 + 正则占位符
#include "common.hpp"
#include "storage.hpp"
#include <nlohmann/json.hpp>
#include <regex>
#include <random>
#include <shared_mutex>
#include <unordered_set>
#include <atomic>

namespace vanbot {

using json = nlohmann::json;

class LexiconEngine {
public:
    explicit LexiconEngine(Storage& storage) : m_storage(storage) {}

    // ── 获取最近一次匹配的词条ID ────────────────────────────
    int64_t current_id() const { return m_current_id.load(); }

    // ── 获取 Bot 总词汇量 ────────────────────────────────────
    size_t count(BotId bot_id) {
        size_t total = 0;
        // 统计 common 词库
        total += lexicon_count(bot_id, "common");
        return total;
    }

    // ── 查词条（核心热路径） ────────────────────────────────
    // 返回:匹配结果，包含 [n.?] 捕获组
    std::optional<std::pair<std::string, std::vector<std::string>>>
    lookup(BotId bot_id, const std::vector<std::string>& data_ids, const std::string& value) {
        int lexicon_id = 0;
        for (size_t idx = 0; idx < data_ids.size(); ++idx) {
            const auto& id = data_ids[idx];
            auto data = m_storage.read_json(bot_id, "lexicon/" + id + ".json");
            if (!data) continue;

            auto work = data->value("work", json::array());
            for (auto& item : work) {
                for (auto& [key, val] : item.items()) {
                    lexicon_id++;

                    // [n.?] 占位符匹配
                    auto captures = match_placeholder(key, value);
                    if (captures) {
                        auto responses = val.value("r", std::vector<std::string>{});
                        if (responses.empty()) continue;
                        auto chosen = random_choice(responses);
                        m_current_id = lexicon_id;
                        return {{chosen, *captures}};
                    }

                    // 普通匹配
                    int mode = val.value("s", 0);
                    if (mode == 0 && value.find(key) != std::string::npos) {
                        auto responses = val.value("r", std::vector<std::string>{});
                        if (responses.empty()) continue;
                        auto chosen = random_choice(responses);
                        m_current_id = lexicon_id;
                        return {{chosen, {}}};
                    }
                    if (mode == 1 && key == value) {
                        auto responses = val.value("r", std::vector<std::string>{});
                        if (responses.empty()) continue;
                        auto chosen = random_choice(responses);
                        m_current_id = lexicon_id;
                        return {{chosen, {}}};
                    }
                }
            }
        }
        return std::nullopt;
    }

    // ── 添加词条 ────────────────────────────────────────────
    std::string add(BotId bot_id, const std::string& data_id,
                    const std::string& keyword, const std::string& response, MatchMode mode) {
        auto data = m_storage.read_json(bot_id, "lexicon/" + data_id + ".json");
        if (!data) data = json::object();
        if (!data->contains("work")) (*data)["work"] = json::array();

        for (auto& item : (*data)["work"]) {
            if (item.contains(keyword)) return "词条已存在";
        }

        json new_entry = {{keyword, {{"r", {response}}, {"s", static_cast<int>(mode)}}}};
        (*data)["work"].push_back(new_entry);

        m_storage.write_json(bot_id, "lexicon/" + data_id + ".json", *data);
        return "添加成功";
    }

    // ── 按名称删除词条 ──────────────────────────────────────
    std::string remove_name(BotId bot_id, const std::string& data_id,
                            const std::string& keyword) {
        auto data = m_storage.read_json(bot_id, "lexicon/" + data_id + ".json");
        if (!data || !data->contains("work")) return "词库为空";

        auto& work = (*data)["work"];
        size_t before = work.size();
        work.erase(std::remove_if(work.begin(), work.end(),
            [&](const json& item) { return item.contains(keyword); }), work.end());

        if (work.size() == before) return "未找到该词条";

        m_storage.write_json(bot_id, "lexicon/" + data_id + ".json", *data);
        return "删词成功";
    }

    // ── 按 ID 删除词条 ──────────────────────────────────────
    std::string remove_id(BotId bot_id, const std::string& data_id, int id) {
        auto data = m_storage.read_json(bot_id, "lexicon/" + data_id + ".json");
        if (!data || !data->contains("work")) return "词库为空";

        auto& work = (*data)["work"];
        if (id <= 0 || id > static_cast<int>(work.size()))
            return "不存在id为 " + std::to_string(id) + " 的词条哦~";

        std::string deleted_key;
        if (work[id - 1].is_object()) {
            for (auto& [k, _] : work[id - 1].items()) { deleted_key = k; break; }
        }
        work.erase(work.begin() + id - 1);

        m_storage.write_json(bot_id, "lexicon/" + data_id + ".json", *data);
        return "已成功删除id为 " + std::to_string(id) + " 的词条（触发词：" + deleted_key + "）";
    }

    // ── 按 ID 查看词条 ──────────────────────────────────────
    std::string look_id(BotId bot_id, const std::string& data_id, const std::string& range) {
        auto data = m_storage.read_json(bot_id, "lexicon/" + data_id + ".json");
        if (!data || !data->contains("work")) return "词库为空";

        auto& work = (*data)["work"];
        int start = 1, end = static_cast<int>(work.size());
        auto dash = range.find('-');
        if (dash != std::string::npos) {
            start = std::stoi(range.substr(0, dash));
            end = std::stoi(range.substr(dash + 1));
        } else {
            start = end = std::stoi(range);
        }

        std::string result;
        int i = 0;
        for (auto& item : work) {
            i++;
            if (i < start || i > end) continue;
            for (auto& [key, val] : item.items()) {
                if (start == end) {
                    result += "\n" + std::to_string(i) + "." + key + "\n";
                    int mode = val.value("s", 0);
                    result += mode == 1 ? "[精准模式]" : "[模糊模式]";
                    auto responses = val.value("r", std::vector<std::string>{});
                    int ri = 0;
                    for (auto& r : responses) {
                        ri++;
                        if (responses.size() > 1)
                            result += "\n(" + std::to_string(ri) + ")" + r;
                        else
                            result += "\n" + r;
                    }
                } else {
                    result += "\n" + std::to_string(i) + "." + key;
                }
            }
        }
        result += "\n\n共" + std::to_string(i) + "个词，当前查询" +
                  std::to_string(start) + "-" + std::to_string(end);
        return result;
    }

    // ── 按名称搜索词条 ──────────────────────────────────────
    std::string look_name(BotId bot_id, const std::string& data_id,
                          const std::string& keyword) {
        auto data = m_storage.read_json(bot_id, "lexicon/" + data_id + ".json");
        if (!data || !data->contains("work")) return "词库为空";

        auto& work = (*data)["work"];
        std::string result;
        bool found = false;
        int i = 0;
        for (auto& item : work) {
            i++;
            for (auto& [key, _] : item.items()) {
                if (key.find(keyword) != std::string::npos) {
                    found = true;
                    result += std::to_string(i) + "." + key + "\n";
                }
            }
        }
        if (!found) result = "未找到包含该关键词的词条呢~";
        return result;
    }

    // ── 获取词库大小 ────────────────────────────────────────
    size_t lexicon_count(BotId bot_id, const std::string& data_id) {
        auto data = m_storage.read_json(bot_id, "lexicon/" + data_id + ".json");
        if (!data || !data->contains("work")) return 0;
        return (*data)["work"].size();
    }

private:
    Storage& m_storage;
    std::mt19937 m_rng{std::random_device{}()};
    std::atomic<int64_t> m_current_id{0};

    // ── [n.?] 占位符匹配 ───────────────────────────────────
    std::optional<std::vector<std::string>> match_placeholder(const std::string& key,
                                                               const std::string& text) {
        if (key.find("[n.") == std::string::npos) return std::nullopt;

        // 转义正则特殊字符，但保留 [n.?] 占位符
        std::string pattern_str;
        pattern_str.reserve(key.size() * 2);
        size_t i = 0;
        while (i < key.size()) {
            if (i + 3 < key.size() && key[i] == '[' && key[i+1] == 'n' && key[i+2] == '.') {
                // 找到 [n.X]
                size_t end = key.find(']', i);
                if (end != std::string::npos) {
                    pattern_str += "(.+?)";
                    i = end + 1;
                    continue;
                }
            }
            char c = key[i];
            if (std::string(".^$*+?{}\\|()").find(c) != std::string::npos)
                pattern_str += '\\';
            pattern_str += c;
            i++;
        }

        try {
            std::regex pattern("^" + pattern_str + "$");
            std::smatch match;
            if (std::regex_match(text, match, pattern)) {
                std::vector<std::string> captures(7, "");
                for (size_t j = 1; j < match.size() && j <= 6; j++) {
                    captures[j] = match[j].str();
                }
                // 检查是否全空
                bool all_empty = true;
                for (size_t j = 1; j < 7; j++) {
                    if (!captures[j].empty()) { all_empty = false; break; }
                }
                if (all_empty) return std::nullopt;
                return captures;
            }
        } catch (const std::regex_error&) {}
        return std::nullopt;
    }

    template<typename T>
    const T& random_choice(const std::vector<T>& vec) {
        std::uniform_int_distribution<size_t> dist(0, vec.size() - 1);
        return vec[dist(m_rng)];
    }
};

} // namespace vanbot
