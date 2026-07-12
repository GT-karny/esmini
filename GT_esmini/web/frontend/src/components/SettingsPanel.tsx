import { useState, type ChangeEvent } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type ExecutionDefaults, type AutoLightConfig } from '../api/client';
import { SlidePanel } from './ui/SlidePanel';
import { Button } from './ui/Button';
import { Checkbox, NumberInput, TextInput, ToggleSwitch } from './ui/Input';

interface SettingsPanelProps {
  open: boolean;
  onClose: () => void;
}

const HARDCODED_DEFAULTS: ExecutionDefaults = {
  hz: 120,
  headless: true,
  record: false,
  no_realtime: false,
  timeout: 60,
  osi: { enabled: true, ip: '127.0.0.1' },
  autolight: true,
  vehicle_physics: true,
  kinematic_mode: false,
  route_drive_mode: false,
  threads: true,
  window: { x: 60, y: 60, w: 1280, h: 720 },
};

export function SettingsPanel({ open, onClose }: SettingsPanelProps) {
  const { data: defaults } = useQuery({
    queryKey: ['execution-defaults'],
    queryFn: () => api.getExecutionDefaults(),
    enabled: open,
  });

  return (
    <SlidePanel open={open} onClose={onClose} title="Settings">
      <div className="space-y-8">
        {defaults ? (
          <SettingsForm defaults={defaults} onClose={onClose} />
        ) : (
          <div className="text-sm text-text-secondary">Loading...</div>
        )}
        <div className="border-t border-glass-edge pt-6">
          <AutoLightSection open={open} />
        </div>
      </div>
    </SlidePanel>
  );
}

