import { useState, useEffect, useRef, useCallback } from 'react';

export type SvConnectionStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

interface SvStreamResult {
  status: SvConnectionStatus;
  variables: Record<string, unknown>;
  simTime: number;
}

const THROTTLE_MS = 100; // 10 Hz max

export function useSvStream(jobId: string | null): SvStreamResult {
  const [status, setStatus] = useState<SvConnectionStatus>('connecting');
  const [variables, setVariables] = useState<Record<string, unknown>>({});
  const [simTime, setSimTime] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);
  const lastUpdateRef = useRef(0);

  const connect = useCallback(() => {
    if (!jobId) return;

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/sv/${jobId}`;

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
        if (msg.type === 'scenario_variables') {
          const now = performance.now();
          if (now - lastUpdateRef.current >= THROTTLE_MS) {
            setVariables(msg.variables ?? {});
            setSimTime(msg.sim_time ?? 0);
            lastUpdateRef.current = now;
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
      setVariables({});
      setSimTime(0);
      return;
    }

    setStatus('connecting');
    connect();
    return () => {
      wsRef.current?.close();
    };
  }, [connect, jobId]);

  return { status, variables, simTime };
}
