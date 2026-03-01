import { useQuery } from '@tanstack/react-query';
import { api } from '../api/client';
import { Button } from './ui/Button';

const statusColors: Record<string, string> = {
  queued: 'bg-yellow-500/20 text-yellow-400',
  running: 'bg-primary/20 text-primary',
  completed: 'bg-green-500/20 text-green-400',
  failed: 'bg-destructive/20 text-destructive',
  cancelled: 'bg-glass-1 text-text-secondary',
  timeout: 'bg-orange-500/20 text-orange-400',
};

export interface InlineSimulationStatusProps {
  jobId: string;
  onViewDetails: () => void;
  onRerun?: () => void;
}

export function InlineSimulationStatus({
  jobId,
  onViewDetails,
  onRerun,
}: InlineSimulationStatusProps) {
  const { data: sim } = useQuery({
    queryKey: ['simulation', jobId],
    queryFn: () => api.getSimulation(jobId),
    refetchInterval: (query) => {
      const status = query.state.data?.status;
      return status === 'running' || status === 'queued' ? 1000 : false;
    },
  });

  if (!sim) {
    return (
      <div className="bg-glass-1 border border-glass-edge p-3 animate-pulse">
        <div className="h-4 bg-glass-hover w-32" />
      </div>
    );
  }

  const isActive = sim.status === 'running' || sim.status === 'queued';
  const isDone = sim.status === 'completed';
  const isFailed = sim.status === 'failed' || sim.status === 'timeout';

  const simTime = (sim as unknown as Record<string, unknown>).sim_time as number | undefined;

  return (
    <div className="bg-glass-1 border border-glass-edge p-3 space-y-2">
      {/* Status row */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className={`inline-block px-2 py-0.5 text-xs font-medium ${statusColors[sim.status] ?? ''}`}>
            {sim.status}
          </span>
          <span className="text-xs text-text-tertiary font-mono">{sim.job_id}</span>
        </div>

        <div className="flex items-center gap-2">
          {(isDone || isFailed) && (
            <Button variant="ghost" size="sm" onClick={onViewDetails}>
              View Details &rarr;
            </Button>
          )}
          {isDone && onRerun && (
            <Button variant="ghost" size="sm" onClick={onRerun}>
              Re-run
            </Button>
          )}
        </div>
      </div>

      {/* Progress bar (running/queued) */}
      {isActive && (
        <div className="space-y-1">
          <div className="w-full h-1.5 bg-glass-1 overflow-hidden">
            <div
              className="h-full bg-primary transition-all duration-500"
              style={{ width: `${Math.min(sim.progress_pct, 100)}%` }}
            />
          </div>
          <div className="flex items-center justify-between text-xs text-text-tertiary">
            <span>{sim.progress_pct}%</span>
            {simTime !== undefined && <span>{simTime.toFixed(1)}s sim time</span>}
          </div>
        </div>
      )}

      {/* Error excerpt (failed) */}
      {isFailed && sim.error_message && (
        <p className="text-xs text-destructive truncate" title={sim.error_message}>
          {sim.error_message.length > 120
            ? sim.error_message.slice(0, 120) + '...'
            : sim.error_message}
        </p>
      )}
    </div>
  );
}
