#pragma once
// ─── VanBot Common Types ──────────────────────────────────────
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
#include <chrono>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace vanbot {

// ── 基础类型别名 ─────────────────────────────────────────────
using UserId    = int64_t;
using GroupId   = int64_t;
using BotId     = int64_t;
using MessageId = int64_t;

// ── 环境类型 ─────────────────────────────────────────────────
enum class Env { Group, Private };

// ── 适配器类型 ───────────────────────────────────────────────
enum class AdapterType {
    OneBotForwardWS,   // 正向WS: 主动连接 OneBot 实现
    OneBotReverseWS,   // 反向WS: OneBot 连接到我们
    Milky,             // Milky 协议
};

// ── 适配器配置 ───────────────────────────────────────────────
struct AdapterConfig {
    std::string  name;                           // 显示名称
    AdapterType  type = AdapterType::OneBotForwardWS;
    std::string  url = "ws://127.0.0.1:6700";   // 正向WS/Milky地址
    int          port = 6701;                    // 反向WS监听端口
    std::string  access_token;                   // 鉴权 Token
    int          reconnect_interval = 5;         // 重连间隔(秒)
    int          heartbeat_interval = 30;        // Milky心跳间隔
};

// ── 消息段类型 ───────────────────────────────────────────────
struct MessageSegment {
    enum Type {
        Text, At, Image, Face, Reply, Record, Video,
        Forward, Json, Music, Poke, Location, File
    } type;

    std::string data;          // 主要数据（文本/URL/ID等）
    std::string extra;         // 附加参数
    int64_t     user_id = 0;   // At 目标等
};

using Message = std::vector<MessageSegment>;

// ── OneBot 事件 ──────────────────────────────────────────────
struct Event {
    enum Type {
        GroupMessage, PrivateMessage, Poke,
        GroupIncrease, GroupDecrease, GroupRecall,
        GroupAdminSet, GroupAdminUnset, GroupBan, GroupUnBan,
        FriendAdd, Lifecycle, Heartbeat
    } type;

    BotId     self_id   = 0;
    UserId    user_id   = 0;
    GroupId   group_id  = 0;
    MessageId message_id = 0;
    std::string raw_message;    // 原始消息文本
    std::string sender_name;    // 昵称
    std::string sender_card;    // 群名片
    int64_t     target_id  = 0; // 戳一戳目标
    int64_t     operator_id = 0;
    int64_t     duration  = 0;  // 禁言时长
    bool        online    = true; // 心跳在线状态
};

// ── 词库词条 ──────────────────────────────────────────────────
enum class MatchMode { Fuzzy = 0, Exact = 1 };

struct LexiconEntry {
    std::string              keyword;
    std::vector<std::string> responses;
    MatchMode                mode = MatchMode::Fuzzy;
};

// ── 积分记录 ─────────────────────────────────────────────────
struct CoinRecord {
    GroupId     group  = 0;
    UserId      user   = 0;
    std::string type;       // 积分类型
    std::string coins;      // 积分值（支持字符串以兼容列表模式）
    std::string uptime;     // 更新时间
};

// ── 冷却记录 ─────────────────────────────────────────────────
struct CoolingRecord {
    UserId          user_id;
    int64_t         lexicon_id = 0;
    double          expire_time = 0.0; // 时间戳
};

// ── 配置 ─────────────────────────────────────────────────────
struct Config {
    bool        self_trigger = true;
    std::string data_dir     = "./Van_keyword";
    std::vector<AdapterConfig> adapters;  // 多适配器配置
};

} // namespace vanbot
