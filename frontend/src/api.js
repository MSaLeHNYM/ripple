const BASE = '';

async function request(path, options = {}) {
  const res = await fetch(`${BASE}${path}`, {
    credentials: 'include',
    headers: {
      'Content-Type': 'application/json',
      ...options.headers,
    },
    ...options,
  });

  let data = null;
  const text = await res.text();
  if (text) {
    try {
      data = JSON.parse(text);
    } catch {
      data = { error: text };
    }
  }

  if (!res.ok) {
    let message = data?.error || data?.message || res.statusText || 'Request failed';
    // socketify::validate errors_json shape
    if (data?.errors && typeof data.errors === 'object') {
      const first = Object.values(data.errors).flat()[0];
      if (first) message = first;
    }
    throw new Error(message);
  }

  return data;
}

export const api = {
  register: (body) =>
    request('/api/register', { method: 'POST', body: JSON.stringify(body) }),

  login: (body) =>
    request('/api/login', { method: 'POST', body: JSON.stringify(body) }),

  logout: () => request('/api/logout', { method: 'POST' }),

  me: () => request('/api/me'),

  searchUsers: (q) =>
    request(`/api/users?q=${encodeURIComponent(q)}`),

  getChats: () => request('/api/chats'),

  createDm: (user_id) =>
    request('/api/chats/dm', { method: 'POST', body: JSON.stringify({ user_id }) }),

  createGroup: (title, member_ids) =>
    request('/api/chats/group', {
      method: 'POST',
      body: JSON.stringify({ title, member_ids }),
    }),

  getMessages: (chatId) => request(`/api/chats/${chatId}/messages`),

  sendMessage: (chatId, body, client_id, kind = 'text', media_url = '') =>
    request(`/api/chats/${chatId}/messages`, {
      method: 'POST',
      body: JSON.stringify({ body, client_id, kind, media_url: media_url || undefined }),
    }),

  setTyping: (chatId, typing) =>
    request(`/api/chats/${chatId}/typing`, {
      method: 'POST',
      body: JSON.stringify({ typing }),
    }),

  markRead: (chatId, last_message_id) =>
    request(`/api/chats/${chatId}/read`, {
      method: 'POST',
      body: JSON.stringify({ last_message_id }),
    }),
};

/**
 * Pulse WebSocket with pulse_easy envelopes + pulse_media binary frames.
 * send(type, data) → {"type","data"}
 * sendBinary(ArrayBuffer)
 */
export function createPulseConnection({ onMessage, onBinary, onStatus }) {
  let ws = null;
  let closed = false;
  let attempt = 0;
  let reconnectTimer = null;

  const clearReconnect = () => {
    if (reconnectTimer != null) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
  };

  const connect = () => {
    if (closed) return;
    clearReconnect();

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const socket = new WebSocket(`${protocol}//${window.location.host}/pulse`);
    socket.binaryType = 'arraybuffer';
    ws = socket;

    socket.addEventListener('open', () => {
      attempt = 0;
      onStatus?.(true);
      try {
        socket.send(JSON.stringify({ type: 'hello', data: {} }));
      } catch {
        /* ignore */
      }
    });

    socket.addEventListener('message', (event) => {
      if (event.data instanceof ArrayBuffer) {
        onBinary?.(event.data);
        return;
      }
      try {
        const msg = JSON.parse(event.data);
        if (msg?.type && Object.prototype.hasOwnProperty.call(msg, 'data')) {
          const data = msg.data && typeof msg.data === 'object' && !Array.isArray(msg.data)
            ? msg.data
            : { value: msg.data };
          onMessage?.({ type: msg.type, ...data });
        } else {
          onMessage?.(msg);
        }
      } catch {
        /* ignore */
      }
    });

    const scheduleReconnect = () => {
      if (closed) return;
      onStatus?.(false);
      const delay = Math.min(8000, 500 * 2 ** attempt);
      attempt += 1;
      reconnectTimer = setTimeout(connect, delay);
    };

    socket.addEventListener('close', () => {
      if (ws === socket) ws = null;
      scheduleReconnect();
    });

    socket.addEventListener('error', () => {
      try { socket.close(); } catch { /* ignore */ }
    });
  };

  connect();

  return {
    /** Send pulse_easy envelope: {type, data} */
    send(typeOrPayload, data) {
      if (ws?.readyState !== WebSocket.OPEN) return false;
      let payload;
      if (typeof typeOrPayload === 'string') {
        payload = { type: typeOrPayload, data: data ?? {} };
      } else if (typeOrPayload && typeof typeOrPayload === 'object') {
        // back-compat: flat {type, chat_id, ...} → envelope
        const { type, ...rest } = typeOrPayload;
        payload = { type, data: rest };
      } else {
        return false;
      }
      ws.send(JSON.stringify(payload));
      return true;
    },
    sendBinary(buf) {
      if (ws?.readyState !== WebSocket.OPEN) return false;
      ws.send(buf);
      return true;
    },
    isOpen() {
      return ws?.readyState === WebSocket.OPEN;
    },
    close() {
      closed = true;
      clearReconnect();
      try { ws?.close(); } catch { /* ignore */ }
      ws = null;
    },
  };
}
