import { useEffect, useMemo, useState } from 'react';
import { useOsiStream, type HvdData } from '../../hooks/useOsiStream';
import { useFps } from '../../hooks/useFps';
import { LiveSceneView, type RoadGeometry } from '../LiveSceneView';
import { api } from '../../api/client';

type DriveMode = 'comfort' | 'sport';

interface EgoLights {
  head: string;      // 'on' | 'off'
  indicator: string; // 'left' | 'right' | 'warning' | 'off'
  brake: string;     // 'normal' | 'strong' | 'off'
}

interface LiveMonitorPanelProps {
  jobId: string;
  projectId?: string;
  scenarioFile?: string;
}

const statusIndicator: Record<string, { color: string; label: string }> = {
  connecting: { color: 'bg-warning', label: 'Connecting' },
  connected: { color: 'bg-success', label: 'Live' },
  disconnected: { color: 'bg-text-tertiary', label: 'Disconnected' },
  error: { color: 'bg-destructive', label: 'Error' },
};

export function LiveMonitorPanel({ jobId, projectId, scenarioFile }: LiveMonitorPanelProps) {
  const { status, objects, simTime, hvdData, trafficLights, frameCount } = useOsiStream(jobId);

  // Render/data FPS meter for tuning scene performance. Shown automatically in
  // dev, and opt-in for packaged builds via `?fps` in the URL or
  // localStorage.gt_show_fps='1' (so it can be measured on a real distribution).
  const { renderFps, dataFps } = useFps(frameCount);
  const showFpsMeter = useMemo(() => {
    if (import.meta.env.DEV) return true;
    if (typeof window === 'undefined') return false;
    try {
      if (new URLSearchParams(window.location.search).has('fps')) return true;
      return window.localStorage.getItem('gt_show_fps') === '1';
    } catch {
      return false;
    }
  }, []);

  // Ego lights come from OSI stream (objects[0]); HVD doesn't carry light fields.
  const egoLights = useMemo<EgoLights | null>(() => {
    if (objects.length === 0) return null;
    const o = objects[0];
    return { head: o.head_light, indicator: o.indicator, brake: o.brake_light };
  }, [objects]);

  // Fetch road geometry once when projectId + scenarioFile are available
  const [roadGeometry, setRoadGeometry] = useState<RoadGeometry | null>(null);
  useEffect(() => {
    if (!projectId || !scenarioFile) return;
    let cancelled = false;
    api.getRoadGeometry(projectId, scenarioFile).then(
      (data) => { if (!cancelled) setRoadGeometry(data); },
      () => { /* silently ignore — road overlay is optional */ },
    );
    return () => { cancelled = true; };
  }, [projectId, scenarioFile]);

  const { color, label } = statusIndicator[status] ?? statusIndicator.connecting;

  return (
    <div className="h-full overflow-y-auto p-3 flex flex-col">
      {/* Status bar */}
      <div className="flex items-center justify-between mb-2">
        <div className="flex items-center gap-2 text-xs">
          <span className={`inline-block w-2 h-2 rounded-full ${color}`} />
          <span className="text-text-secondary">{label}</span>
          {frameCount > 0 && (
            <span className="text-text-tertiary">
              {frameCount} frames
            </span>
          )}
        </div>
        <div className="flex items-center gap-3">
          <DriveModeToggle jobId={jobId} />
          <span className="text-xs font-mono text-text-secondary">
            t = {simTime.toFixed(1)}s
          </span>
        </div>
      </div>

      {/* Progress bar placeholder */}
      <div className="w-full h-1 bg-glass-1 mb-3 overflow-hidden">
        <div
          className="h-full bg-primary animate-pulse"
          style={{ width: status === 'connected' ? '100%' : '0%' }}
        />
      </div>

      {/* 2D scene view + HVD overlay */}
      <div className="flex-1 min-h-0 relative">
        <LiveSceneView
          objects={objects}
          roadGeometry={roadGeometry}
          trafficLights={trafficLights}
          className="h-full"
        />

        {showFpsMeter && (
          <div className="absolute top-2 right-2 pointer-events-none z-20 rounded bg-glass-2/80 backdrop-blur px-2 py-1 font-mono text-[10px] text-text-secondary leading-tight">
            <div>render {renderFps} fps</div>
            <div>data {dataFps} fps</div>
          </div>
        )}

        {hvdData && (
          <div className="absolute top-2 left-2 pointer-events-none">
            <HvdOverlay hvd={hvdData} lights={egoLights} />
          </div>
        )}
      </div>
    </div>
  );
}

