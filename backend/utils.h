#pragma once
#include "models.h"
#include <socketify/detail/utils.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

inline std::string now_iso() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long>(ms.count()));
    return buf;
}

inline std::string bytes_to_hex(const std::array<std::uint8_t, 32>& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(64, '\0');
    for (std::size_t i = 0; i < d.size(); ++i) {
        out[i * 2]     = kHex[d[i] >> 4];
        out[i * 2 + 1] = kHex[d[i] & 0x0f];
    }
    return out;
}

inline std::string hash_password(std::string_view password) {
    const std::string salt   = detail::random_token(16);
    const auto        digest = detail::sha256(salt + ":" + std::string(password));
    return salt + "$" + bytes_to_hex(digest);
}

inline bool verify_password(std::string_view stored, std::string_view password) {
    const auto pos = stored.find('$');
    if (pos == std::string_view::npos) return false;
    const std::string salt(stored.substr(0, pos));
    const std::string expected(stored.substr(pos + 1));
    const auto        digest = detail::sha256(salt + ":" + std::string(password));
    return detail::constant_time_equal(bytes_to_hex(digest), expected);
}

inline std::optional<std::int64_t> uid(const Request& req) {
    const auto sess = sessions::get(req);
    if (!sess || !sess->has("user_id")) return std::nullopt;
    return sess->get("user_id").get<std::int64_t>();
}

inline Middleware require_auth() {
    return [](Request& req, Response& res, Next next) {
        if (!uid(req)) {
            res.status(Status::Unauthorized).json(Json{{"error", "login required"}});
            return;
        }
        next();
    };
}

inline void json_error(Response& res, Status code, std::string_view msg) {
    res.status(code).json(Json{{"error", std::string(msg)}});
}

inline void validation_error(Response& res, const sv::Result& r) {
    res.status(Status::UnprocessableEntity).json(r.errors_json());
}

/** Parse + validate request body. Returns nullopt and sends error response on failure. */
inline std::optional<Json> parse_and_validate(Request& req, Response& res,
                                              const sv::Schema& schema) {
    const auto raw = req.body_view();
    auto doc = sj::parse(raw);
    if (!doc) {
        json_error(res, Status::BadRequest, "invalid json");
        return std::nullopt;
    }
    auto r = sv::validate(*doc, schema);
    if (!r.ok) {
        validation_error(res, r);
        return std::nullopt;
    }
    return doc;
}

inline void ensure_column(Database& db, const char* table, const char* column, const char* decl) {
    const auto rows = db.query(std::string("PRAGMA table_info(") + table + ")");
    for (const auto& row : rows) {
        if (row.at("name").get<std::string>() == column) return;
    }
    db.exec(std::string("ALTER TABLE ") + table + " ADD COLUMN " + column + " " + decl);
}

inline std::string exe_dir() {
    std::error_code ec;
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path().string();
    return fs::current_path().string();
}

inline std::string find_web_root() {
    const fs::path base = exe_dir();
    for (const fs::path& cand : {base / "web", base / "frontend" / "dist",
                                  fs::path("web"), fs::path("frontend") / "dist"}) {
        if (fs::exists(cand / "index.html")) return fs::absolute(cand).string();
    }
    return fs::absolute(base / "web").string();
}
