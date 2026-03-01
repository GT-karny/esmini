import { useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api, type ScenarioInfo, type ScenarioParam, type ParameterPreset } from '../../api/client';
import { TextInput, Checkbox } from '../ui/Input';
import { EmptyState } from '../ui/EmptyState';

interface ParameterPanelProps {
  projectId: string;
  scenario: ScenarioInfo | null;
  paramOverrides: Record<string, string>;
  onParamOverridesChange: (overrides: Record<string, string>) => void;
}

export function ParameterPanel({
  projectId,
  scenario,
  paramOverrides,
  onParamOverridesChange,
}: ParameterPanelProps) {
  const scenarioFile = scenario?.file ?? '';
  const scenarioParams = scenario?.params ?? [];

  const { data: presets } = useQuery({
    queryKey: ['presets', projectId, scenarioFile],
    queryFn: () => api.getPresets(projectId, scenarioFile),
    enabled: !!scenarioFile,
  });

  // Initialize param overrides when scenario changes
  useEffect(() => {
    if (scenarioParams.length === 0) return;
    const next: Record<string, string> = {};
    for (const p of scenarioParams) {
      next[p.name] = paramOverrides[p.name] ?? p.value;
    }
    onParamOverridesChange(next);
    // Only run when scenario changes
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [scenarioFile]);

  if (!scenario) {
    return (
      <div className="h-full flex items-center justify-center">
        <EmptyState message="Select a scenario" />
      </div>
    );
  }

  if (scenarioParams.length === 0) {
    return (
      <div className="h-full overflow-y-auto p-3">
        <h3 className="text-xs font-medium text-text-secondary uppercase tracking-wider mb-2">
          Parameters
        </h3>
        <p className="text-text-tertiary text-xs">No parameters declared in this scenario.</p>
      </div>
    );
  }

  const loadPreset = (preset: ParameterPreset) => {
    onParamOverridesChange({ ...paramOverrides, ...preset.values });
  };

  const presetList = presets ?? [];

  return (
    <div className="h-full overflow-y-auto p-3">
      <h3 className="text-xs font-medium text-text-secondary uppercase tracking-wider mb-2">
        Parameters
      </h3>

      {/* Preset buttons */}
      {presetList.length > 0 && (
        <div className="flex items-center gap-1.5 mb-3 flex-wrap">
          <span className="text-text-tertiary text-[10px]">Presets:</span>
          {presetList.map((p) => (
            <button
              key={p.preset_id}
              onClick={() => loadPreset(p)}
              className="px-2 py-0.5 text-[11px] bg-glass-1 border border-glass-edge hover:border-glass-edge-mid text-text-secondary hover:text-foreground transition-colors cursor-pointer"
            >
              {p.name}
            </button>
          ))}
        </div>
      )}

      {/* Parameter inputs */}
      <div className="space-y-1.5">
        {scenarioParams.map((p: ScenarioParam) => (
          <div key={p.name} className="flex items-center gap-2">
            <span
              className="text-xs font-mono text-text-secondary w-32 shrink-0 truncate"
              title={p.name}
            >
              {p.name}
            </span>
            <span className="text-[10px] text-text-tertiary w-10 shrink-0">{p.type}</span>
            {p.type === 'boolean' ? (
              <Checkbox
                label=""
                checked={paramOverrides[p.name] === 'true' || paramOverrides[p.name] === '1'}
                onChange={(e) =>
                  onParamOverridesChange({
                    ...paramOverrides,
                    [p.name]: e.target.checked ? 'true' : 'false',
                  })
                }
              />
            ) : (
              <TextInput
                value={paramOverrides[p.name] ?? p.value}
                onChange={(e) =>
                  onParamOverridesChange({
                    ...paramOverrides,
                    [p.name]: e.target.value,
                  })
                }
                className="font-mono text-xs"
                placeholder={p.value}
              />
            )}
            {paramOverrides[p.name] !== undefined && paramOverrides[p.name] !== p.value && (
              <button
                onClick={() =>
                  onParamOverridesChange({ ...paramOverrides, [p.name]: p.value })
                }
                className="text-text-tertiary hover:text-foreground text-xs cursor-pointer shrink-0"
                title="Reset to default"
              >
                &#x21BA;
              </button>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}
