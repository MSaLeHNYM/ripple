import { useEffect, useState } from 'react';
import { api } from '../api.js';
import { useTheme } from '../theme.jsx';
import { chatPeer, chatTitle, isGroupChat } from '../normalize.js';

function chatPreview(chat) {
  const last = chat.last_message;
  if (!last) return 'No messages yet';
  const body = last.body ?? '';
  return body.length > 48 ? `${body.slice(0, 48)}…` : body;
}

function formatTime(iso) {
  if (!iso) return '';
  const d = new Date(iso);
  const now = new Date();
  if (d.toDateString() === now.toDateString()) {
    return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  }
  return d.toLocaleDateString([], { month: 'short', day: 'numeric' });
}

export default function Sidebar({
  user,
  chats,
  activeChatId,
  onlineUsers,
  wsConnected,
  open,
  onSelectChat,
  onStartDm,
  onCreateGroup,
  onLogout,
}) {
  const { theme, toggleTheme } = useTheme();
  const [search, setSearch] = useState('');
  const [searchResults, setSearchResults] = useState([]);
  const [searching, setSearching] = useState(false);
  const [showGroupModal, setShowGroupModal] = useState(false);
  const [groupTitle, setGroupTitle] = useState('');
  const [groupMembers, setGroupMembers] = useState([]);
  const [groupSearch, setGroupSearch] = useState('');
  const [groupResults, setGroupResults] = useState([]);

  useEffect(() => {
    if (search.trim().length < 2) {
      setSearchResults([]);
      return;
    }
    const timer = setTimeout(async () => {
      setSearching(true);
      try {
        const data = await api.searchUsers(search.trim());
        const users = (data.users ?? data ?? []).filter((u) => u.id !== user.id);
        setSearchResults(users);
      } catch {
        setSearchResults([]);
      } finally {
        setSearching(false);
      }
    }, 300);
    return () => clearTimeout(timer);
  }, [search, user.id]);

  useEffect(() => {
    if (groupSearch.trim().length < 2) {
      setGroupResults([]);
      return;
    }
    const timer = setTimeout(async () => {
      try {
        const data = await api.searchUsers(groupSearch.trim());
        const users = (data.users ?? data ?? []).filter(
          (u) => u.id !== user.id && !groupMembers.some((m) => m.id === u.id),
        );
        setGroupResults(users);
      } catch {
        setGroupResults([]);
      }
    }, 300);
    return () => clearTimeout(timer);
  }, [groupSearch, user.id, groupMembers]);

  const handleCreateGroup = () => {
    if (!groupTitle.trim() || groupMembers.length === 0) return;
    onCreateGroup(
      groupTitle.trim(),
      groupMembers.map((m) => m.id),
    );
    setShowGroupModal(false);
    setGroupTitle('');
    setGroupMembers([]);
    setGroupSearch('');
  };

  const isUserOnline = (chat) => {
    if (isGroupChat(chat)) return false;
    const other = chatPeer(chat, user.id);
    return other ? onlineUsers.has(other.id) || onlineUsers.has(other.username) : false;
  };

  return (
    <aside className={`sidebar${open ? ' open' : ''}`}>
      <header className="sidebar-header">
        <div className="sidebar-brand">
          <img src="/logo.svg" alt="" className="sidebar-logo" />
          <div>
            <h2>Ripple</h2>
            <span className={`connection-dot${wsConnected ? ' online' : ''}`}>
              {wsConnected ? 'Connected' : 'Reconnecting…'}
            </span>
          </div>
        </div>
        <div className="sidebar-user">
          <span className="user-name">{user.display_name || user.username}</span>
          <div className="sidebar-header-actions">
            <button
              type="button"
              className="theme-toggle"
              onClick={toggleTheme}
              aria-label={theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme'}
              title={theme === 'dark' ? 'Light mode' : 'Dark mode'}
            >
              {theme === 'dark' ? '☀' : '☾'}
            </button>
            <button type="button" className="btn btn-ghost btn-sm" onClick={onLogout}>
              Sign out
            </button>
          </div>
        </div>
      </header>

      <div className="sidebar-actions">
        <div className="search-box">
          <input
            type="search"
            placeholder="Search users…"
            value={search}
            onChange={(e) => setSearch(e.target.value)}
          />
          {searching && <span className="search-spinner" />}
        </div>
        <button
          type="button"
          className="btn btn-secondary btn-sm"
          onClick={() => setShowGroupModal(true)}
        >
          + New group
        </button>
      </div>

      {searchResults.length > 0 && (
        <ul className="search-results">
          {searchResults.map((u) => (
            <li key={u.id}>
              <button type="button" onClick={() => { onStartDm(u.id); setSearch(''); }}>
                <span className="avatar">{initials(u)}</span>
                <span>
                  <strong>{u.display_name || u.username}</strong>
                  <small>@{u.username}</small>
                </span>
                {onlineUsers.has(u.id) && <span className="presence-dot online" />}
              </button>
            </li>
          ))}
        </ul>
      )}

      <ul className="chat-list">
        {chats.length === 0 && (
          <li className="chat-list-empty">No conversations yet — search for someone to chat.</li>
        )}
        {chats.map((chat) => (
          <li key={chat.id}>
            <button
              type="button"
              className={`chat-item${chat.id === activeChatId ? ' active' : ''}`}
              onClick={() => onSelectChat(chat.id)}
            >
              <span className="avatar">
                {isGroupChat(chat) ? 'G' : initialsFromChat(chat, user.id)}
              </span>
              <span className="chat-item-body">
                <span className="chat-item-top">
                  <strong>{chatTitle(chat, user.id)}</strong>
                  <time>{formatTime(chat.last_message?.created_at)}</time>
                </span>
                <span className="chat-item-preview">{chatPreview(chat)}</span>
              </span>
              {(chat.unread_count ?? 0) > 0 && (
                <span className="unread-badge">{chat.unread_count > 99 ? '99+' : chat.unread_count}</span>
              )}
              {isUserOnline(chat) && <span className="presence-dot online pulse" />}
            </button>
          </li>
        ))}
      </ul>

      {showGroupModal && (
        <div className="modal-overlay" onClick={() => setShowGroupModal(false)}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <h3>Create a group</h3>
            <label className="field">
              <span>Group name</span>
              <input
                value={groupTitle}
                onChange={(e) => setGroupTitle(e.target.value)}
                placeholder="Weekend plans"
              />
            </label>
            <label className="field">
              <span>Add members</span>
              <input
                value={groupSearch}
                onChange={(e) => setGroupSearch(e.target.value)}
                placeholder="Search by username…"
              />
            </label>
            {groupResults.length > 0 && (
              <ul className="group-picker">
                {groupResults.map((u) => (
                  <li key={u.id}>
                    <button
                      type="button"
                      onClick={() => {
                        setGroupMembers((m) => [...m, u]);
                        setGroupSearch('');
                      }}
                    >
                      + {u.display_name || u.username}
                    </button>
                  </li>
                ))}
              </ul>
            )}
            {groupMembers.length > 0 && (
              <div className="member-tags">
                {groupMembers.map((m) => (
                  <span key={m.id} className="member-tag">
                    {m.display_name || m.username}
                    <button
                      type="button"
                      onClick={() => setGroupMembers((ms) => ms.filter((x) => x.id !== m.id))}
                    >
                      ×
                    </button>
                  </span>
                ))}
              </div>
            )}
            <div className="modal-actions">
              <button type="button" className="btn btn-ghost" onClick={() => setShowGroupModal(false)}>
                Cancel
              </button>
              <button
                type="button"
                className="btn btn-primary"
                disabled={!groupTitle.trim() || groupMembers.length === 0}
                onClick={handleCreateGroup}
              >
                Create
              </button>
            </div>
          </div>
        </div>
      )}
    </aside>
  );
}

function initials(user) {
  const name = user.display_name || user.username || '?';
  return name.slice(0, 2).toUpperCase();
}

function initialsFromChat(chat, userId) {
  const other = chatPeer(chat, userId);
  if (other) return initials(other);
  return '?';
}
