export function isGroupChat(chat) {
  if (!chat) return false;
  return chat.kind === 'group' || chat.type === 'group' || chat.is_group === true;
}

export function chatPeer(chat, currentUserId) {
  if (!chat) return null;
  if (chat.peer) return chat.peer;
  const members = chat.members ?? [];
  return members.find((m) => m.id !== currentUserId) ?? null;
}

export function chatTitle(chat, currentUserId) {
  if (!chat) return '';
  if (chat.title) return chat.title;
  const other = chatPeer(chat, currentUserId);
  return other?.display_name || other?.username || 'Direct Message';
}

export function messageSenderId(msg) {
  return msg?.sender_id ?? msg?.user_id ?? msg?.sender?.id ?? msg?.user?.id ?? null;
}

export function isOwnMessage(msg, currentUserId) {
  return messageSenderId(msg) === currentUserId;
}

export function messageSenderLabel(msg) {
  return (
    msg?.sender?.display_name ||
    msg?.sender?.username ||
    msg?.user?.display_name ||
    msg?.user?.username ||
    msg?.sender_name ||
    'Unknown'
  );
}

export function normalizeMessage(raw) {
  if (!raw || typeof raw !== 'object') return raw;
  const msg = { ...raw };
  if (msg.message && typeof msg.message === 'object') {
    Object.assign(msg, msg.message);
  }
  if (msg.sender_id == null && msg.user_id != null) msg.sender_id = msg.user_id;
  if (!msg.sender && msg.user) msg.sender = msg.user;
  return msg;
}

export function receiptRank(status) {
  switch (status) {
    case 'seen':
      return 3;
    case 'delivered':
      return 2;
    case 'sent':
      return 1;
    case 'pending':
    default:
      return 0;
  }
}

export function mergeReceiptStatus(prev, next) {
  return receiptRank(next) >= receiptRank(prev) ? next : prev;
}
