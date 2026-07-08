/**
 * Shared replay machinery for the verification pages (replay + annotate).
 *
 * Extracted verbatim from VerificationReplayPage so both pages drive playback the
 * same way: the `useReplay` transport hook, the `useSceneReplay` scene-building
 * hook (road geometry + recorded OSI scene -> LiveSceneView `objects`), the
 * transport button cluster (`ReplayControls`), and the `Timeline` scrubber with
 * event + fail markers.
 */
import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { api, type VdTelemetryFrame, type VerificationTelemetry } from '../../api/client';
import { type RoadGeometry } from '../LiveSceneView';
import type { OsiObject, TrafficLight } from '../../hooks/useOsiStream';

/* ---------- transport hook ---------- */

export interface ReplayState {
  idx: number;
  setIdx: (i: number) => void;
  playing: boolean;
  setPlaying: React.Dispatch<React.SetStateAction<boolean>>;
  speed: number;
  setSpeed: (s: number) => void;
  loop: boolean;
  setLoop: React.Dispatch<React.SetStateAction<boolean>>;
  lastIdx: number;
  atEnd: boolean;
  dt: number;
  frame: VdTelemetryFrame | null;
  reset: () => void;
  stepBy: (d: number) => void;
  togglePlay: () => void;
}

/** Playback state machine over a frame array (idx/play/speed/loop + timer). */
export function useReplay(frames: VdTelemetryFrame[]): ReplayState {
  const [idx, setIdx] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [loop, setLoop] = useState(false);
  const timerRef = useRef<number | null>(null);

  const lastIdx = Math.max(0, frames.length - 1);
  const atEnd = frames.length > 0 && idx >= lastIdx;
  const dt = frames.length > 1 ? Math.max(0.01, frames[1].sim_time - frames[0].sim_time) : 0.05;

  const reset = () => setIdx(0);
  const stepBy = (d: number) => {
    setPlaying(false);
    setIdx((i) => Math.max(0, Math.min(lastIdx, i + d)));
  };
  const togglePlay = () => {
    if (!playing && atEnd && !loop) setIdx(0);
    setPlaying((p) => !p);
  };

  useEffect(() => {
    if (!playing || frames.length === 0) return;
    const period = Math.max(16, (dt * 1000) / speed);
    timerRef.current = window.setInterval(() => {
      setIdx((i) => {
        if (i >= frames.length - 1) {
          if (loop) return 0;
          setPlaying(false);
          return i;
        }
        return i + 1;
      });
    }, period);
    return () => { if (timerRef.current != null) window.clearInterval(timerRef.current); };
  }, [playing, speed, dt, frames.length, loop]);

  const frame: VdTelemetryFrame | null = frames[Math.min(idx, frames.length - 1)] ?? null;
  return { idx, setIdx, playing, setPlaying, speed, setSpeed, loop, setLoop,
           lastIdx, atEnd, dt, frame, reset, stepBy, togglePlay };
}

/* ---------- scene-building hook ---------- */

export interface EventMarker {
  idx: number;
  t: number;
  kind: string;
  label: string;
  color: string;
}

function mk(idx: number, f: VdTelemetryFrame, kind: string, label: string, color: string): EventMarker {
  return { idx, t: f.sim_time, kind, label, color };
}

export interface SceneReplay {
  roadGeometry: RoadGeometry | null;
  currentScene: VerificationTelemetry['scene'][number] | null;
  sceneTrafficLights: TrafficLight[];
  /** ego (+ recorded traffic) at the current frame, WITHOUT any ghost overlay */
  baseEgoObjects: OsiObject[];
  events: EventMarker[];
}

/**
 * Builds LiveSceneView inputs from a recorded run: fetches static road geometry,
 * picks the nearest recorded OSI scene frame, and synthesizes the ego marker when
 * no scene was recorded. Also detects start/stop/lane/override event markers.
 */
