/**
 * Left rail for the annotation page: a verdict/label filter, a text search, and a
 * scannable list of runs. Optimized for fast keyboard-driven labeling — the
 * selected row scrolls into view and a label dot shows what's already done.
 */
import { useEffect, useMemo, useRef } from 'react';
import type { AnnotationRun } from '../../api/client';

export type RunFilter = 'all' | 'unlabeled' | 'pass' | 'fail' | 'needs-review';

const VERDICT_CHIP: Record<string, string> = {
  pass: 'text-success',
  fail: 'text-destructive',
  'needs-review': 'text-warning',
  error: 'text-destructive',
};

function verdictGlyph(v: string | null): string {
  if (v === 'pass') return '✓';
  if (v === 'fail') return '✗';
  if (v === 'error') return '!';
  return '⚠'; // needs-review / unknown
}

export function RunListPanel({
  runs, allRuns, selectedId, onSelect, filter, onFilter, search, onSearch,
}: {
  /** rows to display (already filtered) */
  runs: AnnotationRun[];
  /** the full set, for filter-chip counts (so counts don't collapse to the active filter) */
  allRuns: AnnotationRun[];
  selectedId: string | null;
  onSelect: (id: string) => void;
  filter: RunFilter;
  onFilter: (f: RunFilter) => void;
  search: string;
  onSearch: (s: string) => void;
}) {
  const selRef = useRef<HTMLButtonElement | null>(null);

  // Keep the active row visible as J/K moves the selection.
  useEffect(() => {
    selRef.current?.scrollIntoView({ block: 'nearest' });
  }, [selectedId]);

  // Counts reflect the FULL set (not the active filter) so the chips always show
  // how many total / unlabeled / pass / fail / review runs exist.
  const counts = useMemo(() => {
    const c = { all: allRuns.length, unlabeled: 0, pass: 0, fail: 0, 'needs-review': 0 };
    for (const r of allRuns) {
      if (!r.labeled) c.unlabeled += 1;
      if (r.verdict_overall === 'pass') c.pass += 1;
      else if (r.verdict_overall === 'fail') c.fail += 1;
      else if (r.verdict_overall === 'needs-review') c['needs-review'] += 1;
    }
    return c;
  }, [allRuns]);

  const filters: { key: RunFilter; label: string }[] = [
    { key: 'all', label: 'All' },
    { key: 'unlabeled', label: 'Unlabeled' },
    { key: 'pass', label: 'Pass' },
    { key: 'fail', label: 'Fail' },
    { key: 'needs-review', label: 'Review' },
  ];

  return (
    <div className="flex flex-col h-full gap-2">
      {/* Segmented verdict/label filter */}
      <div className="flex flex-wrap gap-0.5 rounded border border-glass-edge p-0.5 text-[11px]">
        {filters.map((f) => (
          <button
            key={f.key}
            onClick={() => onFilter(f.key)}
            className={`px-1.5 py-0.5 rounded ${
              filter === f.key ? 'bg-primary/80 text-white' : 'text-text-secondary hover:bg-glass-2'
            }`}
          >
            {f.label} <span className="opacity-60">{counts[f.key]}</span>
          </button>
        ))}
      </div>

      <input
        type="text"
        value={search}
        onChange={(e) => onSearch(e.target.value)}
        placeholder="Search scenario / run…"
        className="bg-glass-1 border border-glass-edge rounded px-2 py-1 text-xs text-foreground"
      />

      {/* Run rows */}
      <div className="flex-1 min-h-0 overflow-y-auto -mr-1 pr-1 flex flex-col gap-0.5">
        {runs.length === 0 && (
          <div className="text-xs text-text-tertiary px-1 py-2">No runs match this filter.</div>
        )}
        {runs.map((r) => {
          const active = r.run_id === selectedId;
          return (
            <button
              key={r.run_id}
              ref={active ? selRef : undefined}
              onClick={() => onSelect(r.run_id)}
              className={`text-left rounded px-2 py-1.5 transition-colors ${
                active ? 'bg-glass-active' : 'hover:bg-glass-2'
              }`}
            >
              <div className="flex items-center gap-1.5">
                <span
                  className={`inline-block w-1.5 h-1.5 rounded-full shrink-0 ${
                    r.labeled ? 'bg-primary' : 'bg-transparent border border-glass-edge'
                  }`}
                  title={r.labeled ? `labeled: ${r.label}` : 'unlabeled'}
                />
                <span className="text-xs text-foreground truncate flex-1">
                  {r.scenario_stem ?? r.run_id}
                </span>
                <span
                  className={`text-[11px] shrink-0 ${VERDICT_CHIP[r.verdict_overall ?? ''] ?? 'text-text-tertiary'}`}
                  title={`verdict: ${r.verdict_overall ?? 'n/a'}`}
                >
                  {verdictGlyph(r.verdict_overall)}
                </span>
              </div>
              <div className="text-[10px] text-text-tertiary truncate pl-3">
                {r.batch_id ? `batch/${r.batch_id}` : r.run_id}
                {r.label && <span className="text-primary"> · {r.label}</span>}
              </div>
            </button>
          );
        })}
      </div>
    </div>
  );
}
