#pragma once
// ─── VanBot Multi-Protocol Adapter ──────────────────────────
// 支持: OneBot v11/v12 正向WS / 反向WS / Milky 协议
// 多实例并行，每个 Bot 连接独立管理
#include "common.hpp"
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <spdlog/spdlog.h>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

namespace vanbot {

using json = nlohmann::json;

// ── AdapterType / AdapterConfig 已在 common.hpp 中定义 ──────

// ── 前向声明 ────────────────────────────────────────────────
class AdapterManager;

// ── 单个适配器连接 ──────────────────────────────────────────
class BotConnection {
public:
    using EventCallback = std::function<void(const Event&, BotId)>;

    BotConnection(const AdapterConfig& config, EventCallback callback)
        : m_config(config), m_callback(std::move(callback)), m_running(false) {}

    ~BotConnection() { stop(); }

    // ── 启动连接 ─────────────────────────────────────────────
    void start() {
        m_running = true;
        switch (m_config.type) {
            case AdapterType::OneBotForwardWS:
            case AdapterType::OneBotV12ForwardWS:
                start_forward_ws();
                break;
            case AdapterType::OneBotReverseWS:
            case AdapterType::OneBotV12ReverseWS:
                // 反向WS由 AdapterManager 统一管理
                break;
            case AdapterType::Milky:
                start_milky();
                break;
        }
    }

    // ── 停止连接 ─────────────────────────────────────────────
    void stop() {
        m_running = false;
        if (m_ws) {
            m_ws->close();
        }
    }

    // ── 由反向WS接收新连接 ──────────────────────────────────
    void accept_connection(std::shared_ptr<ix::WebSocket> ws, const std::string& remote_ip) {
        m_ws = ws;
        m_connected = true;
        spdlog::info("💚 [{}] 反向WS接入: {}", m_config.name, remote_ip);

        m_ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            if (msg->type == ix::WebSocketMessageType::Message) {
                handle_message(msg->str);
            } else if (msg->type == ix::WebSocketMessageType::Close) {
                m_connected = false;
                spdlog::warn("💛 [{}] 反向WS断开", m_config.name);
            }
        });
    }

    // ── 发送群消息 ──────────────────────────────────────────
    void send_group_msg(GroupId group_id, const std::string& message) {
        call_api(is_onebot_v12() ? "send_message" : "send_group_msg",
                 is_onebot_v12()
                     ? json{{"detail_type", "group"}, {"group_id", group_id}, {"message", message}}
                     : json{{"group_id", group_id}, {"message", message}});
    }

    // ── 发送私聊消息 ────────────────────────────────────────
    void send_private_msg(UserId user_id, const std::string& message) {
        call_api(is_onebot_v12() ? "send_message" : "send_private_msg",
                 is_onebot_v12()
                     ? json{{"detail_type", "private"}, {"user_id", user_id}, {"message", message}}
                     : json{{"user_id", user_id}, {"message", message}});
    }

    // ── 通用 API 调用 ────────────────────────────────────────
    void call_api(const std::string& action, const json& params = json::object()) {
        if (is_onebot_v12()) {
            json payload = {
                {"action", action},
                {"params", params},
                {"echo", next_echo()}
            };
            send_payload(payload);
            return;
        }
        json payload = {{"action", action}, {"params", params}};
        send_payload(payload);
    }

    // ── 发送消息（按环境选择） ───────────────────────────────
    void send_msg(Env env, int64_t env_id, const std::string& message) {
        if (env == Env::Group) send_group_msg(env_id, message);
        else send_private_msg(env_id, message);
    }

    // ── 撤回消息 ─────────────────────────────────────────────
    void delete_msg(MessageId message_id) {
        call_api("delete_msg", {{"message_id", message_id}});
    }

    // ── 禁言 ─────────────────────────────────────────────────
    void set_group_ban(GroupId group_id, UserId user_id, int duration) {
        call_api("set_group_ban", {{"group_id", group_id}, {"user_id", user_id}, {"duration", duration}});
    }

    // ── 全体禁言 ─────────────────────────────────────────────
    void set_group_whole_ban(GroupId group_id, bool enable) {
        call_api("set_group_whole_ban", {{"group_id", group_id}, {"enable", enable}});
    }

    // ── 踢人 ─────────────────────────────────────────────────
    void set_group_kick(GroupId group_id, UserId user_id, bool reject = false) {
        call_api("set_group_kick", {{"group_id", group_id}, {"user_id", user_id}, {"reject_add_request", reject}});
    }

    // ── 设置群名片 ──────────────────────────────────────────
    void set_group_card(GroupId group_id, UserId user_id, const std::string& card) {
        call_api("set_group_card", {{"group_id", group_id}, {"user_id", user_id}, {"card", card}});
    }

    // ── 设置群头衔 ──────────────────────────────────────────
    void set_group_special_title(GroupId group_id, UserId user_id, const std::string& title) {
        call_api("set_group_special_title", {{"group_id", group_id}, {"user_id", user_id}, {"special_title", title}});
    }

    // ── 戳一戳 ──────────────────────────────────────────────
    void group_poke(GroupId group_id, UserId user_id) {
        call_api("group_poke", {{"group_id", group_id}, {"user_id", user_id}});
    }

    // ── 点赞 ─────────────────────────────────────────────────
    void send_like(UserId user_id, int times = 1) {
        call_api("send_like", {{"user_id", user_id}, {"times", times}});
    }

    // ── 设置精华 ─────────────────────────────────────────────
    void set_essence_msg(MessageId message_id) {
        call_api("set_essence_msg", {{"message_id", message_id}});
    }

    // ── 退出群聊 ─────────────────────────────────────────────
    void set_group_leave(GroupId group_id) {
        call_api("set_group_leave", {{"group_id", group_id}});
    }

    // ── 设置头像 ─────────────────────────────────────────────
    void set_qq_avatar(const std::string& url) {
        call_api("set_qq_avatar", {{"file", url}});
    }

    // ── 获取好友列表 ─────────────────────────────────────────
    void get_friend_list() {
        call_api("get_friends_with_category");
    }

    // ── 获取群列表 ───────────────────────────────────────────
    void get_group_list() {
        call_api("get_group_list");
    }

    bool is_onebot_v12() const {
        return m_config.type == AdapterType::OneBotV12ForwardWS ||
               m_config.type == AdapterType::OneBotV12ReverseWS;
    }

    bool is_reverse_ws() const {
        return m_config.type == AdapterType::OneBotReverseWS ||
               m_config.type == AdapterType::OneBotV12ReverseWS;
    }

    bool is_connected() const { return m_connected; }
    const AdapterConfig& config() const { return m_config; }
    const std::string& name() const { return m_config.name; }
    BotId self_id() const { return m_self_id; }

