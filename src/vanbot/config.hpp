// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
// ─── VanBot INI Configuration ─────────────────────────────────
// 轻量 INI 配置读写：无需额外依赖，支持多适配器配置
#include "common.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace vanbot {

class IniConfig {
public:
    static bool load(const std::string& path, Config& config) {
        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) return false;

        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> ini;
        std::string section = "main";
        std::string line;
        while (std::getline(ifs, line)) {
            line = trim(strip_comment(line));
            if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']') {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            auto key = trim(line.substr(0, pos));
            auto value = trim(line.substr(pos + 1));
            if (!key.empty()) ini[section][key] = value;
        }

        auto get = [&](const std::string& sec, const std::string& key, const std::string& fallback) {
            auto sit = ini.find(sec);
            if (sit == ini.end()) return fallback;
            auto kit = sit->second.find(key);
            return kit == sit->second.end() ? fallback : kit->second;
        };

        config.data_dir = get("main", "data_dir", config.data_dir);
        config.self_trigger = to_bool(get("main", "self_trigger", config.self_trigger ? "true" : "false"));
        config.config_tui = to_bool(get("main", "config_tui", config.config_tui ? "true" : "false"));
        config.storage_backend = parse_storage_backend(get("main", "storage_backend", storage_backend_to_string(config.storage_backend)));
        config.sqlite_path = get("main", "sqlite_path", config.sqlite_path);

        config.web_api_enabled = to_bool(get("web_api", "enabled", config.web_api_enabled ? "true" : "false"));
        config.web_api_host = get("web_api", "host", config.web_api_host);
        config.web_api_port = to_int(get("web_api", "port", std::to_string(config.web_api_port)), config.web_api_port);
        config.web_api_token = get("web_api", "token", config.web_api_token);

        std::vector<AdapterConfig> adapters;
        for (const auto& [sec, kv] : ini) {
            if (sec.rfind("adapter.", 0) != 0) continue;
            AdapterConfig adapter;
            adapter.name = get(sec, "name", sec.substr(8));
            adapter.type = parse_adapter_type(get(sec, "type", "onebot-v11-forward"));
            adapter.url = get(sec, "url", adapter.url);
            adapter.port = to_int(get(sec, "port", std::to_string(adapter.port)), adapter.port);
            adapter.access_token = get(sec, "access_token", adapter.access_token);
            adapter.reconnect_interval = to_int(get(sec, "reconnect_interval", std::to_string(adapter.reconnect_interval)), adapter.reconnect_interval);
            adapter.heartbeat_interval = to_int(get(sec, "heartbeat_interval", std::to_string(adapter.heartbeat_interval)), adapter.heartbeat_interval);
            adapters.push_back(adapter);
        }
        std::sort(adapters.begin(), adapters.end(), [](const AdapterConfig& a, const AdapterConfig& b) { return a.name < b.name; });
        if (!adapters.empty()) config.adapters = adapters;
        return true;
    }

    static bool save(const std::string& path, const Config& config) {
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;

        ofs << "# Van Lexicon 配置文件 / INI Configuration\n";
        ofs << "[main]\n";
        ofs << "data_dir=" << config.data_dir << "\n";
        ofs << "self_trigger=" << (config.self_trigger ? "true" : "false") << "\n";
        ofs << "config_tui=" << (config.config_tui ? "true" : "false") << "\n";
        ofs << "storage_backend=" << storage_backend_to_string(config.storage_backend) << "\n";
        ofs << "sqlite_path=" << config.sqlite_path << "\n\n";

        ofs << "[web_api]\n";
        ofs << "enabled=" << (config.web_api_enabled ? "true" : "false") << "\n";
        ofs << "host=" << config.web_api_host << "\n";
        ofs << "port=" << config.web_api_port << "\n";
        ofs << "token=" << config.web_api_token << "\n\n";

        for (size_t i = 0; i < config.adapters.size(); ++i) {
            const auto& adapter = config.adapters[i];
            ofs << "[adapter." << (adapter.name.empty() ? ("adapter" + std::to_string(i + 1)) : adapter.name) << "]\n";
            ofs << "name=" << adapter.name << "\n";
            ofs << "type=" << adapter_type_to_string(adapter.type) << "\n";
            ofs << "url=" << adapter.url << "\n";
            ofs << "port=" << adapter.port << "\n";
            ofs << "access_token=" << adapter.access_token << "\n";
            ofs << "reconnect_interval=" << adapter.reconnect_interval << "\n";
            ofs << "heartbeat_interval=" << adapter.heartbeat_interval << "\n\n";
        }
        return true;
    }

    static AdapterType parse_adapter_type(const std::string& input) {
        std::string s = lower(trim(input));
        if (s == "reverse" || s == "rev" || s == "onebot-v11-reverse" || s == "ob11-reverse") return AdapterType::OneBotReverseWS;
        if (s == "v12-forward" || s == "onebot-v12-forward" || s == "ob12-forward") return AdapterType::OneBotV12ForwardWS;
        if (s == "v12-reverse" || s == "onebot-v12-reverse" || s == "ob12-reverse") return AdapterType::OneBotV12ReverseWS;
        if (s == "milky") return AdapterType::Milky;
        return AdapterType::OneBotForwardWS;
    }

    static std::string adapter_type_to_string(AdapterType type) {
        switch (type) {
            case AdapterType::OneBotForwardWS: return "onebot-v11-forward";
            case AdapterType::OneBotReverseWS: return "onebot-v11-reverse";
            case AdapterType::OneBotV12ForwardWS: return "onebot-v12-forward";
            case AdapterType::OneBotV12ReverseWS: return "onebot-v12-reverse";
            case AdapterType::Milky: return "milky";
            default: return "onebot-v11-forward";
        }
    }

    static std::string storage_backend_to_string(StorageBackend backend) {
        return backend == StorageBackend::SQLite ? "sqlite" : "file";
    }

    static StorageBackend parse_storage_backend(const std::string& input) {
        auto s = lower(trim(input));
        if (s == "sqlite" || s == "sqlite3" || s == "database" || s == "db") return StorageBackend::SQLite;
        return StorageBackend::File;
    }

private:
    static std::string strip_comment(const std::string& line) {
        bool quoted = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"') quoted = !quoted;
            if (!quoted && (line[i] == '#' || line[i] == ';')) return line.substr(0, i);
        }
        return line;
    }

    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static bool to_bool(const std::string& value) {
        auto s = lower(trim(value));
        return s == "1" || s == "true" || s == "yes" || s == "on" || s == "enable" || s == "enabled";
    }

    static int to_int(const std::string& value, int fallback) {
        try { return std::stoi(trim(value)); } catch (...) { return fallback; }
    }
};

} // namespace vanbot
