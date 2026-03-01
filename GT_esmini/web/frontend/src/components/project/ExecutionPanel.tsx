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
      <h3 className="text-xs font-medium text-text-secondary uppercase tracking-wider mb-2">
        Run Simulation
      </h3>

      {/* Latest job status */}
      {latestJobId && (
        <div className="mb-3">
          <InlineSimulationStatus
            jobId={latestJobId}
            onViewDetails={() => navigate(`/simulations/${latestJobId}`)}
          />
        </div>
      )}

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
    </div>
  );
}
