#pragma once
// ─── VanBot Storage Layer ────────────────────────────────────
// 高并发文件/SQLite 双后端数据持久化
#include "common.hpp"
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace vanbot {

namespace fs = std::filesystem;
using json = nlohmann::json;

class Storage {
public:
    explicit Storage(std::string data_dir = "./Van_keyword",
                     StorageBackend backend = StorageBackend::File,
                     std::string sqlite_path = {})
        : m_data_dir(std::move(data_dir))
        , m_backend(backend)
        , m_sqlite_path(std::move(sqlite_path)) {
        if (m_sqlite_path.empty()) m_sqlite_path = (fs::path(m_data_dir) / "van_lexicon.db").string();
    }

    ~Storage() {
        close_sqlite();
    }

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    // ── 初始化数据目录 / SQLite schema ───────────────────────
    void init(BotId bot_id = 0) {
        auto base = fs::path(m_data_dir);
        create_dirs(base);
        if (m_backend == StorageBackend::SQLite) init_sqlite();
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

    // ── 文件/SQLite 文本读取（线程安全，读锁） ────────────────
    std::optional<std::string> read(BotId bot_id, const std::string& filename) const {
        if (m_backend == StorageBackend::SQLite && is_sqlite_managed_file(filename)) {
            return sqlite_read_text(bot_id, filename);
        }

        auto path = resolve_path(bot_id, filename);
        std::shared_lock lock(m_mutex);
        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) return std::nullopt;
        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }

    // ── 文件/SQLite 文本写入（线程安全，写锁） ────────────────
    bool write(BotId bot_id, const std::string& filename, const std::string& content) {
        if (m_backend == StorageBackend::SQLite && is_sqlite_managed_file(filename)) {
            return sqlite_write_text(bot_id, filename, content);
        }

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

    // ── 读取 JSON 文件 / SQLite JSON 文档 ─────────────────────
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

    // ── 写入 JSON 文件 / SQLite JSON 文档 ─────────────────────
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
    StorageBackend backend() const { return m_backend; }
    const std::string& sqlite_path() const { return m_sqlite_path; }

private:
    std::string m_data_dir;
    StorageBackend m_backend = StorageBackend::File;
    std::string m_sqlite_path;
    mutable std::shared_mutex m_mutex;
    mutable sqlite3* m_db = nullptr;

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

    void init_sqlite() const {
        std::unique_lock lock(m_mutex);
        if (m_db) return;
        auto db_path = fs::path(m_sqlite_path);
        if (db_path.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(db_path.parent_path(), ec);
        }
        if (sqlite3_open(m_sqlite_path.c_str(), &m_db) != SQLITE_OK) {
            close_sqlite_unlocked();
            return;
        }
        exec_sql_unlocked("PRAGMA journal_mode=WAL;");
        exec_sql_unlocked("PRAGMA synchronous=NORMAL;");
        exec_sql_unlocked("CREATE TABLE IF NOT EXISTS kv_store ("
                          "bot_id INTEGER NOT NULL,"
                          "name TEXT NOT NULL,"
                          "content TEXT NOT NULL,"
                          "updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
                          "PRIMARY KEY(bot_id, name));");
        exec_sql_unlocked("CREATE TABLE IF NOT EXISTS lexicon_entries ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                          "bot_id INTEGER NOT NULL,"
                          "data_id TEXT NOT NULL,"
                          "keyword TEXT NOT NULL,"
                          "match_mode INTEGER NOT NULL DEFAULT 0,"
                          "responses TEXT NOT NULL,"
                          "updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
                          "UNIQUE(bot_id, data_id, keyword));");
        exec_sql_unlocked("CREATE TABLE IF NOT EXISTS coin_records ("
                          "bot_id INTEGER NOT NULL,"
                          "group_id INTEGER NOT NULL,"
                          "user_id INTEGER NOT NULL,"
                          "type TEXT NOT NULL,"
                          "coins TEXT NOT NULL,"
                          "updated_at TEXT NOT NULL,"
                          "PRIMARY KEY(bot_id, group_id, user_id, type));");
        exec_sql_unlocked("CREATE TABLE IF NOT EXISTS cooling_records ("
                          "bot_id INTEGER NOT NULL,"
                          "group_id INTEGER NOT NULL,"
                          "user_id INTEGER NOT NULL,"
                          "lexicon_id INTEGER NOT NULL,"
                          "expire_time REAL NOT NULL,"
                          "PRIMARY KEY(bot_id, group_id, user_id, lexicon_id));");
    }

    void close_sqlite() {
        std::unique_lock lock(m_mutex);
        close_sqlite_unlocked();
    }

    void close_sqlite_unlocked() const {
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }

    bool exec_sql_unlocked(const char* sql) const {
        if (!m_db) return false;
        char* err = nullptr;
        int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
        return rc == SQLITE_OK;
    }

    static bool is_sqlite_managed_file(const std::string& filename) {
        return filename.rfind("lexicon/", 0) == 0 ||
               filename.rfind("cooling/", 0) == 0 ||
               filename == "coins.json" ||
               filename == "switch.txt" ||
               filename == "select.txt" ||
               filename == "master.txt" ||
               filename == "admin.txt" ||
               filename == "executive.txt";
    }

    std::optional<std::string> sqlite_read_text(BotId bot_id, const std::string& filename) const {
        init_sqlite();
        std::shared_lock lock(m_mutex);
        if (!m_db) return std::nullopt;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT content FROM kv_store WHERE bot_id=? AND name=?";
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> guard(stmt, sqlite3_finalize);
        sqlite3_bind_int64(stmt, 1, bot_id);
        sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_ROW) return std::nullopt;
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        return text ? std::optional<std::string>(reinterpret_cast<const char*>(text)) : std::optional<std::string>("");
    }

    bool sqlite_write_text(BotId bot_id, const std::string& filename, const std::string& content) {
        init_sqlite();
        std::unique_lock lock(m_mutex);
        if (!m_db) return false;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO kv_store(bot_id,name,content,updated_at) VALUES(?,?,?,strftime('%s','now')) "
                          "ON CONFLICT(bot_id,name) DO UPDATE SET content=excluded.content, updated_at=excluded.updated_at";
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> guard(stmt, sqlite3_finalize);
        sqlite3_bind_int64(stmt, 1, bot_id);
        sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
        return sqlite3_step(stmt) == SQLITE_DONE;
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
