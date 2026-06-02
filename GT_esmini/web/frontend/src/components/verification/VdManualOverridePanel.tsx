import { useEffect, useRef, useState } from 'react';

/* Button bitmask — must match gt_esmini::ButtonBits (VehicleCommand.hpp). */
const BTN = {
  OVERRIDE: 1 << 0,
  INDICATOR_LEFT: 1 << 1,
  INDICATOR_RIGHT: 1 << 2,
  HAZARD: 1 << 6,
} as const;

const SEND_HZ = 30;

/**
 * Live manual-override control for VirtualDriver. Streams pedal/steer/indicator
 * commands to the backend over /ws/input/{jobId}, which packs them into the
 * NetworkInputBridge wire format and forwards via UDP to GT_Sim. Requires the
 * run to have been launched with manual override enabled (input_type=network).
 */
export function VdManualOverridePanel({ jobId }: { jobId: string }) {
  const [steering, setSteering] = useState(0);
  const [throttle, setThrottle] = useState(0);
  const [brake, setBrake] = useState(0);
  const [override, setOverride] = useState(false);
  const [left, setLeft] = useState(false);
  const [right, setRight] = useState(false);
  const [hazard, setHazard] = useState(false);
  const [connected, setConnected] = useState(false);

  const wsRef = useRef<WebSocket | null>(null);
  // Latest command, read by the send interval (avoids re-opening the socket).
  const cmdRef = useRef({ steering, throttle, brake, buttons: 0 });

  useEffect(() => {
    let buttons = 0;
    if (override) buttons |= BTN.OVERRIDE;
    if (left) buttons |= BTN.INDICATOR_LEFT;
    if (right) buttons |= BTN.INDICATOR_RIGHT;
    if (hazard) buttons |= BTN.HAZARD;
    cmdRef.current = { steering, throttle, brake, buttons };
  }, [steering, throttle, brake, override, left, right, hazard]);

  useEffect(() => {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${protocol}//${window.location.host}/ws/input/${jobId}`);
    wsRef.current = ws;
    ws.onopen = () => setConnected(true);
    ws.onclose = () => setConnected(false);
    ws.onerror = () => setConnected(false);

    const timer = window.setInterval(() => {
      if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(cmdRef.current));
    }, 1000 / SEND_HZ);

    return () => {
      window.clearInterval(timer);
      ws.close();
    };
  }, [jobId]);

  // Indicator buttons are mutually exclusive (like a real stalk).
  const toggleLeft = () => { setLeft((v) => !v); setRight(false); };
  const toggleRight = () => { setRight((v) => !v); setLeft(false); };

  return (
    <div className="rounded border border-glass-edge p-3 text-xs space-y-2">
      <div className="flex items-center justify-between">
        <span className="font-medium text-foreground">Manual override</span>
        <span className={connected ? 'text-success' : 'text-text-tertiary'}>
          ● {connected ? 'linked' : 'offline'}
        </span>
      </div>

      <Slider label="steer" min={-1} max={1} step={0.01} value={steering} onChange={setSteering} fmt={(v) => v.toFixed(2)} />
      <Slider label="throttle" min={0} max={1} step={0.01} value={throttle} onChange={(v) => { setThrottle(v); if (v > 0) setBrake(0); }} fmt={(v) => v.toFixed(2)} />
      <Slider label="brake" min={0} max={1} step={0.01} value={brake} onChange={(v) => { setBrake(v); if (v > 0) setThrottle(0); }} fmt={(v) => v.toFixed(2)} />

      <div className="flex flex-wrap gap-1.5 pt-1">
        <Toggle label="Override" active={override} onClick={() => setOverride((v) => !v)} />
        <Toggle label="◀ L" active={left} onClick={toggleLeft} />
        <Toggle label="R ▶" active={right} onClick={toggleRight} />
        <Toggle label="Hazard" active={hazard} onClick={() => setHazard((v) => !v)} />
      </div>

      <button
        onClick={() => { setSteering(0); setThrottle(0); setBrake(0); }}
        className="w-full mt-1 px-2 py-1 rounded text-[11px] border border-glass-edge text-text-secondary hover:bg-glass-2"
      >
        Center / release pedals
      </button>
      <p className="text-[10px] text-text-tertiary leading-tight">
        Steer &gt; 0.05 or pedal &gt; 0.1 takes the corresponding domain (or use Override).
      </p>
    </div>
  );
}

function Slider({
  label, min, max, step, value, onChange, fmt,
}: {
  label: string; min: number; max: number; step: number;
  value: number; onChange: (v: number) => void; fmt: (v: number) => string;
}) {
  return (
    <div>
      <div className="flex justify-between text-[11px] text-text-tertiary">
        <span>{label}</span>
        <span className="font-mono text-foreground">{fmt(value)}</span>
      </div>
      <input
        type="range" min={min} max={max} step={step} value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        className="w-full accent-primary"
      />
    </div>
  );
}

function Toggle({ label, active, onClick }: { label: string; active: boolean; onClick: () => void }) {
  return (
    <button
      onClick={onClick}
      className={`px-2 py-1 rounded text-[11px] border border-glass-edge ${
        active ? 'bg-primary/70 text-white' : 'text-text-secondary hover:bg-glass-2'
      }`}
    >
      {label}
    </button>
  );
}
