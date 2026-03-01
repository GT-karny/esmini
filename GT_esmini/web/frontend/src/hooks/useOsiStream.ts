import { useState, useEffect, useRef, useCallback } from 'react';

export interface OsiObject {
  id: number;
  x: number;
  y: number;
  z: number;
  h: number;
  speed: number;
  head_light: string;
  indicator: string;
  brake_light: string;
}

export interface HvdData {
  sim_time: number;
  throttle: number;
  brake: number;
  steering_angle: number;
  gear: number;
  rpm: number;
  torque: number;
  speed: number;
}

export type OsiConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

interface OsiStreamResult {
  status: OsiConnectionStatus;
  objects: OsiObject[];
  simTime: number;
  hvdData: HvdData | null;
  frameCount: number;
}

const HVD_THROTTLE_MS = 100; // 10Hz max for HVD updates

export function useOsiStream(jobId: string | null): OsiStreamResult {
  const [status, setStatus] = useState<OsiConnectionStatus>('connecting');
  const [objects, setObjects] = useState<OsiObject[]>([]);
  const [simTime, setSimTime] = useState(0);
  const [hvdData, setHvdData] = useState<HvdData | null>(null);
  const [frameCount, setFrameCount] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);
  const lastHvdUpdateRef = useRef(0);

  const connect = useCallback(() => {
    if (!jobId) return;

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/osi/${jobId}`;

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
        if (msg.type === 'ground_truth') {
          setObjects(msg.objects ?? []);
          setSimTime(msg.sim_time ?? 0);
          setFrameCount((c) => c + 1);
        } else if (msg.type === 'host_vehicle_data') {
          const now = performance.now();
          if (now - lastHvdUpdateRef.current >= HVD_THROTTLE_MS) {
            setHvdData(msg as HvdData);
            lastHvdUpdateRef.current = now;
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
    if (!jobId) {
      setStatus('disconnected');
      setObjects([]);
      setSimTime(0);
      setHvdData(null);
      setFrameCount(0);
      return;
    }

    setStatus('connecting');
    connect();
    return () => {
      wsRef.current?.close();
    };
  }, [connect, jobId]);

  return { status, objects, simTime, hvdData, frameCount };
}
