import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type VdTelemetryFrame } from '../api/client';
import { LiveSceneView, type RoadGeometry } from '../components/LiveSceneView';
import { ErrorChart, TelemetryInfoRows } from '../components/verification/TelemetryPanels';
import { VTargetProfileChart } from '../components/verification/VTargetProfileChart';
import { LiveVdPanel } from '../components/verification/LiveVdPanel';
import { VdRunLauncher } from '../components/verification/VdRunLauncher';
import type { OsiObject, TrafficLight } from '../hooks/useOsiStream';

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
  const queryClient = useQueryClient();
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

  // On-demand verification pipeline (writes compare.json / verdict.json into the
  // run dir; refetching telemetry then lights up the existing ghost / verdict UI).
  const refetchRun = () =>
    queryClient.invalidateQueries({ queryKey: ['verification-telemetry', runId] });
  const compareMut = useMutation({
    mutationFn: () => api.runBaselineCompare(runId!),
    onSuccess: refetchRun,
  });
  const assertMut = useMutation({
    mutationFn: () => api.runAssertions(runId!),
    onSuccess: refetchRun,
  });

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

  // --- run-a-scenario mode: launch a VirtualDriver run, watch it live, then
  // auto-switch to replaying the just-finished recording. ---
  const [mode, setMode] = useState<'replay' | 'run'>('replay');
  const [runningJobId, setRunningJobId] = useState<string | null>(null);
  const [runOverride, setRunOverride] = useState(false);
  const [runProject, setRunProject] = useState<string | undefined>(undefined);
  const [runScenario, setRunScenario] = useState<string | undefined>(undefined);

  const { data: runningSim } = useQuery({
    queryKey: ['running-sim', runningJobId],
    queryFn: () => api.getSimulation(runningJobId!),
    enabled: !!runningJobId,
    refetchInterval: (query) => {
      const s = query.state.data?.status;
      return s === 'running' || s === 'queued' ? 1000 : false;
    },
  });

  useEffect(() => {
    if (!runningJobId || !runningSim) return;
    if (['completed', 'failed', 'timeout', 'cancelled'].includes(runningSim.status)) {
      const finished = runningJobId;
      setRunningJobId(null);
      setMode('replay');
      queryClient.invalidateQueries({ queryKey: ['verification-runs'] });
      selectRun(finished); // load the just-finished run as a replay
    }
  }, [runningJobId, runningSim]);  // eslint-disable-line react-hooks/exhaustive-deps

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

  // Recorded OSI scene (other traffic + signal phases), sorted by sim_time.
  const scene = useMemo(() => telemetry?.scene ?? [], [telemetry]);

  // Static road geometry (road network + signs + stop lines) for this run's scenario.
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

  // Nearest recorded scene frame to the current playhead time (binary search).
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

  const egoObjects: OsiObject[] = useMemo(() => {
    if (!frame) return [];
    // Full scene (other traffic + ego) when recorded; else just the ego marker.
    const objs: OsiObject[] = currentScene
      ? (currentScene.objects as unknown as OsiObject[]).slice()
      : [{
          id: 0, name: 'ego', x: frame.ego.x, y: frame.ego.y, z: frame.ego.z, h: frame.ego.h, speed: frame.ego.speed,
          head_light: 'off',
          indicator: frame.indicator.left ? 'left' : frame.indicator.right ? 'right' : 'off',
          brake_light: frame.driver.brake > 0.05 ? 'normal' : 'off',
          obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
        }];
    if (ghost) {
      objs.push({
        id: -1, name: 'Default', x: ghost.x, y: ghost.y, z: 0, h: ghostHeading, speed: ghost.speed,
        head_light: 'off', indicator: 'off', brake_light: 'off',
        obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
      });
    }
    return objs;
  }, [frame, currentScene, ghost, ghostHeading]);

  const ghostDelta = ghost && frame
    ? Math.hypot(frame.ego.x - ghost.x, frame.ego.y - ghost.y) : null;

  return (
    <div className="h-full flex flex-col p-4 gap-3">
      {/* Header / controls */}
      <div className="flex items-center gap-3 flex-wrap">
        <h1 className="font-display text-lg text-foreground">VirtualDriver</h1>

        {/* Mode: replay a past run vs run a scenario now */}
        <div className="inline-flex rounded border border-glass-edge p-0.5 text-xs">
          {(['replay', 'run'] as const).map((m) => (
            <button
              key={m}
              onClick={() => setMode(m)}
              disabled={!!runningJobId}
              className={`px-2 py-1 rounded ${mode === m ? 'bg-primary/80 text-white' : 'text-text-secondary hover:bg-glass-2'} disabled:opacity-40`}
            >
              {m === 'replay' ? 'Replay' : 'Run a scenario'}
            </button>
          ))}
        </div>

        {runningJobId ? (
          <span className="inline-flex items-center gap-2 text-xs">
            <span className="text-warning">● running {runningSim?.status ?? 'starting'}…</span>
            <button
              onClick={() => api.cancelSimulation(runningJobId).catch(() => {})}
              className="px-2 py-1 rounded text-xs font-medium bg-destructive/80 text-white hover:bg-destructive"
            >
              ■ Stop
            </button>
          </span>
        ) : mode === 'run' ? (
          <VdRunLauncher onStarted={(jid, { override, projectId, scenarioFile }) => {
            setRunningJobId(jid); setRunOverride(override);
            setRunProject(projectId); setRunScenario(scenarioFile);
          }} />
        ) : (<>
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
            No runs yet. Run a simulation with the <strong>Virtual Driver</strong> controller —
            it is recorded automatically and appears here.
          </span>
        )}

        {runId && (
          <div className="inline-flex gap-1.5">
            <button
              onClick={() => compareMut.mutate()}
              disabled={compareMut.isPending}
              className="px-2 py-1 rounded text-xs font-medium border border-glass-edge text-text-secondary hover:bg-glass-2 disabled:opacity-50"
              title="Run the same scenario with the Default controller and compare (XY / speed RMSE + ghost)"
            >
              {compareMut.isPending ? 'Comparing…' : 'Compare vs Default'}
            </button>
            <button
              onClick={() => assertMut.mutate()}
              disabled={assertMut.isPending}
              className="px-2 py-1 rounded text-xs font-medium border border-glass-edge text-text-secondary hover:bg-glass-2 disabled:opacity-50"
              title="Evaluate the run against its expectations.yaml (if present)"
            >
              {assertMut.isPending ? 'Asserting…' : 'Run assertions'}
            </button>
          </div>
        )}
        {(compareMut.error || assertMut.error) && (
          <span className="text-xs text-destructive">
            {String((compareMut.error || assertMut.error) as Error)}
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
        </>)}
      </div>

      {/* Live view while a launched run is in progress */}
      {runningJobId ? (
        <div className="flex-1 min-h-0">
          <LiveVdPanel
            jobId={runningJobId}
            projectId={runProject}
            scenarioFile={runScenario}
            showOverride={runOverride}
          />
        </div>
      ) : (<>
      {/* Body: scene + side panel */}
      <div className="flex-1 min-h-0 flex gap-3">
        <div className="flex-1 min-h-0 rounded overflow-hidden border border-glass-edge">
          <LiveSceneView
            objects={egoObjects}
            roadGeometry={roadGeometry}
            trafficLights={sceneTrafficLights}
            vdTelemetry={frame}
            midlong={frame?.midlong ?? null}
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

          {frame && <TelemetryInfoRows frame={frame} />}

          {frames.length > 0 && (
            <ErrorChart frames={frames} idx={idx} />
          )}

          {frames.length > 0 && (
            <VTargetProfileChart frames={frames} idx={idx} midlong={frame?.midlong} />
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
      </>)}
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

