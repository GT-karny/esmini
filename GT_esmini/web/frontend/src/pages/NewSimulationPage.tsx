import { useState, useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';
import { useNavigate, useSearchParams, useLocation, useParams } from 'react-router-dom';
import { api } from '../api/client';
import type { SimulationStatus } from '../api/client';
import { SelectInput } from '../components/ui/Input';
import { Card } from '../components/ui/Card';
import { SimulationRunForm } from '../components/SimulationRunForm';

export function NewSimulationPage() {
  const navigate = useNavigate();
  const location = useLocation();
  const [searchParams] = useSearchParams();
  const { projectId } = useParams<{ projectId: string }>();

  // Scenario selection (stays in page)
  const [scenarioId, setScenarioId] = useState(searchParams.get('scenario') ?? '');

  // Re-run flow
  const rerunSource = location.state?.rerunFrom as SimulationStatus | undefined;
  const [rerunOptions, setRerunOptions] = useState<Record<string, unknown> | undefined>();

  useEffect(() => {
    if (!rerunSource?.options) return;
    setScenarioId(rerunSource.scenario_id);
    setRerunOptions(rerunSource.options);
    window.history.replaceState({}, '');
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Scenario lists
  const { data: scenarios } = useQuery({
    queryKey: ['scenarios'],
    queryFn: () => api.getScenarios(),
    enabled: !projectId,
  });

  const { data: projectScenarios } = useQuery({
    queryKey: ['project-scenarios', projectId],
    queryFn: () => api.getProjectScenarios(projectId!),
    enabled: !!projectId,
  });

  // Fetch params for the selected scenario (project context)
  const { data: scenarioParams } = useQuery({
    queryKey: ['scenario-params', projectId, scenarioId],
    queryFn: () => api.getScenarioParams(projectId!, scenarioId),
    enabled: !!projectId && !!scenarioId,
  });

  // Fetch presets for the selected scenario
  const { data: presets } = useQuery({
    queryKey: ['presets', projectId, scenarioId],
    queryFn: () => api.getPresets(projectId!, scenarioId),
    enabled: !!projectId && !!scenarioId,
  });

  // Determine scenario options
  const scenarioOptions = projectId
    ? (projectScenarios ?? []).map((s) => ({ id: s.file, label: s.filename }))
    : (scenarios ?? []).map((s) => ({ id: s.id, label: s.id }));

  return (
    <div className="max-w-2xl">
      <h1 className="text-2xl font-display font-bold mb-6 tracking-wide">RUN SIMULATION</h1>

      <div className="space-y-4">
        {/* Scenario Selection (page-level) */}
        <Card title="Scenario">
          <SelectInput
            value={scenarioId}
            onChange={(e) => setScenarioId(e.target.value)}
          >
            <option value="">Select a scenario...</option>
            {scenarioOptions.map((s) => (
              <option key={s.id} value={s.id}>{s.label}</option>
            ))}
          </SelectInput>
        </Card>

        {/* Simulation Run Form */}
        <SimulationRunForm
          projectId={projectId ?? ''}
          scenarioFile={scenarioId}
          scenarioParams={scenarioParams ?? []}
          presets={presets ?? []}
          compact={false}
          rerunFrom={rerunOptions}
          onNavigateToJob={(jobId) => navigate(`/simulations/${jobId}`)}
        />
      </div>
    </div>
  );
}