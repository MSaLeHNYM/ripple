#pragma once
#include "broadcasts.h"

// Validation schemas (socketify::validate)
inline const sv::Schema& schema_register() {
    static const sv::Schema s = {
        sv::field("username").required().string().min(3).max(64),
        sv::field("display_name").required().string().min(1).max(128),
        sv::field("password").required().string().min(6).max(128),
    };
    return s;
}
inline const sv::Schema& schema_login() {
    static const sv::Schema s = {
        sv::field("username").required().string().min(1),
        sv::field("password").required().string().min(1),
    };
    return s;
}
inline const sv::Schema& schema_dm() {
    static const sv::Schema s = {
        sv::field("user_id").required().integer().min(1),
    };
    return s;
}
inline const sv::Schema& schema_group() {
    static const sv::Schema s = {
        sv::field("title").required().string().min(1).max(128),
        sv::field("member_ids").required().array(),
    };
    return s;
}
inline const sv::Schema& schema_message() {
    static const sv::Schema s = {
        sv::field("body").required().string().min(1).max(8000),
        sv::field("client_id").optional().string(),
        sv::field("kind").optional().string().one_of({"text", "image", "voice"}),
        sv::field("media_url").optional().string(),
    };
    return s;
}
inline const sv::Schema& schema_typing() {
    static const sv::Schema s = {
        sv::field("typing").required().boolean(),
    };
    return s;
}
inline const sv::Schema& schema_read() {
    static const sv::Schema s = {
        sv::field("last_message_id").required().integer().min(1),
    };
    return s;
}