export function useSceneReplay(
  telemetry: VerificationTelemetry | undefined,
  frames: VdTelemetryFrame[],
  frame: VdTelemetryFrame | null,
): SceneReplay {
  const scene = useMemo(() => telemetry?.scene ?? [], [telemetry]);

  const [roadGeometry, setRoadGeometry] = useState<RoadGeometry | null>(null);
  useEffect(() => {
    const pid = telemetry?.meta?.project_id;
    const sfile = telemetry?.meta?.scenario_file;
    if (!pid || !sfile) { setRoadGeometry(null); return; }
    let cancelled = false;
    api.getRoadGeometry(pid, sfile).then(
      (data) => { if (!cancelled) setRoadGeometry(data as RoadGeometry); },
      () => { /* road overlay is optional */ },
    );
    return () => { cancelled = true; };
  }, [telemetry?.meta?.project_id, telemetry?.meta?.scenario_file]);

  const currentScene = useMemo(() => {
    if (scene.length === 0 || !frame) return null;
    const t = frame.sim_time;
    let lo = 0, hi = scene.length - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (scene[mid].sim_time <= t) lo = mid; else hi = mid;
    }
    return Math.abs(scene[lo].sim_time - t) <= Math.abs(scene[hi].sim_time - t) ? scene[lo] : scene[hi];
  }, [scene, frame]);

  const sceneTrafficLights = useMemo(
    () => (currentScene?.traffic_lights ?? []) as unknown as TrafficLight[],
    [currentScene],
  );

  const baseEgoObjects: OsiObject[] = useMemo(() => {
    if (!frame) return [];
    return currentScene
      ? (currentScene.objects as unknown as OsiObject[]).slice()
      : [{
          id: 0, name: 'ego', x: frame.ego.x, y: frame.ego.y, z: frame.ego.z, h: frame.ego.h, speed: frame.ego.speed,
          head_light: 'off',
          indicator: frame.indicator.left ? 'left' : frame.indicator.right ? 'right' : 'off',
          brake_light: frame.driver.brake > 0.05 ? 'normal' : 'off',
          obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
        }] as unknown as OsiObject[];
  }, [frame, currentScene]);

  const events = useMemo<EventMarker[]>(() => {
    const out: EventMarker[] = [];
    let moving = false;
    let prevLane: number | null | undefined = undefined;
    let prevOverride = false;
    frames.forEach((f, i) => {
      const sp = f.ego.speed;
      if (!moving && sp > 0.8) { moving = true; if (i > 0) out.push(mk(i, f, 'start', 'start', '#4FD18B')); }
      else if (moving && sp < 0.3) { moving = false; out.push(mk(i, f, 'stop', 'stop', '#E8884F')); }
      const lane = f.ego.lane;
      if (prevLane != null && lane != null && lane !== prevLane)
        out.push(mk(i, f, 'lane', `lane ${prevLane}→${lane}`, '#7B88E8'));
      if (lane != null) prevLane = lane;
      const ov = f.override.lateral || f.override.longitudinal;
      if (ov !== prevOverride) out.push(mk(i, f, 'override', ov ? 'override on' : 'override off', '#E8C84F'));
      prevOverride = ov;
    });
    return out;
  }, [frames]);

  return { roadGeometry, currentScene, sceneTrafficLights, baseEgoObjects, events };
}

/* ---------- transport controls (button cluster) ---------- */

export function ReplayControls({ r, frames }: { r: ReplayState; frames: VdTelemetryFrame[] }) {
  if (frames.length === 0) return null;
  return (
    <>
      <div className="inline-flex items-center gap-0.5 rounded border border-glass-edge p-0.5">
        <IconButton title="Jump to start" onClick={r.reset} disabled={r.idx === 0}>
          <SkipStartIcon />
        </IconButton>
        <IconButton title="Step back" onClick={() => r.stepBy(-1)} disabled={r.idx === 0}>
          <StepBackIcon />
        </IconButton>
        <IconButton title={r.playing ? 'Pause' : 'Play'} onClick={r.togglePlay} primary>
          {r.playing ? <PauseIcon /> : <PlayIcon />}
        </IconButton>
        <IconButton title="Step forward" onClick={() => r.stepBy(1)} disabled={r.atEnd}>
          <StepForwardIcon />
        </IconButton>
        <IconButton title="Jump to end" onClick={() => r.setIdx(r.lastIdx)} disabled={r.atEnd}>
          <SkipEndIcon />
        </IconButton>
        <IconButton title="Loop" onClick={() => r.setLoop((v) => !v)} active={r.loop}>
          <LoopIcon />
        </IconButton>
      </div>
      <div className="inline-flex gap-0.5 rounded border border-glass-edge p-0.5">
        {[0.5, 1, 2, 4].map((s) => (
          <button
            key={s}
            onClick={() => r.setSpeed(s)}
            className={`px-1.5 py-0.5 text-[11px] rounded ${
              r.speed === s ? 'bg-primary/80 text-white' : 'text-text-secondary hover:bg-glass-2'
            }`}
          >{s}x</button>
        ))}
      </div>
      <span className="text-xs font-mono text-text-secondary">
        t = {r.frame?.sim_time.toFixed(2)}s ({r.idx + 1}/{frames.length})
      </span>
    </>
  );
}

