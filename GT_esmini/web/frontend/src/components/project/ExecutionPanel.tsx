import { useNavigate } from 'react-router-dom';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type ScenarioInfo, type SimulationStatus } from '../../api/client';
import { SimulationRunForm } from '../SimulationRunForm';
import { InlineSimulationStatus } from '../InlineSimulationStatus';
import { EmptyState } from '../ui/EmptyState';

interface ExecutionPanelProps {
  projectId: string;
  scenario: ScenarioInfo | null;
  paramOverrides: Record<string, string>;
  onRunning: (jobId: string) => void;
  latestJobId: string | null;
  activeJobId: string | null;
}

export function ExecutionPanel({
  projectId,
  scenario,
  paramOverrides,
  onRunning,
  latestJobId,
  activeJobId,
}: ExecutionPanelProps) {
  const navigate = useNavigate();
  const queryClient = useQueryClient();

  const scenarioFile = scenario?.file ?? '';

  const cancelMut = useMutation({
    mutationFn: () => api.cancelSimulation(activeJobId!),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['simulation', activeJobId] });
    },
    onError: (err: Error) => {
      console.error('Cancel simulation failed:', err);
    },
  });

  if (!scenario) {
    return (
      <div className="h-full flex items-center justify-center">
        <EmptyState message="Select a scenario to run" />
      </div>
    );
  }

  const handleSubmitted = (job: SimulationStatus) => {
    onRunning(job.job_id);
  };

  return (
    <div className="h-full overflow-y-auto p-3">
      <h3 className="flex items-center gap-2 text-sm font-display font-medium text-text-secondary uppercase tracking-wider mb-2">
        <svg viewBox="0 0 16 16" fill="currentColor" className="w-3.5 h-3.5">
          <path d="M4 2l10 6-10 6V2z" />
        </svg>
        Run
      </h3>

      {/* Simulation run form (compact, params hidden — managed by ParameterPanel) */}
      <SimulationRunForm
        projectId={projectId}
        scenarioFile={scenarioFile}
        scenarioParams={scenario.params ?? []}
        presets={[]}
        compact
        hideParams
        externalParamOverrides={paramOverrides}
        onSubmitted={handleSubmitted}
        isRunning={!!activeJobId}
        onStop={() => cancelMut.mutate()}
      />

      {/* Latest job status (below Run/Stop buttons) */}
      {latestJobId && (
        <div className="mt-3">
          <InlineSimulationStatus
            jobId={latestJobId}
            onViewDetails={() => navigate(`/simulations/${latestJobId}`)}
          />
        </div>
      )}
    </div>
  );
}
