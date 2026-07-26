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
  'policy_aeb_enabled',
  'horizon_s', 'short_dt', 'max_lateral_accel', 'comfort_decel', 'emergency_decel', 'comfort_jerk',
  'scan_distance', 'scan_step', 'turn_speed', 'min_turn_speed', 'stop_band', 'respect_speed_limit',
  'lookahead_gain', 'min_lookahead', 'max_lookahead', 'max_steer_angle', 'steering_sign',
  'speed_kp', 'speed_ki', 'speed_kd',
  // feature:F7 AD steering safety envelope (clamps AD's commanded steering only).
  'ad_steering_envelope_enabled', 'a_lat_max_steer', 'yaw_rate_max', 'steer_rate_max', 'envelope_v_floor',
  'ad_steering_envelope_steer_jerk_max',
  'control_point_offset', 'control_point_min_speed',
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
  'aeb_ttc_threshold', 'aeb_lateral_tol', 'aeb_min_a_req', 'aeb_stop_margin',
  'override_enabled', 'override_button', 'steering_threshold', 'throttle_threshold',
  'brake_threshold', 'auto_return_timeout', 'override_lateral', 'override_longitudinal',
  // SDL2 wheel bindings (feature:F7 + related). Only consumed under sdl2_wheel input.
  'sdl2_override_button', 'sdl2_indicator_left_button', 'sdl2_indicator_right_button',
  'sdl2_upshift_button', 'sdl2_downshift_button',
  'sdl2_headlight_button', 'sdl2_high_beam_button', 'sdl2_fog_light_button',
  'sdl2_hazard_button', 'sdl2_auto_resume_button',
  // feature:F7 (F7b) FFB target-tracking (AD wheel following + torque-proxy override).
  'ffb_target_track_enabled', 'ffb_target_track_kp', 'ffb_target_track_kd',
  'ffb_target_track_max_force', 'ffb_target_track_hard_stop_zone',
  'ffb_target_track_friction_ff', 'ffb_target_track_friction_ff_eps',
  'ffb_target_track_feel_ratio',
  'ffb_target_track_override_steer_force_threshold',
  'ffb_target_track_override_steer_dev_threshold',
  'ffb_target_track_override_sustain_time',
  'ffb_target_track_override_target_rate_gate',
  'ffb_target_track_override_position_error_rate_gate',
  'ffb_target_track_override_residual_threshold',
  'ffb_target_track_override_residual_reanchor_tau',
  'ffb_target_track_override_shadow_breakaway',
  'ffb_target_track_override_shadow_breakaway_left',
  'ffb_target_track_override_shadow_breakaway_right',
  'ffb_target_track_override_shadow_motion_epsilon',
  'ffb_target_track_override_shadow_kinetic',
  'ffb_target_track_override_shadow_force_to_velocity',
  'ffb_target_track_override_shadow_v_max',
  'ffb_target_track_override_shadow_onset_grace',
  'ffb_target_track_override_shadow_dead_time',
  'ffb_target_track_override_shadow_velocity_tau',
  'ffb_target_track_override_shadow_motion_rate_eps',
  // Deliberately NOT exposed: ffb_safety_max_saturation_seconds /
  // ffb_safety_max_runtime_seconds / ffb_safety_saturation_ratio. These are an
  // unattended-run watchdog (default 0=disabled); enabling them for an
  // interactive session would force-terminate a run mid-drive. Backend keeps
  // them out of _NUMBER_KEYS/KNOWN_KEYS too (virtual_driver_api.py).
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
  { key: 'policy_aeb_enabled', label: 'AEB (emergency braking)' },
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
        <br />
        <span className="text-text-tertiary">
          Read once at the start of each run — there is no hot-reload. A run already in
          progress keeps whatever was loaded when it started; edits here take effect on the
          <em> next</em> run.
        </span>
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

      {/* AEB */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">AEB (Emergency Braking)</h3>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="TTC threshold (s)" step={0.1} value={cfg.aeb_ttc_threshold} onChange={setNum('aeb_ttc_threshold')} />
          <NumberInput label="Lateral tolerance (m)" step={0.1} value={cfg.aeb_lateral_tol} onChange={setNum('aeb_lateral_tol')} />
          <NumberInput label="Min required decel (m/s²)" step={0.1} value={cfg.aeb_min_a_req} onChange={setNum('aeb_min_a_req')} />
          <NumberInput label="Stop margin (m)" step={0.1} value={cfg.aeb_stop_margin} onChange={setNum('aeb_stop_margin')} />
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
                <NumberInput label="Emergency decel (m/s²)" step={0.1} value={cfg.emergency_decel} onChange={setNum('emergency_decel')} />
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

      {/* SDL2 Wheel Bindings — only consumed when input_type=sdl2_wheel. Values
          are raw SDL joystick button IDs; -1 = unassigned. feature:F7 exposed
          these so bindings can be remapped from the GUI without a rebuild. */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">
          Wheel Bindings (SDL2)
        </h3>
        <p className="text-[10px] text-text-tertiary mb-2 leading-tight">
          Raw SDL joystick button IDs. Only used with the sdl2_wheel input source.
          -1 = unassigned. Run the SDL2 probe once to learn your device's IDs.
        </p>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Override" step={1} value={cfg.sdl2_override_button ?? 0}
            onChange={setNum('sdl2_override_button')} />
          <NumberInput label="Auto Resume (F7)" step={1} value={cfg.sdl2_auto_resume_button ?? -1}
            onChange={setNum('sdl2_auto_resume_button')} />
          <NumberInput label="Indicator Left" step={1} value={cfg.sdl2_indicator_left_button ?? 7}
            onChange={setNum('sdl2_indicator_left_button')} />
          <NumberInput label="Indicator Right" step={1} value={cfg.sdl2_indicator_right_button ?? 6}
            onChange={setNum('sdl2_indicator_right_button')} />
          <NumberInput label="Upshift" step={1} value={cfg.sdl2_upshift_button ?? 4}
            onChange={setNum('sdl2_upshift_button')} />
          <NumberInput label="Downshift" step={1} value={cfg.sdl2_downshift_button ?? 5}
            onChange={setNum('sdl2_downshift_button')} />
          <NumberInput label="Headlight" step={1} value={cfg.sdl2_headlight_button ?? -1}
            onChange={setNum('sdl2_headlight_button')} />
          <NumberInput label="High Beam" step={1} value={cfg.sdl2_high_beam_button ?? -1}
            onChange={setNum('sdl2_high_beam_button')} />
          <NumberInput label="Fog Light" step={1} value={cfg.sdl2_fog_light_button ?? -1}
            onChange={setNum('sdl2_fog_light_button')} />
          <NumberInput label="Hazard" step={1} value={cfg.sdl2_hazard_button ?? -1}
            onChange={setNum('sdl2_hazard_button')} />
        </div>
      </section>

      {/* AD Wheel Following (FFB target-tracking) — feature:F7 (F7b).
          SDL2 wheel only; the servo drives the physical wheel to follow AD's
          commanded angle, and a sustained driver push back against it latches
          to MANUAL (torque-proxy override). Default OFF so existing VD
          behavior is bit-identical. Units are NORMALIZED axis-fraction
          (spike-calibrated, scripts/ffb_spike/README.md §1e/§2e). */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">
          AD Wheel Following (FFB)
        </h3>
        <p className="text-[10px] text-text-tertiary mb-2 leading-tight">
          Drives the physical wheel to follow AD's commanded steering (SDL2 only).
          A sustained push against the servo latches to MANUAL. Default OFF;
          calibrate thresholds on the real bench.
          <br />
          <span className="text-text-tertiary">
            Friction feed-forward cancels the wheel's static friction so small
            steering commands can actually move it — keep it BELOW the wheel's
            breakaway force (G29 measured 0.170) or the wheel can creep on its
            own. Road feel scales SAT/friction/damping while AD is steering
            (0 = servo owns the wheel); full feel always returns the moment an
            override latches to MANUAL.
          </span>
        </p>
        <div className="mb-3">
          <ToggleSwitch
            label="Enable AD wheel following"
            checked={Boolean(cfg.ffb_target_track_enabled)}
            onChange={(v) => set('ffb_target_track_enabled', v)}
          />
        </div>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Kp (servo P gain)" step={0.1}
            value={cfg.ffb_target_track_kp ?? 4.0}
            onChange={setNum('ffb_target_track_kp')} />
          <NumberInput label="Kd (servo D gain)" step={0.05}
            value={cfg.ffb_target_track_kd ?? 0.35}
            onChange={setNum('ffb_target_track_kd')} />
          <NumberInput label="Max force (0..1)" step={0.05}
            value={cfg.ffb_target_track_max_force ?? 0.6}
            onChange={setNum('ffb_target_track_max_force')} />
          <NumberInput label="Hard-stop zone (|axis|)" step={0.01}
            value={cfg.ffb_target_track_hard_stop_zone ?? 0.85}
            onChange={setNum('ffb_target_track_hard_stop_zone')} />
          <NumberInput label="Friction feed-forward (< breakaway)" step={0.01}
            value={cfg.ffb_target_track_friction_ff ?? 0.15}
            onChange={setNum('ffb_target_track_friction_ff')} />
          <NumberInput label="Friction FF knee (axis-frac)" step={0.005}
            value={cfg.ffb_target_track_friction_ff_eps ?? 0.01}
            onChange={setNum('ffb_target_track_friction_ff_eps')} />
          <NumberInput label="Road feel while AD steers (0..1)" step={0.05}
            value={cfg.ffb_target_track_feel_ratio ?? 0.0}
            onChange={setNum('ffb_target_track_feel_ratio')} />
          <NumberInput label="Override residual threshold (|axis|)" step={0.01}
            value={cfg.ffb_target_track_override_residual_threshold ?? 0.08}
            onChange={setNum('ffb_target_track_override_residual_threshold')} />
          <NumberInput label="Override sustain (s)" step={0.01}
            value={cfg.ffb_target_track_override_sustain_time ?? 0.10}
            onChange={setNum('ffb_target_track_override_sustain_time')} />
          <NumberInput label="Shadow re-anchor tau (s)" step={0.05}
            value={cfg.ffb_target_track_override_residual_reanchor_tau ?? 1.5}
            onChange={setNum('ffb_target_track_override_residual_reanchor_tau')} />
        </div>
        <p className="text-[10px] text-text-tertiary mt-2 leading-tight">
          Takeover is detected by RESIDUAL, not by direction. Each frame a
          shadow model integrates the force actually delivered to the wheel
          through the device's measured friction/velocity characteristic
          (below) to predict where an untouched wheel would be. If the real
          wheel diverges from that prediction by more than the residual
          threshold for the sustain time, the lateral domain latches MANUAL.
          Because it compares the wheel against a prediction of itself, it
          detects a driver holding SHORT of the AD command (the natural
          reaction to an over-aggressive command, and undetectable by the
          previous direction-based test) just as readily as overtaking or
          countersteering it — and it stays valid while AD is actively
          steering. A stuck wheel predicts itself: below breakaway the shadow
          does not move either, so the residual stays at zero.
          Re-anchor tau bleeds accumulated model error back toward the
          measurement while no intervention is present; the shadow is an
          integrator, so this is what keeps long drives from drifting into a
          false latch. Release is unchanged: AUTO_RESUME button only.
        </p>

        {/* Shadow-plant constants: properties of the WHEEL, not the
            controller. G29-measured (scripts/ffb_spike/CHARACTERIZATION.md
            §2/§3); a different device must be re-calibrated. */}
        <div className="grid grid-cols-2 gap-3 mt-3">
          <NumberInput label="Shadow breakaway, unconditional (force)" step={0.01}
            value={cfg.ffb_target_track_override_shadow_breakaway ?? 0.21}
            onChange={setNum('ffb_target_track_override_shadow_breakaway')} />
          <NumberInput label="Breakaway band bottom, push left (force>0)" step={0.005}
            value={cfg.ffb_target_track_override_shadow_breakaway_left ?? 0.170}
            onChange={setNum('ffb_target_track_override_shadow_breakaway_left')} />
          <NumberInput label="Breakaway band bottom, push right (force<0)" step={0.005}
            value={cfg.ffb_target_track_override_shadow_breakaway_right ?? 0.190}
            onChange={setNum('ffb_target_track_override_shadow_breakaway_right')} />
          <NumberInput label="Shadow kinetic floor (force)" step={0.01}
            value={cfg.ffb_target_track_override_shadow_kinetic ?? 0.16}
            onChange={setNum('ffb_target_track_override_shadow_kinetic')} />
          <NumberInput label="Observed-motion epsilon (|axis|)" step={0.005}
            value={cfg.ffb_target_track_override_shadow_motion_epsilon ?? 0.01}
            onChange={setNum('ffb_target_track_override_shadow_motion_epsilon')} />
          <NumberInput label="Shadow force→velocity ((axis-frac/s)/force)" step={0.05}
            value={cfg.ffb_target_track_override_shadow_force_to_velocity ?? 3.35}
            onChange={setNum('ffb_target_track_override_shadow_force_to_velocity')} />
          <NumberInput label="Shadow v_max (axis-frac/s)" step={0.05}
            value={cfg.ffb_target_track_override_shadow_v_max ?? 1.0}
            onChange={setNum('ffb_target_track_override_shadow_v_max')} />
          <NumberInput label="Wheel dead time (s)" step={0.001}
            value={cfg.ffb_target_track_override_shadow_dead_time ?? 0.041}
            onChange={setNum('ffb_target_track_override_shadow_dead_time')} />
          <NumberInput label="Wheel velocity time constant (s)" step={0.001}
            value={cfg.ffb_target_track_override_shadow_velocity_tau ?? 0.018}
            onChange={setNum('ffb_target_track_override_shadow_velocity_tau')} />
          <NumberInput label="Motion-onset grace (s)" step={0.01}
            value={cfg.ffb_target_track_override_shadow_onset_grace ?? 0.05}
            onChange={setNum('ffb_target_track_override_shadow_onset_grace')} />
          <NumberInput label="Motion rate epsilon (axis-frac/s)" step={0.005}
            value={cfg.ffb_target_track_override_shadow_motion_rate_eps ?? 0.02}
            onChange={setNum('ffb_target_track_override_shadow_motion_rate_eps')} />
        </div>
        <p className="text-[10px] text-text-tertiary mt-2 leading-tight">
          Shadow plant — measured properties of the physical wheel, not of the
          controller; a different wheel must be re-calibrated. What starts a
          wheel at rest is BREAKAWAY, and on a G29 it is a direction-asymmetric
          band: 0.170–0.210 pushing left, 0.190–0.210 pushing right. The kinetic
          floor (0.16) is <em>not</em> an onset threshold — it is where an
          already-moving wheel stops and the intercept of v ≈ 3.35·(|f|−0.16),
          which saturates near 1.0/s. The shadow starts unconditionally at the
          top of the band, and at the band bottom only once the real axis is
          observed to move, which is what pins down where in the band this
          particular wheel sits. Dead time and the velocity time constant are
          the servo's measured delay/lag (dead time first, then a first-order
          approach to the force→velocity line above) — they shape when the
          shadow's prediction starts moving, not just how fast. Motion-onset
          grace briefly tolerates the wheel lagging the shadow right as motion
          starts (before the servo has caught up) so that catch-up alone does
          not read as a takeover; motion rate epsilon is the axis-rate floor
          below which the wheel counts as still (the rate-domain twin of the
          observed-motion epsilon above, which is position-domain).
        </p>

        <div className="grid grid-cols-2 gap-3 mt-3">
          <NumberInput label="Force threshold (diagnostic only)" step={0.01}
            value={cfg.ffb_target_track_override_steer_force_threshold ?? 0.20}
            onChange={setNum('ffb_target_track_override_steer_force_threshold')} />
          <NumberInput label="Deviation threshold (diagnostic only)" step={0.005}
            value={cfg.ffb_target_track_override_steer_dev_threshold ?? 0.04}
            onChange={setNum('ffb_target_track_override_steer_dev_threshold')} />
          <NumberInput label="Target rate gate (diagnostic only)" step={0.05}
            value={cfg.ffb_target_track_override_target_rate_gate ?? 0.30}
            onChange={setNum('ffb_target_track_override_target_rate_gate')} />
          <NumberInput label="Position-error rate gate (diagnostic only)" step={0.05}
            value={cfg.ffb_target_track_override_position_error_rate_gate ?? 0.10}
            onChange={setNum('ffb_target_track_override_position_error_rate_gate')} />
        </div>
        <p className="text-[10px] text-text-tertiary mt-2 leading-tight">
          These four no longer gate the latch. They are still evaluated and
          reported in the override telemetry (over_force / over_dev /
          moving_target / tracking_transient) because they are the context a
          human needs when reading a trace: was the servo pushing at all, how
          far off was the wheel, was AD steering, was the servo still catching
          up.
        </p>
      </section>

      {/* AD Steering Safety Envelope — feature:F7.
          Clamps AD's COMMANDED steering (never manual input) to physical
          lateral-accel / yaw-rate / steering-rate limits. Fixes the
          manual->AUTO_RESUME hard-steer defect (Pure Pursuit + the short
          planner's per-frame lane-center snap have no limit of their own).
          Default ON — this is a safety feature. Independent of the "Max
          lateral accel" field under Planner (Advanced): that value is already
          consumed picking curve speed, so reusing it here would clamp during
          ordinary curve driving. Limits are FINAL (fixed from a real-vehicle
          measurement pool). */}
      <section>
        <h3 className="text-xs font-bold text-text-secondary uppercase tracking-wider mb-2">
          AD Steering Safety Envelope
        </h3>
        <p className="text-[10px] text-text-tertiary mb-2 leading-tight">
          Clamps AD's commanded steering only — manual input is never limited.
          Default ON. Limits are fixed from a real-vehicle measurement pool.
        </p>
        <div className="mb-3">
          <ToggleSwitch
            label="Enable steering safety envelope"
            checked={Boolean(cfg.ad_steering_envelope_enabled)}
            onChange={(v) => set('ad_steering_envelope_enabled', v)}
          />
        </div>
        <div className="grid grid-cols-2 gap-3">
          <NumberInput label="Max lateral accel (m/s²)" step={0.1}
            value={cfg.a_lat_max_steer ?? 4.3}
            onChange={setNum('a_lat_max_steer')} />
          <NumberInput label="Max yaw rate (rad/s)" step={0.05}
            value={cfg.yaw_rate_max ?? 1.0}
            onChange={setNum('yaw_rate_max')} />
          <NumberInput label="Max steering rate (rad/s)" step={0.05}
            value={cfg.steer_rate_max ?? 1.5}
            onChange={setNum('steer_rate_max')} />
          <NumberInput label="Speed floor (m/s)" step={0.1}
            value={cfg.envelope_v_floor ?? 1.0}
            onChange={setNum('envelope_v_floor')} />
          <NumberInput label="Steer jerk cap (1/s², 0=disabled)" step={1}
            value={cfg.ad_steering_envelope_steer_jerk_max ?? 0.0}
            onChange={setNum('ad_steering_envelope_steer_jerk_max')} />
        </div>
        <p className="text-[10px] text-text-tertiary mt-2 leading-tight">
          Steer jerk cap is an optional ride-feel feature, default OFF (0 =
          disabled, restores the rate-only limiter above bit-identically) —
          unlike the three limits above it is not a validated safety limit.
          Enabling it (measured candidate ~25) smooths abrupt steering-rate
          transitions (e.g. an AUTO_RESUME handoff) at the cost of a higher
          residual while AD is steering with "AD Wheel Following (FFB)"
          enabled, since the servo has to track a further-shaped command.
        </p>
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
