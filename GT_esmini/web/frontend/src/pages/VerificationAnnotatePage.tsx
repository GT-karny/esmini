/**
 * Human annotation page (/verification/annotate) — the regression-judgment base for
 * VirtualDriver's decision behaviors (Phase 3d oncoming-yield etc.). Lists recorded
 * verification runs (top-level + batch-nested), replays one with the shared
 * LiveSceneView transport, and lets a human label it pass / fail / needs-discussion.
 *
 * Top priority is labeling speed: keyboard shortcuts (P/F/D label + save +
 * auto-advance, J/K navigate, Space play, ,/. step, C comment) and an optimistic
 * list so the next unlabeled run is one keypress away.
 */
import { useEffect, useMemo, useRef, useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import {
  api, type AnnotationLabel, type AnnotationRun, type MatchResult,
} from '../api/client';
import { buildSimulationRequest } from '../api/simulationRequest';
import { LiveSceneView } from '../components/LiveSceneView';
import { TelemetryInfoRows } from '../components/verification/TelemetryPanels';
import {
  useReplay, useSceneReplay, ReplayControls, Timeline,
} from '../components/verification/ReplayTransport';
import { RunListPanel, type RunFilter } from '../components/verification/RunListPanel';
import { AnnotationLabelBar } from '../components/verification/AnnotationLabelBar';

const VERDICT_TEXT: Record<string, string> = {
  pass: 'text-success', fail: 'text-destructive',
  'needs-review': 'text-warning', error: 'text-destructive',
};

export function VerificationAnnotatePage() {
  const queryClient = useQueryClient();
  const [filter, setFilter] = useState<RunFilter>('unlabeled');
  const [search, setSearch] = useState('');
  const [selectedRunId, setSelectedRunId] = useState<string | null>(null);
  const [autoAdvance, setAutoAdvance] = useState(true);
  const [comment, setComment] = useState('');
  const [saved, setSaved] = useState(false);
  const commentRef = useRef<HTMLTextAreaElement | null>(null);

  // All runs (client-side filter/search so switching is instant and counts are exact).
  const { data: runsData } = useQuery({
    queryKey: ['annotation-runs'],
    queryFn: () => api.getAnnotationRuns(),
    refetchInterval: 8000,
  });
  const allRuns = useMemo(() => runsData?.runs ?? [], [runsData]);

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    return allRuns.filter((r) => {
      if (filter === 'unlabeled' && r.labeled) return false;
      if (filter === 'pass' && r.verdict_overall !== 'pass') return false;
      if (filter === 'fail' && r.verdict_overall !== 'fail') return false;
      if (filter === 'needs-review' && r.verdict_overall !== 'needs-review') return false;
      if (q && !(`${r.scenario_stem ?? ''} ${r.run_id}`.toLowerCase().includes(q))) return false;
      return true;
    });
  }, [allRuns, filter, search]);

  // Effective selection: explicit, else first of the filtered list.
  const effectiveId = selectedRunId ?? (filtered.length > 0 ? filtered[0].run_id : null);
  const selected = useMemo(
    () => allRuns.find((r) => r.run_id === effectiveId) ?? null,
    [allRuns, effectiveId]);

  // Reset comment editor when the active run changes.
  useEffect(() => { setComment(selected?.comment ?? ''); setSaved(false); }, [effectiveId]); // eslint-disable-line react-hooks/exhaustive-deps

  // --- telemetry + replay (shared transport) ---
  const { data: telemetry } = useQuery({
    queryKey: ['verification-telemetry', effectiveId],
    queryFn: () => api.getVerificationTelemetry(effectiveId!),
    enabled: !!effectiveId,
  });
  const frames = useMemo(() => telemetry?.frames ?? [], [telemetry]);
  const r = useReplay(frames);
  const frame = r.frame;
  const { roadGeometry, sceneTrafficLights, baseEgoObjects, events } =
    useSceneReplay(telemetry, frames, frame);

  const verdict = telemetry?.verdict ?? null;
  const failMarkers = useMemo(() => (verdict?.results ?? [])
    .filter((x) => x.status === 'fail' && typeof x.idx === 'number')
    .map((x) => ({ idx: x.idx as number, t: x.t, event: x.event, reason: x.reason })),
  [verdict]);

  // --- selection helpers ---
  const selectRun = (id: string) => { setSelectedRunId(id); r.setIdx(0); r.setPlaying(false); };
  const moveSelection = (delta: number) => {
    if (filtered.length === 0) return;
    const i = filtered.findIndex((x) => x.run_id === effectiveId);
    const next = Math.max(0, Math.min(filtered.length - 1, (i < 0 ? 0 : i) + delta));
    selectRun(filtered[next].run_id);
  };
  const advanceToNextUnlabeled = (justLabeled: string) => {
    const i = filtered.findIndex((x) => x.run_id === justLabeled);
    for (let j = i + 1; j < filtered.length; j++) {
      if (!filtered[j].labeled) { selectRun(filtered[j].run_id); return; }
    }
    // wrap from start
    for (let j = 0; j < filtered.length; j++) {
      if (!filtered[j].labeled && filtered[j].run_id !== justLabeled) {
        selectRun(filtered[j].run_id); return;
      }
    }
  };

  // --- label mutation (optimistic) ---
  const labelMut = useMutation({
    mutationFn: ({ runId, label }: { runId: string; label: AnnotationLabel }) =>
      api.setAnnotation(runId, { label, comment }),
    onMutate: async ({ runId, label }) => {
      await queryClient.cancelQueries({ queryKey: ['annotation-runs'] });
      const prev = queryClient.getQueryData<{ runs: AnnotationRun[] }>(['annotation-runs']);
      queryClient.setQueryData<{ runs: AnnotationRun[] }>(['annotation-runs'], (old) =>
        old ? { runs: old.runs.map((x) => x.run_id === runId
          ? { ...x, label, labeled: true, comment } : x) } : old);
      return { prev };
    },
    onError: (_e, _v, ctx) => {
      if (ctx?.prev) queryClient.setQueryData(['annotation-runs'], ctx.prev);
    },
    onSuccess: (_data, { runId }) => {
      setSaved(true);
      if (autoAdvance) advanceToNextUnlabeled(runId);
    },
    onSettled: () => queryClient.invalidateQueries({ queryKey: ['annotation-runs'] }),
  });

  const applyLabel = (label: AnnotationLabel) => {
    if (!effectiveId) return;
    setSaved(false);
    labelMut.mutate({ runId: effectiveId, label });
  };

  // --- "運転席目線で開く": re-run the scenario with VirtualDriver + esmini's
  // native driver (first-person) camera in a 3D window. Behavior is deterministic
  // so the re-run matches the recorded run; the OSG viewer renders pitch/roll the
  // 2D scene can't. Needs project_id + scenario_file (batch runs lack project_id). ---
  const canDriverView = !!(selected?.project_id && selected?.scenario_file);
  const driverViewMut = useMutation({
    mutationFn: () => {
      const req = buildSimulationRequest({
        scenarioId: selected!.scenario_file!,
        projectId: selected!.project_id!,
        controllerType: 'virtual_driver',
        execution: {
          headless: false,                    // show the native 3D window
          no_realtime: false,                 // real-time so it's watchable
          threads: true,
          window: { x: 80, y: 80 },
          extra_args: ['--camera_mode', 'driver'],
        },
      });
      return api.createSimulation(req);
    },
  });

  // --- "Suggested from history" (rule-based match) ---
  const { data: matchData } = useQuery({
    queryKey: ['annotation-match', effectiveId],
    queryFn: () => api.matchAnnotations(effectiveId!, 5),
    enabled: !!effectiveId,
    retry: false,
  });

  // --- keyboard shortcuts ---
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const el = document.activeElement as HTMLElement | null;
      const typing = el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA');
      if (typing) {
        if (e.key === 'Escape') (el as HTMLElement).blur();
        return;
      }
      switch (e.key.toLowerCase()) {
        case 'p': e.preventDefault(); applyLabel('pass'); break;
        case 'f': e.preventDefault(); applyLabel('fail'); break;
        case 'd': e.preventDefault(); applyLabel('needs-discussion'); break;
        case 'c': e.preventDefault(); commentRef.current?.focus(); break;
        case 'j': e.preventDefault(); moveSelection(1); break;
        case 'k': e.preventDefault(); moveSelection(-1); break;
        case 'arrowdown': e.preventDefault(); moveSelection(1); break;
        case 'arrowup': e.preventDefault(); moveSelection(-1); break;
        case ' ': e.preventDefault(); r.togglePlay(); break;
        case ',': e.preventDefault(); r.stepBy(-1); break;
        case '.': e.preventDefault(); r.stepBy(1); break;
        case 'enter':
          e.preventDefault();
          if (effectiveId) advanceToNextUnlabeled(effectiveId);
          break;
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });

  return (
    <div className="h-full flex flex-col p-4 gap-3">
      <SummaryCard runs={allRuns} />

      <div className="flex-1 min-h-0 flex gap-3">
        {/* Left rail */}
        <div className="w-64 shrink-0">
          <RunListPanel
            runs={filtered}
            allRuns={allRuns}
            selectedId={effectiveId}
            onSelect={selectRun}
            filter={filter}
            onFilter={setFilter}
            search={search}
            onSearch={setSearch}
          />
        </div>

        {/* Center: scene + transport + timeline */}
        <div className="flex-1 min-h-0 flex flex-col gap-2">
          {selected ? (
            <>
              <div className="flex items-center gap-3 flex-wrap text-sm">
                <span className="font-display text-foreground">
                  {selected.scenario_stem ?? selected.run_id}
                </span>
                <span className={`text-xs ${VERDICT_TEXT[selected.verdict_overall ?? ''] ?? 'text-text-tertiary'}`}>
                  verdict: {selected.verdict_overall ?? 'n/a'}
                </span>
                <button
                  onClick={() => driverViewMut.mutate()}
                  disabled={!canDriverView || driverViewMut.isPending}
                  title={canDriverView
                    ? 'このシナリオを VirtualDriver + 運転席カメラでネイティブ 3D 起動'
                    : 'project_id / scenario_file が無い run（batch 等）では再走できません'}
                  className="px-2 py-1 rounded text-xs font-medium border border-glass-edge text-text-secondary hover:bg-glass-2 disabled:opacity-40"
                >
                  {driverViewMut.isPending ? '起動中…' : '運転席目線で開く ▶'}
                </button>
                {driverViewMut.error != null && (
                  <span className="text-xs text-destructive">
                    {/409/.test(String(driverViewMut.error))
                      ? '別のシミュレーションが実行中です — 終了後に再試行してください。'
                      : String(driverViewMut.error as Error)}
                  </span>
                )}
                {frames.length > 0 && <ReplayControls r={r} frames={frames} />}
              </div>

              <div className="flex-1 min-h-0 rounded overflow-hidden border border-glass-edge">
                <LiveSceneView
                  objects={baseEgoObjects}
                  roadGeometry={roadGeometry}
                  trafficLights={sceneTrafficLights}
                  vdTelemetry={frame}
                  midlong={frame?.midlong ?? null}
                  className="h-full"
                  viewRadius={40}
                />
              </div>

              {frames.length > 0 ? (
                <Timeline
                  frames={frames}
                  idx={r.idx}
                  events={events}
                  fails={failMarkers}
                  onSeek={(i) => { r.setIdx(i); r.setPlaying(false); }}
                />
              ) : (
                <div className="text-xs text-text-tertiary">Loading telemetry…</div>
              )}
            </>
          ) : (
            <div className="flex-1 flex items-center justify-center text-sm text-text-tertiary">
              {allRuns.length === 0
                ? 'No runs registered yet. Run gt_sim_test (batch) into the results dir, or a VirtualDriver GUI run.'
                : 'All runs in this filter are labeled. 🎉 Switch the filter to review.'}
            </div>
          )}
        </div>

        {/* Right rail: label + telemetry + suggestions */}
        <div className="w-72 shrink-0 flex flex-col gap-3 overflow-y-auto">
          {selected && (
            <AnnotationLabelBar
              ref={commentRef}
              current={selected.label}
              comment={comment}
              onLabel={applyLabel}
              onComment={(t) => { setComment(t); setSaved(false); }}
              onCommentBlur={() => {
                // Persist a comment edit against the existing label (if any).
                if (selected.label && comment !== (selected.comment ?? ''))
                  labelMut.mutate({ runId: selected.run_id, label: selected.label });
              }}
              saved={saved}
              saving={labelMut.isPending}
              error={labelMut.error ? String(labelMut.error as Error) : null}
              autoAdvance={autoAdvance}
              onToggleAutoAdvance={() => setAutoAdvance((v) => !v)}
            />
          )}

          {frame && <TelemetryInfoRows frame={frame} />}

          {matchData && matchData.matches.length > 0 && (
            <MatchPanel matches={matchData.matches} onSelect={selectRun} />
          )}
        </div>
      </div>
    </div>
  );
}

