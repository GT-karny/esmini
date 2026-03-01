import { useNavigate } from 'react-router-dom';
import { useQuery } from '@tanstack/react-query';
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
}

export function ExecutionPanel({
  projectId,
  scenario,
  paramOverrides: _paramOverrides,
  onRunning,
  latestJobId,
}: ExecutionPanelProps) {
  const navigate = useNavigate();

  const scenarioFile = scenario?.file ?? '';
  const scenarioParams = scenario?.params ?? [];

  const { data: presets } = useQuery({
    queryKey: ['presets', projectId, scenarioFile],
    queryFn: () => api.getPresets(projectId, scenarioFile),
    enabled: !!scenarioFile,
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

      {/* Simulation run form (compact, no parameter section — it's in ParameterPanel) */}
      <SimulationRunForm
        projectId={projectId}
        scenarioFile={scenarioFile}
        scenarioParams={scenarioParams}
        presets={presets ?? []}
        compact
        onSubmitted={handleSubmitted}
        onNavigateToJob={(jobId) => navigate(`/simulations/${jobId}`)}
      />
    </div>
  );
}
