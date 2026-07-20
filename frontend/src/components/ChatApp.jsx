import { useCallback, useEffect, useRef, useState } from 'react';
import { api, createPulseSocket } from '../api.js';
import { useAuth } from '../auth.jsx';
import Sidebar from './Sidebar.jsx';
import MessagePane from './MessagePane.jsx';

export default function ChatApp() {
  const { user, logout } = useAuth();
  const [chats, setChats] = useState([]);
  const [activeChatId, setActiveChatId] = useState(null);
  const [messages, setMessages] = useState([]);
  const [onlineUsers, setOnlineUsers] = useState(new Set());
  const [typing, setTyping] = useState({});
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [wsConnected, setWsConnected] = useState(false);
  const [error, setError] = useState('');
  const wsRef = useRef(null);
  const activeChatIdRef = useRef(activeChatId);

  useEffect(() => {
    activeChatIdRef.current = activeChatId;
  }, [activeChatId]);

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
    if (!chatId) return;
    try {
      const data = await api.getMessages(chatId);
      setMessages(data.messages ?? data ?? []);
    } catch (err) {
      setError(err.message);
    }
  }, []);

  useEffect(() => {
    loadChats();
  }, [loadChats]);

  useEffect(() => {
    loadMessages(activeChatId);
  }, [activeChatId, loadMessages]);

  useEffect(() => {
    const ws = createPulseSocket(
      (event) => {
        switch (event.type) {
          case 'presence':
            setOnlineUsers(new Set(event.users ?? []));
            break;
          case 'typing':
            setTyping((prev) => {
              const key = event.chat_id;
              const next = { ...prev };
              if (event.typing) {
                next[key] = event.user;
              } else {
                delete next[key];
              }
              return next;
            });
            break;
          case 'message': {
            const msg = event.message ?? event;
            if (msg.chat_id === activeChatIdRef.current) {
              setMessages((prev) => {
                if (prev.some((m) => m.id === msg.id)) return prev;
                return [...prev, msg];
              });
            }
            loadChats();
            break;
          }
          case 'chat':
            loadChats();
            break;
          default:
            break;
        }
      },
      () => setWsConnected(true),
      () => setWsConnected(false),
    );
    wsRef.current = ws;
    return () => ws.close();
  }, [loadChats]);

  const handleSelectChat = (id) => {
    setActiveChatId(id);
    setSidebarOpen(false);
    setError('');
  };

  const handleSend = async (body) => {
    if (!activeChatId || !body.trim()) return;

    const ws = wsRef.current;
    if (ws?.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type: 'send', chat_id: activeChatId, body: body.trim() }));
    } else {
      const data = await api.sendMessage(activeChatId, body.trim());
      const msg = data.message ?? data;
      setMessages((prev) => [...prev, msg]);
    }
    loadChats();
  };

  const handleTyping = (isTyping) => {
    if (!activeChatId) return;
    api.setTyping(activeChatId, isTyping).catch(() => {});
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
    wsRef.current?.close();
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
        error={error}
        onDismissError={() => setError('')}
        onSend={handleSend}
        onTyping={handleTyping}
      />
    </div>
  );
}
