import { useEffect, useReducer, useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api } from '../api/client';
import type {
  SimulationRequest,
  SimulationStatus,
  ScenarioParam,
  ParameterPreset,
  ManualDriveConfig,
} from '../api/client';
import { buildSimulationRequest } from '../api/simulationRequest';
import { MANUAL_DRIVE_DEFAULT_PORTS } from '../lib/manualDrive';
import { Button } from './ui/Button';
import { Card } from './ui/Card';
import { ControllerSection, type ControllerType } from './simulation/ControllerSection';
import { ManualDrivePanel } from './simulation/ManualDrivePanel';
import { VirtualDriverPanel } from './simulation/VirtualDriverPanel';
import { QuickOptionsBar } from './simulation/QuickOptionsBar';
import { ParameterOverrides } from './simulation/ParameterOverrides';
import { AdvancedSettings } from './simulation/AdvancedSettings';

const DEFAULT_MANUAL_CONFIG: ManualDriveConfig = {
  input_type: 'sdl2_wheel',
  physics_type: 'real_vehicle',
  ffb_enabled: true,
  domain: { lateral: 'manual', longitudinal: 'manual' },
  // feature:F7 gap #6 -- auto_resume defaults to 3 (the shipped
  // config/manual_drive.json value), NOT -1. This literal is written straight
  // into the run request, and -1 means "unassigned" to C++, which is exactly
  // the gap #5 symptom the exposure work is meant to end.
  sdl2: { device_index: 0, deadzone: 0.05, button_mapping: { upshift: 4, downshift: 5, override: 0, indicator_left: 7, indicator_right: 6, headlight: -1, high_beam: -1, fog_light: -1, hazard: -1, auto_resume: 3 } },
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

/* ---------- Form value state ---------- */

type DriveMode = 'comfort' | 'sport';
type LaneChangeTiming = 'late' | 'normal' | 'early';
type LaneChangeGap = 'wide' | 'normal' | 'tight';

/** All values that feed buildSimulationRequest (controller + execution). */
interface FormValues {
  // Controller
  controllerType: ControllerType;
  manualDriveConfig: ManualDriveConfig;
  driveMode: DriveMode;
  laneChangeTiming: LaneChangeTiming;
  laneChangeGap: LaneChangeGap;
  // Execution
  hz: number;
  headless: boolean;
  record: boolean;
  noRealtime: boolean;
  timeout: number;
  osiEnabled: boolean;
  osiIp: string;
  autolight: boolean;
  autolightHeadlights: boolean;
  vehiclePhysics: boolean;
  kinematicMode: boolean;
  routeDriveMode: boolean;
  threads: boolean;
  winX: number;
  winY: number;
  winW: number;
  winH: number;
}

const INITIAL_VALUES: FormValues = {
  controllerType: 'default',
  manualDriveConfig: DEFAULT_MANUAL_CONFIG,
  driveMode: 'comfort',
  laneChangeTiming: 'normal',
  laneChangeGap: 'normal',
  hz: 120,
  headless: false,
  record: false,
  noRealtime: false,
  timeout: 60,
  osiEnabled: true,
  osiIp: '127.0.0.1',
  autolight: true,
  autolightHeadlights: false,
  vehiclePhysics: true,
  kinematicMode: false,
  routeDriveMode: false,
  threads: true,
  winX: 60,
  winY: 60,
  winW: 1280,
  winH: 720,
};

type ValuesAction = { type: 'patch'; patch: Partial<FormValues> };

function valuesReducer(state: FormValues, action: ValuesAction): FormValues {
  switch (action.type) {
    case 'patch':
      return { ...state, ...action.patch };
    default:
      return state;
  }
}

/* ---------- UI-helper state ---------- */

interface UiState {
  showManualPanel: boolean;
  showVdPanel: boolean;
  showAdvanced: boolean;
  showPresetSave: boolean;
  presetName: string;
}

type UiAction = { type: 'patch'; patch: Partial<UiState> };

function uiReducer(state: UiState, action: UiAction): UiState {
  switch (action.type) {
    case 'patch':
      return { ...state, ...action.patch };
    default:
      return state;
  }
}

export interface SimulationRunFormProps {
  projectId: string;
  scenarioFile: string;
  scenarioParams: ScenarioParam[];
  presets: ParameterPreset[];
  compact?: boolean;
  /** Hide the parameter overrides section (when managed externally) */
  hideParams?: boolean;
  /** External param overrides (used when hideParams is true) */
  externalParamOverrides?: Record<string, string>;
  onSubmitted?: (job: SimulationStatus) => void;
  onNavigateToJob?: (jobId: string) => void;
  /** Whether a simulation is currently running */
  isRunning?: boolean;
  /** Called when Stop is clicked */
  onStop?: () => void;
  /** Initial options to restore (re-run flow) */
  rerunFrom?: Record<string, unknown>;
  /** Callback to expose the current request builder to parent */
  onRequestBuilder?: (builder: (() => SimulationRequest) | null) => void;
}

export function SimulationRunForm({
  projectId,
  scenarioFile,
  scenarioParams,
  presets,
  compact = false,
  hideParams = false,
  externalParamOverrides,
  onSubmitted,
  onNavigateToJob,
  isRunning = false,
  onStop,
  rerunFrom,
  onRequestBuilder,
}: SimulationRunFormProps) {
  const queryClient = useQueryClient();

  // Form values (controller + execution) — one grouped reducer.
  const [values, dispatchValues] = useReducer(valuesReducer, INITIAL_VALUES);
  const {
    controllerType,
    manualDriveConfig,
    driveMode,
    laneChangeTiming,
    laneChangeGap,
    hz,
    headless,
    record,
    noRealtime,
    timeout,
    osiEnabled,
    osiIp,
    autolight,
    autolightHeadlights,
    vehiclePhysics,
    kinematicMode,
    routeDriveMode,
    threads,
    winX,
    winY,
    winW,
    winH,
  } = values;
  const patchValues = (patch: Partial<FormValues>) => dispatchValues({ type: 'patch', patch });

  // UI helpers (panels / advanced toggle / preset-save input) — grouped reducer.
  const [ui, dispatchUi] = useReducer(uiReducer, undefined, () => ({
    showManualPanel: false,
    showVdPanel: false,
    showAdvanced: !compact,
    showPresetSave: false,
    presetName: '',
  }));
  const { showManualPanel, showVdPanel, showAdvanced, showPresetSave, presetName } = ui;
  const patchUi = (patch: Partial<UiState>) => dispatchUi({ type: 'patch', patch });

  // Parameter overrides — kept as useState (uses functional updates + the
  // Dispatch<SetStateAction> contract expected by ParameterOverrides).
  const [paramOverrides, setParamOverrides] = useState<Record<string, string>>({});

  // Validation — set as a whole object by validate().
  const [validationErrors, setValidationErrors] = useState<Record<string, string>>({});

  // Load saved manual drive config from server
  const { data: savedManualDriveConfig } = useQuery({
    queryKey: ['manual-drive-config'],
    queryFn: api.getManualDriveConfig,
  });
  useEffect(() => {
    if (savedManualDriveConfig) {
      // Deep-merge with defaults so missing nested fields (e.g. sdl2.button_mapping) don't crash the UI
      patchValues({
        manualDriveConfig: {
          ...DEFAULT_MANUAL_CONFIG,
          ...savedManualDriveConfig,
          domain: { ...DEFAULT_MANUAL_CONFIG.domain, ...savedManualDriveConfig.domain },
          sdl2: {
            ...DEFAULT_MANUAL_CONFIG.sdl2,
            ...savedManualDriveConfig.sdl2,
            button_mapping: {
              ...DEFAULT_MANUAL_CONFIG.sdl2.button_mapping,
              ...savedManualDriveConfig.sdl2?.button_mapping,
            },
          },
          keyboard: { ...DEFAULT_MANUAL_CONFIG.keyboard, ...savedManualDriveConfig.keyboard },
          input_network: { ...DEFAULT_MANUAL_CONFIG.input_network, ...savedManualDriveConfig.input_network },
          physics_network: { ...DEFAULT_MANUAL_CONFIG.physics_network, ...savedManualDriveConfig.physics_network },
          ffb: { ...DEFAULT_MANUAL_CONFIG.ffb, ...savedManualDriveConfig.ffb },
        },
      });
    }
  }, [savedManualDriveConfig]);

  // Queries

  const { data: execDefaults } = useQuery({
    queryKey: ['execution-defaults'],
    queryFn: () => api.getExecutionDefaults(),
  });

  // Restore settings from a previous run
  useEffect(() => {
    if (!rerunFrom) return;
    const opts = rerunFrom as {
      controller?: { controller_type?: string; manual_drive?: Partial<ManualDriveConfig> };
      execution?: { hz?: number; headless?: boolean; record?: boolean; no_realtime?: boolean; timeout?: number; osi?: { enabled: boolean; ip: string }; autolight?: boolean; autolight_headlights?: boolean; vehicle_physics?: boolean; kinematic_mode?: boolean; route_drive_mode?: boolean; route_drive_timing?: 'late' | 'normal' | 'early'; route_drive_gap?: 'wide' | 'normal' | 'tight'; threads?: boolean; window?: { x: number; y: number; w: number; h: number }; drive_mode?: 'comfort' | 'sport' };
    };

    const patch: Partial<FormValues> = {};
    if (opts.controller) {
      const ct = opts.controller.controller_type ?? 'default';
      patch.controllerType = (ct === 'manual' || ct === 'virtual_driver' ? ct : 'default') as ControllerType;
      if (ct === 'manual' && opts.controller.manual_drive) {
        patch.manualDriveConfig = { ...DEFAULT_MANUAL_CONFIG, ...opts.controller.manual_drive };
      }
    }
    const exec = opts.execution;
    if (exec) {
      if (exec.hz !== undefined) patch.hz = exec.hz;
      if (exec.headless !== undefined) patch.headless = exec.headless;
      if (exec.record !== undefined) patch.record = exec.record;
      if (exec.no_realtime !== undefined) patch.noRealtime = exec.no_realtime;
      if (exec.timeout !== undefined) patch.timeout = exec.timeout;
      if (exec.osi) { patch.osiEnabled = exec.osi.enabled; patch.osiIp = exec.osi.ip; }
      if (exec.autolight !== undefined) patch.autolight = exec.autolight;
      if (exec.autolight_headlights !== undefined) patch.autolightHeadlights = exec.autolight_headlights;
      if (exec.vehicle_physics !== undefined) patch.vehiclePhysics = exec.vehicle_physics;
      if (exec.kinematic_mode !== undefined) patch.kinematicMode = exec.kinematic_mode;
      if (exec.route_drive_mode !== undefined) patch.routeDriveMode = exec.route_drive_mode;
      if (exec.route_drive_timing) patch.laneChangeTiming = exec.route_drive_timing;
      if (exec.route_drive_gap) patch.laneChangeGap = exec.route_drive_gap;
      if (exec.threads !== undefined) patch.threads = exec.threads;
      if (exec.window) { patch.winX = exec.window.x; patch.winY = exec.window.y; patch.winW = exec.window.w; patch.winH = exec.window.h; }
      if (exec.drive_mode) patch.driveMode = exec.drive_mode;
    }
    patchValues(patch);
    patchUi({ showAdvanced: true });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Apply execution defaults (skip if restoring from re-run)
  useEffect(() => {
    if (execDefaults && !rerunFrom) {
      const patch: Partial<FormValues> = {
        hz: execDefaults.hz,
        headless: execDefaults.headless,
        record: execDefaults.record,
        noRealtime: execDefaults.no_realtime,
        timeout: execDefaults.timeout,
        osiEnabled: execDefaults.osi.enabled,
        osiIp: execDefaults.osi.ip,
        autolight: execDefaults.autolight,
      };
      if (execDefaults.vehicle_physics !== undefined) patch.vehiclePhysics = execDefaults.vehicle_physics;
      if (execDefaults.kinematic_mode !== undefined) patch.kinematicMode = execDefaults.kinematic_mode;
      if (execDefaults.route_drive_mode !== undefined) patch.routeDriveMode = execDefaults.route_drive_mode;
      if (execDefaults.route_drive_timing) patch.laneChangeTiming = execDefaults.route_drive_timing;
      if (execDefaults.route_drive_gap) patch.laneChangeGap = execDefaults.route_drive_gap;
      if (execDefaults.threads !== undefined) patch.threads = execDefaults.threads;
      if (execDefaults.window) {
        patch.winX = execDefaults.window.x;
        patch.winY = execDefaults.window.y;
        patch.winW = execDefaults.window.w;
        patch.winH = execDefaults.window.h;
      }
      patchValues(patch);
    }
  }, [execDefaults, rerunFrom]);

  // Initialize param overrides when scenarioParams change
  useEffect(() => {
    if (scenarioParams.length === 0) return;
    setParamOverrides((prev) => {
      const next: Record<string, string> = {};
      for (const p of scenarioParams) {
        next[p.name] = prev[p.name] ?? p.value;
      }
      return next;
    });
  }, [scenarioParams]);

  // Validation
  const validate = (): boolean => {
    const errors: Record<string, string> = {};
    if (!scenarioFile) errors.scenario = 'Select a scenario';
    if (hz <= 0) errors.hz = 'Must be > 0';
    if (timeout <= 0) errors.timeout = 'Must be > 0';
    if (osiEnabled && !/^\d{1,3}(\.\d{1,3}){3}$/.test(osiIp)) {
      errors.osiIp = 'Invalid IP address';
    }
    setValidationErrors(errors);
    return Object.keys(errors).length === 0;
  };

  // Build overrides that differ from defaults
  const getActiveOverrides = (): Record<string, string> | undefined => {
    if (scenarioParams.length === 0) return undefined;
    const source = externalParamOverrides ?? paramOverrides;
    const overrides: Record<string, string> = {};
    for (const p of scenarioParams) {
      const ov = source[p.name];
      if (ov !== undefined && ov !== p.value) {
        overrides[p.name] = ov;
      }
    }
    return Object.keys(overrides).length > 0 ? overrides : undefined;
  };

  // Build the simulation request from current form state
  const buildRequest = (): SimulationRequest => buildSimulationRequest({
    scenarioId: scenarioFile,
    projectId,
    controller: {
      controller_type: controllerType,
      ...(controllerType === 'manual' ? { manual_drive: manualDriveConfig } : {}),
    },
    execution: {
      headless,
      record,
      hz,
      no_realtime: noRealtime,
      timeout,
      osi: { enabled: osiEnabled, ip: osiIp },
      autolight,
      // F6: only meaningful with AutoLight on; the sub-toggle is hidden otherwise.
      autolight_headlights: autolight && autolightHeadlights,
      vehicle_physics: vehiclePhysics,
      kinematic_mode: kinematicMode,
      route_drive_mode: routeDriveMode,
      route_drive_timing: laneChangeTiming,
      route_drive_gap: laneChangeGap,
      threads,
      window: { x: winX, y: winY, w: winW, h: winH },
      extra_args: [],
      drive_mode: driveMode,
    },
    paramOverrides: getActiveOverrides(),
  });

  // Expose request builder to parent
  useEffect(() => {
    onRequestBuilder?.(scenarioFile ? buildRequest : null);
  });

  // Submit
  const mutation = useMutation({
    mutationFn: () => api.createSimulation(buildRequest()),
    onSuccess: (data) => {
      onSubmitted?.(data);
      onNavigateToJob?.(data.job_id);
    },
  });

  // Save preset
  const presetMutation = useMutation({
    mutationFn: () =>
      api.createPreset(projectId, scenarioFile, presetName.trim(), paramOverrides),
    onSuccess: () => {
      patchUi({ showPresetSave: false, presetName: '' });
      queryClient.invalidateQueries({ queryKey: ['presets', projectId, scenarioFile] });
    },
  });

  const handleSubmit = () => {
    if (validate()) mutation.mutate();
  };

  const loadPreset = (preset: ParameterPreset) => {
    setParamOverrides((prev) => ({ ...prev, ...preset.values }));
  };

  const cardCls = compact ? 'p-3' : '';

  return (
    <Card className={cardCls}>
      <ControllerSection
        controllerType={controllerType}
        setControllerType={(v) => patchValues({ controllerType: v })}
        onOpenManualSettings={() => patchUi({ showManualPanel: true })}
        onOpenVirtualDriverSettings={() => patchUi({ showVdPanel: true })}
        driveMode={driveMode}
        setDriveMode={(v) => patchValues({ driveMode: v })}
        routeDriveMode={routeDriveMode}
        laneChangeTiming={laneChangeTiming}
        setLaneChangeTiming={(v) => patchValues({ laneChangeTiming: v })}
        laneChangeGap={laneChangeGap}
        setLaneChangeGap={(v) => patchValues({ laneChangeGap: v })}
      />

      <ManualDrivePanel
        open={showManualPanel}
        onClose={() => patchUi({ showManualPanel: false })}
        config={manualDriveConfig}
        onChange={(v) => patchValues({ manualDriveConfig: v })}
      />

      <VirtualDriverPanel
        open={showVdPanel}
        onClose={() => patchUi({ showVdPanel: false })}
      />

      <div className="border-b border-glass-edge my-3" />

      <QuickOptionsBar
        headless={headless}
        setHeadless={(v) => patchValues({ headless: v })}
        record={record}
        setRecord={(v) => patchValues({ record: v })}
        noRealtime={noRealtime}
        setNoRealtime={(v) => patchValues({ noRealtime: v })}
        autolight={autolight}
        setAutolight={(v) => patchValues(v ? { autolight: v } : { autolight: v, autolightHeadlights: false })}
        autolightHeadlights={autolightHeadlights}
        setAutolightHeadlights={(v) => patchValues({ autolightHeadlights: v })}
        vehiclePhysics={vehiclePhysics}
        setVehiclePhysics={(v) => patchValues({ vehiclePhysics: v })}
        kinematicMode={kinematicMode}
        setKinematicMode={(v) => patchValues({ kinematicMode: v })}
        routeDriveMode={routeDriveMode}
        setRouteDriveMode={(v) => patchValues({ routeDriveMode: v })}
      />

      {/* Parameter Overrides (project context only, hidden when managed externally) */}
      {!hideParams && projectId && scenarioParams.length > 0 && (
        <>
          <div className="border-b border-glass-edge my-3" />
          <ParameterOverrides
            scenarioParams={scenarioParams}
            paramOverrides={paramOverrides}
            setParamOverrides={setParamOverrides}
            presets={presets}
            loadPreset={loadPreset}
            presetName={presetName}
            setPresetName={(v) => patchUi({ presetName: v })}
            showPresetSave={showPresetSave}
            setShowPresetSave={(v) => patchUi({ showPresetSave: v })}
            presetMutation={presetMutation}
            compact={compact}
          />
        </>
      )}

      <div className="border-b border-glass-edge my-3" />

      <AdvancedSettings
        showAdvanced={showAdvanced}
        setShowAdvanced={(v) => patchUi({ showAdvanced: v })}
        hz={hz}
        setHz={(v) => patchValues({ hz: v })}
        timeout={timeout}
        setTimeout_={(v) => patchValues({ timeout: v })}
        osiEnabled={osiEnabled}
        osiIp={osiIp}
        setOsiIp={(v) => patchValues({ osiIp: v })}
        headless={headless}
        threads={threads}
        setThreads={(v) => patchValues({ threads: v })}
        winX={winX}
        setWinX={(v) => patchValues({ winX: v })}
        winY={winY}
        setWinY={(v) => patchValues({ winY: v })}
        winW={winW}
        setWinW={(v) => patchValues({ winW: v })}
        winH={winH}
        setWinH={(v) => patchValues({ winH: v })}
        validationErrors={validationErrors}
        compact={compact}
      />

      <div className="border-b border-glass-edge my-3" />

      {/* Submit + Stop */}
      <div className="flex gap-2">
        <Button
          size={compact ? 'md' : 'lg'}
          className="flex-1"
          onClick={handleSubmit}
          disabled={!scenarioFile || mutation.isPending || isRunning}
        >
          {mutation.isPending ? 'Starting...' : 'Run Simulation'}
        </Button>
        <Button
          size={compact ? 'md' : 'lg'}
          variant="danger"
          className="flex-1"
          onClick={onStop}
          disabled={!isRunning || !onStop}
        >
          &#9632; Stop
        </Button>
      </div>

      {/* Live VirtualDriver telemetry — open a standalone window. Available even
          before/without a run: it connects to the always-on stream and shows
          "waiting" until a Virtual Driver simulation starts emitting. */}
      {controllerType === 'virtual_driver' && (
        <button
          type="button"
          onClick={() => {
            const q = new URLSearchParams({ override: '1' });
            if (projectId) q.set('project', projectId);
            if (scenarioFile) q.set('scenario', scenarioFile);
            window.open(`/live/vd/current?${q.toString()}`, 'vd-live', 'width=960,height=720');
          }}
          className="w-full mt-2 px-3 py-2 rounded text-sm border border-glass-edge text-text-secondary hover:bg-glass-2"
          title="Open the live VirtualDriver telemetry + manual override in a separate window"
        >
          Live Telemetry ↗
        </button>
      )}

      {mutation.error && (
        <p className="text-destructive text-sm">{String(mutation.error)}</p>
      )}
    </Card>
  );
}
