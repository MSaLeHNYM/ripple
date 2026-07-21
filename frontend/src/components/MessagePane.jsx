import { useEffect, useRef, useState } from 'react';
import Composer from './Composer.jsx';
import Lightbox from './Lightbox.jsx';
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

function formatDate(iso) {
  if (!iso) return '';
  const d = new Date(iso);
  const today = new Date();
  const yesterday = new Date(today);
  yesterday.setDate(today.getDate() - 1);
  if (d.toDateString() === today.toDateString()) return 'Today';
  if (d.toDateString() === yesterday.toDateString()) return 'Yesterday';
  return d.toLocaleDateString([], { month: 'short', day: 'numeric', year: 'numeric' });
}

function isOtherOnline(chat, currentUserId, onlineUsers) {
  if (!chat || isGroupChat(chat)) return false;
  const other = chatPeer(chat, currentUserId);
  if (!other) return false;
  return onlineUsers.has(other.id) || onlineUsers.has(other.username);
}

function receiptLabel(status) {
  switch (status) {
    case 'seen':      return '✓✓';
    case 'delivered': return '✓✓';
    case 'sent':      return '✓';
    case 'pending':   default: return '…';
  }
}

function MessageBubble({ msg, isOwn, showSender, isGroup, receipts }) {
  const [lightbox, setLightbox] = useState(null);
  const sender = messageSenderLabel(msg);
  const status = receipts[msg.id] || (msg.pending ? 'pending' : msg.receipt_status);
  const kind = msg.kind || 'text';

  return (
    <li className={`message${isOwn ? ' own' : ''}`}>
      {showSender && <span className="message-sender">{sender}</span>}
      <div className="message-bubble">
        {kind === 'image' && msg.media_url ? (
          <>
            <img
              src={msg.media_url}
              alt="Image"
              className="msg-image"
              onClick={() => setLightbox(msg.media_url)}
              loading="lazy"
            />
            {msg.body && msg.body !== msg.media_url && <p>{msg.body}</p>}
          </>
        ) : kind === 'voice' && msg.media_url ? (
          <div className="msg-voice">
            <span>🎤</span>
            <audio controls src={msg.media_url} />
          </div>
        ) : (
          <p>{msg.body}</p>
        )}
        <div className="message-meta">
          <time>{formatMessageTime(msg.created_at)}</time>
          {isOwn && (
            <span
              className={`message-receipt${status === 'seen' ? ' seen' : status === 'delivered' ? ' delivered' : status === 'pending' ? ' pending' : ''}`}
              title={status}
            >
              {receiptLabel(status)}
            </span>
          )}
        </div>
      </div>
      {lightbox && <Lightbox src={lightbox} onClose={() => setLightbox(null)} />}
    </li>
  );
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
  onVoiceCall,
  onVideoCall,
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

  // group by date for dividers
  let lastDate = '';

  return (
    <main className="message-pane">
      <header className="pane-header">
        <div className="avatar" aria-hidden="true">
          {chatTitle(chat, currentUser.id).charAt(0).toUpperCase()}
        </div>
        <div className="pane-header-info">
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
        <div className="pane-header-actions">
          <button type="button" className="btn-icon" title="Voice call" onClick={onVoiceCall}>
            📞
          </button>
          <button type="button" className="btn-icon" title="Video call" onClick={onVideoCall}>
            🎥
          </button>
        </div>
      </header>

      {error && (
        <div className="pane-error">
          <span>{error}</span>
          <button type="button" onClick={onDismissError} aria-label="Dismiss">×</button>
        </div>
      )}

      <ul className="message-list">
        {messages.map((msg, i) => {
          const isOwn = isOwnMessage(msg, currentUser.id);
          const showSender = isGroup && !isOwn;
          const dateStr = formatDate(msg.created_at);
          const showDivider = dateStr && dateStr !== lastDate;
          if (showDivider) lastDate = dateStr;

          return (
            <div key={msg.id ?? `msg-${i}`}>
              {showDivider && <div className="date-divider">{dateStr}</div>}
              <MessageBubble
                msg={msg}
                isOwn={isOwn}
                showSender={showSender}
                isGroup={isGroup}
                receipts={receipts}
              />
            </div>
          );
        })}
        {typingUser && (
          <li className="message typing-bubble">
            <div className="message-bubble">
              <div className="typing-dots"><span /><span /><span /></div>
            </div>
          </li>
        )}
        <li ref={bottomRef} />
      </ul>

      <Composer onSend={onSend} onTyping={onTyping} />
    </main>
  );
}
