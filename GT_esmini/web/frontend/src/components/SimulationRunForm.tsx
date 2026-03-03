import { useState, useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api } from '../api/client';
import type {
  ScriptInfo,
  SimulationStatus,
  ScenarioParam,
  ParameterPreset,
} from '../api/client';
import { Button } from './ui/Button';
import { SelectInput, NumberInput, TextInput, Checkbox, ToggleSwitch, IconToggle } from './ui/Input';
import { Card } from './ui/Card';

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
}: SimulationRunFormProps) {
  const queryClient = useQueryClient();

  // Controller state
  const [controllerType, setControllerType] = useState<'default' | 'python'>('default');
  const [pythonScript, setPythonScript] = useState('DriverScript/pythondriver/scenario_drive_embedded.py');
  const [pythonClass, setPythonClass] = useState('EmbeddedController');
  const [traceEnabled, setTraceEnabled] = useState(true);

  // Execution state
  const [hz, setHz] = useState(120);
  const [headless, setHeadless] = useState(true);
  const [record, setRecord] = useState(true);
  const [noRealtime, setNoRealtime] = useState(false);
  const [timeout, setTimeout_] = useState(60);
  const [osiEnabled, setOsiEnabled] = useState(true);
  const [osiIp, setOsiIp] = useState('127.0.0.1');
  const [autolight, setAutolight] = useState(true);
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
      execution?: { hz?: number; headless?: boolean; record?: boolean; no_realtime?: boolean; timeout?: number; osi?: { enabled: boolean; ip: string }; autolight?: boolean; threads?: boolean; window?: { x: number; y: number; w: number; h: number } };
    };

    if (opts.controller) {
      setControllerType((opts.controller.controller_type ?? 'default') as 'default' | 'python');
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

  // Submit
  const mutation = useMutation({
    mutationFn: () =>
      api.createSimulation({
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
        },
        execution: {
          headless,
          record,
          hz,
          no_realtime: noRealtime,
          timeout,
          osi: { enabled: osiEnabled, ip: osiIp },
          autolight,
          threads,
          window: { x: winX, y: winY, w: winW, h: winH },
          extra_args: [],
        },
        param_overrides: getActiveOverrides(),
      }),
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

  // Option icons (inline SVG, 16x16 viewBox)
  const iconWindow = headless ? (
    <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
      <path d="M2 3h12v8H2V3zm1 1v6h10V4H3zm2 8h6v1H5v-1z" />
      <path d="M1.5 1.5l13 13" stroke="currentColor" strokeWidth="1.5" fill="none" />
    </svg>
  ) : (
    <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
      <path d="M2 3h12v8H2V3zm1 1v6h10V4H3zm2 8h6v1H5v-1z" />
    </svg>
  );
  const iconRecord = (
    <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
      <circle cx="8" cy="8" r="5" />
    </svg>
  );
  const iconFastForward = (
    <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
      <path d="M2 3l6 5-6 5V3zm6 0l6 5-6 5V3z" />
    </svg>
  );
  const iconAutoLight = (
    <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
      <path d="M8 1C5.5 1 4 3 4 5.5c0 1.5.7 2.8 1.5 3.5.5.5.5 1 .5 1.5V12h4v-1.5c0-.5 0-1 .5-1.5.8-.7 1.5-2 1.5-3.5C12 3 10.5 1 8 1zm-1 12h2v1H7v-1z" />
    </svg>
  );

  // Layout helpers
  const cardCls = compact ? 'p-3' : '';

  return (
    <Card className={cardCls}>
      {/* Controller Selection */}
      <div>
        <h3 className="text-xs text-text-tertiary mb-2">Controller</h3>
        <div className="flex gap-2 mb-4">
          <button
            onClick={() => setControllerType('default')}
            className={`px-4 py-2 text-sm font-medium transition-colors cursor-pointer ${
              controllerType === 'default'
                ? 'bg-primary/80 text-background glow-edge'
                : 'bg-glass-1 text-text-secondary hover:bg-glass-hover hover:text-foreground'
            }`}
          >
            Default
          </button>
          <button
            onClick={() => setControllerType('python')}
            className={`px-4 py-2 text-sm font-medium transition-colors cursor-pointer ${
              controllerType === 'python'
                ? 'bg-primary/80 text-background glow-edge'
                : 'bg-glass-1 text-text-secondary hover:bg-glass-hover hover:text-foreground'
            }`}
          >
            Python Driver
          </button>
        </div>

        {controllerType === 'python' && (
          <div className="space-y-3">
            <SelectInput
              label="Python Script"
              value={pythonScript}
              onChange={(e) => {
                setPythonScript(e.target.value);
                const script = scripts.find((s: ScriptInfo) => s.path === e.target.value);
                if (script?.classes.length) setPythonClass(script.classes[0]);
              }}
            >
              {scripts.map((s: ScriptInfo) => (
                <option key={s.path} value={s.path}>
                  {s.recommended ? '\u2605 ' : ''}{s.name} ({s.category})
                </option>
              ))}
            </SelectInput>
            <SelectInput
              label="Class Name"
              value={pythonClass}
              onChange={(e) => setPythonClass(e.target.value)}
            >
              {(scripts.find((s: ScriptInfo) => s.path === pythonScript)?.classes ?? []).map((c: string) => (
                <option key={c} value={c}>{c}</option>
              ))}
            </SelectInput>
            <Checkbox
              label="Enable trace logging"
              checked={traceEnabled}
              onChange={(e) => setTraceEnabled(e.target.checked)}
            />
          </div>
        )}
      </div>

      <div className="border-b border-glass-edge my-3" />

      {/* Quick Options (icon toggles) */}
      <div className="flex items-center gap-2">
        <IconToggle icon={iconWindow} label="Window" active={!headless} onChange={(v) => setHeadless(!v)} />
        <IconToggle icon={iconRecord} label="Record" active={record} onChange={setRecord} />
        <IconToggle icon={iconFastForward} label="No Realtime" active={noRealtime} onChange={setNoRealtime} />
        <IconToggle icon={iconAutoLight} label="AutoLight" active={autolight} onChange={setAutolight} />
      </div>

      {/* Parameter Overrides (project context only, hidden when managed externally) */}
      {!hideParams && projectId && scenarioParams.length > 0 && (
        <>
        <div className="border-b border-glass-edge my-3" />
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
        </>
      )}

      <div className="border-b border-glass-edge my-3" />

      {/* Advanced Settings (collapsible) */}
      <div>
        <button
          onClick={() => setShowAdvanced((v) => !v)}
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
            <div className={compact ? 'space-y-3' : 'space-y-4'}>
              <div className={`grid grid-cols-2 ${compact ? 'gap-3' : 'gap-4'}`}>
                <div>
                  <NumberInput
                    label="Frequency (Hz)"
                    value={hz}
                    onChange={(e) => setHz(Number(e.target.value))}
                  />
                  {validationErrors.hz && (
                    <p className="text-destructive text-xs mt-1">{validationErrors.hz}</p>
                  )}
                </div>
                <div>
                  <NumberInput
                    label="Timeout (s)"
                    value={timeout}
                    onChange={(e) => setTimeout_(Number(e.target.value))}
                  />
                  {validationErrors.timeout && (
                    <p className="text-destructive text-xs mt-1">{validationErrors.timeout}</p>
                  )}
                </div>
              </div>

              {osiEnabled && (
                <div>
                  <TextInput
                    label="OSI IP Address"
                    value={osiIp}
                    onChange={(e) => setOsiIp(e.target.value)}
                    className="w-48"
                  />
                  {validationErrors.osiIp && (
                    <p className="text-destructive text-xs mt-1">{validationErrors.osiIp}</p>
                  )}
                </div>
              )}

              {!headless && (
                <>
                  <ToggleSwitch
                    label="Threaded viewer"
                    checked={threads}
                    onChange={setThreads}
                    description="(OSG)"
                  />
                  <div>
                    <h3 className="text-xs text-text-tertiary mb-1.5">Window Position & Size</h3>
                    <div className="grid grid-cols-4 gap-3">
                      <NumberInput label="X" value={winX} onChange={(e) => setWinX(Number(e.target.value))} />
                      <NumberInput label="Y" value={winY} onChange={(e) => setWinY(Number(e.target.value))} />
                      <NumberInput label="Width" value={winW} onChange={(e) => setWinW(Number(e.target.value))} />
                      <NumberInput label="Height" value={winH} onChange={(e) => setWinH(Number(e.target.value))} />
                    </div>
                  </div>
                </>
              )}
            </div>
        )}
      </div>

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
