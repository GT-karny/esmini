import { useState, useEffect, useRef, useCallback } from 'react';
import { useWebSocketStream, type WebSocketConnectionStatus } from './useWebSocketStream';

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

export type OsiConnectionStatus = WebSocketConnectionStatus;

interface OsiStreamResult {
  status: OsiConnectionStatus;
  objects: OsiObject[];
  simTime: number;
  hvdData: HvdData | null;
  trafficLights: TrafficLight[];
  frameCount: number;
}

const HVD_THROTTLE_MS = 100; // 10Hz max for HVD updates

/**
 * Subscribes to the OSI GroundTruth stream (/ws/osi/{jobId}) via the shared
 * useWebSocketStream transport. Mirrors useSvStream / useVdStream.
 */
export function useOsiStream(jobId: string | null): OsiStreamResult {
  const [objects, setObjects] = useState<OsiObject[]>([]);
  const [simTime, setSimTime] = useState(0);
  const [hvdData, setHvdData] = useState<HvdData | null>(null);
  const [trafficLights, setTrafficLights] = useState<TrafficLight[]>([]);
  const [frameCount, setFrameCount] = useState(0);
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

  const status = useWebSocketStream({
    path: jobId ? `/ws/osi/${jobId}` : null,
    onReset: () => {
      if (rafRef.current != null) {
        cancelAnimationFrame(rafRef.current);
        rafRef.current = null;
      }
      recvCountRef.current = 0;
      pendingGtRef.current = null;
      lastHvdUpdateRef.current = 0;
      setObjects([]);
      setSimTime(0);
      setHvdData(null);
      setTrafficLights([]);
      setFrameCount(0);
    },
    onMessage: (msg) => {
      if (msg.type === 'ground_truth') {
        recvCountRef.current += 1;
        pendingGtRef.current = {
          objects: (msg.objects as OsiObject[] | undefined) ?? [],
          simTime: (msg.sim_time as number | undefined) ?? 0,
          frames: recvCountRef.current,
          trafficLights: (msg.traffic_lights as TrafficLight[] | undefined) ?? null,
        };
        if (rafRef.current == null) {
          rafRef.current = requestAnimationFrame(flushGt);
        }
      } else if (msg.type === 'host_vehicle_data') {
        const now = performance.now();
        if (now - lastHvdUpdateRef.current >= HVD_THROTTLE_MS) {
          setHvdData(msg as unknown as HvdData);
          lastHvdUpdateRef.current = now;
        }
      }
    },
  });

  // Cancel a pending flush on unmount.
  useEffect(() => {
    return () => {
      if (rafRef.current != null) cancelAnimationFrame(rafRef.current);
    };
  }, []);

  return { status, objects, simTime, hvdData, trafficLights, frameCount };
}
