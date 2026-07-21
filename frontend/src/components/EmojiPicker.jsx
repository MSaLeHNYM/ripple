import { useEffect, useRef, useState } from 'react';

const EMOJI_DATA = [
  {
    label: 'Smileys',
    emojis: ['😀','😁','😂','🤣','😃','😄','😅','😆','😇','😈','🤩','😉','😊','😋','😌','😍',
             '🥰','😎','🤓','🧐','😏','😒','😞','😔','😟','😕','🙁','☹️','😣','😖','😫','😩',
             '🥺','😢','😭','😤','😠','😡','🤬','🤯','😳','🥵','🥶','😱','😨','😰','😥','😓'],
  },
  {
    label: 'Gestures',
    emojis: ['👍','👎','👌','🤌','🤏','✌️','🤞','🤟','🤘','🤙','👈','👉','👆','👇','☝️','👋',
             '🤚','🖐️','✋','🖖','💪','🦾','🦿','🤲','👐','🙌','🤝','👏','🙏','✍️'],
  },
  {
    label: 'Hearts',
    emojis: ['❤️','🧡','💛','💚','💙','💜','🖤','🤍','🤎','💕','💞','💓','💗','💖','💘','💝','💟','❣️'],
  },
  {
    label: 'Nature',
    emojis: ['🌸','🌺','🌻','🌹','🌷','🌿','🍀','🍁','🌊','🔥','⭐','🌟','✨','💫','🌈','🌙','☀️','⛅','🌧️','❄️'],
  },
  {
    label: 'Food',
    emojis: ['🍕','🍔','🌮','🍜','🍣','🍰','🎂','🍩','🍪','🍫','☕','🧋','🍺','🥂','🍾','🎉','🎊'],
  },
  {
    label: 'Animals',
    emojis: ['🐶','🐱','🐭','🐹','🐰','🦊','🐻','🐼','🐨','🐯','🦁','🐸','🐵','🦆','🐔','🦄','🐙','🦋'],
  },
  {
    label: 'Objects',
    emojis: ['💻','📱','📷','🎵','🎶','🎮','🎯','🎲','🎨','📚','📝','✏️','🔑','💡','🔔','📢','💰','🎁'],
  },
];

export default function EmojiPicker({ onSelect, onClose }) {
  const [query, setQuery] = useState('');
  const ref = useRef(null);

  useEffect(() => {
    const handler = (e) => {
      if (ref.current && !ref.current.contains(e.target)) onClose();
    };
    document.addEventListener('mousedown', handler);
    return () => document.removeEventListener('mousedown', handler);
  }, [onClose]);

  const filtered = query.trim()
    ? EMOJI_DATA.map((cat) => ({
        ...cat,
        emojis: cat.emojis.filter(() => true), // simple pass-through for now
      })).filter((cat) => cat.emojis.length > 0)
    : EMOJI_DATA;

  return (
    <div className="emoji-picker-popup" ref={ref}>
      <input
        className="emoji-picker-search"
        placeholder="Search emoji…"
        value={query}
        onChange={(e) => setQuery(e.target.value)}
        autoFocus
      />
      {filtered.map((cat) => (
        <div key={cat.label}>
          <div className="emoji-category-label">{cat.label}</div>
          <div className="emoji-grid">
            {cat.emojis.map((em) => (
              <button
                key={em}
                className="emoji-btn"
                type="button"
                onClick={() => onSelect(em)}
                title={em}
              >
                {em}
              </button>
            ))}
          </div>
        </div>
      ))}
    </div>
  );
}
