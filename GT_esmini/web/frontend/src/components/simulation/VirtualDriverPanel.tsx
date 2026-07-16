import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { SlidePanel } from '../ui/SlidePanel';
import { NumberInput, SelectInput, ToggleSwitch } from '../ui/Input';
import { Button } from '../ui/Button';
import { api, type VirtualDriverConfig } from '../../api/client';

interface VirtualDriverPanelProps {
  open: boolean;
  onClose: () => void;
}

// Editable keys only — the GET payload also carries "_..." comment keys and the
// runner-owned input_type/input_port/input_transport/vehicle_params_file keys
// (see virtual_driver_api.py _EXCLUDED_KEYS); those must never enter form state
// or be sent back on PUT (the backend rejects them with 422).
const EDITABLE_KEYS = [
  'policy_lead_enabled', 'policy_traffic_light_enabled', 'policy_stop_yield_enabled',
  'policy_conflict_enabled', 'policy_crosswalk_enabled', 'policy_junction_priority_enabled',
  'horizon_s', 'short_dt', 'max_lateral_accel', 'comfort_decel', 'comfort_jerk',
  'scan_distance', 'scan_step', 'turn_speed', 'min_turn_speed', 'stop_band', 'respect_speed_limit',
  'lookahead_gain', 'min_lookahead', 'max_lookahead', 'max_steer_angle', 'steering_sign',
  'speed_kp', 'speed_ki', 'speed_kd', 'control_point_offset', 'control_point_min_speed',
  'indicator_lead_time', 'indicator_min_on_time',
  'idm_time_headway', 'idm_min_gap', 'idm_max_accel', 'idm_comfort_decel', 'idm_desired_speed',
  'idm_lookahead', 'idm_lateral_tol', 'idm_target_horizon',
  'tl_lookahead', 'tl_yellow_decel', 'tl_stop_margin',
  'sign_lookahead', 'stop_hold_time', 'stop_detect_speed', 'stop_line_tol',
  'creep_speed', 'creep_advance', 'yield_creep_speed', 'sign_stop_margin',
  'conflict_lookahead', 'conflict_step', 'conflict_lane_margin', 'conflict_standoff',
  'conflict_release_buffer', 'conflict_pet', 'conflict_nominal_speed',
  'conflict_min_cross_angle_deg', 'conflict_other_min_speed', 'conflict_area_eps',
  'crosswalk_lookahead', 'crosswalk_step', 'crosswalk_standoff', 'crosswalk_wait_margin',
  'crosswalk_yield_to_waiting', 'crosswalk_ped_signal_aware', 'crosswalk_signal_link_radius',
  'crosswalk_release_lateral_margin',
  'override_enabled', 'override_button', 'steering_threshold', 'throttle_threshold',
  'brake_threshold', 'auto_return_timeout', 'override_lateral', 'override_longitudinal',
] as const satisfies readonly (keyof VirtualDriverConfig)[];

function pickEditable(src: VirtualDriverConfig): VirtualDriverConfig {
  const out: VirtualDriverConfig = {};
  for (const k of EDITABLE_KEYS) {
    (out as Record<string, unknown>)[k] = src[k];
  }
  return out;
}

const POLICY_ROWS: { key: keyof VirtualDriverConfig; label: string }[] = [
  { key: 'policy_lead_enabled', label: 'Lead vehicle follow (IDM)' },
  { key: 'policy_traffic_light_enabled', label: 'Traffic light' },
  { key: 'policy_stop_yield_enabled', label: 'Stop / Yield sign' },
  { key: 'policy_conflict_enabled', label: 'Conflict corridor' },
  { key: 'policy_crosswalk_enabled', label: 'Crosswalk pedestrian' },
  { key: 'policy_junction_priority_enabled', label: 'Junction priority' },
];

