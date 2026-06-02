import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api, type VdTelemetryFrame } from '../api/client';
import { LiveSceneView } from '../components/LiveSceneView';
import type { OsiObject } from '../hooks/useOsiStream';

interface EventMarker {
  idx: number;
  t: number;
  kind: string;
  label: string;
  color: string;
}

function mk(idx: number, f: VdTelemetryFrame, kind: string, label: string, color: string): EventMarker {
  return { idx, t: f.sim_time, kind, label, color };
}

/**
 * Replays a recorded gt_sim_test run: drives the LiveSceneView VirtualDriver
 * overlay (short-horizon preview) from telemetry.jsonl plus a driver-error
 * chart and verdict/compare summary. Same telemetry shape the live overlay will
 * use once the C++ transport is wired.
 */
export function VerificationReplayPage() {
  const { data: runsData } = useQuery({
    queryKey: ['verification-runs'],
    queryFn: api.getVerificationRuns,
    refetchInterval: 5000,
  });
  const runs = useMemo(() => runsData?.runs ?? [], [runsData]);

  // Effective run = explicit selection, else the most recent one (derived, no effect).
  const [selectedRunId, setSelectedRunId] = useState<string | null>(null);
  const runId = selectedRunId ?? (runs.length > 0 ? runs[runs.length - 1].id : null);

  const { data: telemetry } = useQuery({
    queryKey: ['verification-telemetry', runId],
    queryFn: () => api.getVerificationTelemetry(runId!),
    enabled: !!runId,
  });
  const frames = useMemo(() => telemetry?.frames ?? [], [telemetry]);

  // --- playback ---
  const [idx, setIdx] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [loop, setLoop] = useState(false);
  const [compareOn, setCompareOn] = useState(false);
  const failCursorRef = useRef(0);
  const timerRef = useRef<number | null>(null);

  const selectRun = (id: string | null) => {
    setSelectedRunId(id);
    setIdx(0);
    setPlaying(false);
  };

  const lastIdx = Math.max(0, frames.length - 1);
  const atEnd = frames.length > 0 && idx >= lastIdx;

  const reset = () => { setIdx(0); };
  const stepBy = (d: number) => {
    setPlaying(false);
    setIdx((i) => Math.max(0, Math.min(lastIdx, i + d)));
  };
  const togglePlay = () => {
    // Replaying from the end (without loop) should restart, not stall.
    if (!playing && atEnd && !loop) setIdx(0);
    setPlaying((p) => !p);
  };

  const dt = frames.length > 1 ? Math.max(0.01, frames[1].sim_time - frames[0].sim_time) : 0.05;

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

  const verdict = telemetry?.verdict ?? null;
  const compare = telemetry?.compare ?? null;
  const baselineTrack = telemetry?.baseline_track ?? null;

  // --- A: event markers detected from the telemetry ---
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

  // --- B: fail markers from the verdict ---
  const failMarkers = useMemo(() => (verdict?.results ?? [])
    .filter((r) => r.status === 'fail' && typeof r.idx === 'number')
    .map((r) => ({ idx: r.idx as number, t: r.t, event: r.event, reason: r.reason })),
  [verdict]);

  const jumpToFail = () => {
    if (failMarkers.length === 0) return;
    const c = failCursorRef.current % failMarkers.length;
    setIdx(failMarkers[c].idx);
    setPlaying(false);
    failCursorRef.current = c + 1;
  };

  // --- C: baseline ghost (Default) at the current frame ---
  const ghost = compareOn && baselineTrack && baselineTrack[idx] ? baselineTrack[idx] : null;
  const ghostHeading = useMemo(() => {
    if (!ghost || !baselineTrack) return 0;
    const nxt = baselineTrack[Math.min(idx + 1, baselineTrack.length - 1)];
    return Math.atan2(nxt.y - ghost.y, nxt.x - ghost.x) || 0;
  }, [ghost, baselineTrack, idx]);
  const ghostPathPts = useMemo<[number, number][] | null>(
    () => (compareOn && baselineTrack ? baselineTrack.map((p) => [p.x, p.y]) : null),
    [compareOn, baselineTrack],
  );

  const egoObjects: OsiObject[] = useMemo(() => {
    if (!frame) return [];
    const e = frame.ego;
    const objs: OsiObject[] = [{
      id: 0, name: 'ego', x: e.x, y: e.y, z: e.z, h: e.h, speed: e.speed,
      head_light: 'off',
      indicator: frame.indicator.left ? 'left' : frame.indicator.right ? 'right' : 'off',
      brake_light: frame.driver.brake > 0.05 ? 'normal' : 'off',
      obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
    }];
    if (ghost) {
      objs.push({
        id: 1, name: 'Default', x: ghost.x, y: ghost.y, z: 0, h: ghostHeading, speed: ghost.speed,
        head_light: 'off', indicator: 'off', brake_light: 'off',
        obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
      });
    }
    return objs;
  }, [frame, ghost, ghostHeading]);

  const ghostDelta = ghost && frame
    ? Math.hypot(frame.ego.x - ghost.x, frame.ego.y - ghost.y) : null;

  return (
    <div className="h-full flex flex-col p-4 gap-3">
      {/* Header / controls */}
      <div className="flex items-center gap-3 flex-wrap">
        <h1 className="font-display text-lg text-foreground">VirtualDriver Replay</h1>
        <select
          value={runId ?? ''}
          onChange={(e) => selectRun(e.target.value || null)}
          className="bg-glass-1 border border-glass-edge rounded px-2 py-1 text-sm text-foreground"
        >
          <option value="" disabled>Select a run…</option>
          {runs.map((r) => (
            <option key={r.id} value={r.id}>
              {r.id}{typeof r.meta.frames === 'number' ? ` (${r.meta.frames}f)` : ''}
            </option>
          ))}
        </select>
        {runs.length === 0 && (
          <span className="text-xs text-text-tertiary">
            No runs. Generate one: <code>gt_sim_test run &lt;scenario&gt; --out results/&lt;id&gt;</code>
          </span>
        )}

        {frames.length > 0 && (
          <>
            <div className="inline-flex items-center gap-0.5 rounded border border-glass-edge p-0.5">
              <IconButton title="Jump to start" onClick={reset} disabled={idx === 0}>
                <SkipStartIcon />
              </IconButton>
              <IconButton title="Step back" onClick={() => stepBy(-1)} disabled={idx === 0}>
                <StepBackIcon />
              </IconButton>
              <IconButton title={playing ? 'Pause' : 'Play'} onClick={togglePlay} primary>
                {playing ? <PauseIcon /> : <PlayIcon />}
              </IconButton>
              <IconButton title="Step forward" onClick={() => stepBy(1)} disabled={atEnd}>
                <StepForwardIcon />
              </IconButton>
              <IconButton title="Jump to end" onClick={() => setIdx(lastIdx)} disabled={atEnd}>
                <SkipEndIcon />
              </IconButton>
              <IconButton title="Loop" onClick={() => setLoop((v) => !v)} active={loop}>
                <LoopIcon />
              </IconButton>
            </div>
            <div className="inline-flex gap-0.5 rounded border border-glass-edge p-0.5">
              {[0.5, 1, 2, 4].map((s) => (
                <button
                  key={s}
                  onClick={() => setSpeed(s)}
                  className={`px-1.5 py-0.5 text-[11px] rounded ${
                    speed === s ? 'bg-primary/80 text-white' : 'text-text-secondary hover:bg-glass-2'
                  }`}
                >{s}x</button>
              ))}
            </div>
            <span className="text-xs font-mono text-text-secondary">
              t = {frame?.sim_time.toFixed(2)}s ({idx + 1}/{frames.length})
            </span>

            {failMarkers.length > 0 && (
              <button
                onClick={jumpToFail}
                className="px-2 py-1 rounded text-xs font-medium bg-destructive/80 text-white hover:bg-destructive"
                title="Jump to the next failing event"
              >
                Jump to fail ({failMarkers.length})
              </button>
            )}
            {baselineTrack && baselineTrack.length > 0 && (
              <button
                onClick={() => setCompareOn((v) => !v)}
                className={`px-2 py-1 rounded text-xs font-medium border border-glass-edge ${
                  compareOn ? 'bg-primary/30 text-foreground' : 'text-text-secondary hover:bg-glass-2'
                }`}
                title="Overlay the Default baseline run (ghost)"
              >
                Compare
              </button>
            )}
          </>
        )}
      </div>

      {/* Body: scene + side panel */}
      <div className="flex-1 min-h-0 flex gap-3">
        <div className="flex-1 min-h-0 rounded overflow-hidden border border-glass-edge">
          <LiveSceneView
            objects={egoObjects}
            vdTelemetry={frame}
            ghostPath={ghostPathPts}
            className="h-full"
            viewRadius={40}
          />
        </div>

        <div className="w-72 shrink-0 flex flex-col gap-3 overflow-y-auto">
          {(verdict || compare) && (
            <div className="rounded border border-glass-edge p-3 text-xs space-y-1">
              <div className="font-medium text-foreground mb-1">Verdict</div>
              {verdict?.overall && (
                <div>overall: <span className={
                  verdict.overall === 'pass' ? 'text-success' :
                  verdict.overall === 'fail' ? 'text-destructive' : 'text-warning'
                }>{verdict.overall}</span>
                  {verdict.summary && <span className="text-text-tertiary"> ({verdict.summary.pass}P/{verdict.summary.fail}F/{verdict.summary.skip}S)</span>}
                </div>
              )}
              {compare?.xy_rmse_m != null && (
                <div className="text-text-secondary">
                  XY RMSE {compare.xy_rmse_m}m · speed RMSE {compare.speed_rmse_mps}m/s · max {compare.xy_max_dev_m}m
                </div>
              )}
              {compareOn && ghostDelta != null && (
                <div className="text-text-secondary">
                  <span className="inline-block w-2 h-2 rounded-full mr-1 align-middle" style={{ background: 'rgba(230,200,120,0.9)' }} />
                  Δ to Default: {ghostDelta.toFixed(2)} m
                </div>
              )}
            </div>
          )}

          {frame && (
            <div className="rounded border border-glass-edge p-3 text-xs font-mono space-y-1">
              <Row label="speed" value={`${(frame.ego.speed * 3.6).toFixed(1)} km/h`} />
              <Row label="throttle" value={frame.driver.throttle.toFixed(2)} />
              <Row label="brake" value={frame.driver.brake.toFixed(2)} />
              <Row label="steer" value={frame.driver.steer.toFixed(3)} />
              <Row label="lat err" value={`${frame.driver.lateral_error.toFixed(3)} m`} />
              <Row label="spd err" value={`${frame.driver.speed_error.toFixed(2)} m/s`} />
              {frame.ego.lane != null && <Row label="lane" value={`${frame.ego.lane} @ road ${frame.ego.track}`} />}
              <Row label="override" value={`${frame.override.lateral ? 'lat ' : ''}${frame.override.longitudinal ? 'lon' : ''}${!frame.override.lateral && !frame.override.longitudinal ? 'none' : ''}`} />
            </div>
          )}

          {frames.length > 0 && (
            <ErrorChart frames={frames} idx={idx} />
          )}
        </div>
      </div>

      {/* Scrub bar with event + fail markers */}
      {frames.length > 0 && (
        <Timeline
          frames={frames}
          idx={idx}
          events={events}
          fails={failMarkers}
          onSeek={(i) => { setIdx(i); setPlaying(false); }}
        />
      )}
    </div>
  );
}

