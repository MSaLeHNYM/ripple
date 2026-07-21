import { useCallback, useRef, useState } from 'react';
import EmojiPicker from './EmojiPicker.jsx';
import { requestUserMedia } from '../mediaAccess.js';

const TYPING_DEBOUNCE_MS = 1200;

export default function Composer({ onSend, onTyping, disabled: composerDisabled }) {
  const [text, setText]             = useState('');
  const [showEmoji, setShowEmoji]   = useState(false);
  const [recording, setRecording]   = useState(false);
  const [recSeconds, setRecSeconds] = useState(0);
  const [sending, setSending]       = useState(false);

  const typingTimer   = useRef(null);
  const isTyping      = useRef(false);
  const textareaRef   = useRef(null);
  const mediaRef      = useRef(null);   // MediaRecorder
  const recTimer      = useRef(null);
  const recChunks     = useRef([]);
  const fileInputRef  = useRef(null);

  // ── typing events ──────────────────────────────
  const stopTyping = useCallback(() => {
    if (isTyping.current) { isTyping.current = false; onTyping(false); }
    clearTimeout(typingTimer.current);
    typingTimer.current = null;
  }, [onTyping]);

  const handleChange = (e) => {
    const value = e.target.value;
    setText(value);
    // auto-grow
    const ta = textareaRef.current;
    if (ta) { ta.style.height = 'auto'; ta.style.height = Math.min(ta.scrollHeight, 120) + 'px'; }

    if (value.trim()) {
      if (!isTyping.current) { isTyping.current = true; onTyping(true); }
      clearTimeout(typingTimer.current);
      typingTimer.current = setTimeout(stopTyping, TYPING_DEBOUNCE_MS);
    } else {
      stopTyping();
    }
  };

  // ── send helpers ────────────────────────────────
  const doSend = useCallback(async (body, kind = 'text', media_url = '') => {
    setSending(true);
    try { await onSend(body, kind, media_url); } finally { setSending(false); }
  }, [onSend]);

  const handleSubmit = async (e) => {
    e?.preventDefault();
    const body = text.trim();
    if (!body || sending || composerDisabled) return;
    stopTyping();
    setText('');
    if (textareaRef.current) textareaRef.current.style.height = 'auto';
    await doSend(body);
  };

  const handleKeyDown = (e) => {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); handleSubmit(); }
  };

  // ── emoji ────────────────────────────────────────
  const handleEmojiSelect = (emoji) => {
    const ta = textareaRef.current;
    const start = ta?.selectionStart ?? text.length;
    const end   = ta?.selectionEnd   ?? text.length;
    const newText = text.slice(0, start) + emoji + text.slice(end);
    setText(newText);
    setShowEmoji(false);
    setTimeout(() => {
      ta?.focus();
      const pos = start + emoji.length;
      ta?.setSelectionRange(pos, pos);
    }, 0);
  };

  // ── image upload ─────────────────────────────────
  const handleImageFile = async (file) => {
    if (!file || sending) return;
    setSending(true);
    try {
      const res = await fetch('/api/upload', {
        method: 'POST',
        headers: { 'Content-Type': file.type },
        credentials: 'include',
        body: file,
      });
      if (!res.ok) throw new Error('Upload failed');
      const data = await res.json();
      await doSend(file.name, 'image', data.url);
    } catch (err) {
      console.error(err);
    } finally {
      setSending(false);
      if (fileInputRef.current) fileInputRef.current.value = '';
    }
  };

  const handleFileChange = (e) => {
    const file = e.target.files?.[0];
    if (file) handleImageFile(file);
  };

  // ── paste image ──────────────────────────────────
  const handlePaste = (e) => {
    const items = e.clipboardData?.items;
    if (!items) return;
    for (const item of items) {
      if (item.type.startsWith('image/')) {
        e.preventDefault();
        handleImageFile(item.getAsFile());
        return;
      }
    }
  };

  // ── voice recording ──────────────────────────────
  const startRecording = async () => {
    if (recording || sending) return;
    try {
      const stream = await requestUserMedia({ audio: true }, 'microphone');
      recChunks.current = [];
      const mr = new MediaRecorder(stream);
      mr.ondataavailable = (e) => { if (e.data.size > 0) recChunks.current.push(e.data); };
      mr.onstop = async () => {
        stream.getTracks().forEach((t) => t.stop());
        const blob = new Blob(recChunks.current, { type: 'audio/ogg; codecs=opus' });
        if (blob.size < 500) return; // too short — ignore
        setSending(true);
        try {
          const res = await fetch('/api/upload', {
            method: 'POST',
            headers: { 'Content-Type': 'audio/ogg' },
            credentials: 'include',
            body: blob,
          });
          if (!res.ok) throw new Error('Upload failed');
          const data = await res.json();
          await doSend('🎤 Voice message', 'voice', data.url);
        } finally { setSending(false); }
      };
      mr.start();
      mediaRef.current = mr;
      setRecording(true);
      setRecSeconds(0);
      recTimer.current = setInterval(() => setRecSeconds((s) => s + 1), 1000);
    } catch (err) {
      alert(err?.message || 'Microphone access denied.');
    }
  };

  const stopRecording = () => {
    clearInterval(recTimer.current);
    mediaRef.current?.stop();
    setRecording(false);
    setRecSeconds(0);
  };

  const cancelRecording = () => {
    clearInterval(recTimer.current);
    mediaRef.current?.stream?.getTracks().forEach((t) => t.stop());
    recChunks.current = [];
    mediaRef.current = null;
    setRecording(false);
    setRecSeconds(0);
  };

  const fmtSecs = (s) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;

  return (
    <form className="composer" onSubmit={handleSubmit}>
      {showEmoji && (
        <EmojiPicker onSelect={handleEmojiSelect} onClose={() => setShowEmoji(false)} />
      )}

      {/* hidden file input */}
      <input
        ref={fileInputRef}
        type="file"
        accept="image/*"
        style={{ display: 'none' }}
        onChange={handleFileChange}
      />

      {/* image attach */}
      <button
        type="button"
        className="btn-icon"
        title="Send image"
        disabled={sending || recording || composerDisabled}
        onClick={() => fileInputRef.current?.click()}
      >
        📷
      </button>

      {/* emoji */}
      <button
        type="button"
        className="btn-icon"
        title="Emoji"
        disabled={recording || composerDisabled}
        onClick={() => setShowEmoji((v) => !v)}
      >
        😊
      </button>

      <div className="composer-inner">
        {recording ? (
          <div className="recording-indicator">
            <span className="recording-dot" />
            {fmtSecs(recSeconds)}
            <button type="button" className="btn-ghost btn-sm" onClick={cancelRecording}>✕</button>
          </div>
        ) : (
          <textarea
            ref={textareaRef}
            rows={1}
            value={text}
            onChange={handleChange}
            onKeyDown={handleKeyDown}
            onPaste={handlePaste}
            placeholder="Write a message…"
            disabled={sending || composerDisabled}
          />
        )}
      </div>

      {/* voice / send */}
      {recording ? (
        <button
          type="button"
          className="send-btn"
          title="Send voice"
          onClick={stopRecording}
          disabled={sending}
        >
          ⬆
        </button>
      ) : text.trim() ? (
        <button
          type="submit"
          className="send-btn"
          title="Send"
          disabled={!text.trim() || sending || composerDisabled}
        >
          ➤
        </button>
      ) : (
        <button
          type="button"
          className="send-btn"
          title="Voice message"
          disabled={sending || composerDisabled}
          onClick={startRecording}
          style={{ background: 'var(--ink-soft)' }}
        >
          🎤
        </button>
      )}
    </form>
  );
}