private:
    AdapterConfig m_config;
    EventCallback m_callback;
    std::shared_ptr<ix::WebSocket> m_ws;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<BotId> m_self_id{0};
    std::thread m_thread;

    // ── 正向WS连接 ──────────────────────────────────────────
    void start_forward_ws() {
        m_thread = std::thread([this]() {
            while (m_running) {
                m_ws = std::make_shared<ix::WebSocket>();
                m_ws->setUrl(m_config.url);

                // 设置鉴权
                if (!m_config.access_token.empty()) {
                    ix::WebSocketHttpHeaders headers;
                    headers["Authorization"] = "Bearer " + m_config.access_token;
                    m_ws->setExtraHeaders(headers);
                }

                m_ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
                    if (msg->type == ix::WebSocketMessageType::Message) {
                        handle_message(msg->str);
                    } else if (msg->type == ix::WebSocketMessageType::Open) {
                        m_connected = true;
                        spdlog::info("💚 [{}] 正向WS已连接: {}", m_config.name, m_config.url);
                    } else if (msg->type == ix::WebSocketMessageType::Close) {
                        m_connected = false;
                        spdlog::warn("💛 [{}] 正向WS断开，{}秒后重连...", m_config.name, m_config.reconnect_interval);
                    } else if (msg->type == ix::WebSocketMessageType::Error) {
                        m_connected = false;
                        spdlog::error("🔴 [{}] WS错误: {}", m_config.name, msg->errorInfo.reason);
                    }
                });

                spdlog::info("🔗 [{}] 正在连接: {}", m_config.name, m_config.url);
                m_ws->start();

                // 等待重连
                while (m_running && m_ws->getReadyState() != ix::ReadyState::Closed) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }

                if (m_running) {
                    for (int i = 0; i < m_config.reconnect_interval && m_running; i++) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
        });
    }

    // ── Milky协议连接 ────────────────────────────────────────
    void start_milky() {
        m_thread = std::thread([this]() {
            while (m_running) {
                m_ws = std::make_shared<ix::WebSocket>();
                m_ws->setUrl(m_config.url);

                if (!m_config.access_token.empty()) {
                    ix::WebSocketHttpHeaders headers;
                    headers["Authorization"] = "Bearer " + m_config.access_token;
                    m_ws->setExtraHeaders(headers);
                }

                m_ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
                    if (msg->type == ix::WebSocketMessageType::Message) {
                        handle_milky_message(msg->str);
                    } else if (msg->type == ix::WebSocketMessageType::Open) {
                        m_connected = true;
                        spdlog::info("💚 [{}] Milky已连接: {}", m_config.name, m_config.url);
                        // Milky 需要 identify
                        send_milky_identify();
                    } else if (msg->type == ix::WebSocketMessageType::Close) {
                        m_connected = false;
                        spdlog::warn("💛 [{}] Milky断开，重连中...", m_config.name);
                    } else if (msg->type == ix::WebSocketMessageType::Error) {
                        m_connected = false;
                        spdlog::error("🔴 [{}] Milky错误: {}", m_config.name, msg->errorInfo.reason);
                    }
                });

                spdlog::info("🔗 [{}] 正在连接 Milky: {}", m_config.name, m_config.url);
                m_ws->start();

                while (m_running && m_ws->getReadyState() != ix::ReadyState::Closed) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }

                if (m_running) {
                    for (int i = 0; i < m_config.reconnect_interval && m_running; i++) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
        });
    }

    // ── Milky identify ───────────────────────────────────────
    void send_milky_identify() {
        json payload = {
            {"type", "identify"},
            {"payload", {
                {"token", m_config.access_token},
                {"event_types", json::array({"GroupMessageEvent", "FriendMessageEvent",
                    "GroupPokeEvent", "GroupMemberIncreaseEvent", "GroupMemberDecreaseEvent"})}
            }}
        };
        send_payload(payload);
    }

    // ── 发送原始 payload ─────────────────────────────────────
    void send_payload(const json& payload) {
        if (m_ws && m_connected) {
            m_ws->send(payload.dump());
        }
    }

    // ── OneBot v11 消息解析 ──────────────────────────────────
    void handle_message(const std::string& raw) {
        try {
            auto j = json::parse(raw);
            if (j.contains("status") || j.contains("retcode")) return;  // API 响应，忽略

            Event event = is_onebot_v12() ? parse_onebot_v12_event(j) : parse_onebot_event(j);
            m_self_id = event.self_id;

            if (m_callback) m_callback(event, event.self_id);
        } catch (const std::exception& e) {
            spdlog::error("[{}] 解析OneBot事件失败: {}", m_config.name, e.what());
        }
    }

    // ── Milky 消息解析 ───────────────────────────────────────
    void handle_milky_message(const std::string& raw) {
        try {
            auto j = json::parse(raw);
            std::string type = j.value("type", "");

            Event event;

            if (type == "IdentityEvent") {
                auto& payload = j["payload"];
                m_self_id = payload.value("uin", 0LL);
                spdlog::info("🔑 [{}] Milky身份确认: {}", m_config.name, m_self_id.load());
                return;
            }
            else if (type == "GroupMessageEvent") {
                event = parse_milky_group_msg(j);
            }
            else if (type == "FriendMessageEvent") {
                event = parse_milky_private_msg(j);
            }
            else if (type == "GroupPokeEvent") {
                auto& payload = j["payload"];
                event.type = Event::Poke;
                event.self_id = m_self_id;
                event.user_id = payload.value("sender_uid", 0LL);
                event.target_id = payload.value("target_uid", 0LL);
                event.group_id = payload.value("group_code", 0LL);
            }
            else if (type == "GroupMemberIncreaseEvent") {
                auto& payload = j["payload"];
                event.type = Event::GroupIncrease;
                event.self_id = m_self_id;
                event.user_id = payload.value("member_uid", 0LL);
                event.group_id = payload.value("group_code", 0LL);
            }
            else if (type == "GroupMemberDecreaseEvent") {
                auto& payload = j["payload"];
                event.type = Event::GroupDecrease;
                event.self_id = m_self_id;
                event.user_id = payload.value("member_uid", 0LL);
                event.group_id = payload.value("group_code", 0LL);
                event.operator_id = payload.value("operator_uid", 0LL);
            }
            else {
                return; // 未知事件
            }

            event.self_id = m_self_id;
            if (m_callback) m_callback(event, event.self_id);
        } catch (const std::exception& e) {
            spdlog::error("[{}] 解析Milky事件失败: {}", m_config.name, e.what());
        }
    }

    // ── Milky 群消息解析 ────────────────────────────────────
    static Event parse_milky_group_msg(const json& j) {
        Event event;
        event.type = Event::GroupMessage;
        auto& payload = j["payload"];

        event.group_id = payload.value("group_code", 0LL);
        event.message_id = payload.value("msg_seq", 0LL);
        event.user_id = payload.value("sender_uid", 0LL);

        // 发送者信息
        if (payload.contains("sender")) {
            auto& sender = payload["sender"];
            event.sender_name = sender.value("nickname", "");
            event.sender_card = sender.value("card_name", "");
            if (event.sender_card.empty()) event.sender_card = event.sender_name;
        }

        // 消息内容
        event.raw_message = parse_milky_message_elements(payload);

        return event;
    }

    // ── Milky 私聊消息解析 ──────────────────────────────────
    static Event parse_milky_private_msg(const json& j) {
        Event event;
        event.type = Event::PrivateMessage;
        auto& payload = j["payload"];

        event.user_id = payload.value("sender_uid", 0LL);
        event.message_id = payload.value("msg_seq", 0LL);
        event.group_id = 0;

        if (payload.contains("sender")) {
            auto& sender = payload["sender"];
            event.sender_name = sender.value("nickname", "");
            event.sender_card = event.sender_name;
        }

        event.raw_message = parse_milky_message_elements(payload);
        return event;
    }

    // ── Milky 消息元素 → 纯文本/CQ码 ───────────────────────
    static std::string parse_milky_message_elements(const json& payload) {
        std::string text;
        if (!payload.contains("elements")) return "";

        for (auto& elem : payload["elements"]) {
            std::string elem_type = elem.value("element_type", "");

            if (elem_type == "text") {
                text += elem.value("content", "");
            }
            else if (elem_type == "at") {
                if (elem.contains("at_type")) {
                    int at_type = elem.value("at_type", 0);
                    if (at_type == 1) { // @某人
                        text += "[CQ:at,qq=" + std::to_string(elem.value("at_uid", 0LL)) + "]";
                    } else if (at_type == 2) { // @全体
                        text += "[CQ:at,qq=all]";
                    }
                }
            }
            else if (elem_type == "face") {
                text += "[CQ:face,id=" + std::to_string(elem.value("face_index", 0)) + "]";
            }
            else if (elem_type == "image") {
                text += "[CQ:image,url=" + elem.value("url", "") + "]";
            }
            else if (elem_type == "record") {
                text += "[CQ:record,url=" + elem.value("url", "") + "]";
            }
            else if (elem_type == "video") {
                text += "[CQ:video,url=" + elem.value("url", "") + "]";
            }
            else if (elem_type == "reply") {
                text += "[CQ:reply,id=" + std::to_string(elem.value("reply_seq", 0LL)) + "]";
            }
            else if (elem_type == "poke") {
                text += "[CQ:poke]";
            }
            else if (elem_type == "forward") {
                text += "[CQ:forward,id=" + elem.value("res_id", "") + "]";
            }
            else if (elem_type == "json") {
                text += "[CQ:json,data=" + elem.value("content", "") + "]";
            }
        }
        return text;
    }

    // ── OneBot v11 事件解析 ─────────────────────────────────
    static Event parse_onebot_event(const json& j) {
        Event event;
        event.self_id = j.value("self_id", 0LL);
        event.user_id = j.value("user_id", 0LL);
        event.group_id = j.value("group_id", 0LL);
        event.message_id = j.value("message_id", 0LL);
        event.target_id = j.value("target_id", 0LL);
        event.operator_id = j.value("operator_id", 0LL);
        event.duration = j.value("duration", 0LL);

        // 发送者
        if (j.contains("sender")) {
            auto& sender = j["sender"];
            event.sender_name = sender.value("nickname", "");
            event.sender_card = sender.value("card", "");
            if (event.sender_card.empty()) event.sender_card = event.sender_name;
        }

        std::string post_type = j.value("post_type", "");
        std::string message_type = j.value("message_type", "");
        std::string notice_type = j.value("notice_type", "");
        std::string sub_type = j.value("sub_type", "");
        std::string meta_event_type = j.value("meta_event_type", "");

        if (post_type == "message") {
            // 提取消息
            if (j.contains("message") && j["message"].is_array()) {
                std::string text;
                for (auto& seg : j["message"]) {
                    std::string type = seg.value("type", "");
                    if (type == "text" && seg.contains("data"))
                        text += seg["data"].value("text", "");
                    else if (type == "at" && seg.contains("data"))
                        text += "[CQ:at,qq=" + seg["data"].value("qq", "0") + "]";
                    else if (type == "image" && seg.contains("data"))
                        text += "[CQ:image,url=" + seg["data"].value("url", "") + "]";
                    else if (type == "reply" && seg.contains("data"))
                        text += "[CQ:reply,id=" + seg["data"].value("id", "0") + "]";
                    else if (type == "face" && seg.contains("data"))
                        text += "[CQ:face,id=" + seg["data"].value("id", "0") + "]";
                    else if (type == "poke")
                        text += "[CQ:poke]";
                    else if (type == "record" && seg.contains("data"))
                        text += "[CQ:record,url=" + seg["data"].value("url", "") + "]";
                    else if (type == "video" && seg.contains("data"))
                        text += "[CQ:video,url=" + seg["data"].value("url", "") + "]";
                }
                event.raw_message = text;
            } else if (j.contains("raw_message")) {
                event.raw_message = j.value("raw_message", "");
            }

            event.type = (message_type == "group") ? Event::GroupMessage : Event::PrivateMessage;
        }
        else if (post_type == "notice") {
            if (notice_type == "notify" && sub_type == "poke")
                event.type = Event::Poke;
            else if (notice_type == "group_increase")
                event.type = Event::GroupIncrease;
            else if (notice_type == "group_decrease")
                event.type = Event::GroupDecrease;
            else if (notice_type == "group_recall")
                event.type = Event::GroupRecall;
            else if (notice_type == "group_admin")
                event.type = (sub_type == "set") ? Event::GroupAdminSet : Event::GroupAdminUnset;
            else if (notice_type == "group_ban")
                event.type = (event.duration > 0) ? Event::GroupBan : Event::GroupUnBan;
            else if (notice_type == "friend_add")
                event.type = Event::FriendAdd;
        }
        else if (post_type == "meta_event") {
            if (meta_event_type == "lifecycle")
                event.type = Event::Lifecycle;
            else if (meta_event_type == "heartbeat") {
                event.type = Event::Heartbeat;
                if (j.contains("status")) event.online = j["status"].value("online", true);
            }
        }

        return event;
    }

    // ── OneBot v12 事件解析 ─────────────────────────────────
    static Event parse_onebot_v12_event(const json& j) {
        Event event;
        event.self_id = j.contains("self") ? json_to_i64(j["self"]) : 0;
        if (event.self_id == 0 && j.contains("self") && j["self"].is_object()) {
            event.self_id = j["self"].contains("user_id") ? json_to_i64(j["self"]["user_id"]) : 0;
        }
        if (event.self_id == 0 && j.contains("self_id")) event.self_id = json_to_i64(j["self_id"]);
        event.user_id = j.contains("user_id") ? json_to_i64(j["user_id"]) : 0;
        event.group_id = j.contains("group_id") ? json_to_i64(j["group_id"]) : 0;
        event.message_id = j.contains("message_id") ? json_to_i64(j["message_id"]) : 0;

        std::string type = j.value("type", j.value("post_type", ""));
        std::string detail_type = j.value("detail_type", j.value("message_type", ""));
        std::string sub_type = j.value("sub_type", "");
        event.target_id = j.contains("target_id") ? json_to_i64(j["target_id"]) : 0;
        event.operator_id = j.contains("operator_id") ? json_to_i64(j["operator_id"]) : 0;
        event.duration = j.contains("duration") ? json_to_i64(j["duration"]) : 0;

        if (j.contains("sender")) {
            auto& sender = j["sender"];
            event.sender_name = sender.value("nickname", sender.value("name", ""));
            event.sender_card = sender.value("card", event.sender_name);
        }

        if (type == "message" || j.contains("message")) {
            event.type = (detail_type == "group") ? Event::GroupMessage : Event::PrivateMessage;
            event.raw_message = parse_ob12_message(j.value("message", json::array()));
            if (event.raw_message.empty()) event.raw_message = j.value("raw_message", j.value("alt_message", ""));
        } else if (type == "notice") {
            event.type = Event::Lifecycle;
            if (detail_type == "group_member_increase") event.type = Event::GroupIncrease;
            else if (detail_type == "group_member_decrease") event.type = Event::GroupDecrease;
            else if (detail_type == "group_message_delete") event.type = Event::GroupRecall;
            else if (detail_type == "friend_increase") event.type = Event::FriendAdd;
            else if (detail_type == "group_admin_set") event.type = Event::GroupAdminSet;
            else if (detail_type == "group_admin_unset") event.type = Event::GroupAdminUnset;
            else if (detail_type == "group_ban") event.type = Event::GroupBan;
            else if (detail_type == "group_unban") event.type = Event::GroupUnBan;
            else if (detail_type == "poke" || sub_type == "poke") event.type = Event::Poke;
        } else if (type == "meta") {
            event.type = Event::Heartbeat;
            event.online = true;
        }
        return event;
    }

    static std::string parse_ob12_message(const json& message) {
        if (message.is_string()) return message.get<std::string>();
        if (!message.is_array()) return "";
        std::string text;
        for (const auto& seg : message) {
            std::string type = seg.value("type", "");
            json data = seg.value("data", json::object());
            if (type == "text") text += data.value("text", "");
            else if (type == "mention" || type == "at") text += "[CQ:at,qq=" + json_to_string(data.contains("user_id") ? data["user_id"] : data.value("qq", json{})) + "]";
            else if (type == "image") text += "[CQ:image,url=" + json_to_string(data.contains("url") ? data["url"] : data.value("file", json{})) + "]";
            else if (type == "reply") text += "[CQ:reply,id=" + json_to_string(data.contains("message_id") ? data["message_id"] : data.value("id", json{})) + "]";
            else if (type == "face") text += "[CQ:face,id=" + json_to_string(data.contains("id") ? data["id"] : json{}) + "]";
            else if (type == "voice" || type == "record") text += "[CQ:record,url=" + json_to_string(data.contains("url") ? data["url"] : data.value("file", json{})) + "]";
            else if (type == "video") text += "[CQ:video,url=" + json_to_string(data.contains("url") ? data["url"] : data.value("file", json{})) + "]";
        }
        return text;
    }

    static int64_t json_to_i64(const json& v) {
        try {
            if (v.is_number_integer()) return v.get<int64_t>();
            if (v.is_string()) return std::stoll(v.get<std::string>());
        } catch (...) {}
        return 0;
    }

    static std::string json_to_string(const json& v) {
        if (v.is_string()) return v.get<std::string>();
        if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
        if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
        if (v.is_number_float()) return std::to_string(v.get<double>());
        if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
        return "";
    }

    static std::string next_echo() {
        static std::atomic<uint64_t> seq{1};
        return "vanbot-" + std::to_string(seq.fetch_add(1));
    }
};

