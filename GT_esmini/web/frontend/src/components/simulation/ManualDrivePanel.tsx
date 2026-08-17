import { useState, useCallback, useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { SlidePanel } from '../ui/SlidePanel';
import { SelectInput, NumberInput } from '../ui/Input';
import { Button } from '../ui/Button';
import { useGamepadButtonCapture } from '../../hooks/useGamepadButtonCapture';
import { api, type ManualDriveConfig, type ManualDrivePreset } from '../../api/client';
import { MANUAL_DRIVE_DEFAULT_PORTS } from '../../lib/manualDrive';
import { WheelAxisMappingSection, DEFAULT_AXIS_MAPPING } from './WheelAxisMappingSection';

interface ManualDrivePanelProps {
  open: boolean;
  onClose: () => void;
  config: ManualDriveConfig;
  onChange: (config: ManualDriveConfig) => void;
}

const DEFAULT_CONFIG: ManualDriveConfig = {
  input_type: 'sdl2_wheel',
  physics_type: 'real_vehicle',
  ffb_enabled: true,
  domain: { lateral: 'manual', longitudinal: 'manual' },
  sdl2: {
    device_index: 0,
    deadzone: 0,
    // auto_resume defaults to 3, matching config/manual_drive.json and the
    // pydantic model. NOT -1: an omitted/defaulted value is written straight
    // into the per-run config, so -1 here would hand C++ "unassigned" and
    // re-create gap #5 through the control meant to expose it.
    button_mapping: { upshift: 4, downshift: 5, override: 0, indicator_left: 7, indicator_right: 6, headlight: -1, high_beam: -1, fog_light: -1, hazard: -1, auto_resume: 3 },
    // feature:F8 -- the G29 layout, i.e. what C++ hardcoded before the mapping
    // existed. Same reasoning as auto_resume above: a defaulted value here is
    // written straight into the per-run config, so it must be the shipped
    // layout and not a placeholder.
    axis_mapping: DEFAULT_AXIS_MAPPING,
  },
  keyboard: {
    steer_left: 'A', steer_right: 'D', throttle: 'W', brake: 'S', clutch: 'LShift',
    upshift: 'E', downshift: 'Q', override_key: 'O',
    indicator_left: 'Z', indicator_right: 'X',
    headlight: 'L', high_beam: 'K', fog_light: 'F', hazard: 'H',
    steer_rate: 2.0, centering_rate: 3.0, pedal_press_rate: 4.0, pedal_release_rate: 6.0,
  },
  input_network: { transport_type: 'udp', port: MANUAL_DRIVE_DEFAULT_PORTS.input, level: 'pedal_steer' },
  physics_network: { transport_type: 'udp', host: '127.0.0.1', cmd_port: MANUAL_DRIVE_DEFAULT_PORTS.physicsCmd, state_port: MANUAL_DRIVE_DEFAULT_PORTS.physicsState },
  ffb: { spring_coefficient: 0.5, damper_coefficient: 0.3, constant_gain: 1.0, max_force: 1.0 },
};

const BUTTON_LABELS: Record<string, string> = {
  upshift: 'Upshift',
  downshift: 'Downshift',
  override: 'Override',
  indicator_left: 'Ind. Left',
  indicator_right: 'Ind. Right',
  headlight: 'Headlight',
  high_beam: 'High Beam',
  fog_light: 'Fog Light',
  hazard: 'Hazard',
  // feature:F7 gap #6 -- this Record drives the button-assignment rows, so
  // adding the label is what actually exposes AUTO_RESUME in the GUI.
  auto_resume: 'Auto Resume',
};

// feature:F7 gap #6. Defaults mirror config/manual_drive.json AND
// ManualDriveConfig.hpp:483-488 (they coincide), so filling a blank box with
// the default and sending it changes nothing.
const DEFAULT_OVERRIDE_CFG = {
  enabled: true,
  steering_threshold: 0.05,
  throttle_threshold: 0.1,
  brake_threshold: 0.1,
  auto_return_timeout: 0.0,
  button_override: true,
  button_takeover: false,
};

const OVERRIDE_NUMBER_FIELDS: { key: 'steering_threshold' | 'throttle_threshold' | 'brake_threshold' | 'auto_return_timeout'; label: string; step: number }[] = [
  { key: 'steering_threshold',  label: 'Steering thr.',  step: 0.01 },
  { key: 'throttle_threshold',  label: 'Throttle thr.',  step: 0.01 },
  { key: 'brake_threshold',     label: 'Brake thr.',     step: 0.01 },
  { key: 'auto_return_timeout', label: 'Auto-return (s)', step: 0.1 },
];

const OVERRIDE_BOOL_FIELDS: { key: 'enabled' | 'button_override' | 'button_takeover'; label: string }[] = [
  { key: 'enabled',         label: 'Override enabled' },
  { key: 'button_override', label: 'Button override' },
  { key: 'button_takeover', label: 'Triangle button takeover' },
];

const KEYBOARD_BINDINGS: { key: string; label: string }[] = [
  { key: 'steer_left',      label: 'Steer Left' },
  { key: 'steer_right',     label: 'Steer Right' },
  { key: 'throttle',        label: 'Throttle' },
  { key: 'brake',           label: 'Brake' },
  { key: 'clutch',          label: 'Clutch' },
  { key: 'upshift',         label: 'Upshift' },
  { key: 'downshift',       label: 'Downshift' },
  { key: 'override_key',    label: 'Override' },
  { key: 'indicator_left',  label: 'Ind. Left' },
  { key: 'indicator_right', label: 'Ind. Right' },
  { key: 'headlight',       label: 'Headlight' },
  { key: 'high_beam',       label: 'High Beam' },
  { key: 'fog_light',       label: 'Fog Light' },
  { key: 'hazard',          label: 'Hazard' },
];

// Convert KeyboardEvent.code → SDL scancode-name shorthand the C++ side accepts.
// Returns null for unsupported keys.
function keyEventToSdlName(e: KeyboardEvent): string | null {
  const code = e.code;
  // Letter keys: "KeyA" → "A"
  if (/^Key[A-Z]$/.test(code)) return code.slice(3);
  // Digit row: "Digit1" → "1"
  if (/^Digit[0-9]$/.test(code)) return code.slice(5);
  // Numpad digits
  if (/^Numpad[0-9]$/.test(code)) return 'Keypad ' + code.slice(6);
  // Arrows
  if (code === 'ArrowLeft')  return 'Left';
  if (code === 'ArrowRight') return 'Right';
  if (code === 'ArrowUp')    return 'Up';
  if (code === 'ArrowDown')  return 'Down';
  // Modifiers
  if (code === 'ShiftLeft')    return 'LShift';
  if (code === 'ShiftRight')   return 'RShift';
  if (code === 'ControlLeft')  return 'LCtrl';
  if (code === 'ControlRight') return 'RCtrl';
  if (code === 'AltLeft')      return 'LAlt';
  if (code === 'AltRight')     return 'RAlt';
  // Common
  if (code === 'Space')      return 'Space';
  if (code === 'Enter')      return 'Return';
  if (code === 'Tab')        return 'Tab';
  if (code === 'Escape')     return 'Escape';
  if (code === 'Backspace')  return 'Backspace';
  if (code === 'Comma')      return ',';
  if (code === 'Period')     return '.';
  if (code === 'Slash')      return '/';
  if (code === 'Semicolon')  return ';';
  return null;
}

export function ManualDrivePanel({ open, onClose, config, onChange }: ManualDrivePanelProps) {
  const queryClient = useQueryClient();
  const [assigningKey, setAssigningKey] = useState<string | null>(null);
  const [assigningKbKey, setAssigningKbKey] = useState<string | null>(null);

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

  // Keyboard capture: when assigningKbKey is set, the next keypress is recorded.
  useEffect(() => {
    if (!assigningKbKey) return;
    const handler = (e: KeyboardEvent) => {
      e.preventDefault();
      e.stopPropagation();
      if (e.key === 'Escape') { setAssigningKbKey(null); return; }
      const sdlName = keyEventToSdlName(e);
      if (!sdlName) return;
      onChange({ ...config, keyboard: { ...config.keyboard, [assigningKbKey]: sdlName } });
      setAssigningKbKey(null);
    };
    window.addEventListener('keydown', handler, true);
    return () => window.removeEventListener('keydown', handler, true);
  }, [assigningKbKey, config, onChange]);

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
            <option value="sdl2_keyboard">Keyboard</option>
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
                      {btnIdx >= 0 ? btnIdx : '—'}
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

        {/* Axis Mapping (SDL2 only) — feature:F8. Placed after the buttons
            because it is the same kind of per-device binding, and before the
            takeover thresholds because a wheel whose axes are misassigned makes
            every threshold below meaningless. */}
        {config.input_type === 'sdl2_wheel' && (
          <WheelAxisMappingSection
            mapping={config.sdl2.axis_mapping ?? DEFAULT_AXIS_MAPPING}
            deviceIndex={config.sdl2.device_index}
            onChange={(axis_mapping) =>
              onChange({ ...config, sdl2: { ...config.sdl2, axis_mapping } })
            }
          />
        )}

        {/* Takeover thresholds + misc (feature:F7 gap #6)
            These lived only in config/manual_drive.json with no control
            anywhere -- "GUI から一切触れない" was the complaint. Leaving a
            field blank sends undefined, which the backend reads as "not
            stated" and falls back to the on-disk value, so clearing a box
            restores the file's setting rather than forcing a default. */}
        <section>
          <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Takeover / Misc</h3>
          <div className="space-y-1.5">
            {OVERRIDE_NUMBER_FIELDS.map(({ key, label, step }) => (
              <div key={key} className="flex items-center gap-2">
                <span className="text-xs text-text-secondary w-28 shrink-0">{label}</span>
                <input
                  type="number"
                  step={step}
                  value={config.override_cfg?.[key] ?? ''}
                  placeholder="(config)"
                  onChange={(e) => {
                    const raw = e.target.value;
                    const next = { ...(config.override_cfg ?? DEFAULT_OVERRIDE_CFG) };
                    next[key] = raw === '' ? DEFAULT_OVERRIDE_CFG[key] : Number(raw);
                    onChange({ ...config, override_cfg: raw === '' && !config.override_cfg ? undefined : next });
                  }}
                  className="text-xs font-mono bg-glass-1 border border-glass-edge rounded px-2 py-0.5 w-24"
                />
              </div>
            ))}
            {OVERRIDE_BOOL_FIELDS.map(({ key, label }) => (
              <div key={key} className="flex items-center gap-2">
                <span className="text-xs text-text-secondary w-28 shrink-0">{label}</span>
                <input
                  type="checkbox"
                  checked={config.override_cfg?.[key] ?? DEFAULT_OVERRIDE_CFG[key]}
                  onChange={(e) => onChange({
                    ...config,
                    override_cfg: { ...(config.override_cfg ?? DEFAULT_OVERRIDE_CFG), [key]: e.target.checked },
                  })}
                  className="accent-primary"
                />
              </div>
            ))}
            <div className="flex items-center gap-2">
              <span className="text-xs text-text-secondary w-28 shrink-0">Ind. cancel angle</span>
              <input
                type="number"
                step={0.01}
                value={config.indicator_cancel_angle ?? ''}
                placeholder="(config)"
                onChange={(e) => onChange({
                  ...config,
                  indicator_cancel_angle: e.target.value === '' ? undefined : Number(e.target.value),
                })}
                className="text-xs font-mono bg-glass-1 border border-glass-edge rounded px-2 py-0.5 w-24"
              />
            </div>
            <div className="flex items-center gap-2">
              <span className="text-xs text-text-secondary w-28 shrink-0">Vehicle params</span>
              <input
                type="text"
                value={config.vehicle_params_file ?? ''}
                placeholder="real_vehicle_params.json"
                onChange={(e) => onChange({
                  ...config,
                  vehicle_params_file: e.target.value === '' ? undefined : e.target.value,
                })}
                className="text-xs font-mono bg-glass-1 border border-glass-edge rounded px-2 py-0.5 flex-1"
              />
            </div>
          </div>
        </section>

        {/* Keyboard Mapping (Keyboard input only) */}
        {config.input_type === 'sdl2_keyboard' && (
          <section>
            <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Keyboard Mapping</h3>
            <p className="text-[10px] text-text-tertiary mb-2">
              Click <span className="text-foreground">Assign</span> then press a key. Esc cancels.
              Throttle &amp; brake can be held simultaneously.
            </p>
            <div className="space-y-1.5">
              {KEYBOARD_BINDINGS.map(({ key, label }) => {
                const bound = (config.keyboard as Record<string, string | number>)[key] as string;
                const isAssigning = assigningKbKey === key;
                return (
                  <div key={key} className="flex items-center gap-2">
                    <span className="text-xs text-text-secondary w-20 shrink-0">{label}</span>
                    <span className="text-xs font-mono bg-glass-1 border border-glass-edge rounded px-2 py-0.5 min-w-[3rem] text-center">
                      {bound || '—'}
                    </span>
                    <button
                      onClick={() => setAssigningKbKey(isAssigning ? null : key)}
                      className={`text-[10px] px-2 py-0.5 rounded cursor-pointer transition-colors ${
                        isAssigning
                          ? 'bg-primary/80 text-background animate-pulse'
                          : 'bg-glass-1 border border-glass-edge text-text-tertiary hover:text-foreground hover:bg-glass-hover'
                      }`}
                    >
                      {isAssigning ? 'Press key...' : 'Assign'}
                    </button>
                  </div>
                );
              })}
            </div>

            <h4 className="text-[10px] font-bold text-text-tertiary uppercase tracking-wider mt-4 mb-1.5">Response</h4>
            <div className="space-y-2">
              <NumberInput
                label="Steer Rate (/s)"
                value={config.keyboard.steer_rate}
                step={0.1}
                onChange={(e) => setNested('keyboard', 'steer_rate', Number(e.target.value))}
              />
              <NumberInput
                label="Centering Rate (/s)"
                value={config.keyboard.centering_rate}
                step={0.1}
                onChange={(e) => setNested('keyboard', 'centering_rate', Number(e.target.value))}
              />
              <NumberInput
                label="Pedal Press Rate (/s)"
                value={config.keyboard.pedal_press_rate}
                step={0.1}
                onChange={(e) => setNested('keyboard', 'pedal_press_rate', Number(e.target.value))}
              />
              <NumberInput
                label="Pedal Release Rate (/s)"
                value={config.keyboard.pedal_release_rate}
                step={0.1}
                onChange={(e) => setNested('keyboard', 'pedal_release_rate', Number(e.target.value))}
              />
            </div>
          </section>
        )}

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
