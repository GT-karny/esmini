import { useEffect, useRef, useState } from 'react';

import type { WheelAxisMapping, WheelProbeFrame, WheelProbeMeta } from '../api/client';

export type WheelProbeStatusState = 'idle' | 'connecting' | 'streaming' | 'error';

interface UseWheelProbeOptions {
  /** Open the stream only while the panel section is actually visible. */
  enabled: boolean;
  device: number;
  hz?: number;
  /**
   * Mapping used for the normalized preview. Sent to the server on change, which
   * restarts the probe child with new flags -- so the bars follow edits the user
   * has NOT saved yet, computed by the same C++ normalizer a run would use.
   */
  mapping: WheelAxisMapping;
}

interface UseWheelProbeResult {
  status: WheelProbeStatusState;
  meta: WheelProbeMeta | null;
  frame: WheelProbeFrame | null;
  /** Server-side error text (probe missing, device index absent, ...). */
  error: string | null;
}

// Debounce for mapping pushes: each one restarts the probe process, and a user
// dragging a spinbox would otherwise restart it per keystroke.
const MAPPING_PUSH_DEBOUNCE_MS = 300;

/**
 * Live axis/button readout from GT_WheelProbe (feature:F8).
 *
 * A dedicated hook rather than useWebSocketStream because this stream is
 * bidirectional: the mapping under edit is pushed back so the preview reflects
 * it. The browser's own Gamepad API is deliberately NOT used for axes -- it
 * exposes a different index space than SDL, so an axis "detected" there could
 * be written into the config as the wrong index.
 */
export function useWheelProbe({
  enabled,
  device,
  hz = 30,
  mapping,
}: UseWheelProbeOptions): UseWheelProbeResult {
  const [status, setStatus] = useState<WheelProbeStatusState>('idle');
  const [meta, setMeta] = useState<WheelProbeMeta | null>(null);
  const [frame, setFrame] = useState<WheelProbeFrame | null>(null);
  const [error, setError] = useState<string | null>(null);
  const socketRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    if (!enabled) {
      setStatus('idle');
      setMeta(null);
      setFrame(null);
      setError(null);
      return;
    }

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(
      `${protocol}//${window.location.host}/api/wheel-probe/stream?device=${device}&hz=${hz}`,
    );
    socketRef.current = ws;
    setStatus('connecting');
    setError(null);

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        if (msg.type === 'meta') {
          setMeta(msg as WheelProbeMeta);
          setStatus('streaming');
        } else if (msg.type === 'frame') {
          setFrame(msg as WheelProbeFrame);
          setStatus('streaming');
        } else if (msg.type === 'error') {
          setError(String(msg.message ?? 'probe error'));
          setStatus('error');
        }
      } catch {
        // Ignore malformed frames rather than tearing the stream down.
      }
    };
    ws.onerror = () => setStatus('error');
    ws.onclose = () => {
      socketRef.current = null;
      setStatus((prev) => (prev === 'error' ? prev : 'idle'));
    };

    return () => {
      ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null;
      ws.close();
      socketRef.current = null;
    };
  }, [enabled, device, hz]);

  // Push mapping edits (debounced). Serialized so the effect only fires on an
  // actual value change, not on every parent re-render producing a new object.
  const mappingJson = JSON.stringify(mapping);
  useEffect(() => {
    if (!enabled) return;
    const timer = setTimeout(() => {
      const ws = socketRef.current;
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ axis_mapping: JSON.parse(mappingJson) }));
      }
    }, MAPPING_PUSH_DEBOUNCE_MS);
    return () => clearTimeout(timer);
  }, [enabled, mappingJson]);

  return { status, meta, frame, error };
}
