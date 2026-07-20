// Ripple — Telegram-like messenger backend (Pulse + sessions + ORM + static SPA).

#include <socketify/db.h>
#include <socketify/socketify.h>
#include <socketify/detail/utils.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace socketify;
using namespace socketify::db;
using json = nlohmann::json;

namespace {

// ---------------------------------------------------------------------------
// Models
// ---------------------------------------------------------------------------

struct User : Model<User> {
    static constexpr std::string_view table = "users";
    static Schema schema() {
        return Schema::create(table)
            .integer("id")
            .primary()
            .autoincrement()
            .text("username")
            .unique()
            .not_null()
            .text("display_name")
            .not_null()
            .text("password_hash")
            .not_null()
            .text("created_at")
            .text("last_seen");
    }
};

struct Chat : Model<Chat> {
    static constexpr std::string_view table = "chats";
    static Schema schema() {
        return Schema::create(table)
            .integer("id")
            .primary()
            .autoincrement()
            .text("kind")
            .not_null()
            .text("title")
            .text("created_at");
    }
};

struct ChatMember : Model<ChatMember> {
    static constexpr std::string_view table = "chat_members";
    static Schema schema() {
        return Schema::create(table)
            .integer("id")
            .primary()
            .autoincrement()
            .integer("chat_id")
            .not_null()
            .integer("user_id")
            .not_null()
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
            .integer("id")
            .primary()
            .autoincrement()
            .integer("chat_id")
            .not_null()
            .integer("sender_id")
            .not_null()
            .text("body")
            .not_null()
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
            .integer("id")
            .primary()
            .autoincrement()
            .integer("message_id")
            .not_null()
            .integer("user_id")
            .not_null()
            .text("status")
            .not_null()
            .text("updated_at")
            .foreign_key("message_id", "messages", "id", true)
            .foreign_key("user_id", "users", "id", true)
            .index("idx_message_receipts_unique", {"message_id", "user_id"}, true);
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string now_iso() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long>(ms.count()));
    return buf;
}

std::string bytes_to_hex(const std::array<std::uint8_t, 32>& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(64, '\0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        out[i * 2] = kHex[digest[i] >> 4];
        out[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return out;
}

std::string hash_password(std::string_view password) {
    const std::string salt = detail::random_token(16);
    const auto digest = detail::sha256(salt + ":" + std::string(password));
    return salt + "$" + bytes_to_hex(digest);
}

bool verify_password(std::string_view stored, std::string_view password) {
    const auto pos = stored.find('$');
    if (pos == std::string_view::npos) return false;
    const std::string salt(stored.substr(0, pos));
    const std::string expected(stored.substr(pos + 1));
    const auto digest = detail::sha256(salt + ":" + std::string(password));
    const std::string got = bytes_to_hex(digest);
    return detail::constant_time_equal(got, expected);
}

std::optional<std::int64_t> uid(const Request& req) {
    const auto sess = sessions::get(req);
    if (!sess || !sess->has("user_id")) return std::nullopt;
    return sess->get("user_id").get<std::int64_t>();
}

Middleware require_auth() {
    return [](Request& req, Response& res, Next next) {
        if (!uid(req)) {
            res.status(Status::Unauthorized).json(json{{"error", "login required"}});
            return;
        }
        next();
    };
}

void ensure_column(Database& db, const char* table, const char* column, const char* decl) {
    const auto rows = db.query(std::string("PRAGMA table_info(") + table + ")");
    for (const auto& row : rows) {
        if (row.at("name").get<std::string>() == column) return;
    }
    db.exec(std::string("ALTER TABLE ") + table + " ADD COLUMN " + column + " " + decl);
}

class Presence {
public:
    void track(std::int64_t user_id, pulse::Channel ch) {
        void* key = ch.impl().get();
        std::lock_guard<std::mutex> lk(mu_);
        channel_user_[key] = user_id;
        user_channels_[user_id].insert(key);
    }

    void untrack(pulse::Channel ch) {
        void* key = ch.impl().get();
        std::lock_guard<std::mutex> lk(mu_);
        auto it = channel_user_.find(key);
        if (it == channel_user_.end()) return;
        const auto user_id = it->second;
        channel_user_.erase(it);
        auto uit = user_channels_.find(user_id);
        if (uit != user_channels_.end()) {
            uit->second.erase(key);
            if (uit->second.empty()) user_channels_.erase(uit);
        }
    }

    std::optional<std::int64_t> user_of(const pulse::Channel& ch) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = channel_user_.find(ch.impl().get());
        if (it == channel_user_.end()) return std::nullopt;
        return it->second;
    }

    bool online(std::int64_t user_id) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = user_channels_.find(user_id);
        return it != user_channels_.end() && !it->second.empty();
    }

    json online_user_ids() const {
        std::lock_guard<std::mutex> lk(mu_);
        json ids = json::array();
        for (const auto& [uid, chs] : user_channels_) {
            if (!chs.empty()) ids.push_back(uid);
        }
        return ids;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<void*, std::int64_t> channel_user_;
    std::unordered_map<std::int64_t, std::unordered_set<void*>> user_channels_;
};

json user_public(Database& db, const Presence& pres, const Row& row) {
    const auto id = row.at("id").get<std::int64_t>();
    return json{{"id", id},
                {"username", row.at("username")},
                {"display_name", row.at("display_name")},
                {"created_at", row.value("created_at", "")},
                {"last_seen", row.value("last_seen", "")},
                {"online", pres.online(id)}};
}

std::optional<Row> user_row(Database& db, std::int64_t id) {
    return User::query(db).where_eq("id", id).limit(1).first();
}

void touch_last_seen(Database& db, std::int64_t user_id) {
    User::query(db).where_eq("id", user_id).update({{"last_seen", now_iso()}});
}

std::vector<std::int64_t> chat_member_ids(Database& db, std::int64_t chat_id) {
    std::vector<std::int64_t> out;
    for (const auto& row : ChatMember::query(db).where_eq("chat_id", chat_id).get())
        out.push_back(row.at("user_id").get<std::int64_t>());
    return out;
}

bool is_member(Database& db, std::int64_t chat_id, std::int64_t user_id) {
    return ChatMember::query(db)
        .where_eq("chat_id", chat_id)
        .where_eq("user_id", user_id)
        .exists();
}

std::optional<std::int64_t> find_dm_chat(Database& db, std::int64_t a, std::int64_t b) {
    const auto rows = db.query(
        "SELECT c.id FROM chats c "
        "INNER JOIN chat_members m1 ON m1.chat_id = c.id AND m1.user_id = ? "
        "INNER JOIN chat_members m2 ON m2.chat_id = c.id AND m2.user_id = ? "
        "WHERE c.kind = 'dm' LIMIT 1",
        {a, b});
    if (rows.empty()) return std::nullopt;
    return rows.front().at("id").get<std::int64_t>();
}

int receipt_rank(const std::string& status) {
    if (status == "seen") return 3;
    if (status == "delivered") return 2;
    if (status == "sent") return 1;
    return 0;
}

std::string aggregate_receipt_status(Database& db, std::int64_t message_id,
                                     std::int64_t sender_id, std::int64_t chat_id) {
    const auto members = chat_member_ids(db, chat_id);
    int worst = 3;
    bool any = false;
    for (const auto mid : members) {
        if (mid == sender_id) continue;
        any = true;
        const auto row = MessageReceipt::query(db)
                             .where_eq("message_id", message_id)
                             .where_eq("user_id", mid)
                             .limit(1)
                             .first();
        const int rank = row ? receipt_rank(row->at("status").get<std::string>()) : 0;
        if (rank < worst) worst = rank;
    }
    if (!any) return "sent";
    if (worst >= 3) return "seen";
    if (worst >= 2) return "delivered";
    return "sent";
}

void upsert_receipt(Database& db, std::int64_t message_id, std::int64_t user_id,
                    const std::string& status) {
    const auto existing = MessageReceipt::query(db)
                              .where_eq("message_id", message_id)
                              .where_eq("user_id", user_id)
                              .limit(1)
                              .first();
    const std::string ts = now_iso();
    if (existing) {
        const auto cur = existing->at("status").get<std::string>();
        if (receipt_rank(status) <= receipt_rank(cur)) return;
        MessageReceipt::query(db)
            .where_eq("id", existing->at("id").get<std::int64_t>())
            .update({{"status", status}, {"updated_at", ts}});
    } else {
        MessageReceipt::create(db, {{"message_id", message_id},
                                    {"user_id", user_id},
                                    {"status", status},
                                    {"updated_at", ts}});
    }
}

json message_json(Database& db, const Presence& pres, const Row& row,
                  std::optional<std::int64_t> viewer_id = std::nullopt) {
    const auto sender_id = row.at("sender_id").get<std::int64_t>();
    const auto message_id = row.at("id").get<std::int64_t>();
    const auto chat_id = row.at("chat_id").get<std::int64_t>();
    json j{{"id", message_id},
           {"chat_id", chat_id},
           {"sender_id", sender_id},
           {"body", row.at("body")},
           {"created_at", row.value("created_at", "")}};
    if (auto u = user_row(db, sender_id)) j["sender"] = user_public(db, pres, *u);
    if (viewer_id && *viewer_id == sender_id) {
        j["receipt_status"] = aggregate_receipt_status(db, message_id, sender_id, chat_id);
    }
    return j;
}

std::int64_t unread_count_for(Database& db, std::int64_t chat_id, std::int64_t user_id,
                              const Row& member_row) {
    std::int64_t last_read = 0;
    if (member_row.contains("last_read_message_id") &&
        !member_row.at("last_read_message_id").is_null()) {
        last_read = member_row.at("last_read_message_id").get<std::int64_t>();
    }
    const auto rows = db.query(
        "SELECT COUNT(*) AS c FROM messages WHERE chat_id = ? AND id > ? AND sender_id != ?",
        {chat_id, last_read, user_id});
    if (rows.empty()) return 0;
    return rows.front().at("c").get<std::int64_t>();
}

json chat_json(Database& db, const Presence& pres, const Row& chat_row,
               std::int64_t current_user_id) {
    const auto chat_id = chat_row.at("id").get<std::int64_t>();
    const std::string kind = chat_row.at("kind").get<std::string>();
    json item{{"id", chat_id},
              {"kind", kind},
              {"type", kind},
              {"is_group", kind == "group"},
              {"title", chat_row.contains("title") && !chat_row.at("title").is_null()
                            ? chat_row.at("title")
                            : json(nullptr)},
              {"created_at", chat_row.value("created_at", "")}};

    const auto msgs =
        Message::query(db).where_eq("chat_id", chat_id).order_by("id", false).limit(1).get();
    item["last_message"] =
        msgs.empty() ? json(nullptr) : message_json(db, pres, msgs.front(), current_user_id);

    json members = json::array();
    std::optional<Row> my_membership;
    for (const auto& m : ChatMember::query(db).where_eq("chat_id", chat_id).get()) {
        const auto mid = m.at("user_id").get<std::int64_t>();
        if (mid == current_user_id) my_membership = m;
        if (auto u = user_row(db, mid)) members.push_back(user_public(db, pres, *u));
        if (kind == "dm" && mid != current_user_id) {
            if (auto u = user_row(db, mid)) item["peer"] = user_public(db, pres, *u);
        }
    }
    item["members"] = members;
    item["unread_count"] =
        my_membership ? unread_count_for(db, chat_id, current_user_id, *my_membership) : 0;
    return item;
}

void broadcast_presence(pulse::Hub& hub, const Presence& pres, Database& db,
                        std::int64_t user_id, bool online_flag) {
    touch_last_seen(db, user_id);
    const json evt{{"type", "presence"},
                   {"user_id", user_id},
                   {"online", online_flag},
                   {"last_seen", now_iso()}};
    hub.broadcast_text(evt.dump());
    (void)pres;
}

void broadcast_to_members(pulse::Hub& hub, Database& db, std::int64_t chat_id, const json& msg,
                          std::int64_t exclude_user_id = 0) {
    const std::string payload = msg.dump();
    for (const auto member_id : chat_member_ids(db, chat_id)) {
        if (member_id == exclude_user_id) continue;
        hub.to("user:" + std::to_string(member_id)).broadcast_text(payload);
    }
}

void notify_chat_created(pulse::Hub& hub, Database& db, const Presence& /*pres*/,
                         std::int64_t chat_id, std::int64_t creator_id) {
    const json evt{{"type", "chat"}, {"chat_id", chat_id}};
    broadcast_to_members(hub, db, chat_id, evt, 0);
    (void)creator_id;
}

std::shared_ptr<Message> persist_message(Database& db, std::int64_t chat_id,
                                         std::int64_t sender_id, std::string body) {
    return Message::create(db, {{"chat_id", chat_id},
                                {"sender_id", sender_id},
                                {"body", std::move(body)},
                                {"created_at", now_iso()}});
}

void mark_delivered_for_online(Database& db, pulse::Hub& hub, const Presence& pres,
                               std::int64_t chat_id, std::int64_t message_id,
                               std::int64_t sender_id) {
    for (const auto member_id : chat_member_ids(db, chat_id)) {
        if (member_id == sender_id) continue;
        if (!pres.online(member_id)) continue;
        upsert_receipt(db, message_id, member_id, "delivered");
        const json evt{{"type", "receipt"},
                       {"message_id", message_id},
                       {"chat_id", chat_id},
                       {"user_id", member_id},
                       {"status", "delivered"}};
        hub.to("user:" + std::to_string(sender_id)).broadcast_text(evt.dump());
    }
}

json save_and_broadcast_message(Database& db, pulse::Hub& hub, const Presence& pres,
                                std::int64_t chat_id, std::int64_t sender_id, std::string body,
                                std::optional<std::string> client_id = std::nullopt) {
    const auto rec = persist_message(db, chat_id, sender_id, std::move(body));
    json out = message_json(db, pres, rec->attrs(), sender_id);
    out["type"] = "message";
    out["receipt_status"] = "sent";
    if (client_id) out["client_id"] = *client_id;

    // Echo to everyone including sender (for optimistic reconcile).
    broadcast_to_members(hub, db, chat_id, out, 0);
    mark_delivered_for_online(db, hub, pres, chat_id, rec->id(), sender_id);
    return out;
}

void broadcast_typing(pulse::Hub& hub, Database& db, std::int64_t chat_id,
                      std::int64_t user_id, bool typing) {
    std::string display_name = "Someone";
    if (auto u = user_row(db, user_id)) {
        display_name = u->value("display_name", u->value("username", "Someone"));
    }
    const json evt{{"type", "typing"},
                   {"chat_id", chat_id},
                   {"user_id", user_id},
                   {"display_name", display_name},
                   {"typing", typing}};
    broadcast_to_members(hub, db, chat_id, evt, user_id);
}

void mark_chat_read(Database& db, pulse::Hub& hub, std::int64_t chat_id, std::int64_t user_id,
                    std::int64_t last_message_id) {
    if (last_message_id <= 0) return;
    const auto member = ChatMember::query(db)
                            .where_eq("chat_id", chat_id)
                            .where_eq("user_id", user_id)
                            .limit(1)
                            .first();
    if (!member) return;

    std::int64_t prev = 0;
    if (member->contains("last_read_message_id") &&
        !member->at("last_read_message_id").is_null()) {
        prev = member->at("last_read_message_id").get<std::int64_t>();
    }
    if (last_message_id < prev) last_message_id = prev;

    ChatMember::query(db)
        .where_eq("id", member->at("id").get<std::int64_t>())
        .update({{"last_read_message_id", last_message_id}, {"last_read_at", now_iso()}});

    const auto rows = db.query(
        "SELECT id, sender_id FROM messages WHERE chat_id = ? AND id <= ? AND sender_id != ?",
        {chat_id, last_message_id, user_id});

    std::unordered_set<std::int64_t> notify_senders;
    for (const auto& row : rows) {
        const auto mid = row.at("id").get<std::int64_t>();
        const auto sid = row.at("sender_id").get<std::int64_t>();
        upsert_receipt(db, mid, user_id, "seen");
        notify_senders.insert(sid);
    }

    const json read_evt{{"type", "read"},
                        {"chat_id", chat_id},
                        {"user_id", user_id},
                        {"last_message_id", last_message_id}};
    broadcast_to_members(hub, db, chat_id, read_evt, user_id);

    for (const auto sid : notify_senders) {
        const json receipt_evt{{"type", "receipt"},
                               {"chat_id", chat_id},
                               {"user_id", user_id},
                               {"status", "seen"},
                               {"up_to_message_id", last_message_id}};
        hub.to("user:" + std::to_string(sid)).broadcast_text(receipt_evt.dump());
    }
}

std::string exe_dir() {
    std::error_code ec;
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path().string();
    return fs::current_path().string();
}

/** Prefer absolute web/ next to the binary so cwd / stale processes cannot confuse us. */
std::string find_web_root() {
    const fs::path base = exe_dir();
    for (const fs::path& cand : {base / "web", base / "frontend" / "dist",
                                 fs::path("web"), fs::path("frontend") / "dist"}) {
        if (fs::exists(cand / "index.html")) return fs::absolute(cand).string();
    }
    return fs::absolute(base / "web").string();
}

void json_error(Response& res, Status code, std::string_view msg) {
    res.status(code).json(json{{"error", std::string(msg)}});
}

} // namespace

int main() {
    fs::create_directories("data");

    auto db = Database::open(Sqlite{.path = "data/ripple.db", .pool_size = 4});
    User::migrate_schema(db);
    Chat::migrate_schema(db);
    ChatMember::migrate_schema(db);
    Message::migrate_schema(db);
    MessageReceipt::migrate_schema(db);
    ensure_column(db, "chat_members", "last_read_message_id", "INTEGER");
    ensure_column(db, "chat_members", "last_read_at", "TEXT");

    pulse::Hub hub;
    Presence presence;
    const auto guard = require_auth();

    Server server;

    cors::CorsOptions cors_opts;
    cors_opts.reflect_origin = true;
    cors_opts.allow_credentials = true;
    cors_opts.allow_headers = "Content-Type";
    cors_opts.allow_methods = "GET,POST,PUT,PATCH,DELETE,OPTIONS";
    server.Use(cors::middleware(cors_opts));

    sessions::Options so;
    so.secret = "ripple-dev-secret-change-in-prod-32b";
    so.rolling = true;
    server.Use(sessions::middleware(so));

    // ---- Auth (public) ----
    server.Post("/api/register", [&](Request& req, Response& res) {
        const auto j = body::json(req);
        if (!j || !j->contains("username") || !j->contains("display_name") ||
            !j->contains("password")) {
            json_error(res, Status::UnprocessableEntity, "need username, display_name, password");
            return;
        }
        const std::string username = (*j)["username"].get<std::string>();
        const std::string display_name = (*j)["display_name"].get<std::string>();
        const std::string password = (*j)["password"].get<std::string>();
        if (username.empty() || display_name.empty() || password.empty()) {
            json_error(res, Status::UnprocessableEntity, "fields cannot be empty");
            return;
        }
        if (User::query(db).where_eq("username", username).exists()) {
            json_error(res, Status::Conflict, "username taken");
            return;
        }
        const std::string ts = now_iso();
        auto user = User::create(db, {{"username", username},
                                      {"display_name", display_name},
                                      {"password_hash", hash_password(password)},
                                      {"created_at", ts},
                                      {"last_seen", ts}});
        const auto sess = sessions::get(req);
        sess->regenerate();
        sess->set("user_id", user->id());
        res.status(Status::Created).json(json{{"user", user_public(db, presence, user->attrs())}});
    });

    server.Post("/api/login", [&](Request& req, Response& res) {
        const auto j = body::json(req);
        if (!j || !j->contains("username") || !j->contains("password")) {
            json_error(res, Status::UnprocessableEntity, "need username and password");
            return;
        }
        const std::string username = (*j)["username"].get<std::string>();
        const std::string password = (*j)["password"].get<std::string>();
        const auto row = User::query(db).where_eq("username", username).limit(1).first();
        if (!row || !verify_password(row->at("password_hash").get<std::string>(), password)) {
            json_error(res, Status::Unauthorized, "invalid credentials");
            return;
        }
        const auto user_id = row->at("id").get<std::int64_t>();
        touch_last_seen(db, user_id);
        const auto sess = sessions::get(req);
        sess->regenerate();
        sess->set("user_id", user_id);
        res.json(json{{"user", user_public(db, presence, *row)}});
    });

    server.Post("/api/logout", [&](Request& req, Response& res) {
        if (const auto sess = sessions::get(req)) sess->destroy();
        res.json(json{{"ok", true}});
    }).Use(guard);

    server.Get("/api/me", [&](Request& req, Response& res) {
        const auto id = uid(req);
        if (!id) {
            json_error(res, Status::Unauthorized, "login required");
            return;
        }
        const auto row = user_row(db, *id);
        if (!row) {
            json_error(res, Status::Unauthorized, "user not found");
            return;
        }
        res.json(json{{"user", user_public(db, presence, *row)}});
    }).Use(guard);

    server.Get("/api/users", [&](Request& req, Response& res) {
        const std::string q(req.query_value("q"));
        auto qry = User::query(db);
        if (!q.empty()) {
            const std::string pattern = "%" + q + "%";
            qry.where("username", WhereOp::Like, pattern)
                .or_where("display_name", WhereOp::Like, pattern);
        }
        qry.limit(30).order_by("username", true);
        json users = json::array();
        for (const auto& row : qry.get()) {
            const auto id = row.at("id").get<std::int64_t>();
            if (id == *uid(req)) continue;
            users.push_back(user_public(db, presence, row));
        }
        res.json(json{{"users", users}});
    }).Use(guard);

    server.Get("/api/chats", [&](Request& req, Response& res) {
        const auto me = *uid(req);
        const auto memberships = ChatMember::query(db).where_eq("user_id", me).get();
        json chats = json::array();
        for (const auto& m : memberships) {
            const auto chat_id = m.at("chat_id").get<std::int64_t>();
            const auto chat_row = Chat::query(db).where_eq("id", chat_id).limit(1).first();
            if (!chat_row) continue;
            chats.push_back(chat_json(db, presence, *chat_row, me));
        }
        res.json(json{{"chats", chats}});
    }).Use(guard);

    server.Post("/api/chats/dm", [&](Request& req, Response& res) {
        const auto me = *uid(req);
        const auto j = body::json(req);
        if (!j || !j->contains("user_id")) {
            json_error(res, Status::UnprocessableEntity, "need user_id");
            return;
        }
        const auto peer_id = (*j)["user_id"].get<std::int64_t>();
        if (peer_id == me) {
            json_error(res, Status::UnprocessableEntity, "cannot dm yourself");
            return;
        }
        if (!user_row(db, peer_id)) {
            json_error(res, Status::NotFound, "user not found");
            return;
        }
        std::int64_t chat_id;
        bool created = false;
        if (const auto existing = find_dm_chat(db, me, peer_id)) {
            chat_id = *existing;
        } else {
            const std::string ts = now_iso();
            auto chat = Chat::create(db, {{"kind", "dm"}, {"title", nullptr}, {"created_at", ts}});
            chat_id = chat->id();
            ChatMember::create(db, {{"chat_id", chat_id}, {"user_id", me}});
            ChatMember::create(db, {{"chat_id", chat_id}, {"user_id", peer_id}});
            created = true;
        }
        const auto chat_row = Chat::query(db).where_eq("id", chat_id).limit(1).first();
        if (created) notify_chat_created(hub, db, presence, chat_id, me);
        res.json(json{{"chat", chat_json(db, presence, *chat_row, me)}});
    }).Use(guard);

    server.Post("/api/chats/group", [&](Request& req, Response& res) {
        const auto me = *uid(req);
        const auto j = body::json(req);
        if (!j || !j->contains("title") || !j->contains("member_ids")) {
            json_error(res, Status::UnprocessableEntity, "need title and member_ids");
            return;
        }
        const std::string title = (*j)["title"].get<std::string>();
        if (title.empty()) {
            json_error(res, Status::UnprocessableEntity, "title cannot be empty");
            return;
        }
        if (!(*j)["member_ids"].is_array()) {
            json_error(res, Status::UnprocessableEntity, "member_ids must be an array");
            return;
        }
        const std::string ts = now_iso();
        auto chat = Chat::create(db, {{"kind", "group"}, {"title", title}, {"created_at", ts}});
        const auto chat_id = chat->id();
        ChatMember::create(db, {{"chat_id", chat_id}, {"user_id", me}});
        std::unordered_set<std::int64_t> added{me};
        for (const auto& mid : (*j)["member_ids"]) {
            const auto member_id = mid.get<std::int64_t>();
            if (added.count(member_id)) continue;
            if (!user_row(db, member_id)) continue;
            ChatMember::create(db, {{"chat_id", chat_id}, {"user_id", member_id}});
            added.insert(member_id);
        }
        const auto chat_row = Chat::query(db).where_eq("id", chat_id).limit(1).first();
        notify_chat_created(hub, db, presence, chat_id, me);
        res.status(Status::Created).json(json{{"chat", chat_json(db, presence, *chat_row, me)}});
    }).Use(guard);

    server.Get("/api/chats/:id/messages", [&](Request& req, Response& res) {
        const auto me = *uid(req);
        const auto chat_id = std::stoll(std::string(req.params().at("id")));
        if (!is_member(db, chat_id, me)) {
            json_error(res, Status::Forbidden, "forbidden");
            return;
        }
        int limit = 50;
        const std::string lim = std::string(req.query_value("limit"));
        if (!lim.empty()) {
            try {
                limit = std::stoi(lim);
            } catch (...) {
                limit = 50;
            }
            if (limit < 1) limit = 1;
            if (limit > 200) limit = 200;
        }
        auto rows =
            Message::query(db).where_eq("chat_id", chat_id).order_by("id", false).limit(limit).get();
        std::reverse(rows.begin(), rows.end());
        json messages = json::array();
        for (const auto& row : rows) messages.push_back(message_json(db, presence, row, me));
        res.json(json{{"messages", messages}});
    }).Use(guard);

    server.Post("/api/chats/:id/messages", [&](Request& req, Response& res) {
        const auto me = *uid(req);
        const auto chat_id = std::stoll(std::string(req.params().at("id")));
        if (!is_member(db, chat_id, me)) {
            json_error(res, Status::Forbidden, "forbidden");
            return;
        }
        const auto j = body::json(req);
        if (!j || !j->contains("body")) {
            json_error(res, Status::UnprocessableEntity, "need body");
            return;
        }
        std::string text = (*j)["body"].get<std::string>();
        if (text.empty()) {
            json_error(res, Status::UnprocessableEntity, "body cannot be empty");
            return;
        }
        std::optional<std::string> client_id;
        if (j->contains("client_id") && (*j)["client_id"].is_string()) {
            client_id = (*j)["client_id"].get<std::string>();
        }
        touch_last_seen(db, me);
        json out = save_and_broadcast_message(db, hub, presence, chat_id, me, std::move(text),
                                              client_id);
        out.erase("type");
        res.status(Status::Created).json(json{{"message", out}});
    }).Use(guard);

    server.Post("/api/chats/:id/typing", [&](Request& req, Response& res) {
        const auto me = *uid(req);
        const auto chat_id = std::stoll(std::string(req.params().at("id")));
        if (!is_member(db, chat_id, me)) {
            json_error(res, Status::Forbidden, "forbidden");
            return;
        }
        const auto j = body::json(req);
        if (!j || !j->contains("typing")) {
            json_error(res, Status::UnprocessableEntity, "need typing");
            return;
        }
        const bool typing = (*j)["typing"].get<bool>();
        broadcast_typing(hub, db, chat_id, me, typing);
        res.json(json{{"ok", true}});
    }).Use(guard);

    server.Post("/api/chats/:id/read", [&](Request& req, Response& res) {
        const auto me = *uid(req);
        const auto chat_id = std::stoll(std::string(req.params().at("id")));
        if (!is_member(db, chat_id, me)) {
            json_error(res, Status::Forbidden, "forbidden");
            return;
        }
        const auto j = body::json(req);
        if (!j || !j->contains("last_message_id")) {
            json_error(res, Status::UnprocessableEntity, "need last_message_id");
            return;
        }
        const auto last_id = (*j)["last_message_id"].get<std::int64_t>();
        mark_chat_read(db, hub, chat_id, me, last_id);
        res.json(json{{"ok", true}});
    }).Use(guard);

    // ---- Pulse realtime ----
    server.Get("/pulse", [&](Request& req, Response& res) {
        const auto me = uid(req);
        if (!me) {
            json_error(res, Status::Unauthorized, "login required");
            return;
        }
        auto ch = pulse::upgrade(req, res);
        if (!ch.valid()) return;

        const std::string room = "user:" + std::to_string(*me);
        hub.join(room, ch);
        presence.track(*me, ch);
        touch_last_seen(db, *me);
        broadcast_presence(hub, presence, db, *me, true);

        ch.send_text(json{{"type", "connected"},
                          {"user_id", *me},
                          {"online_users", presence.online_user_ids()}}
                         .dump());

        ch.on_text([&](pulse::Channel& c, std::string_view raw) {
            const auto user_id = presence.user_of(c);
            if (!user_id) return;

            json j;
            try {
                j = json::parse(raw);
            } catch (...) {
                return;
            }
            if (!j.contains("type") || !j["type"].is_string()) return;
            const auto type = j["type"].get<std::string>();

            if (type == "hello") {
                c.send_text(json{{"type", "connected"},
                                 {"user_id", *user_id},
                                 {"online_users", presence.online_user_ids()}}
                                .dump());
                return;
            }

            if (type == "send") {
                if (!j.contains("chat_id") || !j.contains("body")) return;
                const auto chat_id = j["chat_id"].get<std::int64_t>();
                if (!is_member(db, chat_id, *user_id)) return;
                std::string text = j["body"].get<std::string>();
                if (text.empty()) return;
                std::optional<std::string> client_id;
                if (j.contains("client_id") && j["client_id"].is_string()) {
                    client_id = j["client_id"].get<std::string>();
                }
                touch_last_seen(db, *user_id);
                save_and_broadcast_message(db, hub, presence, chat_id, *user_id, std::move(text),
                                           client_id);
                return;
            }

            if (type == "typing") {
                if (!j.contains("chat_id") || !j.contains("typing")) return;
                const auto chat_id = j["chat_id"].get<std::int64_t>();
                if (!is_member(db, chat_id, *user_id)) return;
                broadcast_typing(hub, db, chat_id, *user_id, j["typing"].get<bool>());
                return;
            }

            if (type == "read") {
                if (!j.contains("chat_id") || !j.contains("last_message_id")) return;
                const auto chat_id = j["chat_id"].get<std::int64_t>();
                if (!is_member(db, chat_id, *user_id)) return;
                mark_chat_read(db, hub, chat_id, *user_id, j["last_message_id"].get<std::int64_t>());
            }
        });

        ch.on_close([&](pulse::Channel& c, pulse::CloseCode, std::string_view) {
            const auto user_id = presence.user_of(c);
            hub.leave_all(c);
            presence.untrack(c);
            if (user_id) broadcast_presence(hub, presence, db, *user_id, false);
        });
    });

    // ---- Static SPA ----
    const std::string web = find_web_root();
    if (!fs::exists(fs::path(web) / "index.html")) {
        std::fprintf(stderr,
                     "error: UI not found at %s/index.html\n"
                     "       run: ./run.sh   (builds frontend and stages web/)\n",
                     web.c_str());
        return 1;
    }
    // No long cache on HTML so rebuilds show up immediately.
    server.Use(static_files::serve(web, {.mount = "/", .fallthrough = true, .cache_max_age = 0}));

    server.Get("/*path", [web](Request& req, Response& res) {
        const auto path = std::string(req.path());
        if (path.rfind("/api", 0) == 0) {
            res.status(Status::NotFound).json(json{{"error", "not found"}});
            return;
        }
        if (!res.send_file(web + "/index.html")) {
            res.status(Status::NotFound)
                .html("<h1>Frontend not built</h1><p>Run: <code>./run.sh</code></p>");
        }
    });

    if (!server.Listen(8080)) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        std::fprintf(stderr, "hint: another process may own :8080 — ./run.sh frees it\n");
        return 1;
    }
    std::printf("Ripple on http://localhost:8080  (web=%s)\n", web.c_str());
    server.Wait();
    return 0;
}
