// ─── VanBot v3.0.0 - Main Entry ─────────────────────────────
// 高性能 C++ QQ 关键词词库机器人
// 多适配器架构：OneBot v11/v12 正向/反向WS + Milky协议
// 本版本作者: TsukishiroKokone | https://github.com/TsukishiroKokone
// MIT License

#include "vanbot/bot.hpp"
#include "tui/app.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <string>
#include <memory>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    g_running = false;
}

// ── 显示启动横幅 ─────────────────────────────────────────────
static void show_banner() {
    std::cout << R"(
  ╔═══════════════════════════════════════════════════╗
  ║                                                   ║
  ║     🌸  Van Lexicon v3.0.0  🌸                   ║
  ║     高性能 C++ QQ 关键词词库机器人               ║
  ║     多适配器 · 嵌套变量 · 可爱系TUI              ║
  ║                                                   ║
  ║     本版本作者: TsukishiroKokone                 ║
  ║     GitHub: https://github.com/TsukishiroKokone  ║
  ║     原词库: https://github.com/Van-Zone/VanBot/  ║
  ║                                                   ║
  ╚═══════════════════════════════════════════════════╝
)" << std::endl;
}

// ── 解析适配器类型字符串 ──────────────────────────────────────
static vanbot::AdapterType parse_adapter_type(const std::string& s) {
    if (s == "forward" || s == "fwd" || s == "ob11-forward" || s == "onebot11-forward" || s == "1") return vanbot::AdapterType::OneBotForwardWS;
    if (s == "reverse" || s == "rev" || s == "ob11-reverse" || s == "onebot11-reverse" || s == "2") return vanbot::AdapterType::OneBotReverseWS;
    if (s == "v12-forward" || s == "ob12-forward" || s == "onebot12-forward" || s == "4") return vanbot::AdapterType::OneBotV12ForwardWS;
    if (s == "v12-reverse" || s == "ob12-reverse" || s == "onebot12-reverse" || s == "5") return vanbot::AdapterType::OneBotV12ReverseWS;
    if (s == "milky" || s == "3") return vanbot::AdapterType::Milky;
    return vanbot::AdapterType::OneBotForwardWS;
}

