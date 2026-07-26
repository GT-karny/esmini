import { useState } from 'react';
import type { VdTelemetryFrame } from '../../api/client';

/**
 * FFB override-latch margin gauge (feature:F7b) — "how much headroom is left
 * before a manual takeover latches?" A threshold that is only a number in a
 * config form cannot be tuned; this renders the same quantity the detector
 * itself is comparing against, live.
 *
 * Backed by VirtualDriverTelemetry.ffb.gates (OverrideManager::FfbLatchDiagnostics,
 * VirtualDriverTelemetryJson.cpp `gates` block). Renders the four quantities an
 * operator needs:
 *   - current residual vs. residual_threshold (bar — immediate feel for margin)
 *   - peak residual reached this run (held here; resets on job/connection change)
 *   - approach ratio  = peak residual / threshold (NOT clamped: crossing 1.0 is
 *     exactly the point sustain_accum starts climbing toward a latch)
 *   - arrival ratio   = peak sustain_accum / sustain_time (reaches 1.0 at the
 *     instant the manual latch fires; can keep climbing past it while held)
 *
 * Deliberately NOT "threshold / residual" — that ratio diverges to infinity at
 * rest (residual -> 0), which is the common case, and would make the gauge
 * unusable exactly when nothing is happening.
 *
 * The `gates` block is present on every frame once F7b telemetry exists, but is
 * zeroed (residual_threshold=0, sustain_time=0, block_reason="inactive")
 * whenever the FFB target-track servo isn't running this frame — feature
 * disabled, no AD lateral, or lateral not manual-capable at all (see
 * OverrideManager.cpp: `ffb_diag_ = {}` in the else branch of
 * `lat_configured_manual_ && ffb_sample_.active`). That state is rendered as
 * "detector idle", never as "residual is 0", so a flat line at 0 caused by a
 * disconnected/inactive detector can never be mistaken for a flat line caused
 * by a perfectly tracked wheel.
 */

function clip01(v: number): number {
  return Math.max(0, Math.min(1, v));
}

/** Ratio bar. Fill clips visually at 100%; an "over" marker + the raw
 * (unclamped) percentage text next to it are what show a ratio past 1.0. */
function MarginBar({ ratio, fillClass }: { ratio: number; fillClass: string }) {
  const over = ratio > 1;
  return (
    <div className="relative h-1.5 w-full rounded-full bg-glass-1 overflow-hidden">
      <div
        className={`h-full rounded-full ${over ? 'bg-destructive' : fillClass}`}
        style={{ width: `${clip01(ratio) * 100}%` }}
      />
      {over && <div className="absolute inset-y-0 right-0 w-[3px] bg-destructive" title="exceeds 100%" />}
    </div>
  );
}

function Row({ label, value, title }: { label: string; value: string; title?: string }) {
  return (
    <div className="flex justify-between gap-2" title={title}>
      <span className="text-text-tertiary">{label}</span>
      <span className="font-mono text-foreground">{value}</span>
    </div>
  );
}

function RatioRow({
  label, ratioPct, fillClass, title,
}: {
  label: string; ratioPct: number; fillClass: string; title: string;
}) {
  return (
    <div className="space-y-1" title={title}>
      <div className="flex justify-between">
        <span className="text-text-tertiary">{label}</span>
        <span className="font-mono text-foreground">{ratioPct.toFixed(0)}%</span>
      </div>
      <MarginBar ratio={ratioPct / 100} fillClass={fillClass} />
    </div>
  );
}

interface PeakState {
  runKey: string | null;
  residual: number;
  sustain: number;
}

const INITIAL_PEAK: PeakState = { runKey: null, residual: 0, sustain: 0 };

