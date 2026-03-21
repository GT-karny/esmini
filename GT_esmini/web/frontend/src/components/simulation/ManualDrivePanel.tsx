import { useState, useCallback, useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { SlidePanel } from '../ui/SlidePanel';
import { SelectInput, NumberInput, ToggleSwitch } from '../ui/Input';
import { Button } from '../ui/Button';
import { useGamepadButtonCapture } from '../../hooks/useGamepadButtonCapture';
import { api, type ManualDriveConfig, type ManualDrivePreset } from '../../api/client';

interface ManualDrivePanelProps {
  open: boolean;
  onClose: () => void;
  config: ManualDriveConfig;
  onChange: (config: ManualDriveConfig) => void;
}

const DEFAULT_CONFIG: ManualDriveConfig = {
  input_type: 'stub',
  physics_type: 'real_vehicle',
  ffb_enabled: false,
  domain: { lateral: 'manual', longitudinal: 'manual' },
  sdl2: {
    device_index: 0,
    deadzone: 0.05,
    button_mapping: { upshift: 4, downshift: 5, override: 0, indicator_left: 7, indicator_right: 6 },
  },
  input_network: { transport_type: 'udp', port: 9100, level: 'pedal_steer' },
  physics_network: { transport_type: 'udp', host: '127.0.0.1', cmd_port: 9200, state_port: 9201 },
  ffb: { spring_coefficient: 0.5, damper_coefficient: 0.3, constant_gain: 1.0, max_force: 1.0 },
};

const BUTTON_LABELS: Record<string, string> = {
  upshift: 'Upshift',
  downshift: 'Downshift',
  override: 'Override',
  indicator_left: 'Ind. Left',
  indicator_right: 'Ind. Right',
};

export function ManualDrivePanel({ open, onClose, config, onChange }: ManualDrivePanelProps) {
  const queryClient = useQueryClient();
  const [assigningKey, setAssigningKey] = useState<string | null>(null);

  // Load presets
  const { data: presets } = useQuery({
    queryKey: ['manual-drive-presets'],
    queryFn: api.getManualDrivePresets,
    enabled: open,
  });

  // Save config to file
  const saveMutation = useMutation({
    mutationFn: (cfg: ManualDriveConfig) => api.updateManualDriveConfig(cfg),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['manual-drive-config'] });
    },
  });

  // Gamepad button capture
  const onButtonCapture = useCallback(
    (buttonIndex: number) => {
      if (!assigningKey) return;
      const newMapping = { ...config.sdl2.button_mapping, [assigningKey]: buttonIndex };
      onChange({ ...config, sdl2: { ...config.sdl2, button_mapping: newMapping } });
      setAssigningKey(null);
    },
    [assigningKey, config, onChange],
  );

  const { capturing, startCapture, cancel: cancelCapture } = useGamepadButtonCapture(onButtonCapture);

  const startAssign = (key: string) => {
    setAssigningKey(key);
    startCapture();
  };

  // Update helpers
  const set = <K extends keyof ManualDriveConfig>(key: K, val: ManualDriveConfig[K]) =>
    onChange({ ...config, [key]: val });

  const setNested = <K extends keyof ManualDriveConfig>(
    key: K,
    field: string,
    val: unknown,
  ) => onChange({ ...config, [key]: { ...(config[key] as Record<string, unknown>), [field]: val } });

  const applyPreset = (preset: ManualDrivePreset) => {
    onChange({ ...DEFAULT_CONFIG, ...preset.config });
  };

  return (
    <SlidePanel open={open} onClose={onClose} title="Manual Drive Settings">
      <div className="space-y-6">
        {/* Presets */}
        <section>
          <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Presets</h3>
          <div className="flex flex-wrap gap-1.5">
            {presets?.map((p) => (
              <button
                key={p.name}
                onClick={() => applyPreset(p)}
                className="px-2.5 py-1 text-xs rounded bg-glass-1 border border-glass-edge text-text-secondary hover:bg-glass-hover hover:text-foreground transition-colors cursor-pointer"
              >
                {p.name}
              </button>
            ))}
          </div>
        </section>

        {/* Domain Assignment */}
        <section>
          <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Domain Assignment</h3>
          <div className="space-y-2">
            <SelectInput
              label="Lateral (Steering)"
              value={config.domain.lateral}
              onChange={(e) => onChange({ ...config, domain: { ...config.domain, lateral: e.target.value } })}
            >
              <option value="manual">Manual</option>
              <option value="scenario">Scenario</option>
            </SelectInput>
            <SelectInput
              label="Longitudinal (Throttle/Brake)"
              value={config.domain.longitudinal}
              onChange={(e) => onChange({ ...config, domain: { ...config.domain, longitudinal: e.target.value } })}
            >
              <option value="manual">Manual</option>
              <option value="scenario">Scenario</option>
            </SelectInput>
          </div>
        </section>

        {/* Input Source */}
        <section>
          <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Input Source</h3>
          <SelectInput
            label="Type"
            value={config.input_type}
            onChange={(e) => set('input_type', e.target.value)}
          >
            <option value="sdl2_wheel">SDL2 Wheel</option>
            <option value="network">Network</option>
            <option value="stub">None (Stub)</option>
          </SelectInput>

          {config.input_type === 'network' && (
            <div className="space-y-2 mt-2">
              <SelectInput
                label="Transport"
                value={config.input_network.transport_type}
                onChange={(e) => setNested('input_network', 'transport_type', e.target.value)}
              >
                <option value="udp">UDP</option>
                <option value="tcp">TCP</option>
              </SelectInput>
              <NumberInput
                label="Port"
                value={config.input_network.port}
                onChange={(e) => setNested('input_network', 'port', Number(e.target.value))}
                min={1}
                max={65535}
              />
              <SelectInput
                label="Level"
                value={config.input_network.level}
                onChange={(e) => setNested('input_network', 'level', e.target.value)}
              >
                <option value="pedal_steer">Pedal / Steer</option>
                <option value="motion_request">Motion Request (OSI)</option>
              </SelectInput>
            </div>
          )}
        </section>

        {/* Physics Backend */}
        <section>
          <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Physics Backend</h3>
          <SelectInput
            label="Type"
            value={config.physics_type}
            onChange={(e) => set('physics_type', e.target.value)}
          >
            <option value="real_vehicle">Internal (RealVehicle)</option>
            <option value="network">External Simulator</option>
          </SelectInput>

          {config.physics_type === 'network' && (
            <div className="space-y-2 mt-2">
              <SelectInput
                label="Transport"
                value={config.physics_network.transport_type}
                onChange={(e) => setNested('physics_network', 'transport_type', e.target.value)}
              >
                <option value="udp">UDP</option>
                <option value="tcp">TCP</option>
              </SelectInput>
              <NumberInput
                label="Cmd Port"
                value={config.physics_network.cmd_port}
                onChange={(e) => setNested('physics_network', 'cmd_port', Number(e.target.value))}
              />
              <NumberInput
                label="State Port"
                value={config.physics_network.state_port}
                onChange={(e) => setNested('physics_network', 'state_port', Number(e.target.value))}
              />
            </div>
          )}
        </section>

        {/* Button Mapping (SDL2 only) */}
        {config.input_type === 'sdl2_wheel' && (
          <section>
            <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Button Mapping</h3>
            <div className="space-y-1.5">
              {Object.entries(BUTTON_LABELS).map(([key, label]) => {
                const btnIdx = config.sdl2.button_mapping[key as keyof typeof config.sdl2.button_mapping];
                const isAssigning = capturing && assigningKey === key;
                return (
                  <div key={key} className="flex items-center gap-2">
                    <span className="text-xs text-text-secondary w-20 shrink-0">{label}</span>
                    <span className="text-xs font-mono bg-glass-1 border border-glass-edge rounded px-2 py-0.5 w-12 text-center">
                      {btnIdx}
                    </span>
                    <button
                      onClick={() => isAssigning ? cancelCapture() : startAssign(key)}
                      className={`text-[10px] px-2 py-0.5 rounded cursor-pointer transition-colors ${
                        isAssigning
                          ? 'bg-primary/80 text-background animate-pulse'
                          : 'bg-glass-1 border border-glass-edge text-text-tertiary hover:text-foreground hover:bg-glass-hover'
                      }`}
                    >
                      {isAssigning ? 'Press...' : 'Assign'}
                    </button>
                  </div>
                );
              })}
            </div>
          </section>
        )}

        {/* Force Feedback */}
        <section>
          <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Force Feedback</h3>
          <ToggleSwitch
            label="Enable FFB"
            checked={config.ffb_enabled}
            onChange={(v) => set('ffb_enabled', v)}
          />
          {config.ffb_enabled && (
            <div className="space-y-2 mt-2">
              <RangeInput label="Spring" value={config.ffb.spring_coefficient} min={0} max={1} step={0.05}
                onChange={(v) => onChange({ ...config, ffb: { ...config.ffb, spring_coefficient: v } })} />
              <RangeInput label="Damper" value={config.ffb.damper_coefficient} min={0} max={1} step={0.05}
                onChange={(v) => onChange({ ...config, ffb: { ...config.ffb, damper_coefficient: v } })} />
              <RangeInput label="Gain" value={config.ffb.constant_gain} min={0} max={2} step={0.1}
                onChange={(v) => onChange({ ...config, ffb: { ...config.ffb, constant_gain: v } })} />
            </div>
          )}
        </section>

        {/* Actions */}
        <div className="flex gap-2 pt-2 border-t border-glass-edge">
          <Button variant="ghost" size="sm" onClick={() => onChange(DEFAULT_CONFIG)}>
            Reset
          </Button>
          <Button
            variant="primary"
            size="sm"
            onClick={() => saveMutation.mutate(config)}
            disabled={saveMutation.isPending}
          >
            {saveMutation.isPending ? 'Saving...' : 'Save'}
          </Button>
        </div>
        {saveMutation.error && (
          <p className="text-xs text-destructive mt-1">{String(saveMutation.error)}</p>
        )}
      </div>
    </SlidePanel>
  );
}

// Simple range slider component (inline, not in ui/ to keep it local)
function RangeInput({
  label, value, min, max, step, onChange,
}: {
  label: string; value: number; min: number; max: number; step: number;
  onChange: (v: number) => void;
}) {
  return (
    <div className="flex items-center gap-2">
      <span className="text-xs text-text-secondary w-16 shrink-0">{label}</span>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        className="flex-1 accent-primary h-1"
      />
      <span className="text-xs font-mono text-text-tertiary w-10 text-right">{value.toFixed(2)}</span>
    </div>
  );
}
