import type { HvdMessage, EgoLights } from './OsiLivePanel';
import type { AdasFunction } from '../hooks/useOsiStream';

/* ---------- SVG arc helpers ---------- */

function polarToCartesian(cx: number, cy: number, r: number, angleDeg: number) {
  const rad = ((angleDeg - 90) * Math.PI) / 180;
  return { x: cx + r * Math.cos(rad), y: cy + r * Math.sin(rad) };
}

function describeArc(cx: number, cy: number, r: number, startAngle: number, endAngle: number): string {
  const start = polarToCartesian(cx, cy, r, endAngle);
  const end = polarToCartesian(cx, cy, r, startAngle);
  const largeArc = endAngle - startAngle > 180 ? 1 : 0;
  return `M ${start.x} ${start.y} A ${r} ${r} 0 ${largeArc} 0 ${end.x} ${end.y}`;
}

/* ---------- VerticalBarGauge ---------- */

function VerticalBarGauge({
  label,
  value,
  fillClass,
  height = 80,
}: {
  label: string;
  value: number;
  fillClass: string;
  height?: number;
}) {
  const clamped = Math.max(0, Math.min(1, value));
  const fillH = clamped * height;
  return (
    <div className="flex flex-col items-center gap-1">
      <span className="text-xs text-text-secondary">{label}</span>
      <svg width="32" height={height} className="rounded overflow-hidden">
        <rect width="32" height={height} className="fill-glass-1" />
        <rect
          y={height - fillH}
          width="32"
          height={fillH}
          className={fillClass}
        />
      </svg>
      <span className="text-xs font-mono text-foreground">
        {(clamped * 100).toFixed(0)}%
      </span>
    </div>
  );
}

/* ---------- SteeringIndicator ---------- */

function SteeringIndicator({ angle }: { angle: number }) {
  // angle in radians; positive = left per OSI convention
  const deg = -(angle * 180) / Math.PI;
  const clampedDeg = Math.max(-540, Math.min(540, deg));
  return (
    <div className="flex flex-col items-center gap-1">
      <span className="text-xs text-text-secondary">Steering</span>
      <svg width="64" height="64" viewBox="-32 -32 64 64">
        <circle r="28" className="fill-none stroke-glass-edge" strokeWidth="3" />
        <line
          x1="0" y1="0" x2="0" y2="-24"
          className="stroke-blue-400"
          strokeWidth="3"
          strokeLinecap="round"
          transform={`rotate(${clampedDeg})`}
        />
        <circle r="3" className="fill-text-secondary" />
      </svg>
      <span className="text-xs font-mono text-foreground">
        {(angle * 180 / Math.PI).toFixed(1)}&deg;
      </span>
    </div>
  );
}

/* ---------- SpeedDisplay ---------- */

function SpeedDisplay({ speed }: { speed: number }) {
  const kmh = speed * 3.6;
  const barRatio = Math.min(1, kmh / 200);
  return (
    <div className="flex flex-col items-center gap-1">
      <span className="text-xs text-text-secondary">Speed</span>
      <div className="text-2xl font-mono font-bold text-white leading-none">
        {kmh.toFixed(1)}
      </div>
      <div className="text-[10px] text-text-tertiary">km/h</div>
      <div className="w-16 h-1.5 bg-glass-1 overflow-hidden">
        <div
          className="h-full bg-cyan-500 rounded-full transition-all duration-150"
          style={{ width: `${barRatio * 100}%` }}
        />
      </div>
      <span className="text-[10px] font-mono text-text-secondary">
        {speed.toFixed(1)} m/s
      </span>
    </div>
  );
}

/* ---------- ArcGauge (RPM) ---------- */

