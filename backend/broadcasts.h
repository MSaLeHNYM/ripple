#pragma once
#include "db_helpers.h"
#include <unordered_set>

/** Broadcast a pulse_easy envelope {type, data} to chat members' user rooms. */
inline void emit_to_members(pulse::Hub& hub, Database& db, std::int64_t chat_id,
                            std::string_view type, const Json& data,
                            std::int64_t exclude_user_id = 0) {
    const std::string payload = pulse_easy::envelope(type, data).dump();
    for (const auto member_id : chat_member_ids(db, chat_id)) {
        if (member_id == exclude_user_id) continue;
        hub.to("user:" + std::to_string(member_id)).broadcast_text(payload);
    }
}

inline void emit_to_user(pulse::Hub& hub, std::int64_t user_id,
                         std::string_view type, const Json& data) {
    hub.to("user:" + std::to_string(user_id))
        .broadcast_text(pulse_easy::envelope(type, data).dump());
}

inline void broadcast_presence(pulse::Hub& hub, const Presence& /*pres*/,
                                Database& db, std::int64_t user_id, bool online_flag) {
    touch_last_seen(db, user_id);
    const auto payload = pulse_easy::envelope(
        "presence",
        Json{{"user_id", user_id}, {"online", online_flag}, {"last_seen", now_iso()}}).dump();
    hub.broadcast_text(payload);
}

inline void broadcast_typing(pulse::Hub& hub, Database& db,
                             std::int64_t chat_id, std::int64_t user_id, bool typing) {
    std::string display_name = "Someone";
    if (auto u = user_row(db, user_id))
        display_name = u->value("display_name", u->value("username", "Someone"));
    emit_to_members(hub, db, chat_id, "typing",
                    Json{{"chat_id", chat_id}, {"user_id", user_id},
                         {"display_name", display_name}, {"typing", typing}},
                    user_id);
}

inline void notify_chat_created(pulse::Hub& hub, Database& db, std::int64_t chat_id) {
    emit_to_members(hub, db, chat_id, "chat", Json{{"chat_id", chat_id}});
}

inline void mark_delivered_for_online(Database& db, pulse::Hub& hub, const Presence& pres,
                                       std::int64_t chat_id, std::int64_t message_id,
                                       std::int64_t sender_id) {
    for (const auto member_id : chat_member_ids(db, chat_id)) {
        if (member_id == sender_id || !pres.online(member_id)) continue;
        upsert_receipt(db, message_id, member_id, "delivered");
        emit_to_user(hub, sender_id, "receipt",
                     Json{{"message_id", message_id}, {"chat_id", chat_id},
                          {"user_id", member_id}, {"status", "delivered"}});
    }
}

inline Json save_and_broadcast_message(Database& db, pulse::Hub& hub, const Presence& pres,
                                        std::int64_t chat_id, std::int64_t sender_id,
                                        std::string body,
                                        std::optional<std::string> client_id = std::nullopt,
                                        std::string kind = "text",
                                        std::string media_url = "") {
    Row attrs{{"chat_id", chat_id}, {"sender_id", sender_id},
              {"body", body}, {"kind", kind}, {"created_at", now_iso()}};
    if (!media_url.empty()) attrs["media_url"] = media_url;
    const auto rec = Message::create(db, std::move(attrs));

    Json out = message_json(db, pres, rec->attrs(), sender_id);
    out["receipt_status"] = "sent";
    if (client_id) out["client_id"] = *client_id;

    emit_to_members(hub, db, chat_id, "message", out);
    mark_delivered_for_online(db, hub, pres, chat_id, rec->id(), sender_id);
    return out;
}

inline void mark_chat_read(Database& db, pulse::Hub& hub,
                            std::int64_t chat_id, std::int64_t user_id,
                            std::int64_t last_message_id) {
    if (last_message_id <= 0) return;
    const auto member = ChatMember::query(db).where_eq("chat_id", chat_id)
                            .where_eq("user_id", user_id).limit(1).first();
    if (!member) return;

    std::int64_t prev = 0;
    if (member->contains("last_read_message_id") && !member->at("last_read_message_id").is_null())
        prev = member->at("last_read_message_id").get<std::int64_t>();
    if (last_message_id < prev) last_message_id = prev;

    ChatMember::query(db).where_eq("id", member->at("id").get<std::int64_t>())
        .update({{"last_read_message_id", last_message_id}, {"last_read_at", now_iso()}});

    const auto rows = db.query(
        "SELECT id, sender_id FROM messages WHERE chat_id = ? AND id <= ? AND sender_id != ?",
        {chat_id, last_message_id, user_id});

    std::unordered_set<std::int64_t> notify_senders;
    for (const auto& row : rows) {
        upsert_receipt(db, row.at("id").get<std::int64_t>(), user_id, "seen");
        notify_senders.insert(row.at("sender_id").get<std::int64_t>());
    }

    emit_to_members(hub, db, chat_id, "read",
                    Json{{"chat_id", chat_id}, {"user_id", user_id},
                         {"last_message_id", last_message_id}},
                    user_id);

    for (const auto sid : notify_senders) {
        emit_to_user(hub, sid, "receipt",
                     Json{{"chat_id", chat_id}, {"user_id", user_id},
                          {"status", "seen"}, {"up_to_message_id", last_message_id}});
    }
}