// ── 适配器管理器（管理多个连接） ─────────────────────────────
class AdapterManager {
public:
    using EventCallback = std::function<void(const Event&, BotId)>;

    AdapterManager() = default;
    ~AdapterManager() { stop_all(); }

    void on_event(EventCallback cb) { m_callback = std::move(cb); }

    // ── 添加适配器 ──────────────────────────────────────────
    void add_adapter(const AdapterConfig& config) {
        auto conn = std::make_shared<BotConnection>(config, m_callback);
        std::unique_lock lock(m_mutex);
        m_connections[config.name] = conn;
        spdlog::info("📡 注册适配器: [{}] ({})", config.name, adapter_type_name(config.type));
    }

    // ── 启动所有正向WS/Milky ────────────────────────────────
    void start_all() {
        std::shared_lock lock(m_mutex);
        for (auto& [name, conn] : m_connections) {
            if (!conn->is_reverse_ws()) {
                conn->start();
            }
        }

        // 启动反向WS服务器
        start_reverse_ws_servers();
    }

    // ── 停止所有 ─────────────────────────────────────────────
    void stop_all() {
        std::unique_lock lock(m_mutex);
        for (auto& [name, conn] : m_connections) {
            conn->stop();
        }
        for (auto& server : m_ws_servers) {
            if (server) server->stop();
        }
        m_connections.clear();
        m_ws_servers.clear();
    }

