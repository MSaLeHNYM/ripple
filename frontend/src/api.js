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

  sendMessage: (chatId, body) =>
    request(`/api/chats/${chatId}/messages`, {
      method: 'POST',
      body: JSON.stringify({ body }),
    }),

  setTyping: (chatId, typing) =>
    request(`/api/chats/${chatId}/typing`, {
      method: 'POST',
      body: JSON.stringify({ typing }),
    }),
};

export function createPulseSocket(onMessage, onOpen, onClose) {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const ws = new WebSocket(`${protocol}//${window.location.host}/pulse`);

  ws.addEventListener('open', () => {
    ws.send(JSON.stringify({ type: 'hello' }));
    onOpen?.();
  });

  ws.addEventListener('message', (event) => {
    try {
      const data = JSON.parse(event.data);
      onMessage(data);
    } catch {
      /* ignore malformed frames */
    }
  });

  ws.addEventListener('close', () => onClose?.());
  ws.addEventListener('error', () => onClose?.());

  return ws;
}
