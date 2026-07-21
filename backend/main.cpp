// Ripple backend — entry point.
//
// Usage:
//   ./ripple [--host 0.0.0.0] [--port 8080] [--log info] [--db PATH] [--help]

#include "models.h"
#include "utils.h"
#include "presence.h"
#include "db_helpers.h"
#include "broadcasts.h"
#include "routes.h"
#include "pulse_handler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string host = "0.0.0.0";
    int         port = 8080;
    std::string log_level = "info";  // trace|debug|info|warn|error|off
    std::string db_path = "data/ripple.db";
    std::string secret;
};

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s [options]\n"
                 "\n"
                 "Options:\n"
                 "  --host <addr>     Listen address (default: 0.0.0.0)\n"
                 "  --port <n>        Listen port (default: 8080, or $PORT)\n"
                 "  --log <level>     Log level: trace|debug|info|warn|error|off\n"
                 "                    (default: info, or $LOG_LEVEL)\n"
                 "  --db <path>       SQLite path (default: data/ripple.db, or $DB_PATH)\n"
                 "  --secret <str>    Session secret (default: $SESSION_SECRET)\n"
                 "  -h, --help        Show this help\n"
                 "\n"
                 "Examples:\n"
                 "  %s --log info --port 9999 --host 0.0.0.0\n"
                 "  %s --port 8080 --log debug\n",
                 argv0, argv0, argv0);
}

std::optional<logging::Level> parse_log_level(std::string_view s) {
    if (s == "trace") return logging::Level::Trace;
    if (s == "debug") return logging::Level::Debug;
    if (s == "info")  return logging::Level::Info;
    if (s == "warn" || s == "warning") return logging::Level::Warn;
    if (s == "error") return logging::Level::Error;
    if (s == "off" || s == "silent") return logging::Level::Off;
    return std::nullopt;
}

bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s requires a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (a == "--host") {
            const char* v = need("--host");
            if (!v) return false;
            opt.host = v;
            continue;
        }
        if (a == "--port") {
            const char* v = need("--port");
            if (!v) return false;
            try {
                opt.port = std::stoi(v);
            } catch (...) {
                std::fprintf(stderr, "error: invalid --port '%s'\n", v);
                return false;
            }
            if (opt.port < 0 || opt.port > 65535) {
                std::fprintf(stderr, "error: --port out of range: %d\n", opt.port);
                return false;
            }
            continue;
        }
        if (a == "--log") {
            const char* v = need("--log");
            if (!v) return false;
            opt.log_level = v;
            continue;
        }
        if (a == "--db") {
            const char* v = need("--db");
            if (!v) return false;
            opt.db_path = v;
            continue;
        }
        if (a == "--secret") {
            const char* v = need("--secret");
            if (!v) return false;
            opt.secret = v;
            continue;
        }
        std::fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
        print_usage(argv[0]);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    // Defaults from env / .env, then CLI overrides.
    auto cfg = config::Config::from_env().merge_file(".env");

    Options opt;
    opt.port      = static_cast<int>(cfg.get_int("PORT").value_or(8080));
    opt.host      = cfg.get_or("HOST", "0.0.0.0");
    opt.log_level = cfg.get_or("LOG_LEVEL", "info");
    opt.db_path   = cfg.get_or("DB_PATH", "data/ripple.db");
    opt.secret    = cfg.get_or("SESSION_SECRET",
                               "ripple-dev-secret-change-in-prod-32b");

    if (!parse_args(argc, argv, opt)) return 2;

    const auto level = parse_log_level(opt.log_level);
    if (!level) {
        std::fprintf(stderr, "error: invalid --log '%s' (use trace|debug|info|warn|error|off)\n",
                     opt.log_level.c_str());
        return 2;
    }
    logging::set_level(*level);

    fs::create_directories(fs::path(opt.db_path).parent_path().empty()
                               ? fs::path("data")
                               : fs::path(opt.db_path).parent_path());

    auto db = Database::open(Sqlite{.path = opt.db_path, .pool_size = 4});
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

    // Request access lines are always Info; set_level() filters them.
    // Do not pass the CLI level into middleware.log_level — that re-tags
    // every request as warn/error and makes --log warn still spam access logs.
    if (*level <= logging::Level::Info) {
        server.Use(logging::middleware());
    }

    cors::CorsOptions cors_opts;
    cors_opts.reflect_origin    = true;
    cors_opts.allow_credentials = true;
    cors_opts.allow_headers     = "Content-Type";
    cors_opts.allow_methods     = "GET,POST,PUT,PATCH,DELETE,OPTIONS";
    server.Use(cors::middleware(cors_opts));

    sessions::Options so;
    so.secret  = opt.secret;
    so.rolling = true;
    server.Use(sessions::middleware(so));

    register_routes(server, db, hub, presence);
    register_pulse(server, db, hub, presence, call_rooms, easy, media);

    const std::string web = find_web_root();
    if (!fs::exists(fs::path(web) / "index.html")) {
        logging::error("UI not found at {}/index.html — run ./run.sh", web);
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

    if (!server.Run(opt.host, static_cast<std::uint16_t>(opt.port))) {
        logging::error("failed to start: {}", server.last_error());
        return 1;
    }
    logging::info("Ripple on http://{}:{}  (web={})", opt.host, opt.port, web);
    server.Wait();
    return 0;
}
