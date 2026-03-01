import { useState, useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { useNavigate, useSearchParams, useLocation, useParams } from 'react-router-dom';
import { GlassPanel } from '@osce/theme-apex';
import {
  api,
  type ScriptInfo,
  type SimulationStatus,
  type ScenarioParam,
  type ParameterPreset,
} from '../api/client';
import { Button } from '../components/ui/Button';
import { SelectInput, NumberInput, TextInput, Checkbox } from '../components/ui/Input';
import { Card } from '../components/ui/Card';

export function NewSimulationPage() {
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const location = useLocation();
  const [searchParams] = useSearchParams();
  const { projectId } = useParams<{ projectId: string }>();

  // Form state
  const [scenarioId, setScenarioId] = useState(searchParams.get('scenario') ?? '');
  const [controllerType, setControllerType] = useState<'default' | 'python'>('default');
  const [pythonScript, setPythonScript] = useState('DriverScript/pythondriver/scenario_drive_embedded.py');
  const [pythonClass, setPythonClass] = useState('EmbeddedController');
  const [traceEnabled, setTraceEnabled] = useState(true);
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
  const [showAdvanced, setShowAdvanced] = useState(false);

  // Validation
  const [validationErrors, setValidationErrors] = useState<Record<string, string>>({});

  // Queries
  const { data: scenarios } = useQuery({
    queryKey: ['scenarios'],
    queryFn: () => api.getScenarios(),
    enabled: !projectId,
  });

  const { data: projectScenarios } = useQuery({
    queryKey: ['project-scenarios', projectId],
    queryFn: () => api.getProjectScenarios(projectId!),
    enabled: !!projectId,
  });

  const { data: scriptsData } = useQuery({
    queryKey: ['scripts'],
    queryFn: () => api.getScripts(),
  });

  const { data: execDefaults } = useQuery({
    queryKey: ['execution-defaults'],
    queryFn: () => api.getExecutionDefaults(),
  });

  // Fetch params for the selected scenario (project context)
  const { data: scenarioParams } = useQuery({
    queryKey: ['scenario-params', projectId, scenarioId],
    queryFn: () => api.getScenarioParams(projectId!, scenarioId),
    enabled: !!projectId && !!scenarioId,
  });

  // Fetch presets for the selected scenario
  const { data: presets } = useQuery({
    queryKey: ['presets', projectId, scenarioId],
    queryFn: () => api.getPresets(projectId!, scenarioId),
    enabled: !!projectId && !!scenarioId,
  });

  // Initialize param overrides when params are loaded
  useEffect(() => {
    if (scenarioParams && scenarioParams.length > 0) {
      setParamOverrides((prev) => {
        const next: Record<string, string> = {};
        for (const p of scenarioParams) {
          next[p.name] = prev[p.name] ?? p.value;
        }
        return next;
      });
    }
  }, [scenarioParams]);

  // Restore settings from a previous run (re-run flow)
  const rerunSource = location.state?.rerunFrom as SimulationStatus | undefined;
  useEffect(() => {
    if (!rerunSource?.options) return;
    const opts = rerunSource.options as {
      controller?: { controller_type?: string; python?: { script?: string; python_class?: string; class?: string; trace_enabled?: boolean } };
      execution?: { hz?: number; headless?: boolean; record?: boolean; no_realtime?: boolean; timeout?: number; osi?: { enabled: boolean; ip: string }; autolight?: boolean; threads?: boolean; window?: { x: number; y: number; w: number; h: number } };
    };

    setScenarioId(rerunSource.scenario_id);
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
    window.history.replaceState({}, '');
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Apply defaults on load
  useEffect(() => {
    if (execDefaults && !rerunSource) {
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
  }, [execDefaults, rerunSource]);

  // Validate
  const validate = (): boolean => {
    const errors: Record<string, string> = {};
    if (!scenarioId) errors.scenario = 'Select a scenario';
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
    if (!scenarioParams || scenarioParams.length === 0) return undefined;
    const overrides: Record<string, string> = {};
    for (const p of scenarioParams) {
      const ov = paramOverrides[p.name];
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
        scenario_id: scenarioId,
        project_id: projectId,
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
      navigate(`/simulations/${data.job_id}`);
    },
  });

  // Save preset
  const presetMutation = useMutation({
    mutationFn: () =>
      api.createPreset(projectId!, scenarioId, presetName.trim(), paramOverrides),
    onSuccess: () => {
      setShowPresetSave(false);
      setPresetName('');
      queryClient.invalidateQueries({ queryKey: ['presets', projectId, scenarioId] });
    },
  });

  const handleSubmit = () => {
    if (validate()) mutation.mutate();
  };

  const loadPreset = (preset: ParameterPreset) => {
    setParamOverrides((prev) => ({ ...prev, ...preset.values }));
  };

  const scripts = scriptsData?.scripts ?? [];

  // Determine scenario options
  const scenarioOptions = projectId
    ? (projectScenarios ?? []).map((s) => ({ id: s.file, label: s.filename }))
    : (scenarios ?? []).map((s) => ({ id: s.id, label: s.id }));

  return (
    <div className="max-w-2xl">
      <h1 className="text-2xl font-display font-bold mb-6 tracking-wide">RUN SIMULATION</h1>

      <div className="space-y-4">
        {/* Scenario Selection */}
        <Card title="Scenario">
          <SelectInput
            value={scenarioId}
            onChange={(e) => { setScenarioId(e.target.value); setValidationErrors((v) => ({ ...v, scenario: '' })); }}
          >
            <option value="">Select a scenario...</option>
            {scenarioOptions.map((s) => (
              <option key={s.id} value={s.id}>{s.label}</option>
            ))}
          </SelectInput>
          {validationErrors.scenario && (
            <p className="text-destructive text-xs mt-1">{validationErrors.scenario}</p>
          )}
        </Card>

        {/* Parameter Overrides (project context only) */}
        {projectId && scenarioParams && scenarioParams.length > 0 && (
          <Card title="Parameters">
            <div className="space-y-3">
              {/* Preset selector */}
              {presets && presets.length > 0 && (
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
              <div className="grid grid-cols-1 gap-2">
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
          </Card>
        )}

        {/* Controller Selection */}
        <Card title="Controller">
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
        </Card>

        {/* Quick Options */}
        <Card title="Options">
          <div className="flex flex-wrap gap-x-6 gap-y-3">
            <Checkbox label="Headless" checked={headless} onChange={(e) => setHeadless(e.target.checked)} />
            <Checkbox label="Record" checked={record} onChange={(e) => setRecord(e.target.checked)} />
            <Checkbox label="No Realtime" checked={noRealtime} onChange={(e) => setNoRealtime(e.target.checked)} />
            <Checkbox label="AutoLight" checked={autolight} onChange={(e) => setAutolight(e.target.checked)} />
            <Checkbox label="OSI Output" checked={osiEnabled} onChange={(e) => setOsiEnabled(e.target.checked)} />
          </div>
        </Card>

        {/* Advanced Settings (collapsible) */}
        <div>
          <button
            onClick={() => setShowAdvanced((v) => !v)}
            className="flex items-center gap-2 text-sm text-text-secondary hover:text-foreground transition-colors cursor-pointer mb-2"
          >
            <span className="text-xs">{showAdvanced ? '\u25BC' : '\u25B6'}</span>
            Advanced Settings
          </button>

          {showAdvanced && (
            <Card>
              <div className="space-y-4">
                <div className="grid grid-cols-2 gap-4">
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
                    <Checkbox
                      label="Threaded viewer"
                      description="(OSG viewer in separate thread)"
                      checked={threads}
                      onChange={(e) => setThreads(e.target.checked)}
                    />
                    <div>
                      <h3 className="text-xs text-text-secondary mb-2">Window Position & Size</h3>
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
            </Card>
          )}
        </div>

        {/* Submit */}
        <Button
          size="lg"
          className="w-full"
          onClick={handleSubmit}
          disabled={!scenarioId || mutation.isPending}
        >
          {mutation.isPending ? 'Starting...' : 'Run Simulation'}
        </Button>

        {mutation.error && (
          <p className="text-destructive text-sm">{String(mutation.error)}</p>
        )}
      </div>
    </div>
  );
}