/* ---------- Timeline (scrub + markers) ---------- */

function Timeline({
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
      {/* marker rail */}
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
      {/* legend */}
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

/* ---------- Transport controls ---------- */

function IconButton({
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

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between gap-2">
      <span className="text-text-tertiary">{label}</span>
      <span className="text-foreground">{value}</span>
    </div>
  );
}

/* ---------- Error chart ---------- */

function ErrorChart({ frames, idx }: { frames: VdTelemetryFrame[]; idx: number }) {
  const W = 264, H = 120, padL = 4, padR = 4, padT = 6, padB = 6;
  const t0 = frames[0].sim_time;
  const t1 = frames[frames.length - 1].sim_time;
  const span = Math.max(1e-3, t1 - t0);

  const series = useMemo(() => {
    const lat = frames.map((f) => f.driver.lateral_error);
    const spd = frames.map((f) => f.driver.speed_error);
    const all = [...lat, ...spd];
    const amax = Math.max(0.1, ...all.map(Math.abs));
    const x = (i: number) => padL + ((frames[i].sim_time - t0) / span) * (W - padL - padR);
    const y = (v: number) => padT + (0.5 - v / (2 * amax)) * (H - padT - padB);
    const path = (arr: number[]) =>
      arr.map((v, i) => `${i === 0 ? 'M' : 'L'}${x(i).toFixed(1)},${y(v).toFixed(1)}`).join(' ');
    return { latPath: path(lat), spdPath: path(spd), x, amax };
  }, [frames, t0, span]);

  return (
    <div className="rounded border border-glass-edge p-2">
      <div className="text-[11px] text-text-tertiary mb-1 flex gap-3">
        <span><span className="inline-block w-2 h-2 rounded-full mr-1" style={{ background: '#7B88E8' }} />lateral err</span>
        <span><span className="inline-block w-2 h-2 rounded-full mr-1" style={{ background: '#4FD18B' }} />speed err</span>
        <span className="ml-auto">±{series.amax.toFixed(2)}</span>
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} className="w-full">
        <line x1={padL} y1={H / 2} x2={W - padR} y2={H / 2} stroke="rgba(180,170,230,0.2)" strokeWidth={0.5} />
        <path d={series.latPath} fill="none" stroke="#7B88E8" strokeWidth={1} />
        <path d={series.spdPath} fill="none" stroke="#4FD18B" strokeWidth={1} />
        <line x1={series.x(idx)} y1={padT} x2={series.x(idx)} y2={H - padB} stroke="rgba(255,255,255,0.5)" strokeWidth={0.8} />
      </svg>
    </div>
  );
}
