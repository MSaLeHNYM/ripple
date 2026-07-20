import { useEffect, useRef } from 'react';
import Composer from './Composer.jsx';
import {
  chatPeer,
  chatTitle,
  isGroupChat,
  isOwnMessage,
  messageSenderLabel,
} from '../normalize.js';

function formatMessageTime(iso) {
  if (!iso) return '';
  return new Date(iso).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function isOtherOnline(chat, currentUserId, onlineUsers) {
  if (!chat || isGroupChat(chat)) return false;
  const other = chatPeer(chat, currentUserId);
  if (!other) return false;
  return onlineUsers.has(other.id) || onlineUsers.has(other.username);
}

function receiptLabel(status) {
  switch (status) {
    case 'seen':
      return '✓✓';
    case 'delivered':
      return '✓✓';
    case 'sent':
      return '✓';
    case 'pending':
    default:
      return '…';
  }
}

export default function MessagePane({
  chat,
  messages,
  currentUser,
  onlineUsers,
  typingUser,
  receipts = {},
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
  const isGroup = isGroupChat(chat);
  const memberCount = (chat.members ?? []).length;

  return (
    <main className="message-pane">
      <header className="pane-header">
        <div>
          <h2>{chatTitle(chat, currentUser.id)}</h2>
          <p className="pane-status">
            {typingUser ? (
              <span className="typing-indicator">{typingUser} is typing…</span>
            ) : isGroup ? (
              `${memberCount} members`
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
          const isOwn = isOwnMessage(msg, currentUser.id);
          const sender = messageSenderLabel(msg);
          const showSender = isGroup && !isOwn;
          const status = receipts[msg.id] || (msg.pending ? 'pending' : msg.receipt_status);
          return (
            <li
              key={msg.id ?? i}
              className={`message${isOwn ? ' own' : ''} message-appear`}
            >
              {showSender && <span className="message-sender">{sender}</span>}
              <div className="message-bubble">
                <p>{msg.body}</p>
                <div className="message-meta">
                  <time>{formatMessageTime(msg.created_at)}</time>
                  {isOwn && status && (
                    <span
                      className={`message-receipt${status === 'pending' ? ' pending' : ''}${status === 'seen' ? ' seen' : ''}`}
                      title={status}
                      aria-label={status}
                    >
                      {receiptLabel(status)}
                    </span>
                  )}
                </div>
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
