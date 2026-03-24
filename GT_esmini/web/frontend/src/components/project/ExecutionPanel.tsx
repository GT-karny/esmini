import { useRef } from 'react';
import { useNavigate } from 'react-router-dom';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type ScenarioInfo, type SimulationRequest, type SimulationStatus } from '../../api/client';
import { SimulationRunForm } from '../SimulationRunForm';
import { InlineSimulationStatus } from '../InlineSimulationStatus';
import { CopyApiRequest } from './CopyApiRequest';
import { EmptyState } from '../ui/EmptyState';

interface ExecutionPanelProps {
  projectId: string;
  scenario: ScenarioInfo | null;
  paramOverrides: Record<string, string>;
  onRunning: (jobId: string) => void;
  latestJobId: string | null;
  activeJobId: string | null;
  isLocked?: boolean;
}

export function ExecutionPanel({
  projectId,
  scenario,
  paramOverrides,
  onRunning,
  latestJobId,
  activeJobId,
  isLocked,
}: ExecutionPanelProps) {
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const requestBuilderRef = useRef<(() => SimulationRequest) | null>(null);

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
        {isLocked && (
          <span className="ml-auto flex items-center gap-1 text-[10px] text-warning normal-case tracking-normal font-normal">
            <svg viewBox="0 0 16 16" fill="currentColor" className="w-3 h-3">
              <path d="M8 1a4 4 0 0 0-4 4v2H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V8a1 1 0 0 0-1-1h-1V5a4 4 0 0 0-4-4zm-2 4a2 2 0 1 1 4 0v2H6V5z" />
            </svg>
            {scenario?.filename}
          </span>
        )}
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
        onRequestBuilder={(fn) => { requestBuilderRef.current = fn; }}
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

      {/* Copy API Request section */}
      <div className="mt-4 pt-3 border-t border-border">
        <CopyApiRequest
          getRequest={() => requestBuilderRef.current?.() ?? null}
          scenarioParams={scenario.params ?? []}
          paramSource={paramOverrides}
        />
      </div>
    </div>
  );
}
