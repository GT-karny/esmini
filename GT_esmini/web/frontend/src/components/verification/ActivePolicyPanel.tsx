import { useMemo } from 'react';
import type { VdTelemetryFrame, PolicyConstraint, PolicyConstraintKind } from '../../api/client';
import {
  constraintDetail,
  constraintKindLabel,
  policySourceColor,
  policySourceLabel,
} from '../../lib/policyDisplay';

/**
 * [F5 / Phase 4] Live traffic-policy decision panel.
 *
 * Renders what the VirtualDriver is deciding *right now* and why: one row per
 * active PolicyConstraint (see api/client.ts / VirtualDriverTelemetryJson.cpp
 * `policy` block) with its source (traffic light / stop sign / conflict point /
 * crosswalk / …), decision kind (Stop / Speed limit / Yield / Wait), the value
 * (distance ahead in m, target speed in m/s + km/h), and how long that
 * constraint has been continuously active (derived from the frame history).
 *
 * Rows are ordered nearest-first (smallest s) so the most imminent decision sits
 * on top. Degrades to a neutral empty state for pre-Phase-3 telemetry (no
 * `policy` field), an invalid snapshot (`policy.valid === false`), or a clear
 * road (no constraints) — it never throws on missing data.
 *
 * Complements PolicyTimelinePanel: this is the current decision detail, that is
 * the activity-over-time history. Colours/labels are shared via lib/policyDisplay.
 */

interface ActiveConstraint {
  c: PolicyConstraint;
  /** Seconds the (source, kind) constraint has been continuously active. */
  activeFor: number;
}

/** Stable key for matching "the same" constraint across frames. */
const keyOf = (c: PolicyConstraint) => `${c.source}|${c.kind}`;

/* Display order: the imminent "must act" decisions first (stop, then yield, then
 * a speed-limited zone ahead), then standing speed caps (max_speed carries s=0 —
 * a global cap, not a 0 m point), then time waits. Within a rank, nearer (smaller
 * s) first. Keeps the notable decision on top instead of letting an s=0 cap float
 * up under a naive s-sort. */
const KIND_ORDER: Record<PolicyConstraintKind, number> = {
  stop_at_s: 0,
  yield: 1,
  max_speed_to_s: 2,
  max_speed: 3,
  wait_until: 4,
  none: 5,
};

function compareConstraints(a: PolicyConstraint, b: PolicyConstraint): number {
  const ra = KIND_ORDER[a.kind] ?? 9;
  const rb = KIND_ORDER[b.kind] ?? 9;
  if (ra !== rb) return ra - rb;
  return a.s - b.s;
}

function computeActive(frames: VdTelemetryFrame[]): ActiveConstraint[] {
  if (!frames.length) return [];
  const last = frames.length - 1;
  const cur = frames[last];
  if (!cur.policy?.valid) return [];
  const constraints = cur.policy.constraints ?? [];
  if (constraints.length === 0) return [];

  const tNow = cur.sim_time;
  return constraints
    .map((c) => {
      // Walk back while a matching (source, kind) constraint stays present.
      let start = last;
      for (let j = last; j >= 0; j--) {
        const has = (frames[j].policy?.constraints ?? []).some((o) => keyOf(o) === keyOf(c));
        if (!has) break;
        start = j;
      }
      return { c, activeFor: Math.max(0, tNow - frames[start].sim_time) };
    })
    .sort((a, b) => compareConstraints(a.c, b.c));
}

function EmptyNote({ text }: { text: string }) {
  return <div className="text-[11px] text-text-tertiary py-1">{text}</div>;
}

export function ActivePolicyPanel({ frames }: { frames: VdTelemetryFrame[] }) {
  const active = useMemo(() => computeActive(frames), [frames]);

  if (!frames.length) return null;
  const cur = frames[frames.length - 1];

  let body: React.ReactNode;
  if (!cur.policy) {
    body = <EmptyNote text="No policy telemetry (pre-Phase 3 run or policies disabled)." />;
  } else if (active.length === 0) {
    body = <EmptyNote text="No active policy constraints — clear road." />;
  } else {
    body = (
      <div className="space-y-1">
        {active.map(({ c, activeFor }, i) => (
          <div
            key={`${keyOf(c)}-${i}`}
            className="flex items-center gap-2 rounded bg-white/[0.03] px-2 py-1"
          >
            <span
              className="h-2 w-2 shrink-0 rounded-full"
              style={{ backgroundColor: policySourceColor(c.source) }}
              aria-hidden
            />
            <div className="min-w-0 flex-1">
              <div className="flex items-center justify-between gap-2">
                <span
                  className="truncate text-[11px]"
                  style={{ color: policySourceColor(c.source) }}
                >
                  {policySourceLabel(c.source)}
                </span>
                <span className="shrink-0 rounded bg-white/[0.06] px-1 text-[9px] uppercase tracking-wide text-text-tertiary">
                  {constraintKindLabel(c.kind)}
                </span>
              </div>
              <div className="font-mono text-[10px] text-foreground">{constraintDetail(c)}</div>
            </div>
            <span
              className="shrink-0 font-mono text-[9px] text-text-tertiary"
              title="continuously active for"
            >
              {activeFor.toFixed(1)}s
            </span>
          </div>
        ))}
      </div>
    );
  }

  return (
    <div className="rounded border border-glass-edge p-2">
      <div className="mb-1 flex items-center justify-between">
        <span className="text-[11px] text-text-tertiary">active policy</span>
        {active.length > 0 && (
          <span className="font-mono text-[10px] text-text-tertiary">{active.length}</span>
        )}
      </div>
      {body}
    </div>
  );
}
