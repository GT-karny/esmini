import { useEffect, useRef, useState } from 'react';

export type WebSocketConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

/**
 * A parsed JSON frame from a stream endpoint. `type` / `error` form the shared
 * envelope handled here; endpoint-specific payload fields come through the
 * index signature and are narrowed (or cast) by the consuming hook.
 */
export interface WebSocketStreamMessage {
  type?: string;
  error?: unknown;
  [key: string]: unknown;
}

interface WebSocketStreamOptions {
  /** Absolute path on the current host, e.g. `/ws/vd/{jobId}`. Null disables the stream. */
  path: string | null;
  onMessage: (message: WebSocketStreamMessage) => void;
  onReset?: () => void;
}

export function useWebSocketStream({ path, onMessage, onReset }: WebSocketStreamOptions): WebSocketConnectionStatus {
  const [status, setStatus] = useState<WebSocketConnectionStatus>(
    path ? 'connecting' : 'disconnected',
  );
  const messageRef = useRef(onMessage);
  const resetRef = useRef(onReset);

  // Track the latest callbacks without retriggering the connection effect.
  // (Declared before the connection effect so the refs are fresh when it runs.)
  useEffect(() => {
    messageRef.current = onMessage;
    resetRef.current = onReset;
  });

  // Reset the visible status when the target path changes (render-time
  // adjustment; the effect below then opens the new connection).
  const [prevPath, setPrevPath] = useState(path);
  if (prevPath !== path) {
    setPrevPath(path);
    setStatus(path ? 'connecting' : 'disconnected');
  }

  useEffect(() => {
    resetRef.current?.();
    if (!path) return;

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${protocol}//${window.location.host}${path}`);

    ws.onopen = () => setStatus('connected');
    ws.onmessage = (event) => {
      try {
        const msg: WebSocketStreamMessage = JSON.parse(event.data);
        if (msg.type === 'end') {
          setStatus('disconnected');
          return;
        }
        if (msg.error) {
          setStatus('error');
          return;
        }
        messageRef.current(msg);
      } catch {
        // Ignore malformed stream frames.
      }
    };
    ws.onerror = () => setStatus('error');
    ws.onclose = () => setStatus('disconnected');

    return () => {
      // Detach handlers first so the closing socket's onclose can't clobber
      // the status of a connection opened right after (e.g. StrictMode remount).
      ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null;
      ws.close();
    };
  }, [path]);

  return status;
}