/* ---------- batch dashboard summary ---------- */

function SummaryCard({ runs }: { runs: AnnotationRun[] }) {
  const [byBatch, setByBatch] = useState(false);
  const agg = useMemo(() => {
    const c = { total: runs.length, pass: 0, fail: 0, 'needs-review': 0, error: 0, labeled: 0 };
    for (const r of runs) {
      if (r.labeled) c.labeled += 1;
      const v = r.verdict_overall;
      if (v === 'pass') c.pass += 1;
      else if (v === 'fail') c.fail += 1;
      else if (v === 'needs-review') c['needs-review'] += 1;
      else if (v === 'error') c.error += 1;
    }
    return c;
  }, [runs]);

  const batches = useMemo(() => {
    const map = new Map<string, { total: number; labeled: number; pass: number; fail: number }>();
    for (const r of runs) {
      const key = r.batch_id ?? '(top-level)';
      const e = map.get(key) ?? { total: 0, labeled: 0, pass: 0, fail: 0 };
      e.total += 1;
      if (r.labeled) e.labeled += 1;
      if (r.verdict_overall === 'pass') e.pass += 1;
      else if (r.verdict_overall === 'fail') e.fail += 1;
      map.set(key, e);
    }
    return [...map.entries()];
  }, [runs]);

  const pct = agg.total > 0 ? Math.round((agg.labeled / agg.total) * 100) : 0;

  return (
    <div className="rounded border border-glass-edge p-3 text-xs flex flex-col gap-2">
      <div className="flex items-center gap-4 flex-wrap">
        <span className="text-foreground font-medium">total {agg.total}</span>
        <span className="text-success">✓ pass {agg.pass}</span>
        <span className="text-destructive">✗ fail {agg.fail}</span>
        <span className="text-warning">⚠ review {agg['needs-review']}</span>
        {agg.error > 0 && <span className="text-destructive">! error {agg.error}</span>}
        <span className="text-text-secondary ml-auto">labeled {agg.labeled} / {agg.total}</span>
        <button
          onClick={() => setByBatch((v) => !v)}
          className={`px-2 py-0.5 rounded border text-[11px] ${
            byBatch ? 'bg-primary/30 text-foreground border-glass-edge' : 'border-glass-edge text-text-secondary hover:bg-glass-2'
          }`}
        >
          group by batch
        </button>
      </div>
      <div className="h-1.5 rounded bg-glass-1 overflow-hidden">
        <div className="h-full bg-primary" style={{ width: `${pct}%` }} />
      </div>
      {byBatch && (
        <div className="flex flex-col gap-0.5 pt-1">
          {batches.map(([id, e]) => (
            <div key={id} className="flex items-center gap-3 text-[11px] text-text-secondary">
              <span className="text-foreground truncate flex-1">{id}</span>
              <span className="text-success">✓{e.pass}</span>
              <span className="text-destructive">✗{e.fail}</span>
              <span>labeled {e.labeled}/{e.total}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

/* ---------- suggested-from-history match list ---------- */

function MatchPanel({ matches, onSelect }: { matches: MatchResult[]; onSelect: (id: string) => void }) {
  return (
    <div className="rounded border border-glass-edge p-3 text-xs flex flex-col gap-1.5">
      <div className="font-medium text-foreground">Suggested from history</div>
      {matches.map((m) => (
        <button
          key={m.run_id}
          onClick={() => onSelect(m.run_id)}
          className="text-left rounded px-2 py-1 hover:bg-glass-2"
          title={m.reasons.join(' · ')}
        >
          <div className="flex items-center gap-2">
            <span className={`${m.label === 'pass' ? 'text-success' : m.label === 'fail' ? 'text-destructive' : 'text-warning'}`}>
              {m.label}
            </span>
            <span className="text-text-tertiary ml-auto">{(m.score * 100).toFixed(0)}%</span>
          </div>
          <div className="text-[10px] text-text-tertiary truncate">{m.reasons[0] ?? m.run_id}</div>
          {m.comment && <div className="text-[10px] text-text-secondary truncate">“{m.comment}”</div>}
        </button>
      ))}
    </div>
  );
}
