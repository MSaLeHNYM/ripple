import { useCallback, useRef, useState } from 'react';

const TYPING_DEBOUNCE_MS = 1200;

export default function Composer({ onSend, onTyping }) {
  const [text, setText] = useState('');
  const [sending, setSending] = useState(false);
  const typingTimer = useRef(null);
  const isTyping = useRef(false);

  const stopTyping = useCallback(() => {
    if (isTyping.current) {
      isTyping.current = false;
      onTyping(false);
    }
    if (typingTimer.current) {
      clearTimeout(typingTimer.current);
      typingTimer.current = null;
    }
  }, [onTyping]);

  const handleChange = (e) => {
    const value = e.target.value;
    setText(value);

    if (value.trim()) {
      if (!isTyping.current) {
        isTyping.current = true;
        onTyping(true);
      }
      if (typingTimer.current) clearTimeout(typingTimer.current);
      typingTimer.current = setTimeout(stopTyping, TYPING_DEBOUNCE_MS);
    } else {
      stopTyping();
    }
  };

  const handleSubmit = async (e) => {
    e.preventDefault();
    const body = text.trim();
    if (!body || sending) return;

    stopTyping();
    setSending(true);
    setText('');
    try {
      await onSend(body);
    } catch {
      setText(body);
    } finally {
      setSending(false);
    }
  };

  const handleKeyDown = (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSubmit(e);
    }
  };

  return (
    <form className="composer" onSubmit={handleSubmit}>
      <textarea
        rows={1}
        value={text}
        onChange={handleChange}
        onKeyDown={handleKeyDown}
        placeholder="Write a message…"
        disabled={sending}
      />
      <button type="submit" className="btn btn-primary" disabled={!text.trim() || sending}>
        Send
      </button>
    </form>
  );
}
