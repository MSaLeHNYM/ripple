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
- **SPA** — single binary serves the React build + API + WebSocket

## Quick start

### One command

```bash
./run.sh
# → http://localhost:8080
```

`run.sh` installs/builds the React app, compiles the C++ server, stages `web/`, and starts listening.

### As a Socketify example (recommended)

```bash
git clone --recurse-submodules https://github.com/MSaLeHNYM/Socketify.git
cd Socketify
./scripts/run_examples.sh ripple   # delegates to examples/ripple/run.sh
```

If you already cloned Socketify without submodules:

```bash
git submodule update --init --recursive
```

### Standalone (next to Socketify)

```bash
# expect ../Socketify on disk, or an installed Socketify package
./run.sh
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
| `POST` | `/api/chats/:id/messages` | `{body}` |
| `POST` | `/api/chats/:id/typing` | `{typing}` |

### Pulse events (`GET /pulse`)

| Direction | Example |
|---|---|
| Client → | `{ "type": "send", "chat_id": 1, "body": "hi" }` |
| Client → | `{ "type": "typing", "chat_id": 1, "typing": true }` |
| Server → | `{ "type": "message", ... }` |
| Server → | `{ "type": "presence", "users": [...] }` |
| Server → | `{ "type": "typing", ... }` |

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
