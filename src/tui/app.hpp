#pragma once
// ─── VanBot Cute TUI ────────────────────────────────────────
// 可爱系终端界面：樱花粉 + 薄荷绿 配色
// 支持多适配器连接状态显示
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>
#include <algorithm>

namespace vanbot {

class Bot;
int run_tui(Bot& bot);

// ── 可爱配色方案 ─────────────────────────────────────────────
namespace kawaii {
    using namespace ftxui;

    // 颜色定义
    const Color Pink      = Color::RGB(255, 182, 193);   // 樱花粉
    const Color HotPink   = Color::RGB(255, 105, 180);   // 热粉红
    const Color Mint      = Color::RGB(152, 251, 152);   // 薄荷绿
    const Color Lavender  = Color::RGB(230, 230, 250);   // 薰衣草
    const Color Peach     = Color::RGB(255, 218, 185);   // 蜜桃色
    const Color SkyBlue   = Color::RGB(135, 206, 235);   // 天空蓝
    const Color SoftWhite = Color::RGB(248, 250, 252);   // 柔和白
    const Color DimGray   = Color::RGB(100, 116, 139);   // 暗灰
    const Color RoseBg    = Color::RGB(255, 240, 245);   // 玫瑰背景
    const Color MintBg    = Color::RGB(240, 255, 240);   // 薄荷背景
    const Color GoldBg    = Color::RGB(255, 250, 240);   // 金色背景

    // ── 可爱装饰元素 ─────────────────────────────────────────
    inline Element kawaii_header() {
        return hbox({
            text(" ✦ ") | color(Pink) | bold,
            text("Van Lexicon") | color(SoftWhite) | bold,
            text("  kawaii code console") | color(Lavender),
            filler(),
            text("♡ onebot v11/v12 · milky ♡ ") | color(Peach),
        }) | bgcolor(Color::RGB(24, 24, 37));
    }

    inline Element kawaii_separator() {
        return hbox({
            text("╭"),
            filler(),
            text("─✿─"),
            filler(),
            text("╮"),
        }) | color(Pink);
    }

    inline Element kawaii_bottom_separator() {
        return hbox({
            text("╰"),
            filler(),
            text("─✿─"),
            filler(),
            text("╯"),
        }) | color(Pink);
    }

    // ── 状态指示器 ──────────────────────────────────────────
    inline Element status_indicator(bool online) {
        if (online) {
            return hbox({ text(" ● ") | color(Mint), text("在线") | color(Mint) | bold });
        }
        return hbox({ text(" ● ") | color(Color::Red), text("离线") | color(Color::Red) | bold });
    }

    // ── 统计面板 ────────────────────────────────────────────
    inline Element stats_panel(uint64_t recv, uint64_t send,
                               uint64_t lexicon, int64_t lexicon_id) {
        return window(
            text(" 󰙨 runtime ") | color(HotPink) | bold,
            vbox({
                hbox({ text("  📩 收消息: ") | color(DimGray),
                       text(std::to_string(recv)) | color(Mint) | bold }),
                hbox({ text("  📤 发消息: ") | color(DimGray),
                       text(std::to_string(send)) | color(Mint) | bold }),
                hbox({ text("  📖 词汇量: ") | color(DimGray),
                       text(std::to_string(lexicon)) | color(SkyBlue) | bold }),
                hbox({ text("  🔖 词条ID: ") | color(DimGray),
                       text(std::to_string(lexicon_id)) | color(SkyBlue) | bold }),
            })
        ) | bgcolor(RoseBg);
    }

    // ── 多适配器连接面板 ────────────────────────────────────
    inline Element adapters_panel(
        const std::vector<std::pair<std::string, bool>>& adapter_status) {
        Elements items;
        if (adapter_status.empty()) {
            items.push_back(text("  💤 未配置适配器") | color(DimGray) | dim);
        } else {
            for (auto& [name, connected] : adapter_status) {
                items.push_back(hbox({
                    text("  ") ,
                    status_indicator(connected),
                    text(" " + name) | color(connected ? Peach : DimGray),
                }));
            }
        }
        return window(
            text(" 󰒋 adapters ") | color(SkyBlue) | bold,
            vbox(items)
        ) | bgcolor(MintBg);
    }

    // ── 日志面板 ────────────────────────────────────────────
    inline Element log_panel(const std::vector<std::string>& logs, int scroll_y) {
        Elements log_lines;
        int start = (std::max)(0, static_cast<int>(logs.size()) - 20 + scroll_y);
        int end = (std::min)(static_cast<int>(logs.size()), start + 20);

        for (int i = start; i < end; i++) {
            const auto& line = logs[i];
            Color c = DimGray;
            if (line.find("📤") != std::string::npos) c = Mint;
            else if (line.find("📨") != std::string::npos) c = SkyBlue;
            else if (line.find("💔") != std::string::npos) c = Color::Red;
            else if (line.find("🌟") != std::string::npos) c = HotPink;
            else if (line.find("💚") != std::string::npos) c = Mint;
            else if (line.find("💛") != std::string::npos) c = Color::Yellow;
            else if (line.find("🔑") != std::string::npos) c = Peach;

            log_lines.push_back(text("  " + line) | color(c));
        }

        if (log_lines.empty()) {
            log_lines.push_back(text("  💤 暂无消息...") | color(DimGray) | dim);
        }

        return window(
            text(" ✧ event stream ") | color(Mint) | bold,
            vbox(log_lines) | flex
        ) | bgcolor(Color::RGB(250, 250, 255));
    }

    // ── 最近消息面板 ────────────────────────────────────────
    inline Element recent_panel(const std::string& last_msg, const std::string& last_resp) {
        return window(
            text(" 󰦨 last exchange ") | color(Peach) | bold,
            vbox({
                hbox({ text("  收: ") | color(DimGray) | bold,
                       text(last_msg.substr(0, 60)) | color(SkyBlue) }),
                hbox({ text("  发: ") | color(DimGray) | bold,
                       text(last_resp.substr(0, 60)) | color(Mint) }),
            })
        ) | bgcolor(RoseBg);
    }

    // ── 帮助面板 ────────────────────────────────────────────
    inline Element help_panel() {
        return window(
            text(" ? commands ") | color(Lavender) | bold,
            vbox({
                text("  [q] 退出  [c] 清除日志") | color(DimGray),
                text("  [↑↓] 滚动日志  [r] 重连") | color(DimGray),
            })
        ) | bgcolor(MintBg);
    }

    // ── 时间显示 ────────────────────────────────────────────
    inline Element time_display() {
        auto now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        return hbox({
            text(" 🕐 ") | color(Pink),
            text(buf) | color(Peach) | bold,
        });
    }

} // namespace kawaii

} // namespace vanbot
