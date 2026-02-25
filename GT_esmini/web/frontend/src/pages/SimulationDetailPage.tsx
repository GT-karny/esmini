import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { useParams, Link } from 'react-router-dom';
import { api, type ResultFile } from '../api/client';
import { OsiLivePanel } from '../components/OsiLivePanel';

const statusColors: Record<string, string> = {
  queued: 'bg-yellow-500/20 text-yellow-400 border-yellow-500/30',
  running: 'bg-blue-500/20 text-blue-400 border-blue-500/30',
  completed: 'bg-green-500/20 text-green-400 border-green-500/30',
  failed: 'bg-red-500/20 text-red-400 border-red-500/30',
  cancelled: 'bg-gray-500/20 text-gray-400 border-gray-500/30',
  timeout: 'bg-orange-500/20 text-orange-400 border-orange-500/30',
};

export function SimulationDetailPage() {
  const { jobId } = useParams<{ jobId: string }>();
  const queryClient = useQueryClient();

  const { data: sim, isLoading: simLoading } = useQuery({
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
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ['simulation', jobId] }),
  });

  if (simLoading) return <p className="text-gray-400">Loading...</p>;
  if (!sim) return <p className="text-red-400">Job not found.</p>;

  return (
    <div className="max-w-3xl">
      <div className="flex items-center gap-3 mb-6">
        <Link to="/simulations" className="text-gray-400 hover:text-white">&larr;</Link>
        <h1 className="text-2xl font-bold">Job {sim.job_id}</h1>
        <span className={`inline-block px-3 py-1 rounded border text-sm font-medium ${statusColors[sim.status] ?? ''}`}>
          {sim.status}
        </span>
      </div>

      {/* Job Info */}
      <section className="bg-gray-900 rounded-lg border border-gray-800 p-4 mb-4">
        <h2 className="text-sm font-medium text-gray-400 mb-3">Details</h2>
        <dl className="grid grid-cols-2 gap-x-8 gap-y-2 text-sm">
          <dt className="text-gray-500">Scenario</dt>
          <dd>{sim.scenario_id}</dd>
          <dt className="text-gray-500">Controller</dt>
          <dd>{sim.controller_type}</dd>
          <dt className="text-gray-500">Started</dt>
          <dd>{sim.started_at ? new Date(sim.started_at).toLocaleString() : '-'}</dd>
          <dt className="text-gray-500">Completed</dt>
          <dd>{sim.completed_at ? new Date(sim.completed_at).toLocaleString() : '-'}</dd>
          <dt className="text-gray-500">Exit Code</dt>
          <dd>{sim.exit_code ?? '-'}</dd>
          <dt className="text-gray-500">PID</dt>
          <dd>{sim.pid ?? '-'}</dd>
        </dl>

        {sim.error_message && (
          <div className="mt-3 p-3 bg-red-500/10 border border-red-500/20 rounded text-sm text-red-300 font-mono whitespace-pre-wrap max-h-48 overflow-auto">
            {sim.error_message}
          </div>
        )}

        {sim.status === 'running' && (
          <button
            onClick={() => cancelMutation.mutate()}
            className="mt-3 bg-red-600 hover:bg-red-500 text-white text-sm font-medium px-4 py-2 rounded transition-colors"
          >
            Cancel
          </button>
        )}
      </section>

      {/* Live OSI Data (shown while running) */}
      {sim.status === 'running' && jobId && <OsiLivePanel jobId={jobId} />}

      {/* Metrics */}
      {metrics && !('error' in metrics) && (() => {
        const summary = metrics.summary as Record<string, number> | undefined;
        const finalState = metrics.final_state as Record<string, unknown> | undefined;
        return (
          <section className="bg-gray-900 rounded-lg border border-gray-800 p-4 mb-4">
            <h2 className="text-sm font-medium text-gray-400 mb-3">Metrics</h2>
            {summary && (
              <div className="grid grid-cols-3 gap-4">
                {Object.entries(summary).map(([key, val]) => (
                  <div key={key} className="bg-gray-800/50 rounded p-3">
                    <div className="text-xs text-gray-500 mb-1">{key.replace(/_/g, ' ')}</div>
                    <div className="text-lg font-mono">
                      {typeof val === 'number' ? val.toFixed(2) : String(val)}
                    </div>
                  </div>
                ))}
              </div>
            )}
            {finalState && (
              <div className="mt-4">
                <h3 className="text-xs text-gray-500 mb-2">Final State</h3>
                <div className="grid grid-cols-4 gap-3">
                  {Object.entries(finalState).map(([key, val]) => (
                    <div key={key} className="text-sm">
                      <span className="text-gray-500">{key}: </span>
                      <span className="font-mono">
                        {typeof val === 'number' ? val.toFixed(2) : String(val ?? '-')}
                      </span>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </section>
        );
      })()}

      {/* Files */}
      {resultMeta && resultMeta.files.length > 0 && (
        <section className="bg-gray-900 rounded-lg border border-gray-800 p-4">
          <h2 className="text-sm font-medium text-gray-400 mb-3">Output Files</h2>
          <div className="space-y-1">
            {resultMeta.files.map((f: ResultFile) => (
              <div key={f.name} className="flex items-center justify-between py-1.5 text-sm">
                <div className="flex items-center gap-2">
                  <span className="text-gray-400 text-xs px-1.5 py-0.5 bg-gray-800 rounded">{f.type}</span>
                  <span>{f.name}</span>
                </div>
                <div className="flex items-center gap-3">
                  <span className="text-gray-500">{(f.size / 1024).toFixed(1)} KB</span>
                  <a
                    href={`/api/results/${jobId}/files/${f.name}`}
                    download
                    className="text-blue-400 hover:text-blue-300 text-xs"
                  >
                    Download
                  </a>
                </div>
              </div>
            ))}
          </div>
        </section>
      )}
    </div>
  );
}