function SettingsForm({ defaults, onClose }: { defaults: ExecutionDefaults; onClose: () => void }) {
  const queryClient = useQueryClient();

  // Initialize form state from props (no useEffect needed)
  const [hz, setHz] = useState(defaults.hz);
  const [headless, setHeadless] = useState(defaults.headless);
  const [record, setRecord] = useState(defaults.record);
  const [noRealtime, setNoRealtime] = useState(defaults.no_realtime);
  const [timeout, setTimeout_] = useState(defaults.timeout);
  const [osiEnabled, setOsiEnabled] = useState(defaults.osi.enabled);
  const [osiIp, setOsiIp] = useState(defaults.osi.ip);
  const [autolight, setAutolight] = useState(defaults.autolight);
  const [vehiclePhysics, setVehiclePhysics] = useState(defaults.vehicle_physics);
  const [kinematicMode, setKinematicMode] = useState(defaults.kinematic_mode);
  const [routeDriveMode, setRouteDriveMode] = useState(defaults.route_drive_mode);
  const [threads, setThreads] = useState(defaults.threads);
  const [winX, setWinX] = useState(defaults.window.x);
  const [winY, setWinY] = useState(defaults.window.y);
  const [winW, setWinW] = useState(defaults.window.w);
  const [winH, setWinH] = useState(defaults.window.h);

  const [errors, setErrors] = useState<Record<string, string>>({});

  const validate = (): boolean => {
    const errs: Record<string, string> = {};
    if (hz <= 0) errs.hz = 'Must be > 0';
    if (timeout <= 0) errs.timeout = 'Must be > 0';
    if (osiEnabled && !/^\d{1,3}(\.\d{1,3}){3}$/.test(osiIp)) {
      errs.osiIp = 'Invalid IP address';
    }
    setErrors(errs);
    return Object.keys(errs).length === 0;
  };

  const saveMutation = useMutation({
    mutationFn: (params: ExecutionDefaults) => api.updateExecutionDefaults(params),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['execution-defaults'] });
      onClose();
    },
  });

  const handleSave = () => {
    if (!validate()) return;
    saveMutation.mutate({
      hz,
      headless,
      record,
      no_realtime: noRealtime,
      timeout,
      osi: { enabled: osiEnabled, ip: osiIp },
      autolight,
      vehicle_physics: vehiclePhysics,
      kinematic_mode: kinematicMode,
      route_drive_mode: routeDriveMode,
      threads,
      window: { x: winX, y: winY, w: winW, h: winH },
    });
  };

  const handleReset = () => {
    const d = HARDCODED_DEFAULTS;
    setHz(d.hz);
    setHeadless(d.headless);
    setRecord(d.record);
    setNoRealtime(d.no_realtime);
    setTimeout_(d.timeout);
    setOsiEnabled(d.osi.enabled);
    setOsiIp(d.osi.ip);
    setAutolight(d.autolight);
    setVehiclePhysics(d.vehicle_physics);
    setKinematicMode(d.kinematic_mode);
    setRouteDriveMode(d.route_drive_mode);
    setThreads(d.threads);
    setWinX(d.window.x);
    setWinY(d.window.y);
    setWinW(d.window.w);
    setWinH(d.window.h);
    setErrors({});
  };

  return (
    <div className="space-y-6">
      {/* Section: Execution Defaults */}
      <div>
        <h3 className="text-sm font-semibold text-text-secondary mb-3">Execution Defaults</h3>

        {/* Quick Options */}
        <div className="flex flex-wrap gap-x-5 gap-y-3 mb-4">
          <Checkbox label="Headless" checked={headless} onChange={(e) => setHeadless(e.target.checked)} />
          <Checkbox label="Record" checked={record} onChange={(e) => setRecord(e.target.checked)} />
          <Checkbox label="No Realtime" checked={noRealtime} onChange={(e) => setNoRealtime(e.target.checked)} />
          <Checkbox label="AutoLight" checked={autolight} onChange={(e) => setAutolight(e.target.checked)} />
          <Checkbox label="Vehicle Physics" checked={vehiclePhysics} onChange={(e) => setVehiclePhysics(e.target.checked)} />
          <Checkbox label="Kinematic Controller" checked={kinematicMode} onChange={(e) => setKinematicMode(e.target.checked)} />
          <Checkbox label="Route Drive Controller" checked={routeDriveMode} onChange={(e) => setRouteDriveMode(e.target.checked)} />
          <Checkbox label="OSI Output" checked={osiEnabled} onChange={(e) => setOsiEnabled(e.target.checked)} />
        </div>

        {/* Frequency & Timeout */}
        <div className="grid grid-cols-2 gap-3 mb-4">
          <div>
            <NumberInput label="Frequency (Hz)" value={hz} onChange={(e) => setHz(Number(e.target.value))} />
            {errors.hz && <p className="text-destructive text-xs mt-1">{errors.hz}</p>}
          </div>
          <div>
            <NumberInput label="Timeout (s)" value={timeout} onChange={(e) => setTimeout_(Number(e.target.value))} />
            {errors.timeout && <p className="text-destructive text-xs mt-1">{errors.timeout}</p>}
          </div>
        </div>

        {/* OSI IP */}
        {osiEnabled && (
          <div className="mb-4">
            <TextInput
              label="OSI IP Address"
              value={osiIp}
              onChange={(e) => setOsiIp(e.target.value)}
              className="w-48"
            />
            {errors.osiIp && <p className="text-destructive text-xs mt-1">{errors.osiIp}</p>}
          </div>
        )}

        {/* Viewer settings (when not headless) */}
        {!headless && (
          <div className="space-y-3">
            <Checkbox
              label="Threaded viewer"
              description="(OSG viewer in separate thread)"
              checked={threads}
              onChange={(e) => setThreads(e.target.checked)}
            />
            <div>
              <h4 className="text-xs text-text-secondary mb-2">Window Position & Size</h4>
              <div className="grid grid-cols-4 gap-2">
                <NumberInput label="X" value={winX} onChange={(e) => setWinX(Number(e.target.value))} />
                <NumberInput label="Y" value={winY} onChange={(e) => setWinY(Number(e.target.value))} />
                <NumberInput label="W" value={winW} onChange={(e) => setWinW(Number(e.target.value))} />
                <NumberInput label="H" value={winH} onChange={(e) => setWinH(Number(e.target.value))} />
              </div>
            </div>
          </div>
        )}
      </div>

      {/* Future sections can be added here */}

      {/* Footer actions */}
      <div className="flex gap-3 pt-4 border-t border-glass-edge">
        <Button variant="ghost" size="sm" onClick={handleReset}>
          Reset
        </Button>
        <Button
          size="sm"
          className="flex-1"
          onClick={handleSave}
          disabled={saveMutation.isPending}
        >
          {saveMutation.isPending ? 'Saving...' : 'Save'}
        </Button>
      </div>

      {saveMutation.error && (
        <p className="text-destructive text-sm">{String(saveMutation.error)}</p>
      )}
    </div>
  );
}

