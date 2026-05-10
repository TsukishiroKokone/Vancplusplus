// SPDX-FileCopyrightText: 2026 TsukishiroKokone
// SPDX-License-Identifier: AGPL-3.0-or-later

// ─── VanBot Cute TUI Application ─────────────────────────────
// 多适配器状态显示版本
#include "app.hpp"
#include "vanbot/bot.hpp"
#include "vanbot/common.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace vanbot {

using namespace ftxui;
using namespace kawaii;

namespace {

std::string adapter_type_to_string(AdapterType type) {
    switch (type) {
        case AdapterType::OneBotForwardWS: return "OneBot v11 正向WS";
        case AdapterType::OneBotReverseWS: return "OneBot v11 反向WS";
        case AdapterType::OneBotV12ForwardWS: return "OneBot v12 正向WS";
        case AdapterType::OneBotV12ReverseWS: return "OneBot v12 反向WS";
        case AdapterType::Milky: return "Milky";
        default: return "Unknown";
    }
}

AdapterType next_adapter_type(AdapterType type) {
    switch (type) {
        case AdapterType::OneBotForwardWS: return AdapterType::OneBotReverseWS;
        case AdapterType::OneBotReverseWS: return AdapterType::OneBotV12ForwardWS;
        case AdapterType::OneBotV12ForwardWS: return AdapterType::OneBotV12ReverseWS;
        case AdapterType::OneBotV12ReverseWS: return AdapterType::Milky;
        case AdapterType::Milky: return AdapterType::OneBotForwardWS;
        default: return AdapterType::OneBotForwardWS;
    }
}

AdapterType prev_adapter_type(AdapterType type) {
    switch (type) {
        case AdapterType::OneBotForwardWS: return AdapterType::Milky;
        case AdapterType::OneBotReverseWS: return AdapterType::OneBotForwardWS;
        case AdapterType::OneBotV12ForwardWS: return AdapterType::OneBotReverseWS;
        case AdapterType::OneBotV12ReverseWS: return AdapterType::OneBotV12ForwardWS;
        case AdapterType::Milky: return AdapterType::OneBotV12ReverseWS;
        default: return AdapterType::OneBotForwardWS;
    }
}

bool is_reverse(AdapterType type) {
    return type == AdapterType::OneBotReverseWS || type == AdapterType::OneBotV12ReverseWS;
}

int parse_int_or(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

struct AdapterDraft {
    std::string name;
    AdapterType type = AdapterType::OneBotForwardWS;
    std::string url;
    std::string port;
    std::string access_token;
    std::string reconnect_interval;
    std::string heartbeat_interval;
};

AdapterDraft to_draft(const AdapterConfig& adapter) {
    return AdapterDraft{
        adapter.name,
        adapter.type,
        adapter.url,
        std::to_string(adapter.port),
        adapter.access_token,
        std::to_string(adapter.reconnect_interval),
        std::to_string(adapter.heartbeat_interval),
    };
}

AdapterConfig from_draft(const AdapterDraft& draft) {
    AdapterConfig adapter;
    adapter.name = draft.name.empty() ? "adapter" : draft.name;
    adapter.type = draft.type;
    adapter.url = draft.url.empty() ? "ws://127.0.0.1:6700" : draft.url;
    adapter.port = parse_int_or(draft.port, 6701);
    adapter.access_token = draft.access_token;
    adapter.reconnect_interval = parse_int_or(draft.reconnect_interval, 5);
    adapter.heartbeat_interval = parse_int_or(draft.heartbeat_interval, 30);
    return adapter;
}

Element labeled_input(const std::string& label, Component input) {
    return hbox({
        text("  " + label) | color(DimGray) | size(WIDTH, EQUAL, 14),
        input->Render() | flex,
    });
}

} // namespace

// ── 启动前配置 TUI ────────────────────────────────────────────
int run_config_tui(Config& config) {
    auto screen = ScreenInteractive::Fullscreen();

    std::string data_dir = config.data_dir;
    bool self_trigger = config.self_trigger;
    bool sqlite_enabled = config.storage_backend == StorageBackend::SQLite;
    bool web_api_enabled = config.web_api_enabled;
    std::string sqlite_path = config.sqlite_path;
    std::string web_api_host = config.web_api_host;
    std::string web_api_port = std::to_string(config.web_api_port);
    std::string web_api_token = config.web_api_token;
    std::vector<AdapterDraft> drafts;
    drafts.reserve(config.adapters.size());
    for (const auto& adapter : config.adapters) drafts.push_back(to_draft(adapter));
    if (drafts.empty()) {
        drafts.push_back(to_draft(AdapterConfig{}));
        drafts.front().name = "default";
    }

    int selected = 0;
    bool accepted = false;
    std::string notice = "使用方向键/Tab 切换，Enter 保存启动，a 添加，d 删除，t 切换协议，s 切换自触发，q 退出";

    auto data_input = Input(&data_dir, "./Van_keyword");
    auto sqlite_input = Input(&sqlite_path, "./Van_keyword/van_lexicon.db");
    auto api_host_input = Input(&web_api_host, "127.0.0.1");
    auto api_port_input = Input(&web_api_port, "8080");
    auto api_token_input = Input(&web_api_token, "可留空");
    std::vector<Component> adapter_inputs;

    auto rebuild_inputs = [&] {
        adapter_inputs.clear();
        if (drafts.empty()) drafts.push_back(to_draft(AdapterConfig{}));
        selected = std::clamp(selected, 0, static_cast<int>(drafts.size()) - 1);
        auto& d = drafts[selected];
        adapter_inputs.push_back(Input(&d.name, "default"));
        adapter_inputs.push_back(Input(&d.url, "ws://127.0.0.1:6700"));
        adapter_inputs.push_back(Input(&d.port, "6701"));
        adapter_inputs.push_back(Input(&d.access_token, "可留空"));
        adapter_inputs.push_back(Input(&d.reconnect_interval, "5"));
        adapter_inputs.push_back(Input(&d.heartbeat_interval, "30"));
    };
    rebuild_inputs();

    auto inputs_container = Container::Vertical(adapter_inputs);
    auto root = Container::Vertical({data_input, sqlite_input, api_host_input, api_port_input, api_token_input, inputs_container});

    auto renderer = Renderer(root, [&] {
        auto& d = drafts[selected];
        Elements adapter_list;
        for (size_t i = 0; i < drafts.size(); ++i) {
            const auto& item = drafts[i];
            auto marker = (static_cast<int>(i) == selected) ? "❯ " : "  ";
            adapter_list.push_back(
                hbox({
                    text(marker) | color(static_cast<int>(i) == selected ? HotPink : DimGray),
                    text(item.name.empty() ? "adapter" : item.name) | bold | color(static_cast<int>(i) == selected ? Peach : SkyBlue),
                    text(" · ") | color(DimGray),
                    text(adapter_type_to_string(item.type)) | color(Mint),
                    text(is_reverse(item.type) ? (" · port=" + item.port) : (" · url=" + item.url)) | color(DimGray),
                })
            );
        }

        auto endpoint_label = is_reverse(d.type) ? "监听端口" : "WS 地址";
        auto endpoint_input = is_reverse(d.type) ? adapter_inputs[2] : adapter_inputs[1];

        return vbox({
            kawaii_header(),
            kawaii_separator(),
            window(
                text(" ✧ startup config · 启动配置 ") | color(HotPink) | bold,
                vbox({
                    hbox({ text("  数据目录") | color(DimGray) | size(WIDTH, EQUAL, 14), data_input->Render() | flex }),
                    hbox({ text("  自触发") | color(DimGray) | size(WIDTH, EQUAL, 14), text(self_trigger ? "开启" : "关闭") | color(self_trigger ? Mint : Color::Red) | bold }),
                    hbox({ text("  SQLite") | color(DimGray) | size(WIDTH, EQUAL, 14), text(sqlite_enabled ? "开启" : "关闭") | color(sqlite_enabled ? Mint : Color::Red) | bold }),
                    hbox({ text("  数据库") | color(DimGray) | size(WIDTH, EQUAL, 14), sqlite_input->Render() | flex }),
                    hbox({ text("  Web API") | color(DimGray) | size(WIDTH, EQUAL, 14), text(web_api_enabled ? "开启" : "关闭") | color(web_api_enabled ? Mint : Color::Red) | bold }),
                    hbox({ text("  API Host") | color(DimGray) | size(WIDTH, EQUAL, 14), api_host_input->Render() | flex }),
                    hbox({ text("  API Port") | color(DimGray) | size(WIDTH, EQUAL, 14), api_port_input->Render() | flex }),
                    hbox({ text("  API Token") | color(DimGray) | size(WIDTH, EQUAL, 14), api_token_input->Render() | flex }),
                    separator(),
                    text("  适配器列表") | color(Peach) | bold,
                    vbox(adapter_list) | flex_shrink,
                    separator(),
                    text("  当前适配器详情") | color(Peach) | bold,
                    hbox({ text("  名称") | color(DimGray) | size(WIDTH, EQUAL, 14), adapter_inputs[0]->Render() | flex }),
                    hbox({ text("  协议") | color(DimGray) | size(WIDTH, EQUAL, 14), text(adapter_type_to_string(d.type)) | color(SkyBlue) | bold }),
                    hbox({ text("  " + std::string(endpoint_label)) | color(DimGray) | size(WIDTH, EQUAL, 14), endpoint_input->Render() | flex }),
                    hbox({ text("  Token") | color(DimGray) | size(WIDTH, EQUAL, 14), adapter_inputs[3]->Render() | flex }),
                    hbox({ text("  重连秒数") | color(DimGray) | size(WIDTH, EQUAL, 14), adapter_inputs[4]->Render() | flex }),
                    hbox({ text("  心跳秒数") | color(DimGray) | size(WIDTH, EQUAL, 14), adapter_inputs[5]->Render() | flex }),
                })
            ) | bgcolor((Color::RGB)(250, 250, 255)) | flex,
            kawaii_bottom_separator(),
            hbox({
                text(" Enter 保存启动 ") | color(Mint) | bold,
                separator(),
                text(" ↑↓适配器  a添加  d删除  t协议  s自触发  xSQLite  w WebAPI  q退出 ") | color(DimGray),
                filler(),
                text(" " + notice + " ") | color(Peach),
            }),
        });
    });

    auto component = CatchEvent(renderer, [&](ftxui::Event event) {
        if (event == ftxui::Event::Return) {
            accepted = true;
            screen.Exit();
            return true;
        }
        if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape) {
            accepted = false;
            screen.Exit();
            return true;
        }
        if (event == ftxui::Event::Character('s')) {
            self_trigger = !self_trigger;
            return true;
        }
        if (event == ftxui::Event::Character('x')) {
            sqlite_enabled = !sqlite_enabled;
            return true;
        }
        if (event == ftxui::Event::Character('w')) {
            web_api_enabled = !web_api_enabled;
            return true;
        }
        if (event == ftxui::Event::Character('a')) {
            AdapterDraft d = to_draft(AdapterConfig{});
            d.name = "adapter" + std::to_string(drafts.size() + 1);
            drafts.push_back(d);
            selected = static_cast<int>(drafts.size()) - 1;
            rebuild_inputs();
            inputs_container->DetachAllChildren();
            for (auto& input : adapter_inputs) inputs_container->Add(input);
            return true;
        }
        if (event == ftxui::Event::Character('d')) {
            if (drafts.size() > 1) {
                drafts.erase(drafts.begin() + selected);
                selected = (std::min)(selected, static_cast<int>(drafts.size()) - 1);
                rebuild_inputs();
                inputs_container->DetachAllChildren();
                for (auto& input : adapter_inputs) inputs_container->Add(input);
            } else {
                notice = "至少保留一个适配器喵";
            }
            return true;
        }
        if (event == ftxui::Event::Character('t')) {
            drafts[selected].type = next_adapter_type(drafts[selected].type);
            return true;
        }
        if (event == ftxui::Event::ArrowUp) {
            if (selected > 0) {
                --selected;
                rebuild_inputs();
                inputs_container->DetachAllChildren();
                for (auto& input : adapter_inputs) inputs_container->Add(input);
            }
            return true;
        }
        if (event == ftxui::Event::ArrowDown) {
            if (selected + 1 < static_cast<int>(drafts.size())) {
                ++selected;
                rebuild_inputs();
                inputs_container->DetachAllChildren();
                for (auto& input : adapter_inputs) inputs_container->Add(input);
            }
            return true;
        }
        if (event == ftxui::Event::ArrowLeft) {
            drafts[selected].type = prev_adapter_type(drafts[selected].type);
            return true;
        }
        if (event == ftxui::Event::ArrowRight) {
            drafts[selected].type = next_adapter_type(drafts[selected].type);
            return true;
        }
        return false;
    });

