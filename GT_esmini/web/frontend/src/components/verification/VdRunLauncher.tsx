import { useMemo, useState } from 'react';
import { useQuery, useMutation } from '@tanstack/react-query';
import { api } from '../../api/client';
import { buildSimulationRequest } from '../../api/simulationRequest';

/**
 * Compact launcher for the VERIFY page: pick a project + scenario and start a
 * VirtualDriver run (OSI on, headless) so it can be watched live and then
 * replayed — all without leaving the page. Advanced options (parameters,
 * presets, physics flags) stay on the full RUN panel.
 */
export function VdRunLauncher({
  onStarted,
}: {
  onStarted: (
    jobId: string,
    opts: { override: boolean; projectId: string; scenarioFile: string },
  ) => void;
}) {
  const { data: projects } = useQuery({ queryKey: ['projects'], queryFn: api.getProjects });
  const [projectId, setProjectId] = useState<string>('');
  const [scenarioFile, setScenarioFile] = useState<string>('');
  const [override, setOverride] = useState(false);

  const { data: scenarios } = useQuery({
    queryKey: ['project-scenarios', projectId],
    queryFn: () => api.getProjectScenarios(projectId),
    enabled: !!projectId,
  });
  const scenarioList = useMemo(() => scenarios ?? [], [scenarios]);

  const start = useMutation({
    mutationFn: () => {
      const req = buildSimulationRequest({
        scenarioId: scenarioFile,
        projectId,
        controllerType: 'virtual_driver',
        execution: {
          osi: { enabled: true }, // required for the full scene
        },
      });
      return api.createSimulation(req);
    },
    onSuccess: (job) => onStarted(job.job_id, { override, projectId, scenarioFile }),
  });

  const canRun = !!projectId && !!scenarioFile && !start.isPending;

  return (
    <div className="flex items-center gap-2 flex-wrap text-sm">
      <select
        value={projectId}
        onChange={(e) => { setProjectId(e.target.value); setScenarioFile(''); }}
        className="bg-glass-1 border border-glass-edge rounded px-2 py-1 text-foreground"
      >
        <option value="" disabled>Project…</option>
        {(projects ?? []).map((p) => (
          <option key={p.project_id} value={p.project_id}>{p.name}</option>
        ))}
      </select>

      <select
        value={scenarioFile}
        onChange={(e) => setScenarioFile(e.target.value)}
        disabled={!projectId}
        className="bg-glass-1 border border-glass-edge rounded px-2 py-1 text-foreground disabled:opacity-50"
      >
        <option value="" disabled>Scenario…</option>
        {scenarioList.map((s) => (
          <option key={s.file} value={s.file}>{s.file}</option>
        ))}
      </select>

      <label className="inline-flex items-center gap-1 text-xs text-text-secondary">
        <input type="checkbox" checked={override} onChange={(e) => setOverride(e.target.checked)} />
        Manual override
      </label>

      <button
        onClick={() => start.mutate()}
        disabled={!canRun}
        className="px-3 py-1 rounded text-sm font-medium bg-primary/80 text-white hover:bg-primary disabled:opacity-40"
      >
        {start.isPending ? 'Starting…' : 'Run & Watch ▶'}
      </button>

      {start.error != null && (
        <span className="text-xs text-destructive">
          {/^.*409.*$/.test(String(start.error))
            ? 'A simulation is already running — wait for it to finish.'
            : String(start.error as Error)}
        </span>
      )}
    </div>
  );
}
