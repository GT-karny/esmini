import { useState, useRef } from 'react';
import type { VdTelemetryFrame } from '../api/client';
import { useWebSocketStream, type WebSocketConnectionStatus } from './useWebSocketStream';

export type VdConnectionStatus = WebSocketConnectionStatus;

interface VdStreamResult {
  status: VdConnectionStatus;
  /** Latest telemetry frame (throttled for rendering). */
  frame: VdTelemetryFrame | null;
  /** Rolling history of received frames (for time-series charts). */
  history: VdTelemetryFrame[];
  frameCount: number;
}

const THROTTLE_MS = 50; // ~20 Hz render cap
const MAX_HISTORY = 2000; // ~100 s @ 20 Hz

/**
 * Subscribes to live VirtualDriver telemetry over WebSocket (/ws/vd/{jobId}).
 * Mirrors useSvStream. The frame shape is identical to the recorded replay, so
 * the same overlay/chart components drive both live and replay.
 */
export function useVdStream(jobId: string | null): VdStreamResult {
  const [frame, setFrame] = useState<VdTelemetryFrame | null>(null);
  const [history, setHistory] = useState<VdTelemetryFrame[]>([]);
  const [frameCount, setFrameCount] = useState(0);
  const lastRenderRef = useRef(0);
  const historyRef = useRef<VdTelemetryFrame[]>([]);

  const status = useWebSocketStream({
    path: jobId ? `/ws/vd/${jobId}` : null,
    onReset: () => {
      historyRef.current = [];
      setHistory([]);
      setFrame(null);
      setFrameCount(0);
      lastRenderRef.current = 0;
    },
    onMessage: (msg) => {
      if (msg.type === 'virtual_driver_telemetry') {
        const tel = msg as unknown as VdTelemetryFrame;
        historyRef.current.push(tel);
        if (historyRef.current.length > MAX_HISTORY) historyRef.current.shift();
        setFrameCount((c) => c + 1);

        const now = performance.now();
        if (now - lastRenderRef.current >= THROTTLE_MS) {
          setFrame(tel);
          setHistory(historyRef.current.slice());
          lastRenderRef.current = now;
        }
      }
    },
  });

  return { status, frame, history, frameCount };
}
