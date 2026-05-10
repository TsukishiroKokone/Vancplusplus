// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
// ─── VanBot HTTP Client ─────────────────────────────────────
// 异步 HTTP 请求 + 响应缓存
#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <functional>
#include <memory>

namespace httplib { class Client; }

namespace vanbot {

class HttpClient {
public:
    explicit HttpClient(int timeout = 60, size_t max_cache = 256)
        : m_timeout(timeout), m_max_cache(max_cache) {}

    ~HttpClient() = default;

    // ── 同步 GET 请求（带缓存） ──────────────────────────────
    std::string get(const std::string& url) {
        // 检查缓存
        {
            std::shared_lock lock(m_cache_mutex);
            auto it = m_cache.find(url);
            if (it != m_cache.end()) {
                // 检查是否过期（60秒）
                auto now = std::chrono::steady_clock::now();
                if (now - it->second.timestamp < std::chrono::seconds(60)) {
                    return it->second.data;
                }
            }
        }

        // 执行请求
        std::string result = do_get(url);

        // 更新缓存
        if (!result.empty()) {
            std::unique_lock lock(m_cache_mutex);
            if (m_cache.size() >= m_max_cache) {
                // LRU：简单清除最旧的
                auto oldest = m_cache.end();
                auto oldest_time = std::chrono::steady_clock::time_point::max();
                for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
                    if (it->second.timestamp < oldest_time) {
                        oldest_time = it->second.timestamp;
                        oldest = it;
                    }
                }
                if (oldest != m_cache.end()) m_cache.erase(oldest);
            }
            m_cache[url] = {result, std::chrono::steady_clock::now()};
        }

        return result;
    }

    // ── 清除缓存 ─────────────────────────────────────────────
    void clear_cache() {
        std::unique_lock lock(m_cache_mutex);
        m_cache.clear();
    }

    // ── 清除单个缓存 ─────────────────────────────────────────
    void invalidate(const std::string& url) {
        std::unique_lock lock(m_cache_mutex);
        m_cache.erase(url);
    }

    // ── 设置超时 ─────────────────────────────────────────────
    void set_timeout(int seconds) { m_timeout = seconds; }

private:
    struct CacheEntry {
        std::string data;
        std::chrono::steady_clock::time_point timestamp;
    };

    int m_timeout;
    size_t m_max_cache;
    std::unordered_map<std::string, CacheEntry> m_cache;
    mutable std::shared_mutex m_cache_mutex;

    // ── 低层 GET 实现 ────────────────────────────────────────
    std::string do_get(const std::string& url) const;
};

} // namespace vanbot