function ArcGauge({
  label,
  value,
  min = 0,
  max = 8000,
  unit = '',
  warningThreshold,
}: {
  label: string;
  value: number;
  min?: number;
  max?: number;
  unit?: string;
  warningThreshold?: number;
}) {
  const ratio = Math.max(0, Math.min(1, (value - min) / (max - min)));
  const startAngle = -135;
  const endAngle = 135;
  const sweepAngle = startAngle + ratio * (endAngle - startAngle);
  const r = 28;

  const bgArc = describeArc(0, 0, r, startAngle, endAngle);
  const fillArc = describeArc(0, 0, r, startAngle, sweepAngle);

  const toRad = (d: number) => ((d - 90) * Math.PI) / 180;
  const nx = r * 0.85 * Math.cos(toRad(sweepAngle));
  const ny = r * 0.85 * Math.sin(toRad(sweepAngle));

  const isWarning = warningThreshold !== undefined && value >= warningThreshold;
  const arcColor = isWarning ? 'stroke-red-500' : 'stroke-orange-400';

  return (
    <div className="flex flex-col items-center gap-1">
      <span className="text-xs text-text-secondary">{label}</span>
      <svg width="72" height="52" viewBox="-36 -36 72 36">
        <path d={bgArc} className="fill-none stroke-glass-edge" strokeWidth="4" strokeLinecap="round" />
        <path d={fillArc} className={`fill-none ${arcColor}`} strokeWidth="4" strokeLinecap="round" />
        <line x1="0" y1="0" x2={nx} y2={ny} className={arcColor} strokeWidth="2" strokeLinecap="round" />
        <circle r="2.5" className="fill-text-secondary" />
      </svg>
      <span className="text-xs font-mono text-foreground">
        {value.toFixed(0)} {unit}
      </span>
    </div>
  );
}

/* ---------- GearIndicator ---------- */

function GearIndicator({ gear }: { gear: number }) {
  const display = gear < 0 ? 'R' : gear === 0 ? 'N' : String(gear);
  return (
    <div className="flex flex-col items-center gap-1">
      <span className="text-xs text-text-secondary">Gear</span>
      <div className="w-10 h-10 flex items-center justify-center bg-glass-1 border border-glass-edge">
        <span className="text-xl font-bold font-mono text-white">{display}</span>
      </div>
    </div>
  );
}

/* ---------- LightIndicators ---------- */

function LightDot({
  label,
  active,
  activeClass,
}: {
  label: string;
  active: boolean;
  activeClass: string;
}) {
  return (
    <div className="flex items-center gap-1.5">
      <span className={`inline-block w-2.5 h-2.5 rounded-full ${active ? activeClass : 'bg-glass-edge'}`} />
      <span className={`text-xs ${active ? 'text-foreground' : 'text-text-tertiary'}`}>{label}</span>
    </div>
  );
}

function LightIndicators({ lights }: { lights: EgoLights }) {
  const headOn = lights.head_light === 'on';
  const leftOn = lights.indicator === 'left' || lights.indicator === 'warning';
  const rightOn = lights.indicator === 'right' || lights.indicator === 'warning';
  const brakeOn = lights.brake_light !== 'off';
  const brakeStrong = lights.brake_light === 'strong';

  return (
    <div className="flex items-center gap-4 flex-wrap">
      <LightDot label="Head" active={headOn} activeClass="bg-yellow-400" />
      <LightDot label="L Turn" active={leftOn} activeClass="bg-amber-500" />
      <LightDot label="R Turn" active={rightOn} activeClass="bg-amber-500" />
      <LightDot label="Brake" active={brakeOn} activeClass={brakeStrong ? 'bg-red-400' : 'bg-red-600'} />
    </div>
  );
}

/* ---------- Driver assistance (req-vd-ad:REQ-AD-029) ---------- */

/* Short driver-facing labels for the rows the ManualDrive ADAS stack reports
 * (design manualdrive_adas_design.md sec8-2). Anything else falls back to its
 * own custom_name, so a controller that reports rows we do not know about
 * still shows up honestly instead of vanishing. */
