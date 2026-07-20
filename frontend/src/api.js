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
    const message = data?.error || data?.message || res.statusText || 'Request failed';
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

  sendMessage: (chatId, body, client_id) =>
    request(`/api/chats/${chatId}/messages`, {
      method: 'POST',
      body: JSON.stringify({ body, client_id }),
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
 * Persistent Pulse WebSocket with exponential backoff reconnect.
 * Returns { send, close, getSocket }.
 */
export function createPulseConnection({ onMessage, onStatus }) {
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
    ws = socket;

    socket.addEventListener('open', () => {
      attempt = 0;
      onStatus?.(true);
      try {
        socket.send(JSON.stringify({ type: 'hello' }));
      } catch {
        /* ignore */
      }
    });

    socket.addEventListener('message', (event) => {
      try {
        const data = JSON.parse(event.data);
        onMessage?.(data);
      } catch {
        /* ignore malformed frames */
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
      try {
        socket.close();
      } catch {
        /* ignore */
      }
    });
  };

  connect();

  return {
    send(payload) {
      if (ws?.readyState === WebSocket.OPEN) {
        ws.send(typeof payload === 'string' ? payload : JSON.stringify(payload));
        return true;
      }
      return false;
    },
    isOpen() {
      return ws?.readyState === WebSocket.OPEN;
    },
    close() {
      closed = true;
      clearReconnect();
      try {
        ws?.close();
      } catch {
        /* ignore */
      }
      ws = null;
    },
  };
}

/** @deprecated use createPulseConnection */
export function createPulseSocket(onMessage, onOpen, onClose) {
  return createPulseConnection({
    onMessage,
    onStatus: (connected) => {
      if (connected) onOpen?.();
      else onClose?.();
    },
  });
}
