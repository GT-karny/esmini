import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { useParams, Link, useNavigate } from 'react-router-dom';
import { api, type ResultFile } from '../api/client';
import { OsiLivePanel } from '../components/OsiLivePanel';
import { LiveVdPanel } from '../components/verification/LiveVdPanel';
import { Button } from '../components/ui/Button';
import { Card } from '../components/ui/Card';
import { StatusBadge } from '../components/ui/Badge';
import { ErrorPanel } from '../components/ui/ErrorPanel';

/* ── Metric label & unit mapping ── */

const metricLabels: Record<string, { label: string; unit: string }> = {
  xy_rmse: { label: 'XY RMSE', unit: 'm' },
  endpoint_distance: { label: 'Endpoint Distance', unit: 'm' },
  xy_correlation: { label: 'XY Correlation', unit: '' },
  speed_rmse: { label: 'Speed RMSE', unit: 'm/s' },
  speed_end_delta: { label: 'Speed End Delta', unit: 'm/s' },
  acceleration_correlation: { label: 'Accel Correlation', unit: '' },
  t_offset_rmse: { label: 'Lateral Offset RMSE', unit: 'm' },
  lane_id_match_ratio: { label: 'Lane ID Match', unit: '' },
  s_end_delta: { label: 'S End Delta', unit: 'm' },
  road_id_match_ratio: { label: 'Road ID Match', unit: '' },
  total_time: { label: 'Total Time', unit: 's' },
  total_distance: { label: 'Total Distance', unit: 'm' },
  avg_speed: { label: 'Avg Speed', unit: 'm/s' },
  max_speed: { label: 'Max Speed', unit: 'm/s' },
};

function formatMetricKey(key: string): string {
  const known = metricLabels[key];
  if (known) return known.label;
  return key.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
}

function formatMetricUnit(key: string): string {
  return metricLabels[key]?.unit ?? '';
}

