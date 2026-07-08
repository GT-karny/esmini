import { useEffect, useRef, useState } from 'react';

export type WebSocketConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

interface WebSocketStreamOptions {
  /** Absolute path on the current host, e.g. `/ws/vd/{jobId}`. Null disables the stream. */
  path: string | null;
  onMessage: (message: any) => void;
  onReset?: () => void;
}

export function useWebSocketStream({ path, onMessage, onReset }: WebSocketStreamOptions): WebSocketConnectionStatus {
  const [status, setStatus] = useState<WebSocketConnectionStatus>('connecting');
  const messageRef = useRef(onMessage);
  const resetRef = useRef(onReset);

  messageRef.current = onMessage;
  resetRef.current = onReset;

  useEffect(() => {
    resetRef.current?.();
    if (!path) {
      setStatus('disconnected');
      return;
    }

    setStatus('connecting');
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${protocol}//${window.location.host}${path}`);

    ws.onopen = () => setStatus('connected');
    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
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
      ws.close();
    };
  }, [path]);

  return status;
}
