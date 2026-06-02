import { useState, useEffect, useRef, useCallback } from 'react';
import type { VdTelemetryFrame } from '../api/client';

export type VdConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

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
  const [status, setStatus] = useState<VdConnectionStatus>('connecting');
  const [frame, setFrame] = useState<VdTelemetryFrame | null>(null);
  const [history, setHistory] = useState<VdTelemetryFrame[]>([]);
  const [frameCount, setFrameCount] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);
  const lastRenderRef = useRef(0);
  const historyRef = useRef<VdTelemetryFrame[]>([]);

  const connect = useCallback(() => {
    if (!jobId) return;

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/vd/${jobId}`;

    const ws = new WebSocket(wsUrl);
    wsRef.current = ws;

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
        if (msg.type === 'virtual_driver_telemetry') {
          const tel = msg as VdTelemetryFrame;
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
      } catch {
        // ignore parse errors
      }
    };

    ws.onerror = () => setStatus('error');
    ws.onclose = () => setStatus('disconnected');
  }, [jobId]);

  useEffect(() => {
    historyRef.current = [];
    setHistory([]);
    setFrame(null);
    setFrameCount(0);
    if (!jobId) {
      setStatus('disconnected');
      return;
    }
    setStatus('connecting');
    connect();
    return () => {
      wsRef.current?.close();
    };
  }, [connect, jobId]);

  return { status, frame, history, frameCount };
}
