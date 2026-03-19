import type { ScenarioParam, ParameterPreset } from '../../api/client';
import type { UseMutationResult } from '@tanstack/react-query';
import { Button } from '../ui/Button';
import { TextInput, Checkbox } from '../ui/Input';

export interface ParameterOverridesProps {
  scenarioParams: ScenarioParam[];
  paramOverrides: Record<string, string>;
  setParamOverrides: React.Dispatch<React.SetStateAction<Record<string, string>>>;
  presets: ParameterPreset[];
  loadPreset: (preset: ParameterPreset) => void;
  presetName: string;
  setPresetName: (v: string) => void;
  showPresetSave: boolean;
  setShowPresetSave: (v: boolean) => void;
  presetMutation: UseMutationResult<unknown, Error, void, unknown>;
  compact?: boolean;
}

export function ParameterOverrides({
  scenarioParams,
  paramOverrides,
  setParamOverrides,
  presets,
  loadPreset,
  presetName,
  setPresetName,
  showPresetSave,
  setShowPresetSave,
  presetMutation,
  compact = false,
}: ParameterOverridesProps) {
  return (
    <div>
      <h3 className="text-xs text-text-tertiary mb-2">Parameters</h3>
      <div className="space-y-3">
        {/* Preset selector */}
        {presets.length > 0 && (
          <div className="flex items-center gap-2 mb-2">
            <span className="text-text-secondary text-xs">Presets:</span>
            {presets.map((p) => (
              <button
                key={p.preset_id}
                onClick={() => loadPreset(p)}
                className="px-2 py-0.5 text-xs bg-glass-1 border border-glass-edge hover:border-glass-edge-mid text-text-secondary hover:text-foreground transition-colors cursor-pointer"
              >
                {p.name}
              </button>
            ))}
          </div>
        )}

        {/* Parameter inputs */}
        <div className={`grid grid-cols-1 ${compact ? 'gap-1' : 'gap-2'}`}>
          {scenarioParams.map((p: ScenarioParam) => (
            <div key={p.name} className="flex items-center gap-3">
              <span className="text-sm font-mono text-text-secondary w-40 shrink-0 truncate" title={p.name}>
                {p.name}
              </span>
              <span className="text-[10px] text-text-tertiary w-12 shrink-0">{p.type}</span>
              {p.type === 'boolean' ? (
                <Checkbox
                  label=""
                  checked={paramOverrides[p.name] === 'true' || paramOverrides[p.name] === '1'}
                  onChange={(e) => setParamOverrides((prev) => ({ ...prev, [p.name]: e.target.checked ? 'true' : 'false' }))}
                />
              ) : (
                <TextInput
                  value={paramOverrides[p.name] ?? p.value}
                  onChange={(e) => setParamOverrides((prev) => ({ ...prev, [p.name]: e.target.value }))}
                  className="font-mono text-xs"
                  placeholder={p.value}
                />
              )}
              {paramOverrides[p.name] !== undefined && paramOverrides[p.name] !== p.value && (
                <button
                  onClick={() => setParamOverrides((prev) => ({ ...prev, [p.name]: p.value }))}
                  className="text-text-tertiary hover:text-foreground text-xs cursor-pointer shrink-0"
                  title="Reset to default"
                >
                  &#x21BA;
                </button>
              )}
            </div>
          ))}
        </div>

        {/* Save as preset */}
        <div className="pt-2 border-t border-glass-edge">
          {showPresetSave ? (
            <div className="flex items-center gap-2">
              <TextInput
                placeholder="Preset name..."
                value={presetName}
                onChange={(e) => setPresetName(e.target.value)}
                className="text-xs"
                autoFocus
              />
              <Button
                variant="primary"
                size="sm"
                disabled={!presetName.trim() || presetMutation.isPending}
                onClick={() => presetMutation.mutate()}
              >
                Save
              </Button>
              <Button variant="ghost" size="sm" onClick={() => setShowPresetSave(false)}>
                Cancel
              </Button>
            </div>
          ) : (
            <Button variant="ghost" size="sm" onClick={() => setShowPresetSave(true)}>
              Save as Preset
            </Button>
          )}
        </div>
      </div>
    </div>
  );
}