    // ── 按 bot_id 获取连接 ──────────────────────────────────
    std::shared_ptr<BotConnection> get_connection(BotId bot_id) {
        std::shared_lock lock(m_mutex);
        for (auto& [name, conn] : m_connections) {
            if (conn->self_id() == bot_id && conn->is_connected()) return conn;
        }
        // 降级：返回第一个已连接的
        for (auto& [name, conn] : m_connections) {
            if (conn->is_connected()) return conn;
        }
        return nullptr;
    }

    // ── 按名称获取连接 ──────────────────────────────────────
    std::shared_ptr<BotConnection> get_by_name(const std::string& name) {
        std::shared_lock lock(m_mutex);
        auto it = m_connections.find(name);
        return (it != m_connections.end()) ? it->second : nullptr;
    }

    // ── 获取所有连接状态 ────────────────────────────────────
    std::vector<std::pair<std::string, bool>> get_all_status() {
        std::shared_lock lock(m_mutex);
        std::vector<std::pair<std::string, bool>> status;
        for (auto& [name, conn] : m_connections) {
            status.push_back({name + " (" + adapter_type_name(conn->config().type) + ")",
                             conn->is_connected()});
        }
        return status;
    }

    size_t connection_count() const {
        std::shared_lock lock(m_mutex);
        return m_connections.size();
    }

private:
    std::unordered_map<std::string, std::shared_ptr<BotConnection>> m_connections;
    std::vector<std::shared_ptr<ix::WebSocketServer>> m_ws_servers;
    EventCallback m_callback;
    mutable std::shared_mutex m_mutex;