/* ---------- Timeline (scrub + markers) ---------- */

export function Timeline({
  frames, idx, events, fails, onSeek,
}: {
  frames: VdTelemetryFrame[];
  idx: number;
  events: EventMarker[];
  fails: { idx: number; t?: number; event: string; reason?: string }[];
  onSeek: (i: number) => void;
}) {
  const t0 = frames[0].sim_time;
  const span = Math.max(1e-3, frames[frames.length - 1].sim_time - t0);
  const pct = (t: number) => `${((t - t0) / span) * 100}%`;

  return (
    <div className="shrink-0">
      <div className="relative h-4">
        {events.map((m, i) => (
          <button
            key={`ev-${i}`}
            onClick={() => onSeek(m.idx)}
            title={`${m.label} @ ${m.t.toFixed(1)}s`}
            className="absolute top-1 -translate-x-1/2 w-1.5 h-3 rounded-sm hover:scale-y-125 transition-transform"
            style={{ left: pct(m.t), background: m.color }}
          />
        ))}
        {fails.map((m, i) => (
          <button
            key={`fl-${i}`}
            onClick={() => onSeek(m.idx)}
            title={`FAIL ${m.event}${m.reason ? ` — ${m.reason}` : ''}`}
            className="absolute -top-0.5 -translate-x-1/2 w-0 h-0 hover:scale-125 transition-transform"
            style={{
              left: pct(m.t ?? frames[m.idx].sim_time),
              borderLeft: '4px solid transparent',
              borderRight: '4px solid transparent',
              borderTop: '6px solid var(--color-destructive, #e2466b)',
            }}
          />
        ))}
      </div>
      <input
        type="range"
        min={0}
        max={frames.length - 1}
        value={idx}
        onChange={(e) => onSeek(Number(e.target.value))}
        className="w-full accent-primary"
      />
      <div className="flex gap-3 text-[10px] text-text-tertiary mt-1 flex-wrap">
        <Legend color="#4FD18B" label="start" />
        <Legend color="#E8884F" label="stop" />
        <Legend color="#7B88E8" label="lane change" />
        <Legend color="#E8C84F" label="override" />
        {fails.length > 0 && <Legend color="#e2466b" label="fail" />}
      </div>
    </div>
  );
}

function Legend({ color, label }: { color: string; label: string }) {
  return (
    <span className="inline-flex items-center gap-1">
      <span className="inline-block w-2 h-2 rounded-sm" style={{ background: color }} />
      {label}
    </span>
  );
}

/* ---------- icon button + icons ---------- */

export function IconButton({
  title, onClick, disabled, primary, active, children,
}: {
  title: string;
  onClick: () => void;
  disabled?: boolean;
  primary?: boolean;
  active?: boolean;
  children: ReactNode;
}) {
  return (
    <button
      type="button"
      title={title}
      aria-label={title}
      onClick={onClick}
      disabled={disabled}
      className={`p-1.5 rounded transition-colors disabled:opacity-30 disabled:cursor-not-allowed ${
        primary
          ? 'bg-primary/80 text-white hover:bg-primary'
          : active
          ? 'bg-primary/30 text-foreground'
          : 'text-text-secondary hover:bg-glass-2 hover:text-foreground'
      }`}
    >
      {children}
    </button>
  );
}

const ICON = 'w-3.5 h-3.5';

const SkipStartIcon = () => (
  <svg className={ICON} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
    <path d="M6 6h2v12H6zM19 6v12l-9-6z" />
  </svg>
);
const SkipEndIcon = () => (
  <svg className={ICON} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
    <path d="M16 6h2v12h-2zM5 6v12l9-6z" />
  </svg>
);
const StepBackIcon = () => (
  <svg className={ICON} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2}
    strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <path d="M14 7l-5 5 5 5" />
  </svg>
);
const StepForwardIcon = () => (
  <svg className={ICON} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2}
    strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <path d="M10 7l5 5-5 5" />
  </svg>
);
const PlayIcon = () => (
  <svg className={ICON} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
    <path d="M9 6v12l9-6z" />
  </svg>
);
const PauseIcon = () => (
  <svg className={ICON} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
    <path d="M7 5h3v14H7zM14 5h3v14h-3z" />
  </svg>
);
const LoopIcon = () => (
  <svg className={ICON} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={2}
    strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <path d="M17 2l4 4-4 4" />
    <path d="M3 11V9a4 4 0 0 1 4-4h14" />
    <path d="M7 22l-4-4 4-4" />
    <path d="M21 13v2a4 4 0 0 1-4 4H3" />
  </svg>
);