inline void register_routes(Server& server, Database& db,
                             pulse::Hub& hub, Presence& presence) {
    const auto guard = require_auth();

    server.Post("/api/register", [&](Request& req, Response& res) {
        auto doc = parse_and_validate(req, res, schema_register());
        if (!doc) return;
        const auto username     = sj::require<std::string>(*doc, "username");
        const auto display_name = sj::require<std::string>(*doc, "display_name");
        const auto password     = sj::require<std::string>(*doc, "password");
        if (User::query(db).where_eq("username", username).exists()) {
            json_error(res, Status::Conflict, "username taken");
            return;
        }
        const std::string ts = now_iso();
        auto user = User::create(db, {{"username", username}, {"display_name", display_name},
                                      {"password_hash", hash_password(password)},
                                      {"created_at", ts}, {"last_seen", ts}});
        const auto sess = sessions::get(req);
        sess->regenerate();
        sess->set("user_id", user->id());
        res.status(Status::Created).json(Json{{"user", user_public(db, presence, user->attrs())}});
    });

    server.Post("/api/login", [&](Request& req, Response& res) {
        auto doc = parse_and_validate(req, res, schema_login());
        if (!doc) return;
        const auto username = sj::require<std::string>(*doc, "username");
        const auto password = sj::require<std::string>(*doc, "password");
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
        res.json(Json{{"user", user_public(db, presence, *row)}});
    });

    server.Post("/api/logout", [&](Request& req, Response& res) {
        if (const auto sess = sessions::get(req)) sess->destroy();
        res.json(Json{{"ok", true}});
    }).Use(guard);

    server.Get("/api/me", [&](Request& req, Response& res) {
        const auto id = uid(req);
        if (!id) { json_error(res, Status::Unauthorized, "login required"); return; }
        const auto row = user_row(db, *id);
        if (!row) { json_error(res, Status::Unauthorized, "user not found"); return; }
        res.json(Json{{"user", user_public(db, presence, *row)}});
    }).Use(guard);

    server.Get("/api/users", [&](Request& req, Response& res) {
        const std::string q(req.query_value("q"));
        auto qry = User::query(db);
        if (!q.empty()) {
            const std::string pattern = "%" + q + "%";
            qry.where("username", WhereOp::Like, pattern).or_where("display_name", WhereOp::Like, pattern);
        }
        qry.limit(30).order_by("username", true);
        Json users = Json::array();
        for (const auto& row : qry.get()) {
            if (row.at("id").get<std::int64_t>() == *uid(req)) continue;
            users.push_back(user_public(db, presence, row));
        }
        res.json(Json{{"users", users}});
    }).Use(guard);

    server.Get("/api/chats", [&](Request& req, Response& res) {
        const auto me = *uid(req);
        Json chats = Json::array();
        for (const auto& m : ChatMember::query(db).where_eq("user_id", me).get()) {
            const auto chat_id  = m.at("chat_id").get<std::int64_t>();
            const auto chat_row = Chat::query(db).where_eq("id", chat_id).limit(1).first();
            if (!chat_row) continue;
            chats.push_back(chat_json(db, presence, *chat_row, me));
        }
        res.json(Json{{"chats", chats}});
    }).Use(guard);

    server.Post("/api/chats/dm", [&](Request& req, Response& res) {
        auto doc = parse_and_validate(req, res, schema_dm());
        if (!doc) return;
        const auto me = *uid(req);
        const auto peer_id = sj::require<std::int64_t>(*doc, "user_id");
        if (peer_id == me) { json_error(res, Status::UnprocessableEntity, "cannot dm yourself"); return; }
        if (!user_row(db, peer_id)) { json_error(res, Status::NotFound, "user not found"); return; }

        std::int64_t chat_id; bool created = false;
        if (const auto existing = find_dm_chat(db, me, peer_id)) {
            chat_id = *existing;
        } else {
            auto chat = Chat::create(db, {{"kind", "dm"}, {"title", nullptr}, {"created_at", now_iso()}});
            chat_id = chat->id();
            ChatMember::create(db, {{"chat_id", chat_id}, {"user_id", me}});
            ChatMember::create(db, {{"chat_id", chat_id}, {"user_id", peer_id}});
            created = true;
        }
        const auto chat_row = Chat::query(db).where_eq("id", chat_id).limit(1).first();
        if (created) notify_chat_created(hub, db, chat_id);
        res.json(Json{{"chat", chat_json(db, presence, *chat_row, me)}});
    }).Use(guard);

    server.Post("/api/chats/group", [&](Request& req, Response& res) {
        auto doc = parse_and_validate(req, res, schema_group());
        if (!doc) return;
        const auto me = *uid(req);
        const auto title = sj::require<std::string>(*doc, "title");
        auto chat = Chat::create(db, {{"kind", "group"}, {"title", title}, {"created_at", now_iso()}});
        const auto chat_id = chat->id();
        ChatMember::create(db, {{"chat_id", chat_id}, {"user_id", me}});
        std::unordered_set<std::int64_t> added{me};
        if ((*doc)["member_ids"].is_array()) {
            for (const auto& mid : (*doc)["member_ids"]) {
                if (!mid.is_number_integer()) continue;
                const auto member_id = mid.get<std::int64_t>();
                if (added.count(member_id) || !user_row(db, member_id)) continue;
                ChatMember::create(db, {{"chat_id", chat_id}, {"user_id", member_id}});
                added.insert(member_id);
            }
        }
        const auto chat_row = Chat::query(db).where_eq("id", chat_id).limit(1).first();
        notify_chat_created(hub, db, chat_id);
        res.status(Status::Created).json(Json{{"chat", chat_json(db, presence, *chat_row, me)}});
    }).Use(guard);

    server.Get("/api/chats/:id/messages", [&](Request& req, Response& res) {
        const auto me      = *uid(req);
        const auto chat_id = std::stoll(std::string(req.params().at("id")));
        if (!is_member(db, chat_id, me)) { json_error(res, Status::Forbidden, "forbidden"); return; }
        int limit = 50;
        const std::string lim = std::string(req.query_value("limit"));
        if (!lim.empty()) { try { limit = std::stoi(lim); } catch (...) {} limit = std::clamp(limit, 1, 200); }
        auto rows = Message::query(db).where_eq("chat_id", chat_id).order_by("id", false).limit(limit).get();
        std::reverse(rows.begin(), rows.end());
        Json messages = Json::array();
        for (const auto& row : rows) messages.push_back(message_json(db, presence, row, me));
        res.json(Json{{"messages", messages}});
    }).Use(guard);

    server.Post("/api/chats/:id/messages", [&](Request& req, Response& res) {
        auto doc = parse_and_validate(req, res, schema_message());
        if (!doc) return;
        const auto me      = *uid(req);
        const auto chat_id = std::stoll(std::string(req.params().at("id")));
        if (!is_member(db, chat_id, me)) { json_error(res, Status::Forbidden, "forbidden"); return; }
        auto text = sj::require<std::string>(*doc, "body");
        std::optional<std::string> client_id = sj::get<std::string>(*doc, "client_id");
        const auto kind      = sj::get_or<std::string>(*doc, "kind", "text");
        const auto media_url = sj::get_or<std::string>(*doc, "media_url", "");
        touch_last_seen(db, me);
        Json out = save_and_broadcast_message(db, hub, presence, chat_id, me, std::move(text),
                                              client_id, kind, media_url);
        res.status(Status::Created).json(Json{{"message", out}});
    }).Use(guard);

    server.Post("/api/upload", [&](Request& req, Response& res) {
        if (!uid(req)) { json_error(res, Status::Unauthorized, "login required"); return; }
        const std::string ct  = std::string(req.header("content-type"));
        const std::string ext = (ct.find("audio") != std::string::npos) ? ".ogg"
                              : (ct.find("png")   != std::string::npos) ? ".png"
                              : (ct.find("gif")   != std::string::npos) ? ".gif"
                              :                                            ".jpg";
        const std::string fname  = detail::random_token(16) + ext;
        const fs::path dir = fs::path(find_web_root()) / "uploads";
        fs::create_directories(dir);
        const fs::path path = dir / fname;
        const auto raw = req.body_view();
        if (raw.empty()) { json_error(res, Status::UnprocessableEntity, "empty body"); return; }
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        if (!f) { json_error(res, Status::InternalServerError, "could not write file"); return; }
        std::fwrite(raw.data(), 1, raw.size(), f);
        std::fclose(f);
        res.status(Status::Created).json(Json{{"url", "/uploads/" + fname}});
    }).Use(guard);

    server.Post("/api/chats/:id/typing", [&](Request& req, Response& res) {
        auto doc = parse_and_validate(req, res, schema_typing());
        if (!doc) return;
        const auto me      = *uid(req);
        const auto chat_id = std::stoll(std::string(req.params().at("id")));
        if (!is_member(db, chat_id, me)) { json_error(res, Status::Forbidden, "forbidden"); return; }
        broadcast_typing(hub, db, chat_id, me, sj::require<bool>(*doc, "typing"));
        res.json(Json{{"ok", true}});
    }).Use(guard);

    server.Post("/api/chats/:id/read", [&](Request& req, Response& res) {
        auto doc = parse_and_validate(req, res, schema_read());
        if (!doc) return;
        const auto me      = *uid(req);
        const auto chat_id = std::stoll(std::string(req.params().at("id")));
        if (!is_member(db, chat_id, me)) { json_error(res, Status::Forbidden, "forbidden"); return; }
        mark_chat_read(db, hub, chat_id, me, sj::require<std::int64_t>(*doc, "last_message_id"));
        res.json(Json{{"ok", true}});
    }).Use(guard);
}
