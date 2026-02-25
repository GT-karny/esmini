import type { HvdMessage, EgoLights } from './OsiLivePanel';

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
      <span className="text-xs text-gray-500">{label}</span>
      <svg width="32" height={height} className="rounded overflow-hidden">
        <rect width="32" height={height} className="fill-gray-800" />
        <rect
          y={height - fillH}
          width="32"
          height={fillH}
          className={fillClass}
        />
      </svg>
      <span className="text-xs font-mono text-gray-300">
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
      <span className="text-xs text-gray-500">Steering</span>
      <svg width="64" height="64" viewBox="-32 -32 64 64">
        <circle r="28" className="fill-none stroke-gray-700" strokeWidth="3" />
        <line
          x1="0" y1="0" x2="0" y2="-24"
          className="stroke-blue-400"
          strokeWidth="3"
          strokeLinecap="round"
          transform={`rotate(${clampedDeg})`}
        />
        <circle r="3" className="fill-gray-500" />
      </svg>
      <span className="text-xs font-mono text-gray-300">
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
      <span className="text-xs text-gray-500">Speed</span>
      <div className="text-2xl font-mono font-bold text-white leading-none">
        {kmh.toFixed(1)}
      </div>
      <div className="text-[10px] text-gray-600">km/h</div>
      <div className="w-16 h-1.5 bg-gray-800 rounded-full overflow-hidden">
        <div
          className="h-full bg-cyan-500 rounded-full transition-all duration-150"
          style={{ width: `${barRatio * 100}%` }}
        />
      </div>
      <span className="text-[10px] font-mono text-gray-500">
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
      <span className="text-xs text-gray-500">{label}</span>
      <svg width="72" height="52" viewBox="-36 -36 72 36">
        <path d={bgArc} className="fill-none stroke-gray-700" strokeWidth="4" strokeLinecap="round" />
        <path d={fillArc} className={`fill-none ${arcColor}`} strokeWidth="4" strokeLinecap="round" />
        <line x1="0" y1="0" x2={nx} y2={ny} className={arcColor} strokeWidth="2" strokeLinecap="round" />
        <circle r="2.5" className="fill-gray-500" />
      </svg>
      <span className="text-xs font-mono text-gray-300">
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
      <span className="text-xs text-gray-500">Gear</span>
      <div className="w-10 h-10 flex items-center justify-center bg-gray-800 border border-gray-700 rounded-lg">
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
      <span className={`inline-block w-2.5 h-2.5 rounded-full ${active ? activeClass : 'bg-gray-700'}`} />
      <span className={`text-xs ${active ? 'text-gray-300' : 'text-gray-600'}`}>{label}</span>
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

/* ---------- Main panel ---------- */

interface HvdGaugePanelProps {
  hvd: HvdMessage | null;
  lights: EgoLights | null;
}

export function HvdGaugePanel({ hvd, lights }: HvdGaugePanelProps) {
  return (
    <div className="mt-4 pt-4 border-t border-gray-800">
      <div className="flex items-center justify-between mb-3">
        <h3 className="text-sm font-medium text-gray-400">Vehicle Telemetry</h3>
        {hvd && (
          <span className="text-xs text-gray-500">t = {hvd.sim_time.toFixed(2)}s</span>
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
          <span className="text-xs text-gray-500 mr-1">Lights</span>
          <LightIndicators lights={lights} />
        </div>
      )}
    </div>
  );
}
