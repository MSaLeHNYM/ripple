/** Human-readable errors for getUserMedia / mic+camera. */

export function isSecureMediaContext() {
  if (typeof window === 'undefined') return false;
  if (window.isSecureContext) return true;
  // Some older browsers: https or localhost only
  const h = window.location.hostname;
  return window.location.protocol === 'https:'
    || h === 'localhost'
    || h === '127.0.0.1'
    || h === '[::1]';
}

/**
 * @param {unknown} err
 * @param {'microphone'|'camera'|'media'} [kind]
 */
export function mediaAccessErrorMessage(err, kind = 'microphone') {
  const name = err && typeof err === 'object' && 'name' in err
    ? String(err.name)
    : '';
  const what = kind === 'camera' ? 'Camera' : kind === 'media' ? 'Mic/camera' : 'Microphone';

  if (!navigator.mediaDevices?.getUserMedia) {
    if (!isSecureMediaContext()) {
      return (
        `${what} needs HTTPS.\n\n` +
        `Open Ripple as https://… (not http://).\n` +
        `On a phone/LAN use the HTTPS URL from ./run.sh and accept the certificate warning.`
      );
    }
    return `${what} is not available in this browser.`;
  }

  if (!isSecureMediaContext()) {
    return (
      `${what} blocked: this page is not a secure context.\n\n` +
      `Use https://host:8443 (run ./run.sh). Plain http://192.168.x.x will always deny the mic.`
    );
  }

  if (name === 'NotAllowedError' || name === 'PermissionDeniedError') {
    return (
      `${what} permission denied.\n\n` +
      `Allow access in the browser site settings (lock icon → Permissions),\n` +
      `then reload. If you previously clicked Block, clear that for this site.`
    );
  }
  if (name === 'NotFoundError' || name === 'DevicesNotFoundError') {
    return `No ${what.toLowerCase()} device found.`;
  }
  if (name === 'NotReadableError' || name === 'TrackStartError') {
    return `${what} is busy (another app may be using it).`;
  }
  if (name === 'SecurityError') {
    return (
      `${what} blocked by the browser security policy.\n` +
      `Confirm you are on https:// and have accepted the certificate.`
    );
  }

  const detail = err && typeof err === 'object' && 'message' in err
    ? String(err.message)
    : String(err || 'unknown error');
  return `${what} failed: ${detail || name || 'unknown error'}`;
}

/**
 * @param {MediaStreamConstraints} constraints
 * @param {'microphone'|'camera'|'media'} [kind]
 */
export async function requestUserMedia(constraints, kind = 'media') {
  if (!navigator.mediaDevices?.getUserMedia) {
    throw Object.assign(new Error(mediaAccessErrorMessage(null, kind)), {
      name: 'NotSupportedError',
    });
  }
  try {
    return await navigator.mediaDevices.getUserMedia(constraints);
  } catch (err) {
    const wrapped = new Error(mediaAccessErrorMessage(err, kind));
    wrapped.name = err?.name || 'MediaError';
    wrapped.cause = err;
    throw wrapped;
  }
}
