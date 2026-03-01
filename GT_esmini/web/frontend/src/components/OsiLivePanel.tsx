import { useOsiStream, type HvdData } from '../hooks/useOsiStream';
import { HvdGaugePanel } from './HvdGaugePanel';

// Re-export for HvdGaugePanel compatibility
export type HvdMessage = HvdData & { type: 'host_vehicle_data' };

export interface EgoLights {
  head_light: string;
  indicator: string;
  brake_light: string;
}

export function OsiLivePanel({ jobId }: { jobId: string }) {
  const { status, objects, simTime, hvdData, frameCount } = useOsiStream(jobId);

  const statusIndicator: Record<string, { color: string; label: string }> = {
    connecting: { color: 'bg-warning', label: 'Connecting' },
    connected: { color: 'bg-success', label: 'Connected' },
    disconnected: { color: 'bg-text-tertiary', label: 'Disconnected' },
    error: { color: 'bg-destructive', label: 'Error' },
  };

  const { color, label } = statusIndicator[status] ?? statusIndicator.connecting;

  // Extract ego vehicle (first object) light state for the gauge panel
  const egoLights: EgoLights | null = objects.length > 0
    ? {
        head_light: objects[0].head_light ?? 'off',
        indicator: objects[0].indicator ?? 'off',
        brake_light: objects[0].brake_light ?? 'off',
      }
    : null;

  // Map HvdData to HvdMessage shape expected by HvdGaugePanel
  const hvdMessage: HvdMessage | null = hvdData
    ? { ...hvdData, type: 'host_vehicle_data' as const }
    : null;

  return (
    <section className="glass p-4 mb-4">
      <div className="flex items-center justify-between mb-3">
        <h2 className="text-sm font-medium text-text-secondary">Live OSI Data</h2>
        <div className="flex items-center gap-2 text-xs">
          <span className={`inline-block w-2 h-2 rounded-full ${color}`} />
          <span className="text-text-secondary">{label}</span>
          {frameCount > 0 && (
            <span className="text-text-tertiary ml-2">
              {frameCount} frames
            </span>
          )}
        </div>
      </div>

      {objects.length > 0 ? (
        <>
          <div className="text-xs text-text-secondary mb-2">
            t = {simTime.toFixed(2)}s | {objects.length} object(s)
          </div>
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="text-left text-text-secondary text-xs border-b border-glass-edge">
                  <th className="py-1.5 pr-4">ID</th>
                  <th className="py-1.5 pr-4">X</th>
                  <th className="py-1.5 pr-4">Y</th>
                  <th className="py-1.5 pr-4">Speed (m/s)</th>
                  <th className="py-1.5">Heading (rad)</th>
                </tr>
              </thead>
              <tbody className="font-mono">
                {objects.map((obj) => (
                  <tr key={obj.id} className="border-b border-glass-edge/50">
                    <td className="py-1.5 pr-4 text-foreground">{obj.id}</td>
                    <td className="py-1.5 pr-4">{obj.x.toFixed(2)}</td>
                    <td className="py-1.5 pr-4">{obj.y.toFixed(2)}</td>
                    <td className="py-1.5 pr-4">{obj.speed.toFixed(2)}</td>
                    <td className="py-1.5">{obj.h.toFixed(3)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </>
      ) : status === 'connected' ? (
        <p className="text-text-secondary text-sm">Waiting for OSI data...</p>
      ) : status === 'error' ? (
        <p className="text-destructive text-sm">Failed to connect to OSI stream</p>
      ) : null}

      {(hvdMessage || egoLights) && (
        <HvdGaugePanel hvd={hvdMessage} lights={egoLights} />
      )}
    </section>
  );
}
