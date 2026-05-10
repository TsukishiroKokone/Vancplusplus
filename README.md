# Van Lexicon [This Version is Unofficial !!!]
### We're Very Thank for Van-Zone Organization All Developer!
> 本项目使用 VanBot 词库，来源：[https://github.com/ZiYiQuQ/VanBot](https://github.com/ZiYiQuQ/VanBot)
>
> 本版本作者：TsukishiroKokone ([https://github.com/TsukishiroKokone](https://github.com/TsukishiroKokone))
>
> Tips " This Project Develop by AI {Vibe code} "

**We will not and are prohibited from writing features related to QQ Wallet and red envelope grabbing!!!**

---

### Technology stack:
- Primary development language ： **C Plus Plus [C++]** (C++20)
- Configure Storage ： **INI / JSON File Storage / SQLite Database**
- Terminal UI ： **FTXUI (Kawaii Theme 🌸)**
- Concurrency ： **Shared Mutex + Atomic + Thread Pool**
- Build System ： **CMake + vcpkg**

### Protocol End Compatibility
- [x] OneBot v11 (Forward WebSocket)
- [x] OneBot v11 (Reverse WebSocket)
- [x] OneBot v12 (Forward WebSocket)
- [x] OneBot v12 (Reverse WebSocket)
- [x] Milky

### Function List
- [x] Van Lexicon Basic Function -> https://github.com/Van-Zone/VanBot
- [x] Multi-Adapter Architecture (Multiple bots simultaneously)
- [x] Nested Variable System (Up to 52 levels of recursion)
- [x] 570+ Distinct Useful Variable Families ([VARIABLES.md](VARIABLES.md))
- [x] Five 110+ Distinct Variable Species Categories (Parameterized Tools / User Events / Environment / Time / Random)
- [x] ZeroBot-plugin Style Fun Plugins (Check-in / Fortune / Lottery / Tarot / Dice / Coin / Slot / Poke Reply)
- [x] Cooldown System
- [x] Virtual Coins System
- [x] HTTP GET Variable with LRU Cache
- [x] Delayed Segment Sending
- [x] Condition Judgment Variables
- [x] Regex Capture Groups `[n.?]`
- [x] Claude Code CLI-style Kawaii TUI (Sakura Pink + Mint Green)
- [x] Cross-Platform (Linux / Windows)
- [x] GitHub Actions CI/CD (GCC + Clang + MSVC)
- [x] SQLite Database Support
- [x] INI Configuration File
- [x] Web API
- [x] Built-in Fun Commands without Lexicon Rules

---

## 📦 Dependencies

| Library | Purpose |
|---|---|
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | Kawaii Terminal UI |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON Parsing |
| [spdlog](https://github.com/gabime/spdlog) | High-performance Logging |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | HTTP Client (with LRU Cache) |
| [IXWebSocket](https://github.com/machinezone/IXWebSocket) | WebSocket Client/Server |
| [SQLite](https://sqlite.org/) | Optional SQLite persistence backend |

All dependencies managed via [vcpkg](https://vcpkg.io/).

---

## 🔨 Build

### Prerequisites

- CMake 3.20+
- C++20 Compiler (GCC 12+ / Clang 15+ / MSVC 2022+)
- [vcpkg](https://vcpkg.io/)

### Steps

```bash
git clone https://github.com/Van-Zone/VanBot-Cpp.git
cd VanBot-Cpp

# Install dependencies
vcpkg install

# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg_path]/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release
```

---

## 🚀 Usage

### Basic

```bash
# Default: connect to ws://127.0.0.1:6700
./vanbot

# Specify WS address
./vanbot --ws ws://localhost:6700

# Specify data directory
./vanbot --data ./my_data
```

### Multi-Adapter

```bash
# Add multiple adapters
./vanbot \
  --add-adapter bot1 forward ws://localhost:6700 \
  --add-adapter bot2 reverse 9800 mytoken \
  --add-adapter bot3 v12-forward ws://localhost:6702 \
  --add-adapter bot4 milky ws://localhost:8765 milkypass

# Shortcuts
./vanbot \
  -w ws://localhost:6700 \
  --reverse-ws 9800 \
  --v12-ws ws://localhost:6702 \
  --v12-reverse-ws 9801 \
  --milky ws://localhost:8765 milkypass
```

### Config / Storage / Web API

```bash
# Read INI config file, default is config.ini
./vanbot --config config.ini

# Generate an INI file from current CLI options
./vanbot --data ./Van_keyword --sqlite ./Van_keyword/van_lexicon.db --save-config config.generated.ini

# Enable SQLite persistence
./vanbot --storage sqlite --sqlite ./Van_keyword/van_lexicon.db

# Enable Web API with optional token
./vanbot --web-api --web-api-host 127.0.0.1 --web-api-port 8080 --web-api-token your_token
```

Example INI:

```ini
[main]
data_dir=./Van_keyword
self_trigger=true
config_tui=true
storage_backend=sqlite
sqlite_path=./Van_keyword/van_lexicon.db

[web_api]
enabled=true
host=127.0.0.1
port=8080
token=change_me

[adapter.default]
name=default
type=onebot-v11-forward
url=ws://127.0.0.1:6700
port=6701
access_token=
reconnect_interval=5
heartbeat_interval=30
```

Web API routes:

| Route | Method | Description |
|---|---|---|
| `/api/status` | GET | Runtime stats and adapter statuses |
| `/api/config` | GET | Current config summary without token value |
| `/api/lexicon/<bot_id>/<data_id>` | GET | Read lexicon JSON |
| `/api/lexicon/<bot_id>/<data_id>` | POST | Add lexicon entry using JSON body |
| `/api/logs` | GET | Recent TUI logs |

### CLI Reference

| Parameter | Description |
|---|---|
| `-w, --ws <url>` | OneBot Forward WS address |
| `-d, --data <dir>` | Data directory (default: ./Van_keyword) |
| `--add-adapter <name> <type> [url] [port] [token]` | Add adapter |
| `--reverse-ws <port>` | Quick add Reverse WS adapter |
| `--v12-ws <url> [token]` | Quick add OneBot v12 Forward WS adapter |
| `--v12-reverse-ws <port>` | Quick add OneBot v12 Reverse WS adapter |
| `--milky <url> [token]` | Quick add Milky adapter |
| `--self-trigger` | Enable self-trigger (default) |
| `--no-self-trigger` | Disable self-trigger |
| `-c, --config <file>` | Read INI config file |
| `--storage <file\|sqlite>` | Select storage backend |
| `--sqlite <path>` | Enable SQLite and set DB path |
| `--web-api` / `--no-web-api` | Enable / disable Web API |
| `--web-api-host <host>` | Web API host |
| `--web-api-port <port>` | Web API port |
| `--web-api-token <token>` | Web API Bearer token |
| `--save-config <file>` | Save current config to INI file |
| `-h, --help` | Show help |

### Adapter Types

| Type | Keyword | Description |
|---|---|---|
| OneBot v11 Forward WS | `forward` / `fwd` / `1` | Actively connect to OneBot v11 |
| OneBot v11 Reverse WS | `reverse` / `rev` / `2` | OneBot v11 connects to us |
| Milky | `milky` / `3` | Milky Protocol |
| OneBot v12 Forward WS | `v12-forward` / `ob12-forward` / `4` | Actively connect to OneBot v12 |
| OneBot v12 Reverse WS | `v12-reverse` / `ob12-reverse` / `5` | OneBot v12 connects to us |

---

## 🧩 Nested Variable System

Variables can be nested within each other. The system resolves them in multiple passes (up to 52 rounds):

```
[积分.0.[n.1]]          → First resolve [n.1] → Then resolve 积分
[重复.[随机数字].🌸]     → First resolve random number → Then repeat
[计算.[长度.abc].*2]    → First resolve length=3 → Then calculate 3*2=6
[in.master.txt.[qq]]    → First resolve QQ number → Then check membership
```

---

## 📋 Built-in Variables

完整变量文档请看 [VARIABLES.md](VARIABLES.md)。当前提供 570+ 个不同实用变量族（不是只靠生成实例计数），并且参数化工具族、用户与事件变量、环境变量、时间变量、随机变量五类均为 110+ 个不同种类（不是别名重复计数）。新增 ZeroBot-plugin 风格趣味变量与内置命令；非 HTTP 请求变量目标解析延迟为 5ms 以内。

### 🎀 ZeroBot-plugin Style Fun Commands

无需手写词库即可响应：`签到` / `打卡` / `今日运势` / `抽签` / `塔罗` / `骰子` / `D20` / `硬币` / `老虎机` / `今日老婆` / `菜单`，并支持戳一戳机器人时自动可爱回复。签到数据写入 `fun_plugins.json`，群聊签到奖励也会进入 `coins.json` 的 `sakura` 积分类型。

### 🔧 Parameterized Utility (110+ species)
| Variable | Description |
|---|---|
| `[两位7]` / `[三位7]` / `[四位7]` | Zero padding helpers |
| `[平方9]` / `[立方3]` / `[双倍21]` | Numeric transforms |
| `[MB2]` / `[GB2]` / `[毫秒500]` | Unit and byte helpers |
| `[艾特10001]` / `[URL路径a b]` / `[CSS类card]` | CQ / URL / tag helpers |

### 👤 User Info / Events (110+ species)
| Variable | Description |
|---|---|
| `[qq]` / `[QQ号]` | Sender QQ |
| `[name]` / `[QQ名]` / `[名字]` | Sender nickname |
| `[card]` / `[群昵称]` | Group card |
| `[id]` / `[消息id]` | Message ID |
| `[ai]` / `[AI号]` | Bot ID |
| `[selfid]` / `[自身id]` | Bot self ID |

### 🌍 Environment (110+ species)
| Variable | Description |
|---|---|
| `[group]` / `[群号]` | Group ID (group only) |
| `[env]` / `[环境]` | Environment (group/private) |

### ⏰ Time (110+ species)
| Variable | Description |
|---|---|
| `(Y)` `(M)` `(D)` `(h)` `(m)` `(s)` | Date/Time components |
| `[年]` `[月]` `[日]` `[时]` `[分]` `[秒]` | Chinese date/time |
| `[日期]` | YYYY-MM-DD |
| `[时间]` | HH:MM:SS |
| `[日期时间]` | YYYY-MM-DD HH:MM:SS |
| `[星期]` | Weekday (Chinese) |
| `[周几]` | Weekday (1-7) |
| `[时间戳]` | Unix timestamp |
| `[年中第几天]` | Day of year |
| `[闰年]` | Is leap year |
| `[本月天数]` | Days in month |

### 🎲 Random (110+ species)
| Variable | Description |
|---|---|
| `(a-b)` | Random integer in range |
| `[随机字母]` | Random lowercase letter |
| `[随机大写字母]` | Random uppercase letter |
| `[随机数字]` | Random digit 0-9 |
| `[随机十六进制]` | Random hex character |
| `[随机布尔]` | Random boolean |

### 📊 Statistics
| Variable | Description |
|---|---|
| `[收消息数]` | Received message count |
| `[发消息数]` | Sent message count |
| `[词条id]` | Current lexicon entry ID |
| `[词汇量]` | Lexicon vocabulary count |
| `[选择的词库]` | Selected lexicon name |
| `[使用的词库]` | Active lexicon name |

### 👑 Identity Lists
| Variable | Description |
|---|---|
| `[主人列表]` / `[主人数]` / `<主人列表>` | Master list |
| `[代管列表]` / `[代管数]` / `<代管列表>` | Admin list |
| `[大主人列表]` / `[大主人数]` / `<大主人列表>` | Super master list |
| `[高管列表]` / `[高管数]` / `<高管列表>` | Executive list |
| `[in.列表.值]` | Check if value is in list |

### 🔧 Formatting
| Variable | Description |
|---|---|
| `[重复.N.文本]` | Repeat text N times |
| `[长度.文本]` | Text length |
| `[截取.N.文本]` | Substring first N chars |
| `[大写.文本]` | To uppercase |
| `[小写.文本]` | To lowercase |
| `[替换.原.新.文本]` | Replace text |
| `[计算.表达式]` | Simple math (+−*/) |

### 🖥️ System
| Variable | Description |
|---|---|
| `[平台]` | Platform (Windows/Linux) |
| `[版本]` | VanBot version |
| `[空]` | Empty string |
| `[换行]` | Newline |
| `[制表符]` | Tab |

### 🔀 Control
| Syntax | Description |
|---|---|
| `[or]` | Random choice separator |
| `[n.N]` / `[n.N.t]` | Regex capture groups |
| `[get.URL]` | HTTP GET request |
| `(-seconds-)` | Delayed segment sending |
| `{condition}` | Condition judgment (==, !=, >, <, >=, <=) |
| `(!error!)` | Error reply (hidden) |

---

## 📐 Project Architecture

```
src/
├── main.cpp                    # Entry + CLI + Signal Handling
├── tui/
│   ├── app.hpp                 # Kawaii TUI (Sakura Pink + Mint Green)
│   └── app.cpp                 # TUI Main Loop + Multi-Adapter Status
└── vanbot/
    ├── common.hpp              # Core Types (Event, Config, AdapterType, AdapterConfig)
    ├── storage.hpp / .cpp      # Thread-safe File Storage (shared_mutex)
    ├── lexicon_engine.hpp/.cpp # Lexicon Engine (CRUD + [n.?] Matching)
    ├── variable_engine.hpp     # Nested Variable Engine (570+ Families + 5×110+ Species Categories, 52-level Recursion)
    ├── fun_plugins.hpp         # ZeroBot-plugin Style Fun Commands
    ├── variable_engine.cpp     # Variable Engine Extension
    ├── onebot_adapter.hpp      # Multi-Protocol Adapter (BotConnection + AdapterManager)
    ├── onebot_adapter.cpp      # Adapter Extension
    ├── http_client.hpp / .cpp  # LRU-Cached HTTP Client
    ├── cooling.hpp             # Cooldown System
    ├── coins.hpp               # Virtual Coins System
    └── bot.hpp / .cpp          # Core Orchestration (Event Processing + Message Codec)
```

---

## 📄 License

AGPL-3.0-or-later — See [LICENSE](LICENSE)

This project is licensed under the GNU Affero General Public License v3.0 or later. If you modify this project and provide it as a network service, you must make the corresponding source code available to users under the same license.

---

<p align="center">
  Made with 💖 by <a href="https://github.com/Van-Zone">Van-Zone</a>
</p>
