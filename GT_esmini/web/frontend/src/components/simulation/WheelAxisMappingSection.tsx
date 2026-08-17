import { useEffect, useRef, useState } from 'react';
import { useQuery } from '@tanstack/react-query';

import { api, type WheelAxisMapping } from '../../api/client';
import { useWheelProbe } from '../../hooks/useWheelProbe';

/**
 * feature:F8 -- wheel axis assignment + raw-range calibration.
 *
 * Why this exists: the axis order was hardcoded (0=steer, 1=throttle, 2=brake,
 * 3=clutch) with the G29 raw pedal convention baked in, and a G923 reports a
 * different order -- so the brake pedal arrived as clutch and nothing in the
 * config could say otherwise.
 *
 * Why the live readout comes from the server and not navigator.getGamepads():
 * the browser has its own axis index space, so "assign the axis you just moved"
 * built on it can write an index the simulator reads as a different function.
 * GT_WheelProbe reads through the same SDL the simulator does, and returns the
 * normalized values through the same C++ normalizer -- so what this panel shows
 * is what a run would feed the vehicle.
 */

export const DEFAULT_AXIS_MAPPING: WheelAxisMapping = {
  steer_axis: 0,
  steer_raw_center: 0,
  steer_raw_full: 32767,
  throttle_axis: 1,
  throttle_raw_released: 32767,
  throttle_raw_full: -32768,
  brake_axis: 2,
  brake_raw_released: 32767,
  brake_raw_full: -32768,
  clutch_axis: 3,
  clutch_raw_released: 32767,
  clutch_raw_full: -32768,
};

type PedalFn = 'throttle' | 'brake' | 'clutch';
type AxisFn = 'steer' | PedalFn;

const PEDALS: { fn: PedalFn; label: string }[] = [
  { fn: 'throttle', label: 'Throttle' },
  { fn: 'brake', label: 'Brake' },
  { fn: 'clutch', label: 'Clutch' },
];

// Raw deviation an axis must show during detection to be accepted as "the one
// the user moved". SDL raw range is ±32767, mechanical jitter on a G29 column is
// ~150 raw, so 3000 is ~9% of full travel: far above noise, well below a real
// pedal press.
const DETECT_MIN_DEVIATION = 3000;
const DETECT_WINDOW_MS = 4000;

// Idle test for the pedal-polarity check: how many consecutive frames must agree,
// and how much raw wander still counts as "not moving". 150 raw is ~0.5% of full
// scale -- above a pedal potentiometer's noise, far below any real press. At the
// probe's 30 Hz, 15 frames is ~0.5 s.
const IDLE_FRAMES = 15;
const IDLE_RAW_EPS = 150;
const HISTORY_LEN = 30;

interface DetectState {
  fn: AxisFn;
  baseline: number[];
  /** Most extreme value seen per axis, signed relative to the baseline. */
  extreme: number[];
  deadline: number;
}

interface Props {
  mapping: WheelAxisMapping;
  deviceIndex: number;
  onChange: (mapping: WheelAxisMapping) => void;
}

