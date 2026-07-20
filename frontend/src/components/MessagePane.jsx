import { useEffect, useRef } from 'react';
import Composer from './Composer.jsx';

function chatTitle(chat, currentUserId) {
  if (chat.title) return chat.title;
  const members = chat.members ?? [];
  const other = members.find((m) => m.id !== currentUserId);
  return other?.display_name || other?.username || 'Direct Message';
}

function formatMessageTime(iso) {
  if (!iso) return '';
  return new Date(iso).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function isOtherOnline(chat, currentUserId, onlineUsers) {
  if (!chat || chat.type === 'group' || chat.is_group) return false;
  const members = chat.members ?? [];
  const other = members.find((m) => m.id !== currentUserId);
  if (!other) return false;
  return onlineUsers.has(other.id) || onlineUsers.has(other.username);
}

export default function MessagePane({
  chat,
  messages,
  currentUser,
  onlineUsers,
  typingUser,
  error,
  onDismissError,
  onSend,
  onTyping,
}) {
  const bottomRef = useRef(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages, typingUser]);

  if (!chat) {
    return (
      <main className="message-pane empty">
        <div className="empty-state">
          <img src="/logo.svg" alt="" className="empty-logo" />
          <h2>Select a conversation</h2>
          <p>Search for a user or pick a chat from the sidebar to start messaging.</p>
        </div>
      </main>
    );
  }

  const online = isOtherOnline(chat, currentUser.id, onlineUsers);
  const isGroup = chat.type === 'group' || chat.is_group;

  return (
    <main className="message-pane">
      <header className="pane-header">
        <div>
          <h2>{chatTitle(chat, currentUser.id)}</h2>
          <p className="pane-status">
            {typingUser ? (
              <span className="typing-indicator">{typingUser} is typing…</span>
            ) : isGroup ? (
              `${(chat.members ?? []).length} members`
            ) : online ? (
              <span className="online-label">
                <span className="presence-dot online pulse" /> Online
              </span>
            ) : (
              'Offline'
            )}
          </p>
        </div>
      </header>

      {error && (
        <div className="pane-error">
          <span>{error}</span>
          <button type="button" onClick={onDismissError} aria-label="Dismiss">
            ×
          </button>
        </div>
      )}

      <ul className="message-list">
        {messages.map((msg, i) => {
          const isOwn = msg.user_id === currentUser.id || msg.user?.id === currentUser.id;
          const sender = msg.user?.display_name || msg.user?.username || msg.sender_name || 'Unknown';
          const showSender = isGroup && !isOwn;
          return (
            <li
              key={msg.id ?? i}
              className={`message${isOwn ? ' own' : ''} message-appear`}
            >
              {showSender && <span className="message-sender">{sender}</span>}
              <div className="message-bubble">
                <p>{msg.body}</p>
                <time>{formatMessageTime(msg.created_at)}</time>
              </div>
            </li>
          );
        })}
        {typingUser && (
          <li className="message typing-bubble message-appear">
            <div className="message-bubble typing-dots">
              <span /><span /><span />
            </div>
          </li>
        )}
        <li ref={bottomRef} />
      </ul>

      <Composer onSend={onSend} onTyping={onTyping} />
    </main>
  );
}