export function FfbMarginPanel({
  frame,
  runKey,
}: {
  frame: VdTelemetryFrame | null;
  /** Peak tracking resets whenever this changes — pass the (job id /
   * connection) identity so a new run/reconnect starts from a clean 0. */
  runKey: string | null;
}) {
  const [peak, setPeak] = useState<PeakState>(INITIAL_PEAK);

  const gates = frame?.ffb?.gates ?? null;
  const live = !!gates && gates.block_reason !== 'inactive';

  // Running peaks, derived every render from the (possibly reset) prior peak
  // plus this frame's values — React's documented "adjust state during
  // render" pattern (https://react.dev/learn/you-might-not-need-an-effect),
  // not a ref mutated in render nor a setState tucked inside an effect. Using
  // the freshly-derived values below (not `peak` itself) means this render's
  // output is correct immediately; the conditional setPeak only persists them
  // for the *next* render's "prior peak" — it never causes an extra paint.
  const resetForNewRun = peak.runKey !== runKey;
  const basePeakResidual = resetForNewRun ? 0 : peak.residual;
  const basePeakSustain = resetForNewRun ? 0 : peak.sustain;
  const peakResidual = live ? Math.max(basePeakResidual, gates!.residual) : basePeakResidual;
  const peakSustain = live ? Math.max(basePeakSustain, gates!.sustain_accum) : basePeakSustain;
  if (resetForNewRun || peakResidual !== peak.residual || peakSustain !== peak.sustain) {
    setPeak({ runKey, residual: peakResidual, sustain: peakSustain });
  }

  if (!frame) {
    return (
      <div className="rounded border border-glass-edge p-3 text-xs">
        <div className="text-[11px] text-text-tertiary">override margin</div>
        <div className="text-[11px] text-text-tertiary py-1">No stream — start a run to see live margin.</div>
      </div>
    );
  }

  if (!frame.ffb) {
    return (
      <div className="rounded border border-glass-edge p-3 text-xs">
        <div className="text-[11px] text-text-tertiary">override margin</div>
        <div className="text-[11px] text-text-tertiary py-1">
          No FFB telemetry in this stream (recorded before feature:F7b, or the run predates it).
        </div>
      </div>
    );
  }

  const overrideLateral = frame.override.lateral;
  const hasThreshold = live && gates!.residual_threshold > 1e-9;
  const hasSustainTime = live && gates!.sustain_time > 1e-9;
  const currentRatioPct = hasThreshold ? (gates!.residual / gates!.residual_threshold) * 100 : 0;
  const approachRatioPct = hasThreshold ? (peakResidual / gates!.residual_threshold) * 100 : 0;
  const arrivalRatioPct = hasSustainTime ? (peakSustain / gates!.sustain_time) * 100 : 0;

  return (
    <div className="rounded border border-glass-edge p-3 text-xs space-y-2">
      <div className="flex items-center justify-between">
        <span className="text-[11px] text-text-tertiary">override margin</span>
        <span className={overrideLateral ? 'text-warning' : 'text-success'}>
          {overrideLateral ? 'MANUAL' : 'AUTO'}
        </span>
      </div>

      {!live ? (
        <div className="text-[11px] text-text-tertiary py-1">
          detector idle — FFB target-track servo not running this frame
          (disabled, no AD lateral, or lateral not manual-capable).
        </div>
      ) : (
        <>
          <div className="space-y-1">
            <Row label="residual" value={gates!.residual.toFixed(4)} />
            <Row label="threshold" value={gates!.residual_threshold.toFixed(4)} />
            <MarginBar ratio={currentRatioPct / 100} fillClass={currentRatioPct >= 100 ? 'bg-destructive' : 'bg-cyan-500'} />
          </div>

          <Row
            label="peak residual (run)"
            value={peakResidual.toFixed(4)}
            title="max(residual) since this run/connection started"
          />

          <RatioRow
            label="approach (peak / threshold)"
            ratioPct={approachRatioPct}
            fillClass="bg-amber-500"
            title="peak residual / residual_threshold — not clamped; crossing 100% is exactly where the sustain clock starts climbing"
          />

          <RatioRow
            label="arrival to latch (peak sustain / sustain_time)"
            ratioPct={arrivalRatioPct}
            fillClass="bg-orange-500"
            title="peak sustain_accum / sustain_time — reaches 100% at the instant the manual latch fires"
          />

          <Row label="block reason" value={gates!.block_reason} />
        </>
      )}
    </div>
  );
}
