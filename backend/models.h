#pragma once
#include <socketify/db.h>
#include <socketify/socketify.h>

using namespace socketify;
using namespace socketify::db;

// Json = nlohmann value type; sj = socketify::json_util helpers.
using Json = nlohmann::json;
namespace sj = socketify::json_util;
namespace sv = socketify::validate;

// ---------------------------------------------------------------------------
// ORM Models
// ---------------------------------------------------------------------------

struct User : Model<User> {
    static constexpr std::string_view table = "users";
    static Schema schema() {
        return Schema::create(table)
            .integer("id").primary().autoincrement()
            .text("username").unique().not_null()
            .text("display_name").not_null()
            .text("password_hash").not_null()
            .text("created_at")
            .text("last_seen");
    }
};

struct Chat : Model<Chat> {
    static constexpr std::string_view table = "chats";
    static Schema schema() {
        return Schema::create(table)
            .integer("id").primary().autoincrement()
            .text("kind").not_null()
            .text("title")
            .text("created_at");
    }
};

struct ChatMember : Model<ChatMember> {
    static constexpr std::string_view table = "chat_members";
    static Schema schema() {
        return Schema::create(table)
            .integer("id").primary().autoincrement()
            .integer("chat_id").not_null()
            .integer("user_id").not_null()
            .integer("last_read_message_id")
            .text("last_read_at")
            .foreign_key("chat_id", "chats", "id", true)
            .foreign_key("user_id", "users", "id", true)
            .index("idx_chat_members_unique", {"chat_id", "user_id"}, true);
    }
};

struct Message : Model<Message> {
    static constexpr std::string_view table = "messages";
    static Schema schema() {
        return Schema::create(table)
            .integer("id").primary().autoincrement()
            .integer("chat_id").not_null()
            .integer("sender_id").not_null()
            .text("body").not_null()
            .text("kind")
            .text("media_url")
            .text("created_at")
            .foreign_key("chat_id", "chats", "id", true)
            .foreign_key("sender_id", "users", "id", true)
            .index("idx_messages_chat", {"chat_id"});
    }
};

struct MessageReceipt : Model<MessageReceipt> {
    static constexpr std::string_view table = "message_receipts";
    static Schema schema() {
        return Schema::create(table)
            .integer("id").primary().autoincrement()
            .integer("message_id").not_null()
            .integer("user_id").not_null()
            .text("status").not_null()
            .text("updated_at")
            .foreign_key("message_id", "messages", "id", true)
            .foreign_key("user_id", "users", "id", true)
            .index("idx_message_receipts_unique", {"message_id", "user_id"}, true);
    }
};

inline void migrate_all(Database& db) {
    User::migrate_schema(db);
    Chat::migrate_schema(db);
    ChatMember::migrate_schema(db);
    Message::migrate_schema(db);
    MessageReceipt::migrate_schema(db);
}
