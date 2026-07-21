// Ripple backend — entry point.

#include "models.h"
#include "utils.h"
#include "presence.h"
#include "db_helpers.h"
#include "broadcasts.h"
#include "routes.h"
#include "pulse_handler.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    // Config from env + optional .env
    auto cfg = config::Config::from_env().merge_file(".env");
    const int         port   = static_cast<int>(cfg.get_int("PORT").value_or(8080));
    const std::string secret = cfg.get_or("SESSION_SECRET",
                                          "ripple-dev-secret-change-in-prod-32b");
    const std::string dbpath = cfg.get_or("DB_PATH", "data/ripple.db");

    fs::create_directories("data");

    auto db = Database::open(Sqlite{.path = dbpath, .pool_size = 4});
    migrate_all(db);
    ensure_column(db, "chat_members", "last_read_message_id", "INTEGER");
    ensure_column(db, "chat_members", "last_read_at",         "TEXT");
    ensure_column(db, "messages",     "kind",                 "TEXT DEFAULT 'text'");
    ensure_column(db, "messages",     "media_url",            "TEXT");

    pulse::Hub       hub;
    Presence         presence;
    CallRooms        call_rooms;
    pulse_easy::App  easy(&hub);
    pulse_media::Hub media(&hub);
    Server           server;

    cors::CorsOptions cors_opts;
    cors_opts.reflect_origin    = true;
    cors_opts.allow_credentials = true;
    cors_opts.allow_headers     = "Content-Type";
    cors_opts.allow_methods     = "GET,POST,PUT,PATCH,DELETE,OPTIONS";
    server.Use(cors::middleware(cors_opts));

    sessions::Options so;
    so.secret  = secret;
    so.rolling = true;
    server.Use(sessions::middleware(so));

    register_routes(server, db, hub, presence);
    register_pulse(server, db, hub, presence, call_rooms, easy, media);

    const std::string web = find_web_root();
    if (!fs::exists(fs::path(web) / "index.html")) {
        std::fprintf(stderr, "error: UI not found at %s/index.html\n"
                             "       run: ./run.sh\n", web.c_str());
        return 1;
    }
    server.Use(static_files::serve(web, {.mount = "/", .fallthrough = true, .cache_max_age = 0}));
    server.Get("/*path", [web](Request& req, Response& res) {
        if (std::string(req.path()).rfind("/api", 0) == 0) {
            res.status(Status::NotFound).json(Json{{"error", "not found"}});
            return;
        }
        if (!res.send_file(web + "/index.html"))
            res.status(Status::NotFound).html("<h1>Frontend not built</h1><p>Run: ./run.sh</p>");
    });

    if (!server.Listen(static_cast<std::uint16_t>(port))) {
        std::fprintf(stderr, "failed to start: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("Ripple on http://localhost:%d  (web=%s)\n", port, web.c_str());
    server.Wait();
    return 0;
}