export function SimulationDetailPage() {
  const { jobId } = useParams<{ jobId: string }>();
  const navigate = useNavigate();
  const queryClient = useQueryClient();

  const { data: sim, isLoading: simLoading, error: simError, refetch: simRefetch } = useQuery({
    queryKey: ['simulation', jobId],
    queryFn: () => api.getSimulation(jobId!),
    refetchInterval: (query) => {
      const status = query.state.data?.status;
      return status === 'running' || status === 'queued' ? 1000 : false;
    },
  });

  const { data: resultMeta } = useQuery({
    queryKey: ['result-meta', jobId],
    queryFn: () => api.getResultMeta(jobId!),
    enabled: sim?.status === 'completed' || sim?.status === 'failed' || sim?.status === 'timeout',
  });

  const { data: metrics } = useQuery({
    queryKey: ['metrics', jobId],
    queryFn: () => api.getMetrics(jobId!),
    enabled: sim?.status === 'completed',
  });

  const cancelMutation = useMutation({
    mutationFn: () => api.cancelSimulation(jobId!),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['simulation', jobId] });
    },
  });

  // Loading state
  if (simLoading) {
    return (
      <div className="max-w-3xl space-y-4">
        <div className="h-8 w-48 bg-glass-1 animate-pulse" />
        <div className="glass p-4 space-y-3">
          {[...Array(4)].map((_, i) => (
            <div key={i} className="h-4 bg-glass-1 animate-pulse" style={{ width: `${60 + i * 10}%` }} />
          ))}
        </div>
      </div>
    );
  }

  // Error state
  if (simError) return <ErrorPanel error={simError} onRetry={() => simRefetch()} />;
  if (!sim) return <ErrorPanel error="Job not found." />;

  const isActive = sim.status === 'running' || sim.status === 'queued';
  const isTerminal = !isActive;

  return (
    <div className="max-w-3xl">
      {/* Header */}
      <div className="flex items-center gap-3 mb-6">
        <Link to="/simulations" className="text-text-secondary hover:text-foreground transition-colors">
          &larr;
        </Link>
        <h1 className="text-2xl font-display font-bold tracking-wide">Job {sim.job_id}</h1>
        <StatusBadge status={sim.status} bordered />

        {/* Progress bar (running only) */}
        {sim.status === 'running' && sim.progress_pct > 0 && (
          <div className="flex items-center gap-2 ml-2">
            <div className="w-24 h-1.5 bg-glass-1 overflow-hidden">
              <div
                className="h-full bg-primary transition-all duration-300"
                style={{ width: `${Math.min(100, sim.progress_pct)}%` }}
              />
            </div>
            <span className="text-xs text-text-secondary">{sim.progress_pct.toFixed(0)}%</span>
          </div>
        )}
      </div>

      {/* Job Info */}
      <Card title="Details" className="mb-4">
        <dl className="grid grid-cols-[140px_1fr] gap-x-4 gap-y-2 text-sm">
          <dt className="text-text-secondary">Project</dt>
          <dd>
            {sim.project_id ? (
              <button
                className="text-primary hover:underline cursor-pointer"
                onClick={() => navigate(`/projects/${sim.project_id}`)}
              >
                {sim.project_id}
              </button>
            ) : (
              <span className="text-text-tertiary">-</span>
            )}
          </dd>
          <dt className="text-text-secondary">Scenario</dt>
          <dd>{sim.scenario_id}</dd>
          <dt className="text-text-secondary">Controller</dt>
          <dd>{sim.controller_type}</dd>
          <dt className="text-text-secondary">Started</dt>
          <dd>{sim.started_at ? new Date(sim.started_at).toLocaleString() : '-'}</dd>
          <dt className="text-text-secondary">Completed</dt>
          <dd>{sim.completed_at ? new Date(sim.completed_at).toLocaleString() : '-'}</dd>
          <dt className="text-text-secondary">Exit Code</dt>
          <dd>{sim.exit_code ?? '-'}</dd>
          <dt className="text-text-secondary">PID</dt>
          <dd>{sim.pid ?? '-'}</dd>
        </dl>

        {sim.error_message && (
          <div className="mt-3 p-3 bg-destructive/10 border border-destructive/20 text-sm text-destructive font-mono whitespace-pre-wrap max-h-48 overflow-auto">
            {sim.error_message}
          </div>
        )}

        <div className="flex gap-2 mt-3">
          {sim.status === 'running' && (
            <Button
              variant="danger"
              size="sm"
              onClick={() => cancelMutation.mutate()}
              disabled={cancelMutation.isPending}
            >
              {cancelMutation.isPending ? 'Stopping...' : 'Cancel'}
            </Button>
          )}
          {isTerminal && sim.project_id && (
            <Button
              size="sm"
              onClick={() => navigate(`/projects/${sim.project_id}?scenario=${encodeURIComponent(sim.scenario_id)}`)}
            >
              Re-run
            </Button>
          )}
        </div>
      </Card>

      {/* Live OSI Data (shown while running) */}
      {sim.status === 'running' && jobId && <OsiLivePanel jobId={jobId} />}

      {/* Live VirtualDriver telemetry (shown while running a VirtualDriver run) */}
      {sim.status === 'running' && jobId && sim.controller_type === 'virtual_driver' && (
        <Card className="mt-4">
          <div className="flex items-center justify-between mb-2">
            <h2 className="text-sm font-display text-foreground">VirtualDriver Telemetry</h2>
            <button
              onClick={() => {
                const q = new URLSearchParams({ override: '1' });
                if (sim.project_id) q.set('project', sim.project_id);
                window.open(`/live/vd/${jobId}?${q.toString()}`, `vd-${jobId}`, 'width=960,height=720');
              }}
              className="px-2 py-1 rounded text-xs border border-glass-edge text-text-secondary hover:bg-glass-2"
              title="Open the telemetry view in a separate window"
            >
              Open in window ↗
            </button>
          </div>
          <div className="h-[26rem]">
            <LiveVdPanel jobId={jobId} projectId={sim.project_id ?? undefined} showOverride />
          </div>
        </Card>
      )}

      {/* Metrics */}
      {metrics && !('error' in metrics) && (() => {
        const summary = metrics.summary as Record<string, number> | undefined;
        const finalState = metrics.final_state as Record<string, unknown> | undefined;
        return (
          <Card title="Metrics" className="mb-4">
            {summary && (
              <div className="grid grid-cols-3 gap-3">
                {Object.entries(summary).map(([key, val]) => {
                  const unit = formatMetricUnit(key);
                  return (
                    <div key={key} className="bg-glass-1/50 p-3">
                      <div className="text-xs text-text-secondary mb-1">{formatMetricKey(key)}</div>
                      <div className="text-lg font-mono">
                        {typeof val === 'number' ? val.toFixed(2) : String(val)}
                        {unit && <span className="text-xs text-text-secondary ml-1">{unit}</span>}
                      </div>
                    </div>
                  );
                })}
              </div>
            )}
            {finalState && (
              <div className="mt-4">
                <h3 className="text-xs text-text-secondary mb-2">Final State</h3>
                <div className="grid grid-cols-4 gap-3">
                  {Object.entries(finalState).map(([key, val]) => (
                    <div key={key} className="text-sm">
                      <span className="text-text-secondary">{formatMetricKey(key)}: </span>
                      <span className="font-mono">
                        {typeof val === 'number' ? val.toFixed(2) : String(val ?? '-')}
                      </span>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </Card>
        );
      })()}

      {/* Files */}
      {resultMeta && resultMeta.files.length > 0 && (
        <Card title="Output Files">
          <div className="space-y-1">
            {resultMeta.files.map((f: ResultFile) => (
              <div key={f.name} className="flex items-center justify-between py-1.5 text-sm">
                <div className="flex items-center gap-2">
                  <span className="text-text-secondary text-xs px-1.5 py-0.5 bg-glass-1">
                    {f.type}
                  </span>
                  <span>{f.name}</span>
                </div>
                <div className="flex items-center gap-3">
                  <span className="text-text-secondary">{(f.size / 1024).toFixed(1)} KB</span>
                  <a
                    href={`/api/results/${jobId}/files/${f.name}`}
                    download
                    className="text-primary hover:text-accent-bright text-xs"
                  >
                    Download
                  </a>
                </div>
              </div>
            ))}
          </div>
        </Card>
      )}
    </div>
  );
}
