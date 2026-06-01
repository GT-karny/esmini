import { useState, useEffect, useRef, useCallback } from 'react';

export interface OsiObject {
  id: number;
  name: string;
  x: number;
  y: number;
  z: number;
  h: number;
  speed: number;
  head_light: string;
  indicator: string;
  brake_light: string;
  obj_type: string;
  vehicle_class: string;
  length: number;
  width: number;
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

export interface TrafficLight {
  id: number;
  x: number;
  y: number;
  z: number;
  h: number;
  color: string; // 'red' | 'yellow' | 'green' | 'unknown' | ...
  mode: string;  // 'off' | 'constant' | 'flashing' | 'counting' | ...
  icon: number;
}

export type OsiConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

interface OsiStreamResult {
  status: OsiConnectionStatus;
  objects: OsiObject[];
  simTime: number;
  hvdData: HvdData | null;
  trafficLights: TrafficLight[];
  frameCount: number;
}

const HVD_THROTTLE_MS = 100; // 10Hz max for HVD updates

export function useOsiStream(jobId: string | null): OsiStreamResult {
  const [status, setStatus] = useState<OsiConnectionStatus>('connecting');
  const [objects, setObjects] = useState<OsiObject[]>([]);
  const [simTime, setSimTime] = useState(0);
  const [hvdData, setHvdData] = useState<HvdData | null>(null);
  const [trafficLights, setTrafficLights] = useState<TrafficLight[]>([]);
  const [frameCount, setFrameCount] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);
  const lastHvdUpdateRef = useRef(0);

  // rAF coalescing: GroundTruth can arrive at up to ~100Hz, but the DOM can
  // only paint at the display refresh rate. Buffer the latest frame and flush
  // at most once per animation frame so React commits ≤ refresh rate instead
  // of once per incoming message.
  const pendingGtRef = useRef<{
    objects: OsiObject[];
    simTime: number;
    frames: number;
    trafficLights: TrafficLight[] | null;
  } | null>(null);
  const recvCountRef = useRef(0);
  const rafRef = useRef<number | null>(null);

  const flushGt = useCallback(() => {
    rafRef.current = null;
    const p = pendingGtRef.current;
    if (!p) return;
    pendingGtRef.current = null;
    setObjects(p.objects);
    setSimTime(p.simTime);
    setFrameCount(p.frames);
    // Only replace lights when the frame actually carried them, so scenarios
    // that briefly omit the field don't flicker the overlay off.
    if (p.trafficLights !== null) setTrafficLights(p.trafficLights);
  }, []);

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
          recvCountRef.current += 1;
          pendingGtRef.current = {
            objects: msg.objects ?? [],
            simTime: msg.sim_time ?? 0,
            frames: recvCountRef.current,
            trafficLights: msg.traffic_lights ?? null,
          };
          if (rafRef.current == null) {
            rafRef.current = requestAnimationFrame(flushGt);
          }
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
  }, [jobId, flushGt]);

  useEffect(() => {
    if (!jobId) {
      setStatus('disconnected');
      setObjects([]);
      setSimTime(0);
      setHvdData(null);
      setTrafficLights([]);
      setFrameCount(0);
      return;
    }

    recvCountRef.current = 0;
    pendingGtRef.current = null;
    setStatus('connecting');
    connect();
    return () => {
      if (rafRef.current != null) {
        cancelAnimationFrame(rafRef.current);
        rafRef.current = null;
      }
      wsRef.current?.close();
    };
  }, [connect, jobId]);

  return { status, objects, simTime, hvdData, trafficLights, frameCount };
}