/* ---------- Drive Mode Toggle ---------- */

function DriveModeToggle({ jobId }: { jobId: string }) {
  const [mode, setMode] = useState<DriveMode>('comfort');
  const [pending, setPending] = useState<DriveMode | null>(null);

  // Seed from the simulation's startup config so this toggle reflects the
  // mode that was actually selected on the RUN panel.
  useEffect(() => {
    let cancelled = false;
    api.getSimulation(jobId).then(
      (sim) => {
        if (cancelled) return;
        const exec = (sim.options as { execution?: { drive_mode?: string } } | undefined)?.execution;
        const initial = exec?.drive_mode;
        if (initial === 'comfort' || initial === 'sport') {
          setMode(initial);
        }
      },
      () => { /* fall back to default 'comfort' */ },
    );
    return () => { cancelled = true; };
  }, [jobId]);

  const apply = async (next: DriveMode) => {
    if (next === mode || pending) return;
    setPending(next);
    try {
      await api.setDriveMode(jobId, next);
      setMode(next);
    } catch (e) {
      console.error('setDriveMode failed', e);
    } finally {
      setPending(null);
    }
  };

  const baseBtn = 'px-2 py-0.5 text-[10px] font-medium rounded transition-colors';
  const activeCls = 'bg-blue-500 text-white';
  const inactiveCls = 'bg-glass-1 text-text-secondary hover:bg-glass-2';

  return (
    <div className="inline-flex items-center gap-0.5 rounded border border-glass-edge p-0.5">
      {(['comfort', 'sport'] as DriveMode[]).map((m) => (
        <button
          key={m}
          type="button"
          disabled={pending !== null}
          onClick={() => apply(m)}
          className={`${baseBtn} ${mode === m ? activeCls : inactiveCls} ${pending === m ? 'opacity-60' : ''}`}
        >
          {m === 'comfort' ? 'Comfort' : 'Sport'}
        </button>
      ))}
    </div>
  );
}

/* ---------- Steering Wheel SVG ---------- */

function SteeringWheel({ angle }: { angle: number }) {
  // angle in radians; positive = left per OSI convention
  const deg = -(angle * 180) / Math.PI;
  const clampedDeg = Math.max(-540, Math.min(540, deg));

  return (
    <svg width="64" height="64" viewBox="-36 -36 72 72">
      {/* Fixed 12-o'clock marker */}
      <circle cx="0" cy="-33" r="2.5" className="fill-cyan-400" />

      {/* Rotating wheel group */}
      <g
        style={{
          transform: `rotate(${clampedDeg}deg)`,
          transition: 'transform 150ms ease-out',
          transformOrigin: '0 0',
        }}
      >
        {/* Rim */}
        <circle r="28" className="fill-none stroke-text-secondary" strokeWidth="4" />
        {/* 3 spokes at 0°, 120°, 240° */}
        <line x1="0" y1="0" x2="0" y2="-24" className="stroke-text-secondary" strokeWidth="2.5" strokeLinecap="round" />
        <line x1="0" y1="0" x2="20.8" y2="12" className="stroke-text-secondary" strokeWidth="2.5" strokeLinecap="round" />
        <line x1="0" y1="0" x2="-20.8" y2="12" className="stroke-text-secondary" strokeWidth="2.5" strokeLinecap="round" />
        {/* Hub */}
        <circle r="5" className="fill-text-secondary" />
      </g>
    </svg>
  );
}

/* ---------- HVD Overlay ---------- */