// ── 解析命令行参数 ───────────────────────────────────────────
static vanbot::Config parse_args(int argc, char* argv[]) {
    vanbot::Config config;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--ws" || arg == "-w") && i + 1 < argc) {
            // 兼容旧参数：添加默认正向WS适配器
            vanbot::AdapterConfig adapter;
            adapter.name = "default";
            adapter.type = vanbot::AdapterType::OneBotForwardWS;
            adapter.url  = argv[++i];
            config.adapters.push_back(adapter);
        } else if ((arg == "--data" || arg == "-d") && i + 1 < argc) {
            config.data_dir = argv[++i];
        } else if (arg == "--add-adapter" && i + 2 < argc) {
            // --add-adapter <name> <type> [url] [port] [token]
            vanbot::AdapterConfig adapter;
            adapter.name = argv[++i];
            adapter.type = parse_adapter_type(argv[++i]);

            // 可选参数
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                adapter.url = argv[++i];
            }
            if ((adapter.type == vanbot::AdapterType::OneBotReverseWS || adapter.type == vanbot::AdapterType::OneBotV12ReverseWS) && i + 1 < argc && argv[i + 1][0] != '-') {
                adapter.port = std::stoi(argv[++i]);
            }
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                adapter.access_token = argv[++i];
            }

            config.adapters.push_back(adapter);
        } else if (arg == "--v12-ws" && i + 1 < argc) {
            vanbot::AdapterConfig adapter;
            adapter.name = "onebot-v12";
            adapter.type = vanbot::AdapterType::OneBotV12ForwardWS;
            adapter.url = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                adapter.access_token = argv[++i];
            }
            config.adapters.push_back(adapter);
        } else if (arg == "--v12-reverse-ws" && i + 1 < argc) {
            vanbot::AdapterConfig adapter;
            adapter.name = "onebot-v12-reverse";
            adapter.type = vanbot::AdapterType::OneBotV12ReverseWS;
            adapter.port = std::stoi(argv[++i]);
            config.adapters.push_back(adapter);
        } else if (arg == "--reverse-ws" && i + 1 < argc) {
            // 快捷：添加反向WS适配器
            vanbot::AdapterConfig adapter;
            adapter.name = "reverse";
            adapter.type = vanbot::AdapterType::OneBotReverseWS;
            adapter.port = std::stoi(argv[++i]);
            config.adapters.push_back(adapter);
        } else if (arg == "--milky" && i + 1 < argc) {
            // 快捷：添加Milky适配器
            vanbot::AdapterConfig adapter;
            adapter.name = "milky";
            adapter.type = vanbot::AdapterType::Milky;
            adapter.url  = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                adapter.access_token = argv[++i];
            }
            config.adapters.push_back(adapter);
        } else if (arg == "--self-trigger") {
            config.self_trigger = true;
        } else if (arg == "--no-self-trigger") {
            config.self_trigger = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << R"(
VanBot v3.0.0 - 高性能 C++ QQ 关键词词库机器人
多适配器架构：OneBot v11/v12 正向/反向WS + Milky协议

用法: vanbot [选项]

选项:
  -w, --ws <url>              OneBot 正向WS地址 (兼容旧参数)
  -d, --data <dir>            数据存储目录 (默认: ./Van_keyword)
  --add-adapter <name> <type> [url] [port] [token]
                              添加适配器 (type: forward/rev/v12-forward/v12-reverse/milky)
  --reverse-ws <port>         快捷添加 OneBot v11 反向WS适配器
  --v12-ws <url> [token]      快捷添加 OneBot v12 正向WS适配器
  --v12-reverse-ws <port>     快捷添加 OneBot v12 反向WS适配器
  --milky <url> [token]       快捷添加Milky适配器
  --self-trigger               启用自触发 (默认)
  --no-self-trigger            禁用自触发
  -h, --help                   显示帮助信息

适配器类型:
  forward / fwd / 1           OneBot v11 正向WS (主动连接)
  reverse / rev / 2           OneBot v11 反向WS (被动监听)
  milky / 3                   Milky 协议
  v12-forward / ob12-forward  OneBot v12 正向WS
  v12-reverse / ob12-reverse  OneBot v12 反向WS

示例:
  vanbot --ws ws://localhost:6700
  vanbot --add-adapter bot1 forward ws://localhost:6700
  vanbot --add-adapter bot2 reverse 6701 mytoken
  vanbot --add-adapter bot3 milky ws://localhost:8765 milkypass
  vanbot --reverse-ws 6701 --milky ws://localhost:8765
  vanbot --v12-ws ws://localhost:6702
  vanbot --v12-reverse-ws 6703
  vanbot -w ws://localhost:6700 --reverse-ws 9800 --data ./my_data
)" << std::endl;
            exit(0);
        }
    }

    // 如果未配置任何适配器，使用默认正向WS
    if (config.adapters.empty()) {
        vanbot::AdapterConfig default_adapter;
        default_adapter.name = "default";
        default_adapter.type = vanbot::AdapterType::OneBotForwardWS;
        default_adapter.url  = "ws://127.0.0.1:6700";
        config.adapters.push_back(default_adapter);
    }

    return config;
}

int main(int argc, char* argv[]) {
    // 信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 初始化日志
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);

    show_banner();

    // 解析参数
    auto config = parse_args(argc, argv);

    spdlog::info("🔧 配置:");
    spdlog::info("  数据目录: {}", config.data_dir);
    spdlog::info("  自触发: {}", config.self_trigger ? "开启" : "关闭");
    spdlog::info("  适配器数量: {}", config.adapters.size());
    for (const auto& adapter : config.adapters) {
        std::string type_str;
        switch (adapter.type) {
            case vanbot::AdapterType::OneBotForwardWS: type_str = "OneBot-正向WS"; break;
            case vanbot::AdapterType::OneBotReverseWS: type_str = "OneBot-反向WS"; break;
            case vanbot::AdapterType::OneBotV12ForwardWS: type_str = "OneBot-v12-正向WS"; break;
            case vanbot::AdapterType::OneBotV12ReverseWS: type_str = "OneBot-v12-反向WS"; break;
            case vanbot::AdapterType::Milky: type_str = "Milky"; break;
        }
        if (adapter.type == vanbot::AdapterType::OneBotReverseWS || adapter.type == vanbot::AdapterType::OneBotV12ReverseWS) {
            spdlog::info("    [{}] {} port={}", adapter.name, type_str, adapter.port);
        } else {
            spdlog::info("    [{}] {} url={}", adapter.name, type_str, adapter.url);
        }
    }

    // 创建 Bot 实例
    vanbot::Bot bot(config);

    // 启动 Bot
    bot.start();

    // 启动 TUI
    spdlog::info("🎨 启动可爱系 TUI...");
    int ret = vanbot::run_tui(bot);

    // 停止 Bot
    bot.stop();

    return ret;
}
