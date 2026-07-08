import { useEffect, useMemo } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api, type ScenarioInfo, type ScenarioParam } from '../../api/client';
import { TextInput, Checkbox } from '../ui/Input';
import { EmptyState } from '../ui/EmptyState';
import { PresetSelector } from './PresetSelector';

interface CorruptDetail {
  path: string;
  message: string;
  line?: number;
}

function extractCorruptDetail(err: unknown): CorruptDetail | null {
  if (!(err instanceof Error)) return null;
  const m = err.message.match(/^409:\s*(\{.*\})$/s);
  if (!m) return null;
  try {
    const parsed = JSON.parse(m[1]);
    const detail = parsed?.detail;
    if (detail && detail.code === 'preset_file_corrupted') {
      return {
        path: String(detail.path ?? ''),
        message: String(detail.message ?? ''),
        line: typeof detail.line === 'number' ? detail.line : undefined,
      };
    }
  } catch {
    // fallthrough
  }
  return null;
}

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

  const { data: presets, error: presetsError } = useQuery({
    queryKey: ['presets', projectId, scenarioFile],
    queryFn: () => api.getPresets(projectId, scenarioFile),
    enabled: !!scenarioFile,
    retry: false,
  });

  const corruptDetail = extractCorruptDetail(presetsError);

  // Default values from scenario params
  const defaultValues = useMemo(() => {
    const vals: Record<string, string> = {};
    for (const p of scenarioParams) vals[p.name] = p.value;
    return vals;
  }, [scenarioParams]);

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
        <h3 className="flex items-center gap-2 text-sm font-display font-medium text-text-secondary uppercase tracking-wider mb-2">
          <svg viewBox="0 0 16 16" fill="currentColor" className="w-3.5 h-3.5">
            <path d="M4 1v5h2V1zm0 8v7h2V9zM10 1v9h2V1zm0 12v3h2v-3zM3 6h4v2H3zM9 10h4v2H9z" />
          </svg>
          Parameters
        </h3>
        <p className="text-text-tertiary text-xs">No parameters declared in this scenario.</p>
      </div>
    );
  }

  return (
    <div className="h-full overflow-y-auto p-3">
      <h3 className="flex items-center gap-2 text-sm font-display font-medium text-text-secondary uppercase tracking-wider mb-2">
        <svg viewBox="0 0 16 16" fill="currentColor" className="w-3.5 h-3.5">
          <path d="M4 1v5h2V1zm0 8v7h2V9zM10 1v9h2V1zm0 12v3h2v-3zM3 6h4v2H3zM9 10h4v2H9z" />
        </svg>
        Parameters
      </h3>

      {/* Corrupt preset file banner */}
      {corruptDetail && (
        <div className="mb-2 px-2 py-1.5 bg-destructive/10 border border-destructive/40 text-[10px] text-destructive">
          <div className="font-medium">Preset file is corrupted — saving is disabled.</div>
          <div className="text-text-tertiary mt-0.5 break-all">
            {corruptDetail.path}
            {corruptDetail.line ? ` (line ${corruptDetail.line})` : ''}: {corruptDetail.message}
          </div>
        </div>
      )}

      {/* Preset tabs */}
      <PresetSelector
        projectId={projectId}
        scenarioFile={scenarioFile}
        presets={presets ?? []}
        currentValues={paramOverrides}
        defaultValues={defaultValues}
        onLoad={onParamOverridesChange}
        disabled={!!corruptDetail}
      />

      {/* Parameter inputs */}
      <div className="space-y-1.5">
        {scenarioParams.map((p: ScenarioParam) => {
          const isChanged = paramOverrides[p.name] !== undefined && paramOverrides[p.name] !== p.value;
          return (
            <div key={p.name} className="flex items-center gap-2">
              <span
                className="text-xs font-mono text-text-secondary w-32 shrink-0 truncate"
                title={p.name}
              >
                {p.name}
                {isChanged && <span className="text-warning ml-0.5">*</span>}
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
              {isChanged && (
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
          );
        })}
      </div>
    </div>
  );
}
