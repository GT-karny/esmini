import { useEffect, useMemo, useRef, useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api } from '../api/client';
import { LiveSceneView } from '../components/LiveSceneView';
import { ErrorChart, TelemetryInfoRows } from '../components/verification/TelemetryPanels';
import { VTargetProfileChart } from '../components/verification/VTargetProfileChart';
import { PolicyTimelinePanel } from '../components/verification/PolicyTimelinePanel';
import { LiveVdPanel } from '../components/verification/LiveVdPanel';
import { VdRunLauncher } from '../components/verification/VdRunLauncher';
import { useReplay, useSceneReplay, ReplayControls, Timeline } from '../components/verification/ReplayTransport';
import type { OsiObject } from '../hooks/useOsiStream';

/**
 * Replays a recorded gt_sim_test run: drives the LiveSceneView VirtualDriver
 * overlay (short-horizon preview) from telemetry.jsonl plus a driver-error
 * chart and verdict/compare summary. Same telemetry shape the live overlay will
 * use once the C++ transport is wired. Transport/scene/timeline machinery is
 * shared with the annotate page (see components/verification/ReplayTransport).
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

  // --- playback (shared transport hook) ---
  const r = useReplay(frames);
  const [compareOn, setCompareOn] = useState(false);
  const failCursorRef = useRef(0);

  const selectRun = (id: string | null) => {
    setSelectedRunId(id);
    r.setIdx(0);
    r.setPlaying(false);
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

  const frame = r.frame;

  const verdict = telemetry?.verdict ?? null;
  const compare = telemetry?.compare ?? null;
  const baselineTrack = telemetry?.baseline_track ?? null;

  // Shared scene-building (road geometry + recorded OSI scene + event markers).
  const { roadGeometry, sceneTrafficLights, baseEgoObjects, events } =
    useSceneReplay(telemetry, frames, frame);

  // --- fail markers from the verdict ---
  const failMarkers = useMemo(() => (verdict?.results ?? [])
    .filter((r) => r.status === 'fail' && typeof r.idx === 'number')
    .map((r) => ({ idx: r.idx as number, t: r.t, event: r.event, reason: r.reason })),
  [verdict]);

  const jumpToFail = () => {
    if (failMarkers.length === 0) return;
    const c = failCursorRef.current % failMarkers.length;
    r.setIdx(failMarkers[c].idx);
    r.setPlaying(false);
    failCursorRef.current = c + 1;
  };

  // --- baseline ghost (Default) at the current frame ---
  const ghost = compareOn && baselineTrack && baselineTrack[r.idx] ? baselineTrack[r.idx] : null;
  const ghostHeading = useMemo(() => {
    if (!ghost || !baselineTrack) return 0;
    const nxt = baselineTrack[Math.min(r.idx + 1, baselineTrack.length - 1)];
    return Math.atan2(nxt.y - ghost.y, nxt.x - ghost.x) || 0;
  }, [ghost, baselineTrack, r.idx]);
  const ghostPathPts = useMemo<[number, number][] | null>(
    () => (compareOn && baselineTrack ? baselineTrack.map((p) => [p.x, p.y]) : null),
    [compareOn, baselineTrack],
  );

  const egoObjects: OsiObject[] = useMemo(() => {
    const objs = baseEgoObjects.slice();
    if (ghost) {
      objs.push({
        id: -1, name: 'Default', x: ghost.x, y: ghost.y, z: 0, h: ghostHeading, speed: ghost.speed,
        head_light: 'off', indicator: 'off', brake_light: 'off',
        obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
      } as unknown as OsiObject);
    }
    return objs;
  }, [baseEgoObjects, ghost, ghostHeading]);

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
          {runs.map((rr) => (
            <option key={rr.id} value={rr.id}>
              {rr.id}{typeof rr.meta.frames === 'number' ? ` (${rr.meta.frames}f)` : ''}
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
            <ReplayControls r={r} frames={frames} />

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
            <ErrorChart frames={frames} idx={r.idx} />
          )}

          {frames.length > 0 && (
            <VTargetProfileChart frames={frames} idx={r.idx} midlong={frame?.midlong} />
          )}

          {frames.length > 0 && (
            <PolicyTimelinePanel frames={frames} idx={r.idx} />
          )}
        </div>
      </div>

      {/* Scrub bar with event + fail markers */}
      {frames.length > 0 && (
        <Timeline
          frames={frames}
          idx={r.idx}
          events={events}
          fails={failMarkers}
          onSeek={(i) => { r.setIdx(i); r.setPlaying(false); }}
        />
      )}
      </>)}
    </div>
  );
}
