import { useState, useEffect } from 'react';
import { useQuery, useMutation } from '@tanstack/react-query';
import { useNavigate, useSearchParams } from 'react-router-dom';
import { api, type ScriptInfo } from '../api/client';

export function NewSimulationPage() {
  const navigate = useNavigate();
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
  const [osiEnabled, setOsiEnabled] = useState(false);
  const [osiIp, setOsiIp] = useState('127.0.0.1');
  const [autolight, setAutolight] = useState(false);
  const [winX, setWinX] = useState(60);
  const [winY, setWinY] = useState(60);
  const [winW, setWinW] = useState(1280);
  const [winH, setWinH] = useState(720);

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

  // Apply defaults on load
  useEffect(() => {
    if (execDefaults) {
      setHz(execDefaults.hz);
      setHeadless(execDefaults.headless);
      setRecord(execDefaults.record);
      setNoRealtime(execDefaults.no_realtime);
      setTimeout_(execDefaults.timeout);
      setOsiEnabled(execDefaults.osi.enabled);
      setOsiIp(execDefaults.osi.ip);
      setAutolight(execDefaults.autolight);
      if (execDefaults.window) {
        setWinX(execDefaults.window.x);
        setWinY(execDefaults.window.y);
        setWinW(execDefaults.window.w);
        setWinH(execDefaults.window.h);
      }
    }
  }, [execDefaults]);

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
          window: { x: winX, y: winY, w: winW, h: winH },
          extra_args: [],
        },
      }),
    onSuccess: (data) => {
      navigate(`/simulations/${data.job_id}`);
    },
  });

  const scripts = scriptsData?.scripts ?? [];

  return (
    <div className="max-w-2xl">
      <h1 className="text-2xl font-bold mb-6">Run Simulation</h1>

      <div className="space-y-6">
        {/* Scenario Selection */}
        <section className="bg-gray-900 rounded-lg border border-gray-800 p-4">
          <h2 className="text-sm font-medium text-gray-400 mb-3">Scenario</h2>
          <select
            value={scenarioId}
            onChange={(e) => setScenarioId(e.target.value)}
            className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
          >
            <option value="">Select a scenario...</option>
            {scenarios?.map((s) => (
              <option key={s.id} value={s.id}>{s.id}</option>
            ))}
          </select>
        </section>

        {/* Controller Selection */}
        <section className="bg-gray-900 rounded-lg border border-gray-800 p-4">
          <h2 className="text-sm font-medium text-gray-400 mb-3">Controller</h2>
          <div className="flex gap-3 mb-4">
            <button
              onClick={() => setControllerType('default')}
              className={`px-4 py-2 rounded text-sm font-medium transition-colors ${
                controllerType === 'default'
                  ? 'bg-blue-600 text-white'
                  : 'bg-gray-800 text-gray-300 hover:bg-gray-700'
              }`}
            >
              Default
            </button>
            <button
              onClick={() => setControllerType('python')}
              className={`px-4 py-2 rounded text-sm font-medium transition-colors ${
                controllerType === 'python'
                  ? 'bg-blue-600 text-white'
                  : 'bg-gray-800 text-gray-300 hover:bg-gray-700'
              }`}
            >
              Python Driver
            </button>
          </div>

          {controllerType === 'python' && (
            <div className="space-y-3 pl-1">
              <div>
                <label className="block text-xs text-gray-500 mb-1">Python Script</label>
                <select
                  value={pythonScript}
                  onChange={(e) => {
                    setPythonScript(e.target.value);
                    const script = scripts.find((s: ScriptInfo) => s.path === e.target.value);
                    if (script?.classes.length) {
                      setPythonClass(script.classes[0]);
                    }
                  }}
                  className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
                >
                  {scripts.map((s: ScriptInfo) => (
                    <option key={s.path} value={s.path}>
                      {s.recommended ? '\u2605 ' : ''}{s.name} ({s.category})
                    </option>
                  ))}
                </select>
              </div>
              <div>
                <label className="block text-xs text-gray-500 mb-1">Class Name</label>
                <select
                  value={pythonClass}
                  onChange={(e) => setPythonClass(e.target.value)}
                  className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
                >
                  {(scripts.find((s: ScriptInfo) => s.path === pythonScript)?.classes ?? []).map((c: string) => (
                    <option key={c} value={c}>{c}</option>
                  ))}
                </select>
              </div>
              <label className="flex items-center gap-2 text-sm">
                <input
                  type="checkbox"
                  checked={traceEnabled}
                  onChange={(e) => setTraceEnabled(e.target.checked)}
                  className="rounded"
                />
                Enable trace logging
              </label>
            </div>
          )}
        </section>

        {/* Execution Parameters */}
        <section className="bg-gray-900 rounded-lg border border-gray-800 p-4">
          <h2 className="text-sm font-medium text-gray-400 mb-3">Execution Parameters</h2>
          <div className="grid grid-cols-2 gap-4">
            <div>
              <label className="block text-xs text-gray-500 mb-1">Frequency (Hz)</label>
              <input
                type="number"
                value={hz}
                onChange={(e) => setHz(Number(e.target.value))}
                className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs text-gray-500 mb-1">Timeout (s)</label>
              <input
                type="number"
                value={timeout}
                onChange={(e) => setTimeout_(Number(e.target.value))}
                className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
              />
            </div>
          </div>

          <div className="flex flex-wrap gap-4 mt-4">
            <label className="flex items-center gap-2 text-sm">
              <input type="checkbox" checked={headless} onChange={(e) => setHeadless(e.target.checked)} className="rounded" />
              Headless
            </label>
            <label className="flex items-center gap-2 text-sm">
              <input type="checkbox" checked={record} onChange={(e) => setRecord(e.target.checked)} className="rounded" />
              Record
            </label>
            <label className="flex items-center gap-2 text-sm">
              <input type="checkbox" checked={noRealtime} onChange={(e) => setNoRealtime(e.target.checked)} className="rounded" />
              No Realtime
            </label>
            <label className="flex items-center gap-2 text-sm">
              <input type="checkbox" checked={autolight} onChange={(e) => setAutolight(e.target.checked)} className="rounded" />
              AutoLight
            </label>
          </div>

          <div className="mt-4">
            <label className="flex items-center gap-2 text-sm">
              <input type="checkbox" checked={osiEnabled} onChange={(e) => setOsiEnabled(e.target.checked)} className="rounded" />
              OSI Output
            </label>
            {osiEnabled && (
              <div className="mt-2">
                <label className="block text-xs text-gray-500 mb-1">OSI IP Address</label>
                <input
                  type="text"
                  value={osiIp}
                  onChange={(e) => setOsiIp(e.target.value)}
                  className="w-48 bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
                />
              </div>
            )}
          </div>

          {!headless && (
            <div className="mt-4">
              <h3 className="text-xs text-gray-500 mb-2">Window Position & Size</h3>
              <div className="grid grid-cols-4 gap-3">
                <div>
                  <label className="block text-xs text-gray-500 mb-1">X</label>
                  <input
                    type="number"
                    value={winX}
                    onChange={(e) => setWinX(Number(e.target.value))}
                    className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
                  />
                </div>
                <div>
                  <label className="block text-xs text-gray-500 mb-1">Y</label>
                  <input
                    type="number"
                    value={winY}
                    onChange={(e) => setWinY(Number(e.target.value))}
                    className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
                  />
                </div>
                <div>
                  <label className="block text-xs text-gray-500 mb-1">Width</label>
                  <input
                    type="number"
                    value={winW}
                    onChange={(e) => setWinW(Number(e.target.value))}
                    className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
                  />
                </div>
                <div>
                  <label className="block text-xs text-gray-500 mb-1">Height</label>
                  <input
                    type="number"
                    value={winH}
                    onChange={(e) => setWinH(Number(e.target.value))}
                    className="w-full bg-gray-800 border border-gray-700 rounded px-3 py-2 text-sm focus:outline-none focus:border-blue-500"
                  />
                </div>
              </div>
            </div>
          )}
        </section>

        {/* Submit */}
        <button
          onClick={() => mutation.mutate()}
          disabled={!scenarioId || mutation.isPending}
          className="w-full bg-blue-600 hover:bg-blue-500 disabled:bg-gray-700 disabled:text-gray-500 text-white font-medium py-3 rounded-lg transition-colors"
        >
          {mutation.isPending ? 'Starting...' : 'Run Simulation'}
        </button>

        {mutation.error && (
          <p className="text-red-400 text-sm">{String(mutation.error)}</p>
        )}
      </div>
    </div>
  );
}
