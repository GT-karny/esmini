import { useState, useEffect, useRef, useCallback } from 'react';

interface OsiObject {
  id: number;
  x: number;
  y: number;
  z: number;
  h: number;
  speed: number;
}

interface OsiMessage {
  type: string;
  sim_time: number;
  object_count: number;
  objects: OsiObject[];
  error?: string;
  reason?: string;
}

type ConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

export function OsiLivePanel({ jobId }: { jobId: string }) {
  const [status, setStatus] = useState<ConnectionStatus>('connecting');
  const [data, setData] = useState<OsiMessage | null>(null);
  const [frameCount, setFrameCount] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);

  const connect = useCallback(() => {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/osi/${jobId}`;

    const ws = new WebSocket(wsUrl);
    wsRef.current = ws;

    ws.onopen = () => setStatus('connected');

    ws.onmessage = (event) => {
      try {
        const msg: OsiMessage = JSON.parse(event.data);
        if (msg.type === 'end') {
          setStatus('disconnected');
          return;
        }
        if (msg.error) {
          setStatus('error');
          return;
        }
        setData(msg);
        setFrameCount((c) => c + 1);
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
    connecting: { color: 'bg-yellow-400', label: 'Connecting' },
    connected: { color: 'bg-green-400', label: 'Connected' },
    disconnected: { color: 'bg-gray-400', label: 'Disconnected' },
    error: { color: 'bg-red-400', label: 'Error' },
  };

  const { color, label } = statusIndicator[status];

  return (
    <section className="bg-gray-900 rounded-lg border border-gray-800 p-4 mb-4">
      <div className="flex items-center justify-between mb-3">
        <h2 className="text-sm font-medium text-gray-400">Live OSI Data</h2>
        <div className="flex items-center gap-2 text-xs">
          <span className={`inline-block w-2 h-2 rounded-full ${color}`} />
          <span className="text-gray-500">{label}</span>
          {frameCount > 0 && (
            <span className="text-gray-600 ml-2">
              {frameCount} frames
            </span>
          )}
        </div>
      </div>

      {data && data.objects.length > 0 ? (
        <>
          <div className="text-xs text-gray-500 mb-2">
            t = {data.sim_time.toFixed(2)}s | {data.object_count} object(s)
          </div>
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="text-left text-gray-500 text-xs border-b border-gray-800">
                  <th className="py-1.5 pr-4">ID</th>
                  <th className="py-1.5 pr-4">X</th>
                  <th className="py-1.5 pr-4">Y</th>
                  <th className="py-1.5 pr-4">Speed (m/s)</th>
                  <th className="py-1.5">Heading (rad)</th>
                </tr>
              </thead>
              <tbody className="font-mono">
                {data.objects.map((obj) => (
                  <tr key={obj.id} className="border-b border-gray-800/50">
                    <td className="py-1.5 pr-4 text-gray-300">{obj.id}</td>
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
        <p className="text-gray-500 text-sm">Waiting for OSI data...</p>
      ) : status === 'error' ? (
        <p className="text-red-400 text-sm">Failed to connect to OSI stream</p>
      ) : null}
    </section>
  );
}
