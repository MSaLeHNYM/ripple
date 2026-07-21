<p align="center">
  <img src="assets/logo.svg" alt="Ripple logo" width="120">
</p>

<h1 align="center">Ripple</h1>

<p align="center">
  <strong>Messages that ripple.</strong><br>
  A Telegram-inspired realtime messenger built on
  <a href="https://github.com/MSaLeHNYM/Socketify">Socketify</a>
  — Pulse channels, SQLite ORM, and a React UI.
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white">
  <img alt="React" src="https://img.shields.io/badge/React-18-61DAFB?style=flat-square&logo=react&logoColor=black">
  <img alt="Pulse" src="https://img.shields.io/badge/realtime-Pulse%20%2F%20WebSocket-0D7377?style=flat-square">
  <img alt="HTTPS" src="https://img.shields.io/badge/TLS-HTTPS%20%2F%20WSS-721412?style=flat-square&logo=openssl&logoColor=white">
  <img alt="SQLite" src="https://img.shields.io/badge/DB-SQLite%20ORM-003B57?style=flat-square&logo=sqlite&logoColor=white">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-22C55E?style=flat-square">
</p>

<p align="center">
  <code>messenger</code> · <code>chat</code> · <code>realtime</code> · <code>websocket</code> ·
  <code>pulse</code> · <code>socketify</code> · <code>sqlite</code> · <code>react</code> ·
  <code>cpp20</code> · <code>telegram-like</code> · <code>dm</code> · <code>groups</code> ·
  <code>presence</code> · <code>typing-indicators</code> · <code>https</code>
</p>

---

## Why Ripple?

Socketify’s **Pulse** layer keeps the connection pulsing — Ripple turns that into a full messenger: accounts, DMs, group rooms, presence, typing, and durable history in SQLite. Drop a message in; watch it ripple to everyone in the room.

| Layer | Tech |
|---|---|
| Transport | Socketify **Pulse** over **WSS** (RFC 6455) |
| HTTP | **HTTPS only** (no plain HTTP listener) |
| Persistence | Socketify **`db` ORM** → SQLite |
| Auth | Cookie sessions (`sessions::middleware`, `Secure`) |
| UI | React 18 + Vite |

## Important: Socketify must be built with TLS

Ripple is **HTTPS / WSS only**. CMake will refuse to configure if Socketify is too old or was built without TLS.

| Requirement | Detail |
|---|---|
| Version | **Socketify ≥ 0.2.2** (`find_package(Socketify 0.2.2)`) |
| TLS | **`SOCKETIFY_WITH_TLS=ON`** (default) → `SOCKETIFY_HAS_TLS=1` on the imported target |

Why TLS is mandatory:

- Voice / video calls use `getUserMedia`. Browsers only allow that in a
  [secure context](https://developer.mozilla.org/en-US/docs/Web/Security/Secure_Contexts)
  (`https://` or `localhost`). Plain `http://192.168.x.x` blocks the mic/camera.
- Ripple therefore never binds a cleartext HTTP port; Pulse upgrades as `wss://`.

Install Socketify with TLS enabled:

```bash
git clone https://github.com/MSaLeHNYM/Socketify.git
cd Socketify
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSOCKETIFY_WITH_TLS=ON
cmake --build build -j"$(nproc)"
sudo cmake --install build   # → /usr/local by default
```

Use **0.2.2+** (composable Pulse close handlers — required so `pulse_media` and
presence cleanup coexist under reconnect). If configure fails with a version or
TLS error, reinstall Socketify as above, then `rm -rf build && ./run.sh`.

## Features

- **Accounts** — register / login / logout with salted SHA-256 passwords
- **Direct messages** — start a DM from user search
- **Group chats** — create a room and invite members
- **Realtime** — messages, typing indicators, and online presence over Pulse
- **Voice / video calls** — `pulse_media` frames (needs HTTPS for mic/camera)
- **History** — messages persisted and reloaded per chat
- **Theme** — dark / light mode with persistence
- **Unread + receipts** — sidebar badges, delivered/seen ticks
- **Reconnect** — Pulse auto-reconnect with backoff
- **SPA** — single binary serves the React build + API + WebSocket over TLS

## Quick start

### 1. Install Socketify (with TLS)

See [Important: Socketify must be built with TLS](#important-socketify-must-be-built-with-tls) above.

### 2. Build & run Ripple (HTTPS)

```bash
git clone https://github.com/MSaLeHNYM/ripple.git
cd ripple
chmod +x run.sh gen_certs.sh
./run.sh
# → https://0.0.0.0:8443
```

`run.sh` will:

1. Generate `certs/server.crt` + `certs/server.key` if missing (`./gen_certs.sh`)
2. Build the React UI
3. Compile the C++ server (checks Socketify **version** + **TLS**)
4. Stage `web/` and start **HTTPS / WSS** on port **8443**

Accept the browser warning for the self-signed certificate (required for LAN voice/video testing).

```bash
./run.sh --port 8443 --host 0.0.0.0 --log info
# or after build:
./gen_certs.sh
./build/ripple --cert certs/server.crt --key certs/server.key --help
```

If Socketify was installed to a custom prefix:

```bash
./run.sh --prefix /path/to/prefix
```

### TLS certs only

```bash
./gen_certs.sh              # → certs/server.crt + certs/server.key (includes LAN IPs)
./gen_certs.sh --force      # regenerate after IP change
./gen_certs.sh /tmp/mycerts # custom output directory
```

**Voice / video mic access:** open `https://…:8443` (never `http://192.168.x.x`).
Accept the self-signed warning once, then Allow microphone in the browser prompt.
If you previously clicked Block, clear site permissions and reload.

Override paths via CLI or env: `--cert` / `--key`, or `SOCKETIFY_CERT_FILE` /
`SOCKETIFY_KEY_FILE`.

## Dev mode (Vite + hot reload)

Terminal 1 — backend (HTTPS):

```bash
./gen_certs.sh
./build/ripple --cert certs/server.crt --key certs/server.key
```

Terminal 2 — frontend:

```bash
cd frontend && npm run dev
# configure Vite to proxy to https://localhost:8443 (see frontend/vite.config.*)
```

## Architecture

```
Browser ──HTTPS──► Socketify TLS ──► SQLite (users, chats, messages)
   │
   └──WSS/Pulse──► /pulse hub ──► rooms user:{id} ──► fan-out JSON events
                     └── pulse_media binary frames (voice / video)
```

### REST (cookie session, Secure)

| Method | Path | Notes |
|---|---|---|
| `POST` | `/api/register` | `{username, display_name, password}` |
| `POST` | `/api/login` | `{username, password}` |
| `POST` | `/api/logout` | |
| `GET` | `/api/me` | current user |
| `GET` | `/api/users?q=` | search |
| `GET` | `/api/chats` | your conversations |
| `POST` | `/api/chats/dm` | `{user_id}` |
| `POST` | `/api/chats/group` | `{title, member_ids}` |
| `GET` | `/api/chats/:id/messages` | history |
| `POST` | `/api/chats/:id/messages` | `{body, client_id?}` |
| `POST` | `/api/chats/:id/typing` | `{typing}` |
| `POST` | `/api/chats/:id/read` | `{last_message_id}` |

### Pulse events (`GET /pulse` over WSS)

Envelopes use `{ "type", "data" }` (pulse_easy). Example client → server:

| Type | `data` |
|---|---|
| `hello` | `{}` |
| `send` | `{ chat_id, body, client_id?, kind?, media_url? }` |
| `typing` | `{ chat_id, typing }` |
| `read` | `{ chat_id, last_message_id }` |
| `call_invite` / `call_accept` / … | call signaling |

## Project layout

```
ripple/
├── assets/logo.svg
├── backend/main.cpp         # HTTPS API + Pulse + SQLite
├── frontend/                # React SPA
├── gen_certs.sh             # self-signed TLS for local/LAN
├── run.sh                   # build + HTTPS run
├── CMakeLists.txt           # requires Socketify ≥ 0.2.2 + TLS
├── LICENSE                  # MIT
└── README.md
```

## License

MIT © 2026 [M SaLeH NYM](https://github.com/MSaLeHNYM)

Powered by [Socketify](https://github.com/MSaLeHNYM/Socketify) — keep the connection pulsing.
