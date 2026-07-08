import { useMemo } from 'react';
import type { VdTelemetryFrame } from '../../api/client';

/* Shared VirtualDriver telemetry panels — used by both the recorded replay
 * (VerificationReplayPage) and the live view (LiveVdPanel) so the two stay
 * visually identical. */

export function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between gap-2">
      <span className="text-text-tertiary">{label}</span>
      <span className="text-foreground">{value}</span>
    </div>
  );
}

/** Current-frame readouts: speed / pedals / steer / errors / lane / override. */
export function TelemetryInfoRows({ frame }: { frame: VdTelemetryFrame }) {
  return (
    <div className="rounded border border-glass-edge p-3 text-xs font-mono space-y-1">
      <Row label="speed" value={`${(frame.ego.speed * 3.6).toFixed(1)} km/h`} />
      <Row label="throttle" value={frame.driver.throttle.toFixed(2)} />
      <Row label="brake" value={frame.driver.brake.toFixed(2)} />
      <Row label="steer" value={frame.driver.steer.toFixed(3)} />
      <Row label="lat err" value={`${frame.driver.lateral_error.toFixed(3)} m`} />
      <Row label="spd err" value={`${frame.driver.speed_error.toFixed(2)} m/s`} />
      {frame.ego.lane != null && <Row label="lane" value={`${frame.ego.lane} @ road ${frame.ego.track}`} />}
      <Row
        label="indicator"
        value={frame.indicator.left ? 'left' : frame.indicator.right ? 'right' : 'off'}
      />
      <Row
        label="override"
        value={`${frame.override.lateral ? 'lat ' : ''}${frame.override.longitudinal ? 'lon' : ''}${
          !frame.override.lateral && !frame.override.longitudinal ? 'none' : ''
        }`}
      />
    </div>
  );
}

/** Lateral- and speed-error time series with a playhead cursor at `idx`. */
export function ErrorChart({ frames, idx }: { frames: VdTelemetryFrame[]; idx: number }) {
  const W = 264, H = 120, padL = 4, padR = 4, padT = 6, padB = 6;
  const t0 = frames.length ? frames[0].sim_time : 0;
  const t1 = frames.length ? frames[frames.length - 1].sim_time : 1;
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

  if (!frames.length) return null;
  const cursor = Math.max(0, Math.min(idx, frames.length - 1));

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
        <line x1={series.x(cursor)} y1={padT} x2={series.x(cursor)} y2={H - padB} stroke="rgba(255,255,255,0.5)" strokeWidth={0.8} />
      </svg>
    </div>
  );
}
