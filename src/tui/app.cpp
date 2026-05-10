// ─── VanBot Cute TUI Application ─────────────────────────────
// 多适配器状态显示版本
#include "app.hpp"
#include "vanbot/bot.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>
#include <iostream>

namespace vanbot {

using namespace ftxui;
using namespace kawaii;

// ── 主 TUI 循环 ──────────────────────────────────────────────
int run_tui(Bot& bot) {
    auto screen = ScreenInteractive::Fullscreen();

    // 滚动偏移
    int scroll_y = 0;
    bool running = true;

    // 渲染主界面
    auto renderer = Renderer([&] {
        auto& stats = bot.stats();

        // 获取所有适配器状态
        auto adapter_status = bot.adapter_mgr().get_all_status();

        // 判断全局在线状态（任意一个已连接即为在线）
        bool any_connected = false;
        for (auto& [name, connected] : adapter_status) {
            if (connected) { any_connected = true; break; }
        }

        // 主布局
        auto document = vbox({
            // ─── 顶部标题栏 ───────────────────────────────
            kawaii_header(),
            separator(),
            kawaii_separator(),

            // ─── 主体内容 ─────────────────────────────────
            hbox({
                // 左侧面板
                vbox({
                    stats_panel(
                        stats.recv_count.load(),
                        stats.send_count.load(),
                        stats.lexicon_count.load(),
                        stats.lexicon_id.load()
                    ) | flex_shrink,

                    adapters_panel(adapter_status) | flex_shrink,

                    recent_panel(
                        stats.last_message,
                        stats.last_response
                    ) | flex_shrink,

                    help_panel() | flex_shrink,
                }) | flex_shrink | size(WIDTH, LESS_THAN, 46),

                separator(),

                // 右侧日志
                log_panel(stats.recent_logs, scroll_y) | flex,
            }) | flex,

            kawaii_bottom_separator(),

            // ─── 底部状态栏 ───────────────────────────────
            hbox({
                text(" 🌸 VanBot ") | bold | color(HotPink),
                separator(),
                status_indicator(any_connected),
                separator(),
                text(" 适配器: " + std::to_string(adapter_status.size())) | color(SkyBlue),
                separator(),
                time_display(),
                filler(),
                text(" Made with 💖 ") | color(Pink),
            }),
        });

        return document;
    });

    // 捕获键盘事件
    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }
        if (event == Event::ArrowUp) {
            scroll_y++;
            return true;
        }
        if (event == Event::ArrowDown) {
            scroll_y = std::max(0, scroll_y - 1);
            return true;
        }
        if (event == Event::Character('c')) {
            bot.add_log("🗑️ 日志已清除");
            std::lock_guard lock(bot.stats().log_mutex);
            bot.stats().recent_logs.clear();
            return true;
        }
        if (event == Event::Character('r')) {
            bot.add_log("🔄 正在重连所有适配器...");
            bot.adapter_mgr().start_all();
            return true;
        }
        return false;
    });

    // 主循环（每 500ms 刷新一次）
    std::atomic<bool> refresh_running{true};
    std::thread refresh_thread([&] {
        while (refresh_running) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(500ms);
            screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(component);

    refresh_running = false;
    refresh_thread.join();

    return 0;
}

} // namespace vanbot