const ADAS_LABELS: Record<string, string> = {
  'gt.aeb': 'AEB',
  'gt.fcw': 'FCW',
  'gt.acc': 'ACC',
  'gt.lka': 'LKA',
  'gt.ldw': 'LDW',
  'gt.msl': 'Limiter',
};

function adasLabel(fn: AdasFunction): string {
  return ADAS_LABELS[fn.key] ?? fn.key.replace(/^gt\./, '') ?? '?';
}

/* State -> dot colour. The three-value discipline (design sec8-2) has to stay
 * READABLE here, not just present in the data: "off / not owned"
 * (unavailable), "watching, has not fired" (standby) and "intervening"
 * (active) are three different things to a driver. Only ACTIVE gets the accent
 * colour, so a lit dot always means "this function is doing something to the
 * car right now". */
const ADAS_STATE_STYLE: Record<string, { dot: string; text: string }> = {
  active: { dot: 'bg-cyan-400', text: 'text-foreground' },
  standby: { dot: 'bg-glass-edge-active', text: 'text-text-secondary' },
  available: { dot: 'bg-glass-edge-active', text: 'text-text-secondary' },
  unavailable: { dot: 'bg-glass-edge', text: 'text-text-tertiary' },
  errored: { dot: 'bg-destructive', text: 'text-foreground' },
};

/** Numeric gt.* detail value, or null when absent/unparseable. */
function detailNum(fn: AdasFunction, key: string): number | null {
  const raw = fn.detail[key];
  if (raw === undefined) return null;
  const v = Number(raw);
  return Number.isFinite(v) ? v : null;
}

function detailFlag(fn: AdasFunction, key: string): boolean {
  return fn.detail[key] === 'true';
}

/** The settings a driver acts on, in the units a driver reads them in.
 *
 * Deliberately NOT every gt.* key: the stack emits ~40 of them and they are
 * diagnostics, not dashboard content. Face 3 keeps all of them (they are in
 * the telemetry either way); this line carries only what REQ-AD-026 steps e/h
 * and REQ-AD-030 make the driver responsible for -- if the set speed, the gap
 * setting and the limiter cap are not visible, changing them while driving is
 * not a usable feature.
 */
function adasSettings(fn: AdasFunction): string | null {
  const parts: string[] = [];
  const set = detailNum(fn, 'gt.acc.set_speed_mps');
  const cap = detailNum(fn, 'gt.acc.effective_cap_mps');
  const thw = detailNum(fn, 'gt.acc.thw_setting_s');
  const msl = detailNum(fn, 'gt.msl.cap_mps');
  if (set !== null) parts.push(`set ${(set * 3.6).toFixed(0)} km/h`);
  // Only worth the pixels when the road's limit actually pulled the set speed
  // down -- otherwise it repeats the number to its left.
  if (cap !== null && set !== null && Math.abs(cap - set) > 0.05) {
    parts.push(`capped ${(cap * 3.6).toFixed(0)}`);
  }
  if (thw !== null) parts.push(`gap ${thw.toFixed(1)} s`);
  if (msl !== null) parts.push(`max ${(msl * 3.6).toFixed(0)} km/h`);
  return parts.length > 0 ? parts.join(' · ') : null;
}

/** Active warnings, named after the WARNING rather than the row carrying it.
 *
 * design sec8-4 puts the flags on the intervening function's row: FCW (forward
 * collision) rides on gt.aeb.warning and LDW (lane departure) on
 * gt.lka.warning. Labelling the banner "AEB" or "LKA" would tell the driver
 * which module raised it instead of what is wrong -- the opposite of what a
 * warning is for. */
function adasWarnings(functions: AdasFunction[]): string[] {
  const out: string[] = [];
  for (const fn of functions) {
    if (detailFlag(fn, 'gt.aeb.warning')) out.push('Forward collision');
    if (detailFlag(fn, 'gt.lka.warning')) out.push('Lane departure');
  }
  return out;
}