function HvdOverlay({ hvd, lights }: { hvd: HvdData; lights: EgoLights | null }) {
  const speedKmh = hvd.speed * 3.6;
  const steeringDeg = (hvd.steering_angle * 180) / Math.PI;
  const gear = hvd.gear < 0 ? 'R' : hvd.gear === 0 ? 'N' : String(hvd.gear);

  return (
    <div className="flex flex-col items-center gap-1 bg-glass-2/80 backdrop-blur rounded-lg p-2">
      {/* Steering wheel */}
      <SteeringWheel angle={hvd.steering_angle} />
      <span className="text-[10px] font-mono text-text-secondary">
        {steeringDeg.toFixed(1)}&deg;
      </span>

      {/* Speed + Gear */}
      <div className="flex items-baseline gap-1.5 mt-1">
        <span className="text-sm font-mono font-bold text-white leading-none">
          {speedKmh.toFixed(0)}
        </span>
        <span className="text-[10px] text-text-tertiary">km/h</span>
        <span className="text-xs font-mono font-bold text-white ml-1">{gear}</span>
      </div>

      {/* RPM */}
      <div className="flex items-baseline gap-1 mt-0.5">
        <span className="text-xs font-mono text-white leading-none">
          {hvd.rpm.toFixed(0)}
        </span>
        <span className="text-[9px] text-text-tertiary">rpm</span>
      </div>

      {/* Throttle / Brake mini bars */}
      <div className="flex gap-2 mt-0.5">
        <MiniBar label="T" ratio={hvd.throttle} color="bg-green-500" />
        <MiniBar label="B" ratio={hvd.brake} color="bg-red-500" />
      </div>

      {/* Light indicators */}
      {lights && <LightRow lights={lights} />}
    </div>
  );
}

/* ---------- Light Indicators ---------- */

function LightRow({ lights }: { lights: EgoLights }) {
  const headOn = lights.head === 'on';
  const blinkLeft  = lights.indicator === 'left'  || lights.indicator === 'warning';
  const blinkRight = lights.indicator === 'right' || lights.indicator === 'warning';
  const brakeStrong = lights.brake === 'strong';
  const brakeNormal = lights.brake === 'normal';

  return (
    <div className="flex gap-1.5 mt-0.5" aria-label="vehicle lights">
      <LightDot
        label="H"
        active={headOn}
        activeFill="rgb(250 220 130)"
      />
      <LightDot
        label="L"
        active={blinkLeft}
        activeFill="rgb(255 170 50)"
        blink
      />
      <LightDot
        label="R"
        active={blinkRight}
        activeFill="rgb(255 170 50)"
        blink
      />
      <LightDot
        label="B"
        active={brakeStrong || brakeNormal}
        activeFill={brakeStrong ? 'rgb(220 40 40)' : 'rgb(200 80 80)'}
      />
    </div>
  );
}

function LightDot({
  label,
  active,
  activeFill,
  blink = false,
}: {
  label: string;
  active: boolean;
  activeFill: string;
  blink?: boolean;
}) {
  return (
    <div className="flex flex-col items-center gap-0.5">
      <svg width="14" height="14" viewBox="-7 -7 14 14">
        {active ? (
          <circle cx="0" cy="0" r="5" fill={activeFill}>
            {blink && (
              <animate
                attributeName="opacity"
                values="1;0.2;1"
                dur="0.7s"
                repeatCount="indefinite"
              />
            )}
          </circle>
        ) : (
          <circle
            cx="0"
            cy="0"
            r="5"
            fill="none"
            stroke="rgb(120 120 120)"
            strokeWidth="1"
          />
        )}
      </svg>
      <span className="text-[8px] text-text-tertiary leading-none">{label}</span>
    </div>
  );
}

function MiniBar({ label, ratio, color }: { label: string; ratio: number; color: string }) {
  const clamped = Math.max(0, Math.min(1, ratio));
  return (
    <div className="flex items-center gap-1">
      <span className="text-[9px] text-text-tertiary w-2">{label}</span>
      <div className="w-10 h-1.5 bg-glass-1 overflow-hidden rounded-full">
        <div
          className={`h-full ${color} transition-all duration-150 rounded-full`}
          style={{ width: `${clamped * 100}%` }}
        />
      </div>
    </div>
  );
}
