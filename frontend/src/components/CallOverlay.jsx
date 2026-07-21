import { useEffect, useRef, useState } from 'react';
import { Kind, packVideo, packVoice, unpack } from '../pulse_media.js';

/**
 * In-call UI: captures mic (+ optional camera), streams via pulse_media frames,
 * plays remote audio and shows remote JPEG video frames.
 */
export default function CallOverlay({
  call,
  currentUserId,
  onSendBinary,
  onEnd,
  incomingFrame,
}) {
  const localVideoRef = useRef(null);
  const remoteImgRef = useRef(null);
  const [muted, setMuted] = useState(false);
  const [elapsed, setElapsed] = useState(0);
  const streamRef = useRef(null);
  const audioCtxRef = useRef(null);
  const processorRef = useRef(null);
  const videoTimerRef = useRef(null);
  const canvasRef = useRef(null);
  const playCtxRef = useRef(null);
  const nextPlayTimeRef = useRef(0);
  const ownStreamId = useRef((currentUserId % 60000) + 1);
  const mutedRef = useRef(false);

  useEffect(() => { mutedRef.current = muted; }, [muted]);

  // Elapsed timer
  useEffect(() => {
    const t = setInterval(() => setElapsed((s) => s + 1), 1000);
    return () => clearInterval(t);
  }, []);

  // Start local capture
  useEffect(() => {
    let cancelled = false;
    const video = call.kind === 'video';

    (async () => {
      try {
        const stream = await navigator.mediaDevices.getUserMedia({
          audio: {
            echoCancellation: true,
            noiseSuppression: true,
            sampleRate: 16000,
          },
          video: video ? { width: 320, height: 240, frameRate: 8 } : false,
        });
        if (cancelled) {
          stream.getTracks().forEach((t) => t.stop());
          return;
        }
        streamRef.current = stream;

        if (video && localVideoRef.current) {
          localVideoRef.current.srcObject = stream;
        }

        // --- Audio capture → PCM Int16 → Voice frames ---
        const audioCtx = new AudioContext({ sampleRate: 16000 });
        audioCtxRef.current = audioCtx;
        const source = audioCtx.createMediaStreamSource(stream);
        const processor = audioCtx.createScriptProcessor(2048, 1, 1);
        processorRef.current = processor;
        processor.onaudioprocess = (e) => {
          if (mutedRef.current) return;
          const input = e.inputBuffer.getChannelData(0);
          const pcm = new Int16Array(input.length);
          for (let i = 0; i < input.length; i++) {
            const s = Math.max(-1, Math.min(1, input[i]));
            pcm[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
          }
          onSendBinary?.(packVoice(pcm.buffer, ownStreamId.current));
        };
        source.connect(processor);
        processor.connect(audioCtx.destination);

        // --- Video: JPEG snapshots as Video frames ---
        if (video) {
          const canvas = document.createElement('canvas');
          canvas.width = 320;
          canvas.height = 240;
          canvasRef.current = canvas;
          const ctx = canvas.getContext('2d');
          videoTimerRef.current = setInterval(() => {
            const track = stream.getVideoTracks()[0];
            if (!track || track.readyState !== 'live') return;
            const vid = localVideoRef.current;
            if (!vid || vid.readyState < 2) return;
            ctx.drawImage(vid, 0, 0, canvas.width, canvas.height);
            canvas.toBlob(
              (blob) => {
                if (!blob) return;
                blob.arrayBuffer().then((buf) => {
                  onSendBinary?.(packVideo(buf, ownStreamId.current, true));
                });
              },
              'image/jpeg',
              0.55,
            );
          }, 120);
        }
      } catch (err) {
        console.error('media error', err);
        onEnd?.();
      }
    })();

    return () => {
      cancelled = true;
      clearInterval(videoTimerRef.current);
      try { processorRef.current?.disconnect(); } catch { /* */ }
      try { audioCtxRef.current?.close(); } catch { /* */ }
      streamRef.current?.getTracks().forEach((t) => t.stop());
      try { playCtxRef.current?.close(); } catch { /* */ }
    };
  }, [call.kind, onSendBinary, onEnd]);

  // Play remote frames
  useEffect(() => {
    if (!incomingFrame) return;
    const frame = unpack(incomingFrame);
    if (!frame) return;
    if (frame.streamId === ownStreamId.current) return; // skip echo

    if (frame.kind === Kind.Voice) {
      playPcm(frame.payload);
    } else if (frame.kind === Kind.Video || frame.kind === Kind.Image) {
      if (remoteImgRef.current && frame.payload.length) {
        const blob = new Blob([frame.payload], { type: frame.mime || 'image/jpeg' });
        const url = URL.createObjectURL(blob);
        const prev = remoteImgRef.current.src;
        remoteImgRef.current.src = url;
        if (prev?.startsWith('blob:')) URL.revokeObjectURL(prev);
      }
    }
  }, [incomingFrame]);

  function playPcm(payload) {
    try {
      if (!playCtxRef.current) {
        playCtxRef.current = new AudioContext({ sampleRate: 16000 });
        nextPlayTimeRef.current = 0;
      }
      const ctx = playCtxRef.current;
      const int16 = new Int16Array(payload.buffer, payload.byteOffset, payload.byteLength / 2);
      const float32 = new Float32Array(int16.length);
      for (let i = 0; i < int16.length; i++) float32[i] = int16[i] / 0x8000;
      const buf = ctx.createBuffer(1, float32.length, 16000);
      buf.copyToChannel(float32, 0);
      const src = ctx.createBufferSource();
      src.buffer = buf;
      src.connect(ctx.destination);
      const now = ctx.currentTime;
      const start = Math.max(now + 0.02, nextPlayTimeRef.current);
      src.start(start);
      nextPlayTimeRef.current = start + buf.duration;
    } catch {
      /* ignore */
    }
  }

  const fmt = (s) =>
    `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;

  const isVideo = call.kind === 'video';
  const title =
    call.status === 'ringing'
      ? `Incoming ${call.kind} call from ${call.from_name || '…'}`
      : call.status === 'calling'
        ? `Calling…`
        : `${call.kind === 'video' ? 'Video' : 'Voice'} call`;

  return (
    <div className="call-overlay">
      <div className="call-panel">
        <p className="call-title">{title}</p>
        <p className="call-timer">{fmt(elapsed)}</p>

        {isVideo && call.status === 'active' && (
          <div className="call-videos">
            <img ref={remoteImgRef} className="call-remote" alt="Remote" />
            <video ref={localVideoRef} className="call-local" muted autoPlay playsInline />
          </div>
        )}

        {!isVideo && call.status === 'active' && (
          <div className="call-avatar-lg">{(call.from_name || '?').charAt(0).toUpperCase()}</div>
        )}

        <div className="call-actions">
          {call.status === 'ringing' ? (
            <>
              <button type="button" className="call-btn accept" onClick={() => call.onAccept?.()}>
                Accept
              </button>
              <button type="button" className="call-btn reject" onClick={() => call.onReject?.()}>
                Decline
              </button>
            </>
          ) : (
            <>
              {call.status === 'active' && (
                <button
                  type="button"
                  className={`call-btn mute${muted ? ' on' : ''}`}
                  onClick={() => setMuted((m) => !m)}
                >
                  {muted ? 'Unmute' : 'Mute'}
                </button>
              )}
              <button type="button" className="call-btn hangup" onClick={onEnd}>
                End
              </button>
            </>
          )}
        </div>
      </div>
    </div>
  );
}
