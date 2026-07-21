/** pulse_media PM binary frame pack/unpack (matches Socketify pulse_media.cpp). */

export const Kind = { Voice: 1, Video: 2, Image: 3, ImageEnd: 4 };
export const FrameFlags = { None: 0, KeyFrame: 1, Last: 2 };

function writeU16(view, offset, v) {
  view.setUint8(offset, (v >> 8) & 0xff);
  view.setUint8(offset + 1, v & 0xff);
}
function writeU32(view, offset, v) {
  view.setUint8(offset, (v >>> 24) & 0xff);
  view.setUint8(offset + 1, (v >>> 16) & 0xff);
  view.setUint8(offset + 2, (v >>> 8) & 0xff);
  view.setUint8(offset + 3, v & 0xff);
}
function writeU64(view, offset, v) {
  // JS safe integer — store as two u32
  const hi = Math.floor(v / 0x100000000);
  const lo = v >>> 0;
  writeU32(view, offset, hi);
  writeU32(view, offset + 4, lo);
}
function readU16(view, offset) {
  return (view.getUint8(offset) << 8) | view.getUint8(offset + 1);
}
function readU32(view, offset) {
  return (
    (view.getUint8(offset) << 24) |
    (view.getUint8(offset + 1) << 16) |
    (view.getUint8(offset + 2) << 8) |
    view.getUint8(offset + 3)
  ) >>> 0;
}
function readU64(view, offset) {
  const hi = readU32(view, offset);
  const lo = readU32(view, offset + 4);
  return hi * 0x100000000 + lo;
}

let seqCounter = 0;
export function nextSeq() {
  seqCounter = (seqCounter + 1) >>> 0;
  return seqCounter;
}

export function nowUs() {
  return Math.floor(performance.now() * 1000);
}

/**
 * @returns {ArrayBuffer}
 */
export function pack(kind, streamId, seq, timestampUs, flags, payload, mime = '') {
  const hasMime = kind === Kind.Image || kind === Kind.ImageEnd;
  const mimeBytes = hasMime && mime ? new TextEncoder().encode(mime) : new Uint8Array(0);
  const payloadBytes =
    payload instanceof ArrayBuffer
      ? new Uint8Array(payload)
      : payload instanceof Uint8Array
        ? payload
        : new Uint8Array(payload || []);

  const headerBase = 18;
  const total = headerBase + (hasMime ? 2 + mimeBytes.length : 0) + payloadBytes.length;
  const buf = new ArrayBuffer(total);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  bytes[0] = 0x50; // 'P'
  bytes[1] = 0x4d; // 'M'
  bytes[2] = kind;
  writeU16(view, 3, streamId);
  writeU32(view, 5, seq);
  writeU64(view, 9, timestampUs);
  bytes[17] = flags;

  let off = headerBase;
  if (hasMime) {
    writeU16(view, off, mimeBytes.length);
    off += 2;
    bytes.set(mimeBytes, off);
    off += mimeBytes.length;
  }
  bytes.set(payloadBytes, off);
  return buf;
}

export function unpack(data) {
  const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data);
  if (bytes.length < 18) return null;
  if (bytes[0] !== 0x50 || bytes[1] !== 0x4d) return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

  const kind = bytes[2];
  const streamId = readU16(view, 3);
  const seq = readU32(view, 5);
  const timestampUs = readU64(view, 9);
  const flags = bytes[17];
  let off = 18;
  let mime = '';
  if (kind === Kind.Image || kind === Kind.ImageEnd) {
    if (bytes.length < off + 2) return null;
    const mimeLen = readU16(view, off);
    off += 2;
    if (bytes.length < off + mimeLen) return null;
    if (mimeLen) mime = new TextDecoder().decode(bytes.subarray(off, off + mimeLen));
    off += mimeLen;
  }
  const payload = bytes.subarray(off);
  return { kind, streamId, seq, timestampUs, flags, mime, payload };
}

export function packVoice(pcm, streamId = 1) {
  return pack(Kind.Voice, streamId, nextSeq(), nowUs(), FrameFlags.None, pcm);
}

export function packVideo(jpegBytes, streamId = 1, keyframe = true) {
  return pack(
    Kind.Video,
    streamId,
    nextSeq(),
    nowUs(),
    keyframe ? FrameFlags.KeyFrame : FrameFlags.None,
    jpegBytes,
  );
}
