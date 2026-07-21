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
  <img alt="SQLite" src="https://img.shields.io/badge/DB-SQLite%20ORM-003B57?style=flat-square&logo=sqlite&logoColor=white">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-22C55E?style=flat-square">
</p>

<p align="center">
  <code>messenger</code> · <code>chat</code> · <code>realtime</code> · <code>websocket</code> ·
  <code>pulse</code> · <code>socketify</code> · <code>sqlite</code> · <code>react</code> ·
  <code>cpp20</code> · <code>telegram-like</code> · <code>dm</code> · <code>groups</code> ·
  <code>presence</code> · <code>typing-indicators</code>
</p>

---

## Why Ripple?

Socketify’s **Pulse** layer keeps the connection pulsing — Ripple turns that into a full messenger: accounts, DMs, group rooms, presence, typing, and durable history in SQLite. Drop a message in; watch it ripple to everyone in the room.

| Layer | Tech |
|---|---|
| Transport | Socketify **Pulse** (RFC 6455 WebSocket) |
| Persistence | Socketify **`db` ORM** → SQLite |
| Auth | Cookie sessions (`sessions::middleware`) |
| UI | React 18 + Vite |

## Features

- **Accounts** — register / login / logout with salted SHA-256 passwords
- **Direct messages** — start a DM from user search
- **Group chats** — create a room and invite members
- **Realtime** — messages, typing indicators, and online presence over Pulse
- **History** — messages persisted and reloaded per chat
- **Theme** — dark / light mode with persistence
- **Unread + receipts** — sidebar badges, delivered/seen ticks
- **Reconnect** — Pulse auto-reconnect with backoff
- **SPA** — single binary serves the React build + API + WebSocket

## Quick start

### 1. Install Socketify

Ripple links an **installed** Socketify package (`find_package(Socketify 0.2)`). It does not build Socketify itself.

```bash
git clone https://github.com/MSaLeHNYM/Socketify.git
cd Socketify
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build   # → /usr/local by default
```

(Or install via apt if a Socketify package is available.)

### 2. Build & run Ripple

```bash
git clone https://github.com/MSaLeHNYM/ripple.git
cd ripple
./run.sh
# → http://localhost:8080
```

`run.sh` builds the React UI, compiles the C++ server against `Socketify::socketify`, stages `web/`, and starts listening.

If Socketify was installed to a custom prefix:

```bash
./run.sh --prefix /path/to/prefix
```

## Dev mode (Vite + hot reload)

Terminal 1 — backend:

```bash
./build/ripple
```

Terminal 2 — frontend:

```bash
cd frontend && npm run dev
# http://localhost:5173  (proxies /api and /pulse → :8080)
```

## Architecture

```
Browser ──REST──► Socketify HTTP ──► SQLite (users, chats, messages)
   │
   └──Pulse/WS──► /pulse hub ──► rooms user:{id} ──► fan-out JSON events
```

### REST (cookie session)

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

### Pulse events (`GET /pulse`)

| Direction | Example |
|---|---|
| Client → | `{ "type": "hello" }` |
| Client → | `{ "type": "send", "chat_id": 1, "body": "hi", "client_id": "…" }` |
| Client → | `{ "type": "typing", "chat_id": 1, "typing": true }` |
| Client → | `{ "type": "read", "chat_id": 1, "last_message_id": 42 }` |
| Server → | `{ "type": "connected", "user_id": 1, "online_users": [1, 2] }` |
| Server → | `{ "type": "message", "id", "chat_id", "sender_id", "body", "client_id?", "sender": {…} }` |
| Server → | `{ "type": "presence", "user_id", "online", "last_seen" }` |
| Server → | `{ "type": "typing", "chat_id", "user_id", "display_name", "typing" }` |
| Server → | `{ "type": "chat", "chat_id" }` |
| Server → | `{ "type": "receipt", "message_id"\|"up_to_message_id", "chat_id", "user_id", "status" }` |
| Server → | `{ "type": "read", "chat_id", "user_id", "last_message_id" }` |

## Project layout

```
ripple/
├── assets/logo.svg          # brand mark (transparent)
├── backend/main.cpp         # API + Pulse + SQLite
├── frontend/                # React SPA
├── CMakeLists.txt
├── LICENSE                  # MIT
└── README.md
```

## License

MIT © 2026 [M SaLeH NYM](https://github.com/MSaLeHNYM)

Powered by [Socketify](https://github.com/MSaLeHNYM/Socketify) — keep the connection pulsing.