export function WheelAxisMappingSection({ mapping, deviceIndex, onChange }: Props) {
  const [probeEnabled, setProbeEnabled] = useState(false);
  const [detect, setDetect] = useState<DetectState | null>(null);
  const [detectResult, setDetectResult] = useState<string | null>(null);
  // Short frame history, used only by the idle test in pedalReadsPressedWhileIdle
  // (a single frame cannot tell "resting" from "being held").
  const [history, setHistory] = useState<{ axes: number[] }[]>([]);

  const { data: status } = useQuery({
    queryKey: ['wheel-probe-status'],
    queryFn: api.getWheelProbeStatus,
  });

  const { status: streamStatus, meta, frame, error } = useWheelProbe({
    enabled: probeEnabled && status?.available === true,
    device: deviceIndex,
    mapping,
  });

  const numAxes = meta?.num_axes ?? 4;

  // --- detection ----------------------------------------------------------
  // Accumulate the largest signed deviation per axis while a detection window
  // is open. Done in an effect on the latest frame rather than by sampling on a
  // timer, so no reported movement is missed between renders.
  const detectRef = useRef<DetectState | null>(null);
  detectRef.current = detect;

  useEffect(() => {
    if (!frame) return;
    setHistory((prev) => [...prev.slice(-(HISTORY_LEN - 1)), { axes: frame.axes }]);
  }, [frame]);

  useEffect(() => {
    const active = detectRef.current;
    if (!active || !frame) return;

    const extreme = active.extreme.slice();
    frame.axes.forEach((raw, i) => {
      const dev = raw - (active.baseline[i] ?? 0);
      const prev = extreme[i] ?? active.baseline[i] ?? 0;
      const prevDev = prev - (active.baseline[i] ?? 0);
      if (Math.abs(dev) > Math.abs(prevDev)) extreme[i] = raw;
    });

    if (Date.now() >= active.deadline) {
      finishDetection({ ...active, extreme });
      return;
    }
    setDetect({ ...active, extreme });
    // eslint-disable-next-line react-hooks/exhaustive-deps -- keyed on the frame
  }, [frame]);

  const startDetection = (fn: AxisFn) => {
    if (!frame) {
      setDetectResult('No live data yet — start the readout first.');
      return;
    }
    setDetectResult(null);
    setDetect({
      fn,
      baseline: frame.axes.slice(),
      extreme: frame.axes.slice(),
      deadline: Date.now() + DETECT_WINDOW_MS,
    });
  };

  const finishDetection = (state: DetectState) => {
    setDetect(null);

    let bestAxis = -1;
    let bestDev = 0;
    state.extreme.forEach((value, i) => {
      const dev = Math.abs(value - (state.baseline[i] ?? 0));
      if (dev > bestDev) {
        bestDev = dev;
        bestAxis = i;
      }
    });

    if (bestAxis < 0 || bestDev < DETECT_MIN_DEVIATION) {
      // Refusing to guess is the point: assigning the largest jitter would look
      // like success and put the brake on whatever axis drifted most.
      setDetectResult(
        `No clear movement (largest change ${bestDev} raw, need ${DETECT_MIN_DEVIATION}). ` +
          'Press the control fully while detecting.',
      );
      return;
    }

    const baseline = state.baseline[bestAxis] ?? 0;
    const extreme = state.extreme[bestAxis] ?? 0;
    if (state.fn === 'steer') {
      // Axis + full-right ONLY. The centre is deliberately NOT taken from the
      // value the axis had when detection started.
      //
      // Measured on a real G29 (2026-08-06): with FFB idle the wheel has no
      // centring spring, so it simply stays wherever it was last left -- the
      // recording rests at raw -548 after a sweep, not at 0. Baking that into
      // raw_center would pin the vehicle's straight-ahead to an arbitrary
      // position and give a permanent steering bias with hands off. Pedals do
      // not have this problem (a released pedal returns to a real mechanical
      // rest), which is why only this branch differs.
      //
      // The centre is set by its own button, and on a wheel that self-calibrates
      // at power-up it is simply 0.
      onChange({ ...mapping, steer_axis: bestAxis, steer_raw_full: extreme });
    } else {
      onChange({
        ...mapping,
        [`${state.fn}_axis`]: bestAxis,
        [`${state.fn}_raw_released`]: baseline,
        [`${state.fn}_raw_full`]: extreme,
      });
    }
    setDetectResult(
      state.fn === 'steer'
        ? `steer: axis ${bestAxis}, full ${extreme} (centre unchanged — use “Set centre”)`
        : `${state.fn}: axis ${bestAxis}, released ${baseline} → full ${extreme}`,
    );
  };

  /** Capture the wheel's current position as the straight-ahead reference. */
  const setSteerCentre = () => {
    const raw = rawFor(mapping.steer_axis);
    if (raw === null || !hasReported(mapping.steer_axis)) {
      setDetectResult('No live steering value yet — start the readout first.');
      return;
    }
    setField('steer_raw_center', raw);
    setDetectResult(`steer centre set to ${raw}`);
  };

  // --- rendering helpers --------------------------------------------------

  const axisOptions = [-1, ...Array.from({ length: numAxes }, (_, i) => i)];

  const rawFor = (axis: number): number | null =>
    frame && axis >= 0 && axis < frame.axes.length ? frame.axes[axis] : null;

  const hasReported = (axis: number): boolean =>
    !!frame && axis >= 0 && axis < frame.reported.length && frame.reported[axis];

  const setField = (key: keyof WheelAxisMapping, value: number | boolean) =>
    onChange({ ...mapping, [key]: value });

  /**
   * Does this pedal's calibration have its ends the wrong way round?
   *
   * NOT answerable from the configuration -- `raw_released > raw_full` merely
   * restates which numbers are stored (and is TRUE for a perfectly correct G29,
   * whose released reading is +32767). A label built on that would report
   * "normal" for a pedal wired backwards, i.e. it would be a restatement
   * masquerading as a check.
   *
   * The measurement that DOES decide it: a pedal returns mechanically, so the
   * raw value it sits at while nobody touches it IS the released value --
   * therefore an IDLE pedal must normalize to ~0. If an idle pedal reads
   * pressed, either its ends are swapped or the axis index points at something
   * else. (Steering has no equivalent test: with FFB idle the wheel stays
   * wherever it was left, so its resting value means nothing -- the same
   * asymmetry that made "Set centre" a separate explicit action.)
   *
   * Returns null while the answer is not measurable yet: no live data, the axis
   * has never reported, or the axis is still moving (a pedal held down is not
   * evidence of anything).
   */
  const pedalReadsPressedWhileIdle = (fn: PedalFn): boolean | null => {
    const axis = mapping[`${fn}_axis`];
    if (!frame || axis < 0 || axis >= frame.reported.length || !frame.reported[axis]) return null;
    const recent = history.map((h) => h.axes[axis]).filter((v) => v !== undefined);
    if (recent.length < IDLE_FRAMES) return null;
    const span = Math.max(...recent) - Math.min(...recent);
    if (span > IDLE_RAW_EPS) return null; // still moving -> undecidable
    return frame.norm[fn] > 0.5;
  };

  /**
   * Invert an axis. Uniform across all four, because "which way does this device
   * count" is the same hardware-matching question everywhere -- there is no
   * invert flag anywhere, and this button IS the mechanism.
   *
   * The arithmetic differs only because the two calibrations mean different
   * things: a pedal's ends are exchanged, while steering mirrors its span about
   * the centre so that the centre the user set is preserved.
   */
  const flipAxis = (fn: AxisFn) => {
    if (fn === 'steer') {
      onChange({
        ...mapping,
        steer_raw_full: 2 * mapping.steer_raw_center - mapping.steer_raw_full,
      });
      return;
    }
    onChange({
      ...mapping,
      [`${fn}_raw_released`]: mapping[`${fn}_raw_full`],
      [`${fn}_raw_full`]: mapping[`${fn}_raw_released`],
    });
  };

  const bar = (fn: AxisFn, value: number, axis: number) => {
    // Steering is bipolar, pedals are unipolar; both are drawn on 0..100% with
    // the steering zero at the centre.
    const pct = fn === 'steer' ? (value + 1) * 50 : value * 100;
    const reported = hasReported(axis);
    return (
      <div className="relative h-1.5 w-full rounded bg-glass-1 border border-glass-edge overflow-hidden">
        {reported ? (
          <div
            className="absolute inset-y-0 bg-primary/70"
            style={
              fn === 'steer'
                ? { left: `${Math.min(pct, 50)}%`, width: `${Math.abs(pct - 50)}%` }
                : { left: 0, width: `${Math.max(0, Math.min(100, pct))}%` }
            }
          />
        ) : null}
      </div>
    );
  };

  const axisRow = (fn: AxisFn, label: string, axis: number, normValue: number) => {
    const raw = rawFor(axis);
    const detecting = detect?.fn === fn;
    return (
      <div key={fn} className="space-y-1">
        <div className="flex items-center gap-2">
          <span className="text-xs text-text-secondary w-16 shrink-0">{label}</span>
          <select
            value={axis}
            onChange={(e) => setField(`${fn}_axis` as keyof WheelAxisMapping, Number(e.target.value))}
            className="text-xs font-mono bg-glass-1 border border-glass-edge rounded px-1.5 py-0.5 cursor-pointer"
          >
            {axisOptions.map((i) => (
              <option key={i} value={i}>
                {i < 0 ? '—' : `a${i}`}
              </option>
            ))}
          </select>
          <span className="text-[10px] font-mono text-text-tertiary w-24 shrink-0">
            {raw === null ? '—' : hasReported(axis) ? `${raw} → ${normValue.toFixed(2)}` : 'no report'}
          </span>
          <button
            onClick={() => startDetection(fn)}
            disabled={!frame || (detect !== null && !detecting)}
            className={`text-[10px] px-2 py-0.5 rounded cursor-pointer transition-colors ${
              detecting
                ? 'bg-primary/80 text-background animate-pulse'
                : 'bg-glass-1 border border-glass-edge text-text-tertiary hover:text-foreground hover:bg-glass-hover disabled:opacity-40'
            }`}
          >
            {detecting ? 'Move it...' : 'Detect'}
          </button>
          {/* Flip on EVERY axis, no invert checkbox anywhere: polarity is the
              order of the calibrated pair, and this button is the one way to
              change it. Uniform because "which way does this device count" is
              the same hardware-matching question on the wheel as on a pedal. */}
          <button
            onClick={() => flipAxis(fn)}
            title={
              fn === 'steer'
                ? 'Mirror the steering span about its centre (use when the wheel steers the wrong way)'
                : 'Exchange the released / fully-pressed raw values (use when the pedal works backwards)'
            }
            className="text-[10px] px-2 py-0.5 rounded bg-glass-1 border border-glass-edge text-text-tertiary hover:text-foreground hover:bg-glass-hover cursor-pointer"
          >
            Flip
          </button>
          {/* Reports the MEASUREMENT, not the stored numbers: an idle pedal
              reading pressed is the only evidence that the ends are the wrong way
              round. Silent when undecidable (no data / still moving) rather than
              guessing, and absent for steering, which has no equivalent test (a
              wheel at rest is wherever it was left). */}
          {fn !== 'steer' && pedalReadsPressedWhileIdle(fn) === true ? (
            <span className="text-[10px] text-warning">reads pressed while idle → Flip</span>
          ) : null}
        </div>
        {bar(fn, normValue, axis)}
      </div>
    );
  };

  return (
    <section>
      <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">
        Axis Mapping
      </h3>

      {status && !status.available ? (
        <p className="text-[10px] text-text-tertiary mb-2">{status.message}</p>
      ) : (
        <div className="flex items-center gap-2 mb-2">
          <button
            onClick={() => setProbeEnabled((v) => !v)}
            className="text-[10px] px-2 py-0.5 rounded bg-glass-1 border border-glass-edge text-text-tertiary hover:text-foreground hover:bg-glass-hover cursor-pointer"
          >
            {probeEnabled ? 'Stop live readout' : 'Start live readout'}
          </button>
          <span className="text-[10px] text-text-tertiary">
            {!probeEnabled
              ? 'off'
              : streamStatus === 'streaming'
                ? `${meta?.name ?? 'device'} — ${numAxes} axes`
                : streamStatus}
          </span>
        </div>
      )}

      {error ? <p className="text-[10px] text-warning mb-2">{error}</p> : null}
      {meta?.problems?.length ? (
        <ul className="text-[10px] text-warning mb-2 list-disc pl-4">
          {meta.problems.map((p) => (
            <li key={p}>{p}</li>
          ))}
        </ul>
      ) : null}

      <div className="space-y-2">
        {axisRow('steer', 'Steering', mapping.steer_axis, frame?.norm.steering ?? 0)}
        <div className="flex items-center gap-3 pl-[4.5rem]">
          {/* Separate from Detect on purpose: a wheel with FFB idle has no
              centring spring and rests wherever it was left, so the centre has
              to be an explicit "it is straight ahead right now" statement. */}
          <button
            onClick={setSteerCentre}
            className="text-[10px] px-2 py-0.5 rounded bg-glass-1 border border-glass-edge text-text-tertiary hover:text-foreground hover:bg-glass-hover cursor-pointer"
          >
            Set centre
          </button>
          <span className="text-[10px] text-text-tertiary">
            Flip also reverses the FFB force direction
          </span>
        </div>
        {PEDALS.map(({ fn, label }) =>
          axisRow(fn, label, mapping[`${fn}_axis`], frame?.norm[fn] ?? 0),
        )}
      </div>

      {detect ? (
        <p className="text-[10px] text-text-tertiary mt-2">
          {detect.fn === 'steer'
            ? 'Turn the wheel fully RIGHT'
            : `Press ${detect.fn} as hard as you would in an emergency stop`}{' '}
          — detecting for {Math.max(0, Math.ceil((detect.deadline - Date.now()) / 1000))}s
        </p>
      ) : null}
      {detectResult ? <p className="text-[10px] text-text-tertiary mt-2">{detectResult}</p> : null}

      {frame && frame.reported.some((r) => !r) ? (
        <p className="text-[10px] text-text-tertiary mt-2">
          Axes marked “no report” have sent nothing since the readout started. A wheel can enumerate
          and still report nothing (check that it is powered on); until it reports, its normalized
          value is not meaningful.
        </p>
      ) : null}

      <details className="mt-2">
        <summary className="text-[10px] text-text-tertiary cursor-pointer">Raw calibration</summary>
        <div className="space-y-1 mt-1">
          {(
            [
              ['steer_raw_center', 'Steer centre'],
              ['steer_raw_full', 'Steer full right'],
              ['throttle_raw_released', 'Throttle released'],
              ['throttle_raw_full', 'Throttle full'],
              ['brake_raw_released', 'Brake released'],
              ['brake_raw_full', 'Brake full'],
              ['clutch_raw_released', 'Clutch released'],
              ['clutch_raw_full', 'Clutch full'],
            ] as [keyof WheelAxisMapping, string][]
          ).map(([key, label]) => (
            <div key={key} className="flex items-center gap-2">
              <span className="text-[10px] text-text-secondary w-32 shrink-0">{label}</span>
              <input
                type="number"
                value={String(mapping[key])}
                onChange={(e) => setField(key, Number(e.target.value))}
                className="text-xs font-mono bg-glass-1 border border-glass-edge rounded px-2 py-0.5 w-24"
              />
            </div>
          ))}
        </div>
      </details>
    </section>
  );
}
