import { useState, useEffect, useRef, useCallback } from 'react';
import { HvdGaugePanel } from './HvdGaugePanel';

interface OsiObject {
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

interface OsiMessage {
  type: string;
  sim_time: number;
  object_count: number;
  objects: OsiObject[];
  error?: string;
  reason?: string;
}

export interface HvdMessage {
  type: 'host_vehicle_data';
  sim_time: number;
  throttle: number;
  brake: number;
  steering_angle: number;
  gear: number;
  rpm: number;
  torque: number;
  speed: number;
}

export interface EgoLights {
  head_light: string;
  indicator: string;
  brake_light: string;
}

type ConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

const HVD_THROTTLE_MS = 100; // update HVD gauges at most 10 Hz

export function OsiLivePanel({ jobId }: { jobId: string }) {
  const [status, setStatus] = useState<ConnectionStatus>('connecting');
  const [data, setData] = useState<OsiMessage | null>(null);
  const [hvdData, setHvdData] = useState<HvdMessage | null>(null);
  const [frameCount, setFrameCount] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);
  const lastHvdUpdateRef = useRef(0);

  const connect = useCallback(() => {
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
          setData(msg as OsiMessage);
          setFrameCount((c) => c + 1);
        } else if (msg.type === 'host_vehicle_data') {
          const now = performance.now();
          if (now - lastHvdUpdateRef.current >= HVD_THROTTLE_MS) {
            setHvdData(msg as HvdMessage);
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
    connect();
    return () => {
      wsRef.current?.close();
    };
  }, [connect]);

  const statusIndicator: Record<ConnectionStatus, { color: string; label: string }> = {
    connecting: { color: 'bg-warning', label: 'Connecting' },
    connected: { color: 'bg-success', label: 'Connected' },
    disconnected: { color: 'bg-text-tertiary', label: 'Disconnected' },
    error: { color: 'bg-destructive', label: 'Error' },
  };

  const { color, label } = statusIndicator[status];

  // Extract ego vehicle (first object) light state for the gauge panel
  const egoLights: EgoLights | null = data && data.objects.length > 0
    ? {
        head_light: data.objects[0].head_light ?? 'off',
        indicator: data.objects[0].indicator ?? 'off',
        brake_light: data.objects[0].brake_light ?? 'off',
      }
    : null;

  return (
    <section className="glass p-4 mb-4">
      <div className="flex items-center justify-between mb-3">
        <h2 className="text-sm font-medium text-text-secondary">Live OSI Data</h2>
        <div className="flex items-center gap-2 text-xs">
          <span className={`inline-block w-2 h-2 rounded-full ${color}`} />
          <span className="text-text-secondary">{label}</span>
          {frameCount > 0 && (
            <span className="text-text-tertiary ml-2">
              {frameCount} frames
            </span>
          )}
        </div>
      </div>

      {data && data.objects.length > 0 ? (
        <>
          <div className="text-xs text-text-secondary mb-2">
            t = {data.sim_time.toFixed(2)}s | {data.object_count} object(s)
          </div>
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="text-left text-text-secondary text-xs border-b border-glass-edge">
                  <th className="py-1.5 pr-4">ID</th>
                  <th className="py-1.5 pr-4">X</th>
                  <th className="py-1.5 pr-4">Y</th>
                  <th className="py-1.5 pr-4">Speed (m/s)</th>
                  <th className="py-1.5">Heading (rad)</th>
                </tr>
              </thead>
              <tbody className="font-mono">
                {data.objects.map((obj) => (
                  <tr key={obj.id} className="border-b border-glass-edge/50">
                    <td className="py-1.5 pr-4 text-foreground">{obj.id}</td>
                    <td className="py-1.5 pr-4">{obj.x.toFixed(2)}</td>
                    <td className="py-1.5 pr-4">{obj.y.toFixed(2)}</td>
                    <td className="py-1.5 pr-4">{obj.speed.toFixed(2)}</td>
                    <td className="py-1.5">{obj.h.toFixed(3)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </>
      ) : status === 'connected' ? (
        <p className="text-text-secondary text-sm">Waiting for OSI data...</p>
      ) : status === 'error' ? (
        <p className="text-destructive text-sm">Failed to connect to OSI stream</p>
      ) : null}

      {(hvdData || egoLights) && (
        <HvdGaugePanel hvd={hvdData} lights={egoLights} />
      )}
    </section>
  );
}