/* ============================ AutoLight (F6) settings ============================ */

// Extract only the editable keys (the GET payload also carries "// ..." comment
// keys, which are preserved server-side and must not enter the form state).
function pickAutoLight(src: AutoLightConfig): AutoLightConfig {
  return {
    headlight_enabled: src.headlight_enabled,
    headlight_illuminance_lux_threshold: src.headlight_illuminance_lux_threshold,
    headlight_sun_elevation_deg: src.headlight_sun_elevation_deg,
    headlight_use_time_of_day: src.headlight_use_time_of_day,
    headlight_dusk_hour: src.headlight_dusk_hour,
    headlight_dawn_hour: src.headlight_dawn_hour,
    headlight_tunnel_enabled: src.headlight_tunnel_enabled,
    highbeam_enabled: src.highbeam_enabled,
    highbeam_range_m: src.highbeam_range_m,
    highbeam_range_hysteresis_m: src.highbeam_range_hysteresis_m,
    highbeam_corridor_half_width_m: src.highbeam_corridor_half_width_m,
    highbeam_on_delay_s: src.highbeam_on_delay_s,
    highbeam_off_delay_s: src.highbeam_off_delay_s,
  };
}

function AutoLightSection({ open }: { open: boolean }) {
  const { data: config } = useQuery({
    queryKey: ['auto-light-config'],
    queryFn: api.getAutoLightConfig,
    enabled: open,
  });
  const { data: defaults } = useQuery({
    queryKey: ['auto-light-defaults'],
    queryFn: api.getAutoLightDefaults,
    enabled: open,
  });

  return (
    <div>
      <h3 className="text-sm font-semibold text-text-secondary mb-1">Auto Headlights (F6)</h3>
      <p className="text-xs text-text-tertiary mb-4">
        Environment-driven headlights: night / tunnel low beam and automatic high beam.
      </p>
      {config && defaults ? (
        <AutoLightForm initial={config} defaults={defaults} />
      ) : (
        <div className="text-sm text-text-secondary">Loading...</div>
      )}
    </div>
  );
}