export function VirtualDriverPanel({ open, onClose }: VirtualDriverPanelProps) {
  const { data: config } = useQuery({
    queryKey: ['virtual-driver-config'],
    queryFn: api.getVirtualDriverConfig,
    enabled: open,
  });
  const { data: defaults } = useQuery({
    queryKey: ['virtual-driver-defaults'],
    queryFn: api.getVirtualDriverDefaults,
    enabled: open,
  });

  return (
    <SlidePanel open={open} onClose={onClose} title="Virtual Driver Settings">
      {config && defaults ? (
        <VirtualDriverForm initial={config} defaults={defaults} />
      ) : (
        <div className="text-sm text-text-secondary">Loading...</div>
      )}
    </SlidePanel>
  );
}

function VirtualDriverForm({ initial, defaults }: { initial: VirtualDriverConfig; defaults: VirtualDriverConfig }) {
  const queryClient = useQueryClient();
  const [cfg, setCfg] = useState<VirtualDriverConfig>(() => pickEditable(initial));
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [saved, setSaved] = useState(false);

  const set = <K extends keyof VirtualDriverConfig>(key: K, val: VirtualDriverConfig[K]) => {
    setCfg((c) => ({ ...c, [key]: val }));
    setSaved(false);
  };
  const setNum = (key: keyof VirtualDriverConfig) => (e: { target: { value: string } }) =>
    set(key, Number(e.target.value) as VirtualDriverConfig[typeof key]);

  const saveMutation = useMutation({
    mutationFn: (c: VirtualDriverConfig) => api.updateVirtualDriverConfig(pickEditable(c)),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['virtual-driver-config'] });
      setSaved(true);
    },
  });

  const handleReset = () => {
    setCfg(pickEditable(defaults));
    setSaved(false);
  };

  return (
    <div className="space-y-6">
      <p className="text-xs text-text-tertiary">
        Persisted to <span className="font-mono">config/virtual_driver.json</span> — applies to
        every Virtual Driver run. A scenario's own <span className="font-mono">policies</span>{' '}
        property additively enables further policies on top of these.
      </p>

      {/* Policies */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">
          Traffic Policies (Phase 3)
        </h3>
        <div className="space-y-2">
          {POLICY_ROWS.map(({ key, label }) => (
            <ToggleSwitch
              key={key}
              label={label}
              checked={Boolean(cfg[key])}
              onChange={(v) => set(key, v as VirtualDriverConfig[typeof key])}
            />
          ))}
        </div>
      </section>

      {/* Lead / IDM */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Lead / IDM</h3>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Time headway (s)" step={0.1} value={cfg.idm_time_headway} onChange={setNum('idm_time_headway')} />
          <NumberInput label="Min gap (m)" step={0.1} value={cfg.idm_min_gap} onChange={setNum('idm_min_gap')} />
          <NumberInput label="Max accel (m/s²)" step={0.1} value={cfg.idm_max_accel} onChange={setNum('idm_max_accel')} />
          <NumberInput label="Comfort decel (m/s²)" step={0.1} value={cfg.idm_comfort_decel} onChange={setNum('idm_comfort_decel')} />
          <NumberInput label="Desired speed (km/h)" step={1} value={cfg.idm_desired_speed} onChange={setNum('idm_desired_speed')} />
          <NumberInput label="Lookahead (m)" step={1} value={cfg.idm_lookahead} onChange={setNum('idm_lookahead')} />
          <NumberInput label="Lateral tolerance (m)" step={0.1} value={cfg.idm_lateral_tol} onChange={setNum('idm_lateral_tol')} />
          <NumberInput label="Target horizon (s)" step={0.1} value={cfg.idm_target_horizon} onChange={setNum('idm_target_horizon')} />
        </div>
      </section>

      {/* Traffic light */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Traffic Light</h3>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Lookahead (m)" step={1} value={cfg.tl_lookahead} onChange={setNum('tl_lookahead')} />
          <NumberInput label="Yellow decel (m/s²)" step={0.1} value={cfg.tl_yellow_decel} onChange={setNum('tl_yellow_decel')} />
          <NumberInput label="Stop margin (m)" step={0.1} value={cfg.tl_stop_margin} onChange={setNum('tl_stop_margin')} />
        </div>
      </section>

      {/* Stop / Yield */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Stop / Yield</h3>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Sign lookahead (m)" step={1} value={cfg.sign_lookahead} onChange={setNum('sign_lookahead')} />
          <NumberInput label="Stop hold time (s)" step={0.1} value={cfg.stop_hold_time} onChange={setNum('stop_hold_time')} />
          <NumberInput label="Stop detect speed (m/s)" step={0.1} value={cfg.stop_detect_speed} onChange={setNum('stop_detect_speed')} />
          <NumberInput label="Stop line tolerance (m)" step={0.1} value={cfg.stop_line_tol} onChange={setNum('stop_line_tol')} />
          <NumberInput label="Creep speed (m/s)" step={0.1} value={cfg.creep_speed} onChange={setNum('creep_speed')} />
          <NumberInput label="Creep advance (m)" step={0.1} value={cfg.creep_advance} onChange={setNum('creep_advance')} />
          <NumberInput label="Yield creep speed (m/s)" step={0.1} value={cfg.yield_creep_speed} onChange={setNum('yield_creep_speed')} />
          <NumberInput label="Sign stop margin (m)" step={0.1} value={cfg.sign_stop_margin} onChange={setNum('sign_stop_margin')} />
        </div>
      </section>

      {/* Conflict */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Conflict Corridor</h3>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Lookahead (m)" step={1} value={cfg.conflict_lookahead} onChange={setNum('conflict_lookahead')} />
          <NumberInput label="Step (m)" step={0.1} value={cfg.conflict_step} onChange={setNum('conflict_step')} />
          <NumberInput label="Lane margin (m)" step={0.05} value={cfg.conflict_lane_margin} onChange={setNum('conflict_lane_margin')} />
          <NumberInput label="Standoff (m)" step={0.1} value={cfg.conflict_standoff} onChange={setNum('conflict_standoff')} />
          <NumberInput label="Release buffer (m)" step={0.1} value={cfg.conflict_release_buffer} onChange={setNum('conflict_release_buffer')} />
          <NumberInput label="PET (s)" step={0.1} value={cfg.conflict_pet} onChange={setNum('conflict_pet')} />
          <NumberInput label="Nominal speed (m/s)" step={0.1} value={cfg.conflict_nominal_speed} onChange={setNum('conflict_nominal_speed')} />
          <NumberInput label="Min cross angle (deg)" step={1} value={cfg.conflict_min_cross_angle_deg} onChange={setNum('conflict_min_cross_angle_deg')} />
          <NumberInput label="Other min speed (m/s)" step={0.1} value={cfg.conflict_other_min_speed} onChange={setNum('conflict_other_min_speed')} />
          <NumberInput label="Area epsilon" step={0.01} value={cfg.conflict_area_eps} onChange={setNum('conflict_area_eps')} />
        </div>
      </section>

      {/* Crosswalk */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">Crosswalk</h3>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Lookahead (m)" step={1} value={cfg.crosswalk_lookahead} onChange={setNum('crosswalk_lookahead')} />
          <NumberInput label="Step (m)" step={0.1} value={cfg.crosswalk_step} onChange={setNum('crosswalk_step')} />
          <NumberInput label="Standoff (m)" step={0.1} value={cfg.crosswalk_standoff} onChange={setNum('crosswalk_standoff')} />
          <NumberInput label="Wait margin (m)" step={0.1} value={cfg.crosswalk_wait_margin} onChange={setNum('crosswalk_wait_margin')} />
          <NumberInput label="Signal link radius (m)" step={0.5} value={cfg.crosswalk_signal_link_radius} onChange={setNum('crosswalk_signal_link_radius')} />
          <NumberInput label="Release lateral margin (m)" step={0.1} value={cfg.crosswalk_release_lateral_margin} onChange={setNum('crosswalk_release_lateral_margin')} />
        </div>
        <div className="mt-3 space-y-2">
          <ToggleSwitch
            label="Yield to waiting pedestrian"
            checked={Boolean(cfg.crosswalk_yield_to_waiting)}
            onChange={(v) => set('crosswalk_yield_to_waiting', v)}
          />
          <ToggleSwitch
            label="Pedestrian-signal aware"
            checked={Boolean(cfg.crosswalk_ped_signal_aware)}
            onChange={(v) => set('crosswalk_ped_signal_aware', v)}
          />
        </div>
      </section>

      {/* Advanced (collapsible, collapsed by default) */}
      <section>
        <button
          onClick={() => setShowAdvanced(!showAdvanced)}
          className="flex items-center gap-1.5 text-xs text-text-tertiary hover:text-text-secondary transition-colors cursor-pointer mb-1.5"
        >
          <svg
            className={`w-3 h-3 transition-transform ${showAdvanced ? 'rotate-90' : ''}`}
            viewBox="0 0 16 16"
            fill="currentColor"
          >
            <path d="M6 3l5 5-5 5V3z" />
          </svg>
          Advanced
        </button>

        {showAdvanced && (
          <div className="space-y-5">
            {/* Planner */}
            <div>
              <h4 className="text-[10px] font-bold text-text-tertiary uppercase tracking-wider mb-1.5">Planner</h4>
              <div className="grid grid-cols-2 gap-3">
                <NumberInput label="Horizon (s)" step={0.1} value={cfg.horizon_s} onChange={setNum('horizon_s')} />
                <NumberInput label="Short dt (s)" step={0.01} value={cfg.short_dt} onChange={setNum('short_dt')} />
                <NumberInput label="Max lateral accel (m/s²)" step={0.1} value={cfg.max_lateral_accel} onChange={setNum('max_lateral_accel')} />
                <NumberInput label="Comfort decel (m/s²)" step={0.1} value={cfg.comfort_decel} onChange={setNum('comfort_decel')} />
                <NumberInput label="Comfort jerk (m/s³)" step={0.1} value={cfg.comfort_jerk} onChange={setNum('comfort_jerk')} />
                <NumberInput label="Scan distance (m)" step={1} value={cfg.scan_distance} onChange={setNum('scan_distance')} />
                <NumberInput label="Scan step (m)" step={0.1} value={cfg.scan_step} onChange={setNum('scan_step')} />
                <NumberInput label="Turn speed (m/s)" step={0.1} value={cfg.turn_speed} onChange={setNum('turn_speed')} />
                <NumberInput label="Min turn speed (m/s)" step={0.1} value={cfg.min_turn_speed} onChange={setNum('min_turn_speed')} />
                <NumberInput label="Stop band (m)" step={0.1} value={cfg.stop_band} onChange={setNum('stop_band')} />
              </div>
              <div className="mt-3">
                <ToggleSwitch
                  label="Respect speed limit"
                  checked={Boolean(cfg.respect_speed_limit)}
                  onChange={(v) => set('respect_speed_limit', v)}
                />
              </div>
            </div>

            {/* Driver model */}
            <div>
              <h4 className="text-[10px] font-bold text-text-tertiary uppercase tracking-wider mb-1.5">Driver Model</h4>
              <div className="grid grid-cols-2 gap-3">
                <NumberInput label="Lookahead gain" step={0.05} value={cfg.lookahead_gain} onChange={setNum('lookahead_gain')} />
                <NumberInput label="Min lookahead (m)" step={0.5} value={cfg.min_lookahead} onChange={setNum('min_lookahead')} />
                <NumberInput label="Max lookahead (m)" step={0.5} value={cfg.max_lookahead} onChange={setNum('max_lookahead')} />
                <NumberInput label="Max steer angle (rad)" step={0.01} value={cfg.max_steer_angle} onChange={setNum('max_steer_angle')} />
                <NumberInput label="Steering sign" step={1} value={cfg.steering_sign} onChange={setNum('steering_sign')} />
                <NumberInput label="Speed Kp" step={0.05} value={cfg.speed_kp} onChange={setNum('speed_kp')} />
                <NumberInput label="Speed Ki" step={0.05} value={cfg.speed_ki} onChange={setNum('speed_ki')} />
                <NumberInput label="Speed Kd" step={0.05} value={cfg.speed_kd} onChange={setNum('speed_kd')} />
                <NumberInput label="Control point offset (m)" step={0.1} value={cfg.control_point_offset} onChange={setNum('control_point_offset')} />
                <NumberInput label="Control point min speed (m/s)" step={0.1} value={cfg.control_point_min_speed} onChange={setNum('control_point_min_speed')} />
              </div>
            </div>

            {/* Indicator */}
            <div>
              <h4 className="text-[10px] font-bold text-text-tertiary uppercase tracking-wider mb-1.5">Indicator</h4>
              <div className="grid grid-cols-2 gap-3">
                <NumberInput label="Lead time (s)" step={0.1} value={cfg.indicator_lead_time} onChange={setNum('indicator_lead_time')} />
                <NumberInput label="Min on time (s)" step={0.1} value={cfg.indicator_min_on_time} onChange={setNum('indicator_min_on_time')} />
              </div>
            </div>

            {/* Override */}
            <div>
              <h4 className="text-[10px] font-bold text-text-tertiary uppercase tracking-wider mb-1.5">Manual Override</h4>
              <div className="space-y-2 mb-3">
                <ToggleSwitch
                  label="Override enabled"
                  checked={Boolean(cfg.override_enabled)}
                  onChange={(v) => set('override_enabled', v)}
                />
                <ToggleSwitch
                  label="Override button"
                  checked={Boolean(cfg.override_button)}
                  onChange={(v) => set('override_button', v)}
                />
              </div>
              <div className="grid grid-cols-2 gap-3">
                <NumberInput label="Steering threshold" step={0.01} value={cfg.steering_threshold} onChange={setNum('steering_threshold')} />
                <NumberInput label="Throttle threshold" step={0.01} value={cfg.throttle_threshold} onChange={setNum('throttle_threshold')} />
                <NumberInput label="Brake threshold" step={0.01} value={cfg.brake_threshold} onChange={setNum('brake_threshold')} />
                <NumberInput label="Auto return timeout (s)" step={0.1} value={cfg.auto_return_timeout} onChange={setNum('auto_return_timeout')} />
              </div>
              <div className="grid grid-cols-2 gap-3 mt-3">
                <SelectInput
                  label="Lateral"
                  value={cfg.override_lateral}
                  onChange={(e) => set('override_lateral', e.target.value as VirtualDriverConfig['override_lateral'])}
                >
                  <option value="manual">Manual</option>
                  <option value="scenario">Scenario</option>
                </SelectInput>
                <SelectInput
                  label="Longitudinal"
                  value={cfg.override_longitudinal}
                  onChange={(e) => set('override_longitudinal', e.target.value as VirtualDriverConfig['override_longitudinal'])}
                >
                  <option value="manual">Manual</option>
                  <option value="scenario">Scenario</option>
                </SelectInput>
              </div>
            </div>
          </div>
        )}
      </section>

      {/* Footer actions */}
      <div className="flex items-center gap-3 pt-2 border-t border-glass-edge">
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
        <p className="text-xs text-primary">Saved to config/virtual_driver.json.</p>
      )}
      {saveMutation.error && (
        <p className="text-destructive text-sm">{String(saveMutation.error)}</p>
      )}
    </div>
  );
}
