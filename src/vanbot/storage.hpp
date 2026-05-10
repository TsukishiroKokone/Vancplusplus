#pragma once
// ─── VanBot Storage Layer ────────────────────────────────────
// 高并发文件操作与数据持久化
#include "common.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vanbot {

namespace fs = std::filesystem;
using json = nlohmann::json;

class Storage {
public:
    explicit Storage(std::string data_dir = "./Van_keyword")
        : m_data_dir(std::move(data_dir)) {}

    // ── 初始化数据目录 ──────────────────────────────────────
    void init(BotId bot_id = 0) {
        auto base = fs::path(m_data_dir);
        create_dirs(base);
        if (bot_id) {
            auto bot_dir = base / std::to_string(bot_id);
            create_dirs(bot_dir);
            create_dirs(bot_dir / "lexicon");
            create_dirs(bot_dir / "expand");
            create_dirs(bot_dir / "cooling");
            create_dirs(bot_dir / "screenshot");
            create_dirs(bot_dir / "filedata");
            create_dirs(bot_dir / "filecache");
        }
    }

    // ── 文件读取（线程安全，读锁） ──────────────────────────
    std::optional<std::string> read(BotId bot_id, const std::string& filename) const {
        auto path = resolve_path(bot_id, filename);
        std::shared_lock lock(m_mutex);
        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) return std::nullopt;
        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }

    // ── 文件写入（线程安全，写锁） ──────────────────────────
    bool write(BotId bot_id, const std::string& filename, const std::string& content) {
        auto path = resolve_path(bot_id, filename);
        auto dir = fs::path(path).parent_path();
        std::unique_lock lock(m_mutex);
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << content;
        return true;
    }

    // ── 读取 JSON 文件 ──────────────────────────────────────
    std::optional<json> read_json(BotId bot_id, const std::string& filename) const {
        auto content = read(bot_id, filename);
        if (!content) {
            // 自动创建空词库
            json empty = {{"work", json::array()}};
            return empty;
        }
        try {
            return json::parse(*content);
        } catch (...) {
            return std::nullopt;
        }
    }

    // ── 写入 JSON 文件 ──────────────────────────────────────
    bool write_json(BotId bot_id, const std::string& filename, const json& data, int indent = 4) {
        return write(bot_id, filename, data.dump(indent, ' ', false, json::error_handler_t::replace));
    }

    // ── KV 文件操作（select.txt / switch.txt） ──────────────
    std::unordered_map<std::string, std::string> read_kv(BotId bot_id, const std::string& filename) const {
        auto content = read(bot_id, filename);
        std::unordered_map<std::string, std::string> map;
        if (!content || content->empty()) return map;
        std::istringstream stream(*content);
        std::string line;
        while (std::getline(stream, line)) {
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            auto key = trim(line.substr(0, pos));
            auto val = trim(line.substr(pos + 1));
            if (!key.empty()) map[key] = val;
        }
        return map;
    }

    bool write_kv(BotId bot_id, const std::string& filename,
                  const std::unordered_map<std::string, std::string>& map) {
        std::ostringstream oss;
        for (auto& [k, v] : map) {
            oss << k << '=' << v << '\n';
        }
        return write(bot_id, filename, oss.str());
    }

    // ── 身份列表读写 ────────────────────────────────────────
    std::vector<std::string> read_id_list(const std::string& filename, BotId bot_id = 0) const {
        auto content = read(bot_id, filename);
        if (!content || content->empty()) return {};
        std::vector<std::string> result;
        std::istringstream stream(*content);
        std::string token;
        while (std::getline(stream, token, ',')) {
            auto t = trim(token);
            if (!t.empty()) result.push_back(t);
        }
        return result;
    }

    bool write_id_list(const std::string& filename, const std::vector<std::string>& ids, BotId bot_id = 0) {
        std::string result;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i) result += ',';
            result += ids[i];
        }
        return write(bot_id, filename, result);
    }

    // ── 逗号分隔列表增删 ────────────────────────────────────
    static std::string identity_operation(const std::string& data, const std::string& text) {
        std::vector<std::string> ids = split(data, ',');
        if (text.empty()) return data;
        char op = text[0];
        std::string value = text.substr(1);
        if (op == '+') {
            if (std::find(ids.begin(), ids.end(), value) == ids.end())
                ids.push_back(value);
        } else if (op == '-') {
            ids.erase(std::remove(ids.begin(), ids.end(), value), ids.end());
        }
        return join(ids, ',');
    }

    const std::string& data_dir() const { return m_data_dir; }

private:
    std::string m_data_dir;
    mutable std::shared_mutex m_mutex;

    std::string resolve_path(BotId bot_id, const std::string& filename) const {
        fs::path base(m_data_dir);
        if (bot_id && !filename.empty())
            return (base / std::to_string(bot_id) / filename).string();
        if (!filename.empty())
            return (base / filename).string();
        return (base / "master.txt").string();
    }

    void create_dirs(const fs::path& p) {
        std::error_code ec;
        fs::create_directories(p, ec);
    }

    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> tokens;
        std::istringstream stream(s);
        std::string token;
        while (std::getline(stream, token, delim)) {
            auto t = trim(token);
            if (!t.empty()) tokens.push_back(t);
        }
        return tokens;
    }

    static std::string join(const std::vector<std::string>& v, char delim) {
        std::string result;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) result += delim;
            result += v[i];
        }
        return result;
    }
};

} // namespace vanbot
