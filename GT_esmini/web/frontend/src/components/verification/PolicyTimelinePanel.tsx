import { useMemo } from 'react';
import type { VdTelemetryFrame, PolicyConstraint } from '../../api/client';

/* [A3 / Phase 3] Traffic-policy timeline. One row per constraint source
 * (traffic_light / stop_sign / yield_sign / lead_vehicle), shaded where that
 * policy is emitting a constraint, with a playhead cursor at `idx` and a
 * current-frame readout. Reads VdTelemetryFrame.policy; degrades to an empty
 * note when no policy is active (Phase 1/2 runs, or policies disabled). Mirrors
 * the hand-SVG style of ErrorChart so the panels stay visually consistent. */

const SOURCE_COLOR: Record<string, string> = {
  traffic_light: '#E8A24F',
  stop_sign: '#E03131',
  yield_sign: '#E0568A',
  lead_vehicle: '#4F9DE8',
};
const sourceColor = (s: string) => SOURCE_COLOR[s] ?? '#9AA7FF';

function constraintLabel(c: PolicyConstraint): string {
  if (c.kind === 'stop_at_s') return `stop @${c.s.toFixed(0)} m`;
  if (c.kind === 'max_speed') return `max ${c.value.toFixed(1)} m/s`;
  if (c.kind === 'max_speed_to_s') return `max ${c.value.toFixed(1)} m/s →${c.s.toFixed(0)} m`;
  if (c.kind === 'yield') return `yield @${c.s.toFixed(0)} m`;
  if (c.kind === 'wait_until') return `wait ${c.value.toFixed(1)} s`;
  return c.kind;
}

export function PolicyTimelinePanel({ frames, idx }: { frames: VdTelemetryFrame[]; idx: number }) {
  const W = 264, padL = 4, padR = 4;
  const t0 = frames.length ? frames[0].sim_time : 0;
  const t1 = frames.length ? frames[frames.length - 1].sim_time : 1;
  const span = Math.max(1e-3, t1 - t0);

  const { rows, xAt } = useMemo(() => {
    const sources: string[] = [];
    for (const f of frames)
      for (const c of f.policy?.constraints ?? [])
        if (!sources.includes(c.source)) sources.push(c.source);
    const xAt = (i: number) => padL + ((frames[i].sim_time - t0) / span) * (W - padL - padR);
    const rows = sources.map((src) => ({
      src,
      active: frames.map((f) => (f.policy?.constraints ?? []).some((c) => c.source === src)),
    }));
    return { rows, xAt };
  }, [frames, t0, span]);

  if (!frames.length) return null;
  const cursor = Math.max(0, Math.min(idx, frames.length - 1));
  const now = frames[cursor].policy?.constraints ?? [];

  const rowH = 13, gap = 4, pad = 4;
  const H = Math.max(24, pad * 2 + Math.max(1, rows.length) * (rowH + gap) - gap);

  const segments = (active: boolean[]) => {
    const out: { x0: number; x1: number }[] = [];
    let start = -1;
    for (let i = 0; i < active.length; i++) {
      if (active[i] && start < 0) start = i;
      else if (!active[i] && start >= 0) { out.push({ x0: xAt(start), x1: xAt(i - 1) }); start = -1; }
    }
    if (start >= 0) out.push({ x0: xAt(start), x1: xAt(active.length - 1) });
    return out;
  };

  return (
    <div className="rounded border border-glass-edge p-2">
      <div className="text-[11px] text-text-tertiary mb-1">traffic policy</div>
      {rows.length === 0 ? (
        <div className="text-[11px] text-text-tertiary py-1">no policy constraints in this run</div>
      ) : (
        <svg viewBox={`0 0 ${W} ${H}`} className="w-full">
          {rows.map((r, ri) => {
            const y = pad + ri * (rowH + gap);
            return (
              <g key={r.src}>
                <rect x={padL} y={y} width={W - padL - padR} height={rowH} fill="rgba(180,170,230,0.06)" rx={2} />
                {segments(r.active).map((s, si) => (
                  <rect key={si} x={s.x0} y={y} width={Math.max(1, s.x1 - s.x0)} height={rowH}
                        fill={sourceColor(r.src)} opacity={0.7} rx={2} />
                ))}
                <text x={padL + 3} y={y + rowH - 3} fontSize={8} fill="#fff" opacity={0.85}>{r.src}</text>
              </g>
            );
          })}
          <line x1={xAt(cursor)} y1={0} x2={xAt(cursor)} y2={H} stroke="rgba(255,255,255,0.6)" strokeWidth={0.8} />
        </svg>
      )}
      {now.length > 0 && (
        <div className="mt-1 text-[10px] font-mono space-y-0.5">
          {now.map((c, i) => (
            <div key={i} className="flex justify-between gap-2">
              <span style={{ color: sourceColor(c.source) }}>{c.source}</span>
              <span className="text-foreground">{constraintLabel(c)}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
