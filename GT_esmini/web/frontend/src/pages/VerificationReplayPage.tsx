import { useEffect, useMemo, useRef, useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api, type VdTelemetryFrame } from '../api/client';
import { LiveSceneView } from '../components/LiveSceneView';
import type { OsiObject } from '../hooks/useOsiStream';

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
  const timerRef = useRef<number | null>(null);

  const selectRun = (id: string | null) => {
    setSelectedRunId(id);
    setIdx(0);
    setPlaying(false);
  };

  const dt = frames.length > 1 ? Math.max(0.01, frames[1].sim_time - frames[0].sim_time) : 0.05;

  useEffect(() => {
    if (!playing || frames.length === 0) return;
    const period = Math.max(16, (dt * 1000) / speed);
    timerRef.current = window.setInterval(() => {
      setIdx((i) => {
        if (i >= frames.length - 1) { setPlaying(false); return i; }
        return i + 1;
      });
    }, period);
    return () => { if (timerRef.current != null) window.clearInterval(timerRef.current); };
  }, [playing, speed, dt, frames.length]);

  const frame: VdTelemetryFrame | null = frames[Math.min(idx, frames.length - 1)] ?? null;

  const egoObjects: OsiObject[] = useMemo(() => {
    if (!frame) return [];
    const e = frame.ego;
    return [{
      id: 0, name: 'ego', x: e.x, y: e.y, z: e.z, h: e.h, speed: e.speed,
      head_light: 'off',
      indicator: frame.indicator.left ? 'left' : frame.indicator.right ? 'right' : 'off',
      brake_light: frame.driver.brake > 0.05 ? 'normal' : 'off',
      obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
    }];
  }, [frame]);

  const verdict = telemetry?.verdict as
    | { overall?: string; summary?: { pass: number; fail: number; skip: number } }
    | null | undefined;
  const compare = telemetry?.compare as
    | { xy_rmse_m?: number; speed_rmse_mps?: number; xy_max_dev_m?: number }
    | null | undefined;

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
            <button
              onClick={() => setPlaying((p) => !p)}
              className="px-3 py-1 rounded bg-primary/80 text-white text-sm font-medium hover:bg-primary"
            >
              {playing ? 'Pause' : 'Play'}
            </button>
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
          </>
        )}
      </div>

      {/* Body: scene + side panel */}
      <div className="flex-1 min-h-0 flex gap-3">
        <div className="flex-1 min-h-0 rounded overflow-hidden border border-glass-edge">
          <LiveSceneView objects={egoObjects} vdTelemetry={frame} className="h-full" viewRadius={40} />
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

      {/* Scrub bar */}
      {frames.length > 0 && (
        <input
          type="range"
          min={0}
          max={frames.length - 1}
          value={idx}
          onChange={(e) => { setIdx(Number(e.target.value)); setPlaying(false); }}
          className="w-full accent-primary"
        />
      )}
    </div>
  );
}

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
