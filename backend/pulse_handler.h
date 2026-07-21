#pragma once
#include "broadcasts.h"
#include "presence.h"

/**
 * Pulse endpoint using pulse_easy (JSON envelopes) + pulse_media (binary voice/video).
 *
 * Text protocol:  {"type":"<event>","data":{...}}
 * Binary protocol: pulse_media PM frames relayed in call:<chat_id> rooms.
 */
inline void register_pulse(Server& server, Database& db, pulse::Hub& hub,
                            Presence& presence, CallRooms& call_rooms,
                            pulse_easy::App& easy, pulse_media::Hub& media) {
    server.Get("/pulse", [&](Request& req, Response& res) {
        const auto me = uid(req);
        if (!me) { json_error(res, Status::Unauthorized, "login required"); return; }

        auto ch = pulse::upgrade(req, res);
        if (!ch.valid()) return;

        hub.join("user:" + std::to_string(*me), ch);
        presence.track(*me, ch);
        touch_last_seen(db, *me);
        broadcast_presence(hub, presence, db, *me, true);

        // pulse_easy connection (JSON event dispatch)
        auto conn = easy.adopt(ch);

        auto wire_close = [&](pulse::Channel& channel) {
            channel.on_close([&](pulse::Channel& c, pulse::CloseCode, std::string_view) {
                const auto user_id = presence.user_of(c);
                call_rooms.clear(c);
                hub.leave_all(c);
                presence.untrack(c);
                if (user_id) broadcast_presence(hub, presence, db, *user_id, false);
            });
        };
        wire_close(conn.channel());

        auto join_call_room = [&](pulse_easy::Connection& c, std::int64_t chat_id) {
            const std::string room = "call:" + std::to_string(chat_id);
            media.join(room, c.channel());
            call_rooms.set(c.channel(), room);
            // media.join/attach replaces on_close — restore presence cleanup
            wire_close(c.channel());

            media.on_voice(room, [&media, room](pulse::Channel&, const pulse_media::Frame& f) {
                media.send_voice(room, f.payload, f.stream_id, f.flags);
            });
            media.on_video(room, [&media, room](pulse::Channel&, const pulse_media::Frame& f) {
                media.send_video(room, f.payload,
                                 (f.flags & pulse_media::FrameFlags::KeyFrame) != 0, f.stream_id);
            });
            media.on_image(room, [&media, room](pulse::Channel&, const pulse_media::Frame& f) {
                if (f.kind == pulse_media::Kind::ImageEnd)
                    media.end_image(room, f.stream_id);
                else
                    media.send_image(room, f.payload,
                                     f.mime.empty() ? "image/jpeg" : f.mime, f.flags);
            });
            return room;
        };

        conn.emit("connected", Json{{"user_id", *me},
                                    {"online_users", presence.online_user_ids()}});

        conn.on("hello", [&](pulse_easy::Connection& c, const Json&) {
            const auto user_id = presence.user_of(c.channel());
            if (!user_id) return;
            c.emit("connected", Json{{"user_id", *user_id},
                                     {"online_users", presence.online_user_ids()}});
        });

        conn.on("send", [&](pulse_easy::Connection& c, const Json& data) {
            const auto user_id = presence.user_of(c.channel());
            if (!user_id) return;
            const auto chat_id = sj::get<std::int64_t>(data, "chat_id");
            const auto body    = sj::get<std::string>(data, "body");
            if (!chat_id || !body || body->empty()) return;
            if (!is_member(db, *chat_id, *user_id)) return;
            touch_last_seen(db, *user_id);
            save_and_broadcast_message(
                db, hub, presence, *chat_id, *user_id, *body,
                sj::get<std::string>(data, "client_id"),
                sj::get_or<std::string>(data, "kind", "text"),
                sj::get_or<std::string>(data, "media_url", ""));
        });

        conn.on("typing", [&](pulse_easy::Connection& c, const Json& data) {
            const auto user_id = presence.user_of(c.channel());
            if (!user_id) return;
            const auto chat_id = sj::get<std::int64_t>(data, "chat_id");
            const auto typing  = sj::get<bool>(data, "typing");
            if (!chat_id || !typing) return;
            if (!is_member(db, *chat_id, *user_id)) return;
            broadcast_typing(hub, db, *chat_id, *user_id, *typing);
        });

        conn.on("read", [&](pulse_easy::Connection& c, const Json& data) {
            const auto user_id = presence.user_of(c.channel());
            if (!user_id) return;
            const auto chat_id = sj::get<std::int64_t>(data, "chat_id");
            const auto last_id = sj::get<std::int64_t>(data, "last_message_id");
            if (!chat_id || !last_id) return;
            if (!is_member(db, *chat_id, *user_id)) return;
            mark_chat_read(db, hub, *chat_id, *user_id, *last_id);
        });

        // ---- Call signaling (voice / video via pulse_media) ----
        conn.on("call_invite", [&](pulse_easy::Connection& c, const Json& data) {
            const auto user_id = presence.user_of(c.channel());
            if (!user_id) return;
            const auto chat_id = sj::get<std::int64_t>(data, "chat_id");
            const auto kind    = sj::get_or<std::string>(data, "kind", "voice");
            if (!chat_id || !is_member(db, *chat_id, *user_id)) return;

            const std::string call_id = sj::get_or<std::string>(
                data, "call_id", detail::random_token(12));
            const std::string room = join_call_room(c, *chat_id);

            std::string from_name = "Someone";
            if (auto u = user_row(db, *user_id))
                from_name = u->value("display_name", u->value("username", "Someone"));

            emit_to_members(hub, db, *chat_id, "call_invite",
                            Json{{"chat_id", *chat_id}, {"call_id", call_id},
                                 {"kind", kind}, {"from_user_id", *user_id},
                                 {"from_name", from_name}},
                            *user_id);
            c.emit("call_joined", Json{{"chat_id", *chat_id}, {"call_id", call_id},
                                       {"kind", kind}, {"room", room}});
        });

        conn.on("call_accept", [&](pulse_easy::Connection& c, const Json& data) {
            const auto user_id = presence.user_of(c.channel());
            if (!user_id) return;
            const auto chat_id = sj::get<std::int64_t>(data, "chat_id");
            const auto call_id = sj::get<std::string>(data, "call_id");
            const auto kind    = sj::get_or<std::string>(data, "kind", "voice");
            if (!chat_id || !call_id || !is_member(db, *chat_id, *user_id)) return;

            const std::string room = join_call_room(c, *chat_id);

            emit_to_members(hub, db, *chat_id, "call_accept",
                            Json{{"chat_id", *chat_id}, {"call_id", *call_id},
                                 {"kind", kind}, {"user_id", *user_id}});
            c.emit("call_joined", Json{{"chat_id", *chat_id}, {"call_id", *call_id},
                                       {"kind", kind}, {"room", room}});
        });

        conn.on("call_reject", [&](pulse_easy::Connection& c, const Json& data) {
            const auto user_id = presence.user_of(c.channel());
            if (!user_id) return;
            const auto chat_id = sj::get<std::int64_t>(data, "chat_id");
            const auto call_id = sj::get<std::string>(data, "call_id");
            if (!chat_id || !call_id) return;
            emit_to_members(hub, db, *chat_id, "call_reject",
                            Json{{"chat_id", *chat_id}, {"call_id", *call_id},
                                 {"user_id", *user_id}},
                            *user_id);
        });

        conn.on("call_end", [&](pulse_easy::Connection& c, const Json& data) {
            const auto user_id = presence.user_of(c.channel());
            if (!user_id) return;
            const auto chat_id = sj::get<std::int64_t>(data, "chat_id");
            const auto call_id = sj::get_or<std::string>(data, "call_id", "");
            call_rooms.clear(c.channel());
            if (chat_id) {
                emit_to_members(hub, db, *chat_id, "call_end",
                                Json{{"chat_id", *chat_id}, {"call_id", call_id},
                                     {"user_id", *user_id}});
            }
        });

    });
}
