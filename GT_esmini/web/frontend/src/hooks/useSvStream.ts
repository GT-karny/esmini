import { useState, useRef } from 'react';
import { useWebSocketStream, type WebSocketConnectionStatus } from './useWebSocketStream';

export type SvConnectionStatus = WebSocketConnectionStatus;

interface SvStreamResult {
  status: SvConnectionStatus;
  variables: Record<string, unknown>;
  simTime: number;
}

const THROTTLE_MS = 100; // 10 Hz max

export function useSvStream(jobId: string | null): SvStreamResult {
  const [variables, setVariables] = useState<Record<string, unknown>>({});
  const [simTime, setSimTime] = useState(0);
  const lastUpdateRef = useRef(0);

  const status = useWebSocketStream({
    path: jobId ? `/ws/sv/${jobId}` : null,
    onReset: () => {
      setVariables({});
      setSimTime(0);
      lastUpdateRef.current = 0;
    },
    onMessage: (msg) => {
      if (msg.type === 'scenario_variables') {
        const now = performance.now();
        if (now - lastUpdateRef.current >= THROTTLE_MS) {
          setVariables((msg.variables as Record<string, unknown> | undefined) ?? {});
          setSimTime((msg.sim_time as number | undefined) ?? 0);
          lastUpdateRef.current = now;
        }
      }
    },
  });

  return { status, variables, simTime };
}
