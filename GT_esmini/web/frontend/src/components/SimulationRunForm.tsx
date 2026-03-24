import { useState, useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api } from '../api/client';
import type {
  SimulationRequest,
  SimulationStatus,
  ScenarioParam,
  ParameterPreset,
  ManualDriveConfig,
} from '../api/client';
import { Button } from './ui/Button';
import { Card } from './ui/Card';
import { ControllerSection } from './simulation/ControllerSection';
import { ManualDrivePanel } from './simulation/ManualDrivePanel';
import { QuickOptionsBar } from './simulation/QuickOptionsBar';
import { ParameterOverrides } from './simulation/ParameterOverrides';
import { AdvancedSettings } from './simulation/AdvancedSettings';

const DEFAULT_MANUAL_CONFIG: ManualDriveConfig = {
  input_type: 'sdl2_wheel',
  physics_type: 'real_vehicle',
  ffb_enabled: true,
  domain: { lateral: 'manual', longitudinal: 'manual' },
  sdl2: { device_index: 0, deadzone: 0.05, button_mapping: { upshift: 4, downshift: 5, override: 0, indicator_left: 7, indicator_right: 6, headlight: -1, high_beam: -1, fog_light: -1, hazard: -1 } },
  input_network: { transport_type: 'udp', port: 9100, level: 'pedal_steer' },
  physics_network: { transport_type: 'udp', host: '127.0.0.1', cmd_port: 9200, state_port: 9201 },
  ffb: { spring_coefficient: 0.5, damper_coefficient: 0.3, constant_gain: 1.0, max_force: 1.0 },
};

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

  // Controller state
  const [controllerType, setControllerType] = useState<'default' | 'python' | 'manual'>('default');
  const [manualDriveConfig, setManualDriveConfig] = useState<ManualDriveConfig>(DEFAULT_MANUAL_CONFIG);
  const [showManualPanel, setShowManualPanel] = useState(false);
  const [pythonScript, setPythonScript] = useState('DriverScript/pythondriver/scenario_drive_embedded.py');
  const [pythonClass, setPythonClass] = useState('EmbeddedController');
  const [traceEnabled, setTraceEnabled] = useState(true);

  // Execution state
  const [hz, setHz] = useState(120);
  const [headless, setHeadless] = useState(false);
  const [record, setRecord] = useState(false);
  const [noRealtime, setNoRealtime] = useState(false);
  const [timeout, setTimeout_] = useState(60);
  const [osiEnabled, setOsiEnabled] = useState(true);
  const [osiIp, setOsiIp] = useState('127.0.0.1');
  const [autolight, setAutolight] = useState(true);
  const [vehiclePhysics, setVehiclePhysics] = useState(true);
  const [threads, setThreads] = useState(true);
  const [winX, setWinX] = useState(60);
  const [winY, setWinY] = useState(60);
  const [winW, setWinW] = useState(1280);
  const [winH, setWinH] = useState(720);

  // Parameter overrides
  const [paramOverrides, setParamOverrides] = useState<Record<string, string>>({});
  const [presetName, setPresetName] = useState('');
  const [showPresetSave, setShowPresetSave] = useState(false);

  // Advanced section toggle
  const [showAdvanced, setShowAdvanced] = useState(!compact);

  // Validation
  const [validationErrors, setValidationErrors] = useState<Record<string, string>>({});

  // Queries
  const { data: scriptsData } = useQuery({
    queryKey: ['scripts'],
    queryFn: () => api.getScripts(),
  });

  const { data: execDefaults } = useQuery({
    queryKey: ['execution-defaults'],
    queryFn: () => api.getExecutionDefaults(),
  });

  // Restore settings from a previous run
  useEffect(() => {
    if (!rerunFrom) return;
    const opts = rerunFrom as {
      controller?: { controller_type?: string; python?: { script?: string; python_class?: string; class?: string; trace_enabled?: boolean } };
      execution?: { hz?: number; headless?: boolean; record?: boolean; no_realtime?: boolean; timeout?: number; osi?: { enabled: boolean; ip: string }; autolight?: boolean; vehicle_physics?: boolean; threads?: boolean; window?: { x: number; y: number; w: number; h: number } };
    };

    if (opts.controller) {
      setControllerType((opts.controller.controller_type ?? 'default') as 'default' | 'python' | 'manual');
      if (opts.controller.controller_type === 'manual' && (opts.controller as any).manual_drive) {
        setManualDriveConfig({ ...DEFAULT_MANUAL_CONFIG, ...(opts.controller as any).manual_drive });
      }
      const py = opts.controller.python;
      if (py) {
        if (py.script) setPythonScript(py.script);
        setPythonClass(py.python_class ?? py['class'] ?? 'EmbeddedController');
        if (py.trace_enabled !== undefined) setTraceEnabled(py.trace_enabled);
      }
    }
    const exec = opts.execution;
    if (exec) {
      if (exec.hz !== undefined) setHz(exec.hz);
      if (exec.headless !== undefined) setHeadless(exec.headless);
      if (exec.record !== undefined) setRecord(exec.record);
      if (exec.no_realtime !== undefined) setNoRealtime(exec.no_realtime);
      if (exec.timeout !== undefined) setTimeout_(exec.timeout);
      if (exec.osi) { setOsiEnabled(exec.osi.enabled); setOsiIp(exec.osi.ip); }
      if (exec.autolight !== undefined) setAutolight(exec.autolight);
      if (exec.vehicle_physics !== undefined) setVehiclePhysics(exec.vehicle_physics);
      if (exec.threads !== undefined) setThreads(exec.threads);
      if (exec.window) { setWinX(exec.window.x); setWinY(exec.window.y); setWinW(exec.window.w); setWinH(exec.window.h); }
    }
    setShowAdvanced(true);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Apply execution defaults (skip if restoring from re-run)
  useEffect(() => {
    if (execDefaults && !rerunFrom) {
      setHz(execDefaults.hz);
      setHeadless(execDefaults.headless);
      setRecord(execDefaults.record);
      setNoRealtime(execDefaults.no_realtime);
      setTimeout_(execDefaults.timeout);
      setOsiEnabled(execDefaults.osi.enabled);
      setOsiIp(execDefaults.osi.ip);
      setAutolight(execDefaults.autolight);
      if (execDefaults.vehicle_physics !== undefined) setVehiclePhysics(execDefaults.vehicle_physics);
      if (execDefaults.threads !== undefined) setThreads(execDefaults.threads);
      if (execDefaults.window) {
        setWinX(execDefaults.window.x);
        setWinY(execDefaults.window.y);
        setWinW(execDefaults.window.w);
        setWinH(execDefaults.window.h);
      }
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
  const buildRequest = (): SimulationRequest => ({
    scenario_id: scenarioFile,
    project_id: projectId || undefined,
    controller: {
      controller_type: controllerType,
      python: {
        script: pythonScript,
        class: pythonClass,
        python_home: '',
        trace_enabled: traceEnabled,
        trace_dir: '',
      },
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
      vehicle_physics: vehiclePhysics,
      threads,
      window: { x: winX, y: winY, w: winW, h: winH },
      extra_args: [],
    },
    param_overrides: getActiveOverrides(),
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
      setShowPresetSave(false);
      setPresetName('');
      queryClient.invalidateQueries({ queryKey: ['presets', projectId, scenarioFile] });
    },
  });

  const handleSubmit = () => {
    if (validate()) mutation.mutate();
  };

  const loadPreset = (preset: ParameterPreset) => {
    setParamOverrides((prev) => ({ ...prev, ...preset.values }));
  };

  const scripts = scriptsData?.scripts ?? [];

  const cardCls = compact ? 'p-3' : '';

  return (
    <Card className={cardCls}>
      <ControllerSection
        controllerType={controllerType}
        setControllerType={setControllerType}
        pythonScript={pythonScript}
        setPythonScript={setPythonScript}
        pythonClass={pythonClass}
        setPythonClass={setPythonClass}
        traceEnabled={traceEnabled}
        setTraceEnabled={setTraceEnabled}
        scripts={scripts}
        onOpenManualSettings={() => setShowManualPanel(true)}
      />

      <ManualDrivePanel
        open={showManualPanel}
        onClose={() => setShowManualPanel(false)}
        config={manualDriveConfig}
        onChange={setManualDriveConfig}
      />

      <div className="border-b border-glass-edge my-3" />

      <QuickOptionsBar
        headless={headless}
        setHeadless={setHeadless}
        record={record}
        setRecord={setRecord}
        noRealtime={noRealtime}
        setNoRealtime={setNoRealtime}
        autolight={autolight}
        setAutolight={setAutolight}
        vehiclePhysics={vehiclePhysics}
        setVehiclePhysics={setVehiclePhysics}
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
            setPresetName={setPresetName}
            showPresetSave={showPresetSave}
            setShowPresetSave={setShowPresetSave}
            presetMutation={presetMutation}
            compact={compact}
          />
        </>
      )}

      <div className="border-b border-glass-edge my-3" />

      <AdvancedSettings
        showAdvanced={showAdvanced}
        setShowAdvanced={setShowAdvanced}
        hz={hz}
        setHz={setHz}
        timeout={timeout}
        setTimeout_={setTimeout_}
        osiEnabled={osiEnabled}
        osiIp={osiIp}
        setOsiIp={setOsiIp}
        headless={headless}
        threads={threads}
        setThreads={setThreads}
        winX={winX}
        setWinX={setWinX}
        winY={winY}
        setWinY={setWinY}
        winW={winW}
        setWinW={setWinW}
        winH={winH}
        setWinH={setWinH}
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

      {mutation.error && (
        <p className="text-destructive text-sm">{String(mutation.error)}</p>
      )}
    </Card>
  );
}
