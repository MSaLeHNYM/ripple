// Ripple backend — entry point (HTTPS / WSS only).
//
// Usage:
//   ./ripple --cert certs/server.crt --key certs/server.key [--host 0.0.0.0] [--port 8443]
//   SOCKETIFY_CERT_FILE=... SOCKETIFY_KEY_FILE=... ./ripple

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

#if !defined(SOCKETIFY_HAS_TLS) || !SOCKETIFY_HAS_TLS
#error "Ripple requires Socketify built with SOCKETIFY_WITH_TLS=ON"
#endif

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string host = "0.0.0.0";
    int         port = 8443;
    std::string log_level = "info";  // trace|debug|info|warn|error|fatal|off
    std::string db_path = "data/ripple.db";
    std::string secret;
    std::string cert_file;
    std::string key_file;
};

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s --cert <pem> --key <pem> [options]\n"
                 "\n"
                 "Ripple listens on HTTPS / WSS only (no plain HTTP).\n"
                 "\n"
                 "Options:\n"
                 "  --cert <path>     TLS certificate PEM (or $SOCKETIFY_CERT_FILE)\n"
                 "  --key <path>      TLS private key PEM (or $SOCKETIFY_KEY_FILE)\n"
                 "  --host <addr>     Listen address (default: 0.0.0.0)\n"
                 "  --port <n>        Listen port (default: 8443, or $PORT)\n"
                 "  --log <level>     Log level: trace|debug|info|warn|error|fatal|off\n"
                 "                    (default: info, or $LOG_LEVEL)\n"
                 "  --db <path>       SQLite path (default: data/ripple.db, or $DB_PATH)\n"
                 "  --secret <str>    Session secret (default: $SESSION_SECRET)\n"
                 "  -h, --help        Show this help\n"
                 "\n"
                 "Generate a self-signed cert:\n"
                 "  ./gen_certs.sh\n"
                 "\n"
                 "Examples:\n"
                 "  %s --cert certs/server.crt --key certs/server.key\n"
                 "  %s --cert certs/server.crt --key certs/server.key --port 8443 --log debug\n",
                 argv0, argv0, argv0);
}

std::optional<logging::Level> parse_log_level(std::string_view s) {
    if (s == "trace") return logging::Level::Trace;
    if (s == "debug") return logging::Level::Debug;
    if (s == "info")  return logging::Level::Info;
    if (s == "warn" || s == "warning") return logging::Level::Warn;
    if (s == "error") return logging::Level::Error;
    if (s == "fatal") return logging::Level::Fatal;
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
        if (a == "--cert") {
            const char* v = need("--cert");
            if (!v) return false;
            opt.cert_file = v;
            continue;
        }
        if (a == "--key") {
            const char* v = need("--key");
            if (!v) return false;
            opt.key_file = v;
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
    opt.port      = static_cast<int>(cfg.get_int("PORT").value_or(8443));
    opt.host      = cfg.get_or("HOST", "0.0.0.0");
    opt.log_level = cfg.get_or("LOG_LEVEL", "info");
    opt.db_path   = cfg.get_or("DB_PATH", "data/ripple.db");
    opt.secret    = cfg.get_or("SESSION_SECRET",
                               "ripple-dev-secret-change-in-prod-32b");
    opt.cert_file = cfg.get_or("SOCKETIFY_CERT_FILE", "");
    opt.key_file  = cfg.get_or("SOCKETIFY_KEY_FILE", "");

    if (!parse_args(argc, argv, opt)) return 2;

    if (opt.cert_file.empty() || opt.key_file.empty()) {
        if (auto env = TlsOptions::from_env()) {
            if (opt.cert_file.empty()) opt.cert_file = env->cert_file;
            if (opt.key_file.empty())  opt.key_file  = env->key_file;
        }
    }
    if (opt.cert_file.empty()) opt.cert_file = "certs/server.crt";
    if (opt.key_file.empty())  opt.key_file  = "certs/server.key";

    if (!fs::exists(opt.cert_file) || !fs::exists(opt.key_file)) {
        std::fprintf(stderr,
                     "error: TLS certificate/key not found:\n"
                     "  cert: %s\n"
                     "  key:  %s\n"
                     "\n"
                     "Ripple does not serve plain HTTP. Generate a self-signed pair:\n"
                     "  ./gen_certs.sh\n"
                     "or pass --cert / --key (or SOCKETIFY_CERT_FILE / SOCKETIFY_KEY_FILE).\n",
                     opt.cert_file.c_str(), opt.key_file.c_str());
        return 2;
    }

    const auto level = parse_log_level(opt.log_level);
    if (!level) {
        std::fprintf(stderr, "error: invalid --log '%s' (use trace|debug|info|warn|error|fatal|off)\n",
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

    ServerOptions sopts;
    sopts.tls = TlsOptions{.cert_file = opt.cert_file, .key_file = opt.key_file};
    Server server(sopts);

    // Access log: 2xx/3xx → Info, 4xx → Warn, 5xx → Error (filtered by set_level).
    if (*level != logging::Level::Off) {
        server.Use(logging::middleware());
    }

    cors::CorsOptions cors_opts;
    cors_opts.reflect_origin    = true;
    cors_opts.allow_credentials = true;
    cors_opts.allow_headers     = "Content-Type";
    cors_opts.allow_methods     = "GET,POST,PUT,PATCH,DELETE,OPTIONS";
    server.Use(cors::middleware(cors_opts));

    sessions::Options so;
    so.secret        = opt.secret;
    so.rolling       = true;
    so.cookie_secure = true;  // HTTPS-only cookies
    server.Use(sessions::middleware(so));

    register_routes(server, db, hub, presence);
    register_pulse(server, db, hub, presence, call_rooms, easy, media);

    const std::string web = find_web_root();
    if (!fs::exists(fs::path(web) / "index.html")) {
        logging::fatal("UI not found at {}/index.html — run ./run.sh", web);
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
        logging::fatal("failed to start: {}", server.last_error());
        return 1;
    }
    logging::info("Ripple on https://{}:{}  (web={}  cert={})",
                  opt.host, opt.port, web, opt.cert_file);
    server.Wait();
    return 0;
}
