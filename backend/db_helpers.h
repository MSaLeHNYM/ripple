#pragma once
#include "models.h"
#include "presence.h"
#include "utils.h"
#include <optional>
#include <unordered_set>
#include <vector>

inline Json user_public(Database& /*db*/, const Presence& pres, const Row& row) {
    const auto id = row.at("id").get<std::int64_t>();
    return Json{{"id", id},
                {"username", row.at("username")},
                {"display_name", row.at("display_name")},
                {"created_at", row.value("created_at", "")},
                {"last_seen", row.value("last_seen", "")},
                {"online", pres.online(id)}};
}

inline std::optional<Row> user_row(Database& db, std::int64_t id) {
    return User::query(db).where_eq("id", id).limit(1).first();
}

inline void touch_last_seen(Database& db, std::int64_t user_id) {
    User::query(db).where_eq("id", user_id).update({{"last_seen", now_iso()}});
}

inline std::vector<std::int64_t> chat_member_ids(Database& db, std::int64_t chat_id) {
    std::vector<std::int64_t> out;
    for (const auto& row : ChatMember::query(db).where_eq("chat_id", chat_id).get())
        out.push_back(row.at("user_id").get<std::int64_t>());
    return out;
}

inline bool is_member(Database& db, std::int64_t chat_id, std::int64_t user_id) {
    return ChatMember::query(db).where_eq("chat_id", chat_id).where_eq("user_id", user_id).exists();
}

inline std::optional<std::int64_t> find_dm_chat(Database& db, std::int64_t a, std::int64_t b) {
    const auto rows = db.query(
        "SELECT c.id FROM chats c "
        "INNER JOIN chat_members m1 ON m1.chat_id = c.id AND m1.user_id = ? "
        "INNER JOIN chat_members m2 ON m2.chat_id = c.id AND m2.user_id = ? "
        "WHERE c.kind = 'dm' LIMIT 1",
        {a, b});
    return rows.empty() ? std::nullopt
                        : std::optional<std::int64_t>(rows.front().at("id").get<std::int64_t>());
}

inline int receipt_rank(const std::string& s) {
    if (s == "seen")      return 3;
    if (s == "delivered") return 2;
    if (s == "sent")      return 1;
    return 0;
}

inline void upsert_receipt(Database& db, std::int64_t message_id,
                           std::int64_t user_id, const std::string& status) {
    const auto existing = MessageReceipt::query(db)
                              .where_eq("message_id", message_id)
                              .where_eq("user_id", user_id)
                              .limit(1).first();
    const std::string ts = now_iso();
    if (existing) {
        if (receipt_rank(status) <= receipt_rank(existing->at("status").get<std::string>())) return;
        MessageReceipt::query(db)
            .where_eq("id", existing->at("id").get<std::int64_t>())
            .update({{"status", status}, {"updated_at", ts}});
    } else {
        MessageReceipt::create(db, {{"message_id", message_id}, {"user_id", user_id},
                                    {"status", status}, {"updated_at", ts}});
    }
}

inline std::string aggregate_receipt_status(Database& db, std::int64_t message_id,
                                            std::int64_t sender_id, std::int64_t chat_id) {
    const auto members = chat_member_ids(db, chat_id);
    int worst = 3; bool any = false;
    for (const auto mid : members) {
        if (mid == sender_id) continue;
        any = true;
        const auto row = MessageReceipt::query(db).where_eq("message_id", message_id)
                             .where_eq("user_id", mid).limit(1).first();
        const int rank = row ? receipt_rank(row->at("status").get<std::string>()) : 0;
        if (rank < worst) worst = rank;
    }
    if (!any) return "sent";
    if (worst >= 3) return "seen";
    if (worst >= 2) return "delivered";
    return "sent";
}

inline Json message_json(Database& db, const Presence& pres, const Row& row,
                         std::optional<std::int64_t> viewer_id = std::nullopt) {
    const auto sender_id  = row.at("sender_id").get<std::int64_t>();
    const auto message_id = row.at("id").get<std::int64_t>();
    const auto chat_id    = row.at("chat_id").get<std::int64_t>();
    Json j{{"id", message_id},
           {"chat_id", chat_id},
           {"sender_id", sender_id},
           {"body", row.at("body")},
           {"kind", row.value("kind", "text")},
           {"created_at", row.value("created_at", "")}};
    if (row.contains("media_url") && !row.at("media_url").is_null())
        j["media_url"] = row.at("media_url");
    if (auto u = user_row(db, sender_id)) j["sender"] = user_public(db, pres, *u);
    if (viewer_id && *viewer_id == sender_id)
        j["receipt_status"] = aggregate_receipt_status(db, message_id, sender_id, chat_id);
    return j;
}

inline std::int64_t unread_count_for(Database& db, std::int64_t chat_id,
                                     std::int64_t user_id, const Row& member_row) {
    std::int64_t last_read = 0;
    if (member_row.contains("last_read_message_id") &&
        !member_row.at("last_read_message_id").is_null())
        last_read = member_row.at("last_read_message_id").get<std::int64_t>();
    const auto rows = db.query(
        "SELECT COUNT(*) AS c FROM messages WHERE chat_id = ? AND id > ? AND sender_id != ?",
        {chat_id, last_read, user_id});
    return rows.empty() ? 0 : rows.front().at("c").get<std::int64_t>();
}

inline Json chat_json(Database& db, const Presence& pres, const Row& chat_row,
                      std::int64_t current_user_id) {
    const auto chat_id = chat_row.at("id").get<std::int64_t>();
    const std::string kind = chat_row.at("kind").get<std::string>();
    Json item{{"id", chat_id}, {"kind", kind}, {"type", kind}, {"is_group", kind == "group"},
              {"title", (chat_row.contains("title") && !chat_row.at("title").is_null())
                            ? chat_row.at("title") : Json(nullptr)},
              {"created_at", chat_row.value("created_at", "")}};

    const auto msgs = Message::query(db).where_eq("chat_id", chat_id)
                          .order_by("id", false).limit(1).get();
    item["last_message"] =
        msgs.empty() ? Json(nullptr) : message_json(db, pres, msgs.front(), current_user_id);

    Json members = Json::array();
    std::optional<Row> my_membership;
    for (const auto& m : ChatMember::query(db).where_eq("chat_id", chat_id).get()) {
        const auto mid = m.at("user_id").get<std::int64_t>();
        if (mid == current_user_id) my_membership = m;
        if (auto u = user_row(db, mid)) members.push_back(user_public(db, pres, *u));
        if (kind == "dm" && mid != current_user_id)
            if (auto u = user_row(db, mid)) item["peer"] = user_public(db, pres, *u);
    }
    item["members"]      = members;
    item["unread_count"] = my_membership
        ? unread_count_for(db, chat_id, current_user_id, *my_membership) : 0;
    return item;
}
