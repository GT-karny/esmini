import { useOsiStream } from '../../hooks/useOsiStream';
import { LiveSceneView } from '../LiveSceneView';

interface LiveMonitorPanelProps {
  jobId: string;
}

const statusIndicator: Record<string, { color: string; label: string }> = {
  connecting: { color: 'bg-warning', label: 'Connecting' },
  connected: { color: 'bg-success', label: 'Live' },
  disconnected: { color: 'bg-text-tertiary', label: 'Disconnected' },
  error: { color: 'bg-destructive', label: 'Error' },
};

export function LiveMonitorPanel({ jobId }: LiveMonitorPanelProps) {
  const { status, objects, simTime, hvdData, frameCount } = useOsiStream(jobId);

  const { color, label } = statusIndicator[status] ?? statusIndicator.connecting;

  const speedKmh = hvdData ? hvdData.speed * 3.6 : null;
  const throttlePct = hvdData ? (hvdData.throttle * 100) : null;
  const brakePct = hvdData ? (hvdData.brake * 100) : null;
  const steeringDeg = hvdData ? (hvdData.steering_angle * 180 / Math.PI) : null;

  return (
    <div className="h-full overflow-y-auto p-3 flex flex-col">
      {/* Status bar */}
      <div className="flex items-center justify-between mb-2">
        <div className="flex items-center gap-2 text-xs">
          <span className={`inline-block w-2 h-2 rounded-full ${color}`} />
          <span className="text-text-secondary">{label}</span>
          {frameCount > 0 && (
            <span className="text-text-tertiary">
              {frameCount} frames
            </span>
          )}
        </div>
        <span className="text-xs font-mono text-text-secondary">
          t = {simTime.toFixed(1)}s
        </span>
      </div>

      {/* Progress bar placeholder */}
      <div className="w-full h-1 bg-glass-1 mb-3 overflow-hidden">
        <div
          className="h-full bg-primary animate-pulse"
          style={{ width: status === 'connected' ? '100%' : '0%' }}
        />
      </div>

      {/* 2D scene view */}
      <div className="flex-1 min-h-0 mb-3">
        <LiveSceneView objects={objects} className="h-full" />
      </div>

      {/* HVD compact display */}
      {hvdData && (
        <div className="grid grid-cols-4 gap-2">
          <HvdMiniBar label="Speed" value={`${speedKmh!.toFixed(0)}`} unit="km/h" ratio={Math.min(1, speedKmh! / 200)} color="bg-cyan-500" />
          <HvdMiniBar label="Throttle" value={`${throttlePct!.toFixed(0)}`} unit="%" ratio={hvdData.throttle} color="bg-green-500" />
          <HvdMiniBar label="Brake" value={`${brakePct!.toFixed(0)}`} unit="%" ratio={hvdData.brake} color="bg-red-500" />
          <HvdMiniBar label="Steering" value={`${steeringDeg!.toFixed(1)}`} unit="deg" ratio={Math.min(1, Math.abs(steeringDeg!) / 45)} color="bg-blue-400" />
        </div>
      )}
    </div>
  );
}

function HvdMiniBar({ label, value, unit, ratio, color }: {
  label: string;
  value: string;
  unit: string;
  ratio: number;
  color: string;
}) {
  const clamped = Math.max(0, Math.min(1, ratio));
  return (
    <div className="text-center">
      <div className="text-[10px] text-text-tertiary mb-0.5">{label}</div>
      <div className="text-xs font-mono text-foreground">{value} <span className="text-text-tertiary">{unit}</span></div>
      <div className="w-full h-1 bg-glass-1 mt-1 overflow-hidden">
        <div className={`h-full ${color} transition-all duration-150`} style={{ width: `${clamped * 100}%` }} />
      </div>
    </div>
  );
}