function AdasFunctionRow({ fn }: { fn: AdasFunction }) {
  const style = ADAS_STATE_STYLE[fn.state_name] ?? ADAS_STATE_STYLE.unavailable;
  const settings = adasSettings(fn);
  // The driver overrode this function (brake / steering per OSI's Reason enum,
  // accelerator via custom_state -- the enum has no value for it, design
  // sec8-3). Shown because "the function is not acting" and "you are holding it
  // off" are different situations for the person holding the wheel.
  const overridden =
    fn.driver_override.active || fn.custom_state.startsWith('DRIVER_OVERRIDE');

  return (
    <div className="flex items-center gap-2 min-w-0">
      <span className={`inline-block w-2.5 h-2.5 rounded-full shrink-0 ${style.dot}`} />
      <span className={`text-xs font-medium shrink-0 ${style.text}`}>{adasLabel(fn)}</span>
      {settings && (
        <span className="text-[10px] font-mono text-text-secondary truncate">{settings}</span>
      )}
      {overridden && (
        <span className="text-[10px] text-warning shrink-0">override</span>
      )}
    </div>
  );
}

function AdasFunctions({ functions }: { functions: AdasFunction[] }) {
  if (functions.length === 0) return null;

  const warnings = adasWarnings(functions);

  return (
    <div className="mt-4">
      <div className="text-xs text-text-secondary">Driver Assistance</div>

      {/* Warnings first and on their own line. A driver should not have to
          decode which chip is lit to find out that something is warning them,
          which is the whole point of REQ-AD-027 step f / REQ-AD-025 step e. */}
      {warnings.length > 0 && (
        <div className="mt-2 inline-flex items-center gap-2 px-3 py-1.5 border border-warning/40 bg-warning/10">
          <span className="inline-block w-2 h-2 rounded-full bg-warning shrink-0" />
          <span className="text-xs font-medium text-warning">{warnings.join(' · ')}</span>
        </div>
      )}

      {/* Packed left with wrapping rather than an even grid: the settings text
          varies in width, so equal columns leave the row looking half-empty.
          Same flow as the Lights row directly above, which keeps the two
          status groups reading as siblings. */}
      <div className="mt-2 flex flex-wrap items-center gap-x-6 gap-y-2">
        {functions.map((fn) => (
          <AdasFunctionRow key={fn.key} fn={fn} />
        ))}
      </div>
    </div>
  );
}

/* ---------- Main panel ---------- */

interface HvdGaugePanelProps {
  hvd: HvdMessage | null;
  lights: EgoLights | null;
}

export function HvdGaugePanel({ hvd, lights }: HvdGaugePanelProps) {
  return (
    <div className="mt-4 pt-4 border-t border-glass-edge">
      <div className="flex items-center justify-between mb-3">
        <h3 className="text-sm font-medium text-text-secondary">Vehicle Telemetry</h3>
        {hvd && (
          <span className="text-xs text-text-secondary">t = {hvd.sim_time.toFixed(2)}s</span>
        )}
      </div>

      {hvd && (
        <div className="grid grid-cols-3 sm:grid-cols-6 gap-4 justify-items-center mb-4">
          <VerticalBarGauge label="Throttle" value={hvd.throttle} fillClass="fill-green-500" />
          <VerticalBarGauge label="Brake" value={hvd.brake} fillClass="fill-red-500" />
          <SteeringIndicator angle={hvd.steering_angle} />
          <SpeedDisplay speed={hvd.speed} />
          <ArcGauge label="RPM" value={hvd.rpm} min={0} max={8000} unit="rpm" warningThreshold={6500} />
          <GearIndicator gear={hvd.gear} />
        </div>
      )}

      {lights && (
        <div className="flex items-center gap-2">
          <span className="text-xs text-text-secondary mr-1">Lights</span>
          <LightIndicators lights={lights} />
        </div>
      )}

      {hvd && <AdasFunctions functions={hvd.adas_functions ?? []} />}
    </div>
  );
}
