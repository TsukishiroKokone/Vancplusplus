#pragma once
// ─── VanBot Coins System ────────────────────────────────────
// 积分/虚拟货币系统
#include "common.hpp"
#include "storage.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <ctime>
#include <sstream>
#include <algorithm>

namespace vanbot {

using json = nlohmann::json;

class CoinsSystem {
public:
    explicit CoinsSystem(Storage& storage) : m_storage(storage) {}

    // ── 积分操作 ─────────────────────────────────────────────
    // change: "0"=查询, "+N"/"-N"=增减, "/val/"=赋值, "++val"=追加, "--val"=移除
    std::string operate(BotId bot_id, GroupId group, UserId user,
                        const std::string& change, const std::string& type) {
        auto data = m_storage.read_json(bot_id, "coins.json");
        if (!data) data = json::object();
        if (!data->contains("work")) (*data)["work"] = json::array();

        // 查询模式
        if (change == "0") {
            for (auto& item : (*data)["work"]) {
                if (item.value("group", 0) == group &&
                    item.value("type", "") == type &&
                    item.value("user", 0) == user) {
                    return std::to_string(item.value("coins", 0));
                }
            }
            return "0";
        }

        std::string return_change = change;
        bool assign_mode = false;
        bool append_mode = false;
        bool remove_mode = false;
        long long coins_change = 0;
        std::string target_value;

        if (change.size() >= 2 && change.front() == '/' && change.back() == '/') {
            // /val/ 赋值模式
            return_change = change.substr(1, change.size() - 2);
            assign_mode = true;
        } else if (change.size() >= 2 && change.substr(0, 2) == "++") {
            target_value = change.substr(2);
            append_mode = true;
        } else if (change.size() >= 2 && change.substr(0, 2) == "--") {
            target_value = change.substr(2);
            remove_mode = true;
        } else if (!change.empty() && (change[0] == '+' || change[0] == '-')) {
            try {
                int sign = (change[0] == '+') ? 1 : -1;
                coins_change = sign * std::stoll(change.substr(1));
            } catch (...) {
                return "错误参数：" + change;
            }
        } else {
            return "错误参数：" + change;
        }

        bool updated = false;
        for (auto& item : (*data)["work"]) {
            if (item.value("group", 0) == group &&
                item.value("type", "") == type &&
                item.value("user", 0) == user) {

                if (assign_mode) {
                    item["coins"] = return_change;
                } else if (append_mode) {
                    std::string current = item.value("coins", "");
                    auto list = split_comma(current);
                    if (std::find(list.begin(), list.end(), target_value) == list.end())
                        list.push_back(target_value);
                    item["coins"] = join_comma(list);
                    return_change = item["coins"].get<std::string>();
                } else if (remove_mode) {
                    std::string current = item.value("coins", "");
                    auto list = split_comma(current);
                    list.erase(std::remove(list.begin(), list.end(), target_value), list.end());
                    item["coins"] = join_comma(list);
                    return_change = item["coins"].get<std::string>();
                } else {
                    long long current = 0;
                    try { current = item.value("coins", 0); } catch (...) {}
                    item["coins"] = current + coins_change;
                }
                item["uptime"] = time_str();
                updated = true;
                break;
            }
        }

        if (!updated) {
            json new_record;
            new_record["group"] = group;
            new_record["type"] = type;
            new_record["user"] = user;
            new_record["uptime"] = time_str();

            if (assign_mode) {
                new_record["coins"] = return_change;
            } else if (append_mode) {
                new_record["coins"] = target_value;
                return_change = target_value;
            } else if (remove_mode) {
                new_record["coins"] = "";
                return_change = "";
            } else {
                new_record["coins"] = coins_change;
            }
            (*data)["work"].push_back(new_record);
        }

        m_storage.write_json(bot_id, "coins.json", *data);
        return std::to_string(return_change);
    }

private:
    Storage& m_storage;

    static std::string time_str() {
        auto now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        return buf;
    }

    static std::vector<std::string> split_comma(const std::string& s) {
        if (s.empty()) return {};
        std::vector<std::string> result;
        std::istringstream stream(s);
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (!token.empty()) result.push_back(token);
        }
        return result;
    }

    static std::string join_comma(const std::vector<std::string>& v) {
        std::string result;
        for (size_t i = 0; i < v.size(); i++) {
            if (i) result += ',';
            result += v[i];
        }
        return result;
    }
};

} // namespace vanbot
