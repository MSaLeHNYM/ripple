import { useCallback, useEffect, useRef, useState } from 'react';
import { api, createPulseConnection } from '../api.js';
import { useAuth } from '../auth.jsx';
import { mergeReceiptStatus, normalizeMessage } from '../normalize.js';
import Sidebar from './Sidebar.jsx';
import MessagePane from './MessagePane.jsx';

function makeClientId() {
  return `c-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`;
}

export default function ChatApp() {
  const { user, logout } = useAuth();
  const [chats, setChats] = useState([]);
  const [activeChatId, setActiveChatId] = useState(null);
  const [messages, setMessages] = useState([]);
  const [onlineUsers, setOnlineUsers] = useState(() => new Set());
  const [typing, setTyping] = useState({});
  const [receipts, setReceipts] = useState({});
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [wsConnected, setWsConnected] = useState(false);
  const [error, setError] = useState('');
  const pulseRef = useRef(null);
  const activeChatIdRef = useRef(activeChatId);
  const userIdRef = useRef(user.id);
  const messagesRef = useRef([]);
  const lastMarkedRef = useRef({});

  useEffect(() => {
    activeChatIdRef.current = activeChatId;
  }, [activeChatId]);

  useEffect(() => {
    userIdRef.current = user.id;
  }, [user.id]);

  useEffect(() => {
    messagesRef.current = messages;
  }, [messages]);

  const activeChat = chats.find((c) => c.id === activeChatId) ?? null;

  const loadChats = useCallback(async () => {
    try {
      const data = await api.getChats();
      setChats(data.chats ?? data ?? []);
    } catch (err) {
      setError(err.message);
    }
  }, []);

  const loadMessages = useCallback(async (chatId) => {
    if (!chatId) {
      setMessages([]);
      return;
    }
    try {
      const data = await api.getMessages(chatId);
      const list = (data.messages ?? data ?? []).map(normalizeMessage);
      setMessages(list);
      setReceipts((prev) => {
        const next = { ...prev };
        for (const msg of list) {
          if (msg.receipt_status) {
            next[msg.id] = mergeReceiptStatus(next[msg.id], msg.receipt_status);
          } else if (msg.sender_id === userIdRef.current && !next[msg.id]) {
            next[msg.id] = 'sent';
          }
        }
        return next;
      });
    } catch (err) {
      setError(err.message);
    }
  }, []);

  const markActiveRead = useCallback(
    async (chatId, messageList) => {
      if (!chatId || !messageList?.length) return;
      const last = messageList[messageList.length - 1];
      const lastId = last?.id;
      if (lastId == null || String(lastId).startsWith('temp-')) return;
      if (lastMarkedRef.current[chatId] === lastId) return;
      lastMarkedRef.current[chatId] = lastId;

      const sent = pulseRef.current?.send({
        type: 'read',
        chat_id: chatId,
        last_message_id: lastId,
      });
      if (!sent) {
        try {
          await api.markRead(chatId, lastId);
        } catch {
          /* ignore */
        }
      }
      setChats((prev) =>
        prev.map((c) => (c.id === chatId ? { ...c, unread_count: 0 } : c)),
      );
    },
    [],
  );

  useEffect(() => {
    loadChats();
  }, [loadChats]);

  useEffect(() => {
    loadMessages(activeChatId);
  }, [activeChatId, loadMessages]);

  useEffect(() => {
    if (activeChatId && messages.length) {
      markActiveRead(activeChatId, messages);
    }
  }, [activeChatId, messages, markActiveRead]);

  const ingestMessage = useCallback((raw) => {
    const msg = normalizeMessage(raw);
    const clientId = msg.client_id;
    const active = activeChatIdRef.current;

    if (msg.chat_id === active) {
      setMessages((prev) => {
        if (clientId) {
          const idx = prev.findIndex(
            (m) => m.client_id === clientId || m.id === clientId || m.id === `temp-${clientId}`,
          );
          if (idx >= 0) {
            const next = [...prev];
            next[idx] = { ...msg, pending: false };
            return next;
          }
        }
        if (prev.some((m) => m.id === msg.id)) return prev;
        // Replace optimistic pending with same body from self
        if (msg.sender_id === userIdRef.current) {
          const pendingIdx = prev.findIndex(
            (m) =>
              m.pending &&
              m.body === msg.body &&
              m.sender_id === msg.sender_id,
          );
          if (pendingIdx >= 0) {
            const next = [...prev];
            next[pendingIdx] = { ...msg, pending: false };
            return next;
          }
        }
        return [...prev, msg];
      });
    }

    if (msg.sender_id === userIdRef.current && msg.id != null) {
      setReceipts((prev) => ({
        ...prev,
        [msg.id]: mergeReceiptStatus(prev[msg.id], msg.receipt_status || 'sent'),
      }));
    }

    loadChats();
  }, [loadChats]);

  useEffect(() => {
    const pulse = createPulseConnection({
      onStatus: (connected) => {
        setWsConnected(connected);
        if (connected) {
          loadChats();
          if (activeChatIdRef.current) loadMessages(activeChatIdRef.current);
        }
      },
      onMessage: (event) => {
        switch (event.type) {
          case 'connected': {
            const ids = event.online_users ?? [];
            setOnlineUsers(new Set(ids));
            break;
          }
          case 'presence': {
            const uid = event.user_id;
            if (uid == null) break;
            setOnlineUsers((prev) => {
              const next = new Set(prev);
              if (event.online) next.add(uid);
              else next.delete(uid);
              return next;
            });
            break;
          }
          case 'typing': {
            setTyping((prev) => {
              const key = event.chat_id;
              const next = { ...prev };
              if (event.typing) {
                next[key] =
                  event.display_name ||
                  event.user ||
                  `User ${event.user_id}`;
              } else {
                delete next[key];
              }
              return next;
            });
            break;
          }
          case 'message':
            ingestMessage(event.message ?? event);
            break;
          case 'chat':
            loadChats();
            break;
          case 'receipt': {
            if (event.up_to_message_id != null && event.chat_id != null) {
              setReceipts((prev) => {
                const next = { ...prev };
                for (const msg of messagesRef.current) {
                  if (
                    msg.chat_id === event.chat_id &&
                    msg.sender_id === userIdRef.current &&
                    typeof msg.id === 'number' &&
                    msg.id <= event.up_to_message_id
                  ) {
                    next[msg.id] = mergeReceiptStatus(next[msg.id], event.status || 'seen');
                  }
                }
                return next;
              });
            } else if (event.message_id != null) {
              setReceipts((prev) => ({
                ...prev,
                [event.message_id]: mergeReceiptStatus(
                  prev[event.message_id],
                  event.status || 'delivered',
                ),
              }));
            }
            break;
          }
          case 'read':
            loadChats();
            break;
          default:
            break;
        }
      },
    });
    pulseRef.current = pulse;
    return () => pulse.close();
  }, [loadChats, loadMessages, ingestMessage]);

  const handleSelectChat = (id) => {
    setActiveChatId(id);
    setSidebarOpen(false);
    setError('');
  };

  const handleSend = async (body) => {
    if (!activeChatId || !body.trim()) return;
    const text = body.trim();
    const clientId = makeClientId();
    const tempId = `temp-${clientId}`;
    const optimistic = {
      id: tempId,
      client_id: clientId,
      chat_id: activeChatId,
      sender_id: user.id,
      sender: {
        id: user.id,
        username: user.username,
        display_name: user.display_name,
      },
      body: text,
      created_at: new Date().toISOString(),
      pending: true,
    };

    setMessages((prev) => [...prev, optimistic]);
    setReceipts((prev) => ({ ...prev, [tempId]: 'pending' }));

    const sent = pulseRef.current?.send({
      type: 'send',
      chat_id: activeChatId,
      body: text,
      client_id: clientId,
    });

    if (!sent) {
      try {
        const data = await api.sendMessage(activeChatId, text, clientId);
        const msg = normalizeMessage(data.message ?? data);
        setMessages((prev) => {
          const without = prev.filter((m) => m.id !== tempId && m.client_id !== clientId);
          if (without.some((m) => m.id === msg.id)) return without;
          return [...without, msg];
        });
        setReceipts((prev) => {
          const next = { ...prev };
          delete next[tempId];
          next[msg.id] = mergeReceiptStatus(next[msg.id], 'sent');
          return next;
        });
      } catch (err) {
        setMessages((prev) => prev.filter((m) => m.id !== tempId));
        setError(err.message);
      }
    }
    loadChats();
  };

  const handleTyping = (isTyping) => {
    if (!activeChatId) return;
    const sent = pulseRef.current?.send({
      type: 'typing',
      chat_id: activeChatId,
      typing: isTyping,
    });
    if (!sent) {
      api.setTyping(activeChatId, isTyping).catch(() => {});
    }
  };

  const handleStartDm = async (userId) => {
    try {
      const data = await api.createDm(userId);
      const chat = data.chat ?? data;
      await loadChats();
      setActiveChatId(chat.id);
      setSidebarOpen(false);
    } catch (err) {
      setError(err.message);
    }
  };

  const handleCreateGroup = async (title, memberIds) => {
    try {
      const data = await api.createGroup(title, memberIds);
      const chat = data.chat ?? data;
      await loadChats();
      setActiveChatId(chat.id);
      setSidebarOpen(false);
    } catch (err) {
      setError(err.message);
    }
  };

  const handleLogout = async () => {
    pulseRef.current?.close();
    await logout();
  };

  return (
    <div className="chat-app">
      <button
        type="button"
        className="sidebar-toggle"
        onClick={() => setSidebarOpen((o) => !o)}
        aria-label="Toggle sidebar"
      >
        <span />
        <span />
        <span />
      </button>

      <div
        className={`sidebar-overlay${sidebarOpen ? ' open' : ''}`}
        onClick={() => setSidebarOpen(false)}
        aria-hidden="true"
      />

      <Sidebar
        user={user}
        chats={chats}
        activeChatId={activeChatId}
        onlineUsers={onlineUsers}
        wsConnected={wsConnected}
        open={sidebarOpen}
        onSelectChat={handleSelectChat}
        onStartDm={handleStartDm}
        onCreateGroup={handleCreateGroup}
        onLogout={handleLogout}
      />

      <MessagePane
        chat={activeChat}
        messages={messages}
        currentUser={user}
        onlineUsers={onlineUsers}
        typingUser={typing[activeChatId]}
        receipts={receipts}
        error={error}
        onDismissError={() => setError('')}
        onSend={handleSend}
        onTyping={handleTyping}
      />
    </div>
  );
}