    screen.Loop(component);
    if (!accepted) return 1;

    config.data_dir = data_dir.empty() ? "./Van_keyword" : data_dir;
    config.self_trigger = self_trigger;
    config.storage_backend = sqlite_enabled ? StorageBackend::SQLite : StorageBackend::File;
    config.sqlite_path = sqlite_path.empty() ? "./Van_keyword/van_lexicon.db" : sqlite_path;
    config.web_api_enabled = web_api_enabled;
    config.web_api_host = web_api_host.empty() ? "127.0.0.1" : web_api_host;
    config.web_api_port = parse_int_or(web_api_port, 8080);
    config.web_api_token = web_api_token;
    config.adapters.clear();
    for (const auto& draft : drafts) config.adapters.push_back(from_draft(draft));
    return 0;
}

// ── 主 TUI 循环 ──────────────────────────────────────────────
int run_tui(Bot& bot) {
    auto screen = ScreenInteractive::Fullscreen();

    // 滚动偏移
    int scroll_y = 0;

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
                text(" ✦ Van Lexicon ") | bold | color(HotPink),
                separator(),
                status_indicator(any_connected),
                separator(),
                text(" 适配器: " + std::to_string(adapter_status.size())) | color(SkyBlue),
                separator(),
                time_display(),
                filler(),
                text(" claude-code style · made with ♡ ") | color(Pink),
            }),
        });

        return document;
    });

    // 捕获键盘事件
    auto component = CatchEvent(renderer, [&](ftxui::Event event) {
        if (event == ftxui::Event::Character('q')) {
            screen.Exit();
            return true;
        }
        if (event == ftxui::Event::ArrowUp) {
            scroll_y++;
            return true;
        }
        if (event == ftxui::Event::ArrowDown) {
            scroll_y = (std::max)(0, scroll_y - 1);
            return true;
        }
        if (event == ftxui::Event::Character('c')) {
            bot.add_log("🗑️ 日志已清除");
            std::lock_guard lock(bot.stats().log_mutex);
            bot.stats().recent_logs.clear();
            return true;
        }
        if (event == ftxui::Event::Character('r')) {
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
            screen.PostEvent(ftxui::Event::Custom);
        }
    });

    screen.Loop(component);

    refresh_running = false;
    refresh_thread.join();

    return 0;
}

} // namespace vanbot
