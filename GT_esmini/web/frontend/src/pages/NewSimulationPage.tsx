import { useState, useEffect } from 'react';
import { useQuery, useMutation } from '@tanstack/react-query';
import { useNavigate, useSearchParams, useLocation } from 'react-router-dom';
import { api, type ScriptInfo, type SimulationStatus } from '../api/client';
import { Button } from '../components/ui/Button';
import { SelectInput, NumberInput, TextInput, Checkbox } from '../components/ui/Input';
import { Card } from '../components/ui/Card';

export function NewSimulationPage() {
  const navigate = useNavigate();
  const location = useLocation();
  const [searchParams] = useSearchParams();

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

  // Advanced section toggle
  const [showAdvanced, setShowAdvanced] = useState(false);

  // Validation
  const [validationErrors, setValidationErrors] = useState<Record<string, string>>({});

  // Queries
  const { data: scenarios } = useQuery({
    queryKey: ['scenarios'],
    queryFn: () => api.getScenarios(),
  });

  const { data: scriptsData } = useQuery({
    queryKey: ['scripts'],
    queryFn: () => api.getScripts(),
  });

  const { data: execDefaults } = useQuery({
    queryKey: ['execution-defaults'],
    queryFn: () => api.getExecutionDefaults(),
  });

  // Restore settings from a previous run (re-run flow)
  const rerunSource = location.state?.rerunFrom as SimulationStatus | undefined;
  useEffect(() => {
    if (!rerunSource?.options) return;
    // Options is Record<string, unknown> from API — use typed wrapper
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
    // Show advanced if re-running so user can see what was configured
    setShowAdvanced(true);
    window.history.replaceState({}, '');
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Apply defaults on load (skip if restoring from re-run)
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

  // Submit
  const mutation = useMutation({
    mutationFn: () =>
      api.createSimulation({
        scenario_id: scenarioId,
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
      }),
    onSuccess: (data) => {
      navigate(`/simulations/${data.job_id}`);
    },
  });

  const handleSubmit = () => {
    if (validate()) mutation.mutate();
  };

  const scripts = scriptsData?.scripts ?? [];

  return (
    <div className="max-w-2xl">
      <h1 className="text-2xl font-bold mb-6">Run Simulation</h1>

      <div className="space-y-4">
        {/* ─── Scenario Selection ─── */}
        <Card title="Scenario">
          <SelectInput
            value={scenarioId}
            onChange={(e) => { setScenarioId(e.target.value); setValidationErrors((v) => ({ ...v, scenario: '' })); }}
          >
            <option value="">Select a scenario...</option>
            {scenarios?.map((s) => (
              <option key={s.id} value={s.id}>{s.id}</option>
            ))}
          </SelectInput>
          {validationErrors.scenario && (
            <p className="text-red-400 text-xs mt-1">{validationErrors.scenario}</p>
          )}
        </Card>

        {/* ─── Controller Selection ─── */}
        <Card title="Controller">
          <div className="flex gap-2 mb-4">
            <button
              onClick={() => setControllerType('default')}
              className={`px-4 py-2 rounded text-sm font-medium transition-colors cursor-pointer ${
                controllerType === 'default'
                  ? 'bg-blue-600 text-white'
                  : 'bg-gray-800 text-gray-300 hover:bg-gray-700'
              }`}
            >
              Default
            </button>
            <button
              onClick={() => setControllerType('python')}
              className={`px-4 py-2 rounded text-sm font-medium transition-colors cursor-pointer ${
                controllerType === 'python'
                  ? 'bg-blue-600 text-white'
                  : 'bg-gray-800 text-gray-300 hover:bg-gray-700'
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

        {/* ─── Quick Options ─── */}
        <Card title="Options">
          <div className="flex flex-wrap gap-x-6 gap-y-3">
            <Checkbox label="Headless" checked={headless} onChange={(e) => setHeadless(e.target.checked)} />
            <Checkbox label="Record" checked={record} onChange={(e) => setRecord(e.target.checked)} />
            <Checkbox label="No Realtime" checked={noRealtime} onChange={(e) => setNoRealtime(e.target.checked)} />
            <Checkbox label="AutoLight" checked={autolight} onChange={(e) => setAutolight(e.target.checked)} />
            <Checkbox label="OSI Output" checked={osiEnabled} onChange={(e) => setOsiEnabled(e.target.checked)} />
          </div>
        </Card>

        {/* ─── Advanced Settings (collapsible) ─── */}
        <div>
          <button
            onClick={() => setShowAdvanced((v) => !v)}
            className="flex items-center gap-2 text-sm text-gray-400 hover:text-gray-200 transition-colors cursor-pointer mb-2"
          >
            <span className="text-xs">{showAdvanced ? '\u25BC' : '\u25B6'}</span>
            Advanced Settings
          </button>

          {showAdvanced && (
            <Card>
              <div className="space-y-4">
                {/* Frequency & Timeout */}
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <NumberInput
                      label="Frequency (Hz)"
                      value={hz}
                      onChange={(e) => setHz(Number(e.target.value))}
                    />
                    {validationErrors.hz && (
                      <p className="text-red-400 text-xs mt-1">{validationErrors.hz}</p>
                    )}
                  </div>
                  <div>
                    <NumberInput
                      label="Timeout (s)"
                      value={timeout}
                      onChange={(e) => setTimeout_(Number(e.target.value))}
                    />
                    {validationErrors.timeout && (
                      <p className="text-red-400 text-xs mt-1">{validationErrors.timeout}</p>
                    )}
                  </div>
                </div>

                {/* OSI IP (if enabled) */}
                {osiEnabled && (
                  <div>
                    <TextInput
                      label="OSI IP Address"
                      value={osiIp}
                      onChange={(e) => setOsiIp(e.target.value)}
                      className="w-48"
                    />
                    {validationErrors.osiIp && (
                      <p className="text-red-400 text-xs mt-1">{validationErrors.osiIp}</p>
                    )}
                  </div>
                )}

                {/* Viewer settings (when not headless) */}
                {!headless && (
                  <>
                    <Checkbox
                      label="Threaded viewer"
                      description="(OSG viewer in separate thread)"
                      checked={threads}
                      onChange={(e) => setThreads(e.target.checked)}
                    />
                    <div>
                      <h3 className="text-xs text-gray-500 mb-2">Window Position & Size</h3>
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

        {/* ─── Submit ─── */}
        <Button
          size="lg"
          className="w-full"
          onClick={handleSubmit}
          disabled={!scenarioId || mutation.isPending}
        >
          {mutation.isPending ? 'Starting...' : 'Run Simulation'}
        </Button>

        {mutation.error && (
          <p className="text-red-400 text-sm">{String(mutation.error)}</p>
        )}
      </div>
    </div>
  );
}