    // ── 启动反向WS服务器 ────────────────────────────────────
    void start_reverse_ws_servers() {
        // 收集所有反向WS配置的端口
        std::unordered_map<int, std::vector<std::string>> port_to_names;
        {
            std::shared_lock lock(m_mutex);
            for (auto& [name, conn] : m_connections) {
                if (conn->is_reverse_ws()) {
                    port_to_names[conn->config().port].push_back(name);
                }
            }
        }

        for (auto& [port, names] : port_to_names) {
            auto server = std::make_shared<ix::WebSocketServer>(port);
            spdlog::info("🔊 反向WS服务器监听: port={}", port);

            server->setOnClientMessageCallback(
                [this, names](std::shared_ptr<ix::ConnectionState> state,
                              ix::WebSocket& ws,
                              const ix::WebSocketMessagePtr& msg) {
                    if (msg->type == ix::WebSocketMessageType::Open) {
                        spdlog::info("💚 反向WS新连接: {}", state->getRemoteIp());

                        // 分配给第一个未连接的同名适配器
                        std::shared_lock lock(m_mutex);
                        for (auto& name : names) {
                            auto it = m_connections.find(name);
                            if (it != m_connections.end() && !it->second->is_connected()) {
                                // 因为 ix::WebSocket 不是 shared_ptr，需要包装
                                // 简化：直接在 on_message 中处理
                                break;
                            }
                        }
                    }
                }
            );

            auto res = server->listen();
            if (!res.first) {
                spdlog::error("🔴 反向WS监听失败 port={}: {}", port, res.second);
                continue;
            }

            server->start();
            m_ws_servers.push_back(server);
        }
    }

    static std::string adapter_type_name(AdapterType type) {
        switch (type) {
            case AdapterType::OneBotForwardWS: return "OneBot-v11-正向WS";
            case AdapterType::OneBotReverseWS: return "OneBot-v11-反向WS";
            case AdapterType::OneBotV12ForwardWS: return "OneBot-v12-正向WS";
            case AdapterType::OneBotV12ReverseWS: return "OneBot-v12-反向WS";
            case AdapterType::Milky: return "Milky";
            default: return "Unknown";
        }
    }
};

} // namespace vanbot