function AutoLightForm({ initial, defaults }: { initial: AutoLightConfig; defaults: AutoLightConfig }) {
  const queryClient = useQueryClient();
  const [cfg, setCfg] = useState<AutoLightConfig>(() => pickAutoLight(initial));
  const [saved, setSaved] = useState(false);

  const set = <K extends keyof AutoLightConfig>(key: K, val: AutoLightConfig[K]) => {
    setCfg((c) => ({ ...c, [key]: val }));
    setSaved(false);
  };
  const setNum = (key: keyof AutoLightConfig) => (e: ChangeEvent<HTMLInputElement>) =>
    set(key, Number(e.target.value) as AutoLightConfig[typeof key]);

  const saveMutation = useMutation({
    mutationFn: (c: AutoLightConfig) => api.updateAutoLightConfig(c),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['auto-light-config'] });
      setSaved(true);
    },
  });

  const handleReset = () => {
    setCfg(pickAutoLight(defaults));
    setSaved(false);
  };

  return (
    <div className="space-y-5">
      {/* Master switch */}
      <div>
        <ToggleSwitch
          label="Headlights enabled"
          checked={cfg.headlight_enabled}
          onChange={(v) => set('headlight_enabled', v)}
        />
        <p className="text-xs text-text-tertiary mt-1.5">
          Running with <span className="font-mono">--autolight-headlights</span> force-enables this
          even when Headlights enabled is off.
        </p>
      </div>

      {/* Night detection */}
      <div>
        <h4 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Night Detection</h4>
        <p className="text-[10px] text-text-tertiary mb-2">Priority: illuminance &gt; sun elevation &gt; time of day.</p>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Illuminance threshold (lux)" value={cfg.headlight_illuminance_lux_threshold} onChange={setNum('headlight_illuminance_lux_threshold')} />
          <NumberInput label="Sun elevation (deg)" step={0.1} value={cfg.headlight_sun_elevation_deg} onChange={setNum('headlight_sun_elevation_deg')} />
        </div>
        <div className="mt-3">
          <ToggleSwitch
            label="Use time-of-day fallback"
            checked={cfg.headlight_use_time_of_day}
            onChange={(v) => set('headlight_use_time_of_day', v)}
          />
        </div>
        {cfg.headlight_use_time_of_day && (
          <div className="grid grid-cols-2 gap-3 mt-3">
            <NumberInput label="Dusk hour (h)" step={0.5} value={cfg.headlight_dusk_hour} onChange={setNum('headlight_dusk_hour')} />
            <NumberInput label="Dawn hour (h)" step={0.5} value={cfg.headlight_dawn_hour} onChange={setNum('headlight_dawn_hour')} />
          </div>
        )}
      </div>

      {/* Tunnel */}
      <div>
        <h4 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Tunnel</h4>
        <ToggleSwitch
          label="Low beam inside tunnels"
          checked={cfg.headlight_tunnel_enabled}
          onChange={(v) => set('headlight_tunnel_enabled', v)}
        />
      </div>

      {/* High beam */}
      <div>
        <h4 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">High Beam</h4>
        <ToggleSwitch
          label="Auto high beam"
          description="(when no vehicle ahead)"
          checked={cfg.highbeam_enabled}
          onChange={(v) => set('highbeam_enabled', v)}
        />
        {cfg.highbeam_enabled && (
          <div className="grid grid-cols-2 gap-3 mt-3">
            <NumberInput label="Detection range (m)" step={0.5} value={cfg.highbeam_range_m} onChange={setNum('highbeam_range_m')} />
            <NumberInput label="Range hysteresis (m)" step={0.5} value={cfg.highbeam_range_hysteresis_m} onChange={setNum('highbeam_range_hysteresis_m')} />
            <NumberInput label="Corridor half-width (m)" step={0.5} value={cfg.highbeam_corridor_half_width_m} onChange={setNum('highbeam_corridor_half_width_m')} />
            <NumberInput label="On delay (s)" step={0.1} value={cfg.highbeam_on_delay_s} onChange={setNum('highbeam_on_delay_s')} />
            <NumberInput label="Off delay (s)" step={0.1} value={cfg.highbeam_off_delay_s} onChange={setNum('highbeam_off_delay_s')} />
          </div>
        )}
      </div>

      {/* Footer actions */}
      <div className="flex items-center gap-3 pt-2">
        <Button variant="ghost" size="sm" onClick={handleReset}>
          Reset to defaults
        </Button>
        <Button
          size="sm"
          className="flex-1"
          onClick={() => saveMutation.mutate(cfg)}
          disabled={saveMutation.isPending}
        >
          {saveMutation.isPending ? 'Saving...' : 'Save'}
        </Button>
      </div>

      {saved && !saveMutation.isPending && (
        <p className="text-xs text-primary">Saved to config/auto_light.json.</p>
      )}
      {saveMutation.error && (
        <p className="text-destructive text-sm">{String(saveMutation.error)}</p>
      )}
    </div>
  );
}
