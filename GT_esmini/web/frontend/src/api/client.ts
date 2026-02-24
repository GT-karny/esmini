const BASE = '';

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json', ...init?.headers },
    ...init,
  });
  if (!res.ok) {
    const body = await res.text();
    throw new Error(`${res.status}: ${body}`);
  }
  return res.json();
}

// --- Types ---

export interface Scenario {
  id: string;
  filename: string;
  path: string;
  modified: string;
  size: number;
}

export interface ScenarioEntity {
  name: string;
  vehicle: string | null;
  controller: string | null;
}

export interface ScenarioDetail {
  id: string;
  filename: string;
  path: string;
  road_file: string | null;
  entities: ScenarioEntity[];
  has_controller: boolean;
}

export interface ScriptInfo {
  path: string;
  name: string;
  category: string;
  classes: string[];
  recommended: boolean;
}

export interface ControllerConfig {
  controller_type: string;
  python: {
    script: string;
    class: string;
    python_home: string;
    trace_enabled: boolean;
    trace_dir: string;
  };
}

export interface ExecutionDefaults {
  hz: number;
  headless: boolean;
  record: boolean;
  no_realtime: boolean;
  timeout: number;
  osi: { enabled: boolean; ip: string };
  autolight: boolean;
  window: number[];
}

export interface SimulationRequest {
  scenario_id: string;
  controller: ControllerConfig;
  execution: {
    headless: boolean;
    record: boolean;
    hz: number;
    no_realtime: boolean;
    timeout: number;
    osi: { enabled: boolean; ip: string };
    autolight: boolean;
    extra_args: string[];
  };
}

export interface SimulationStatus {
  job_id: string;
  scenario_id: string;
  status: string;
  controller_type: string;
  progress_pct: number;
  pid: number | null;
  exit_code: number | null;
  output_dir: string | null;
  started_at: string | null;
  completed_at: string | null;
  error_message: string | null;
  options: Record<string, unknown>;
}

export interface ResultFile {
  name: string;
  size: number;
  type: string;
}

export interface ResultMeta {
  job_id: string;
  scenario_id: string;
  files: ResultFile[];
  metrics: Record<string, unknown> | null;
}

// --- API functions ---

export const api = {
  // Scenarios
  getScenarios: (search?: string) =>
    request<Scenario[]>(`/api/scenarios${search ? `?search=${encodeURIComponent(search)}` : ''}`),

  getScenario: (id: string) =>
    request<ScenarioDetail>(`/api/scenarios/${id}`),

  // Scripts
  getScripts: () =>
    request<{ scripts: ScriptInfo[] }>('/api/scripts'),

  // Controller config
  getControllerPresets: () =>
    request<Array<{ name: string; description: string; config: ControllerConfig }>>('/api/controller-config/presets'),

  getControllerConfig: () =>
    request<ControllerConfig>('/api/controller-config/current'),

  updateControllerConfig: (config: ControllerConfig) =>
    request<ControllerConfig>('/api/controller-config/current', {
      method: 'PUT',
      body: JSON.stringify(config),
    }),

  // Execution defaults
  getExecutionDefaults: () =>
    request<ExecutionDefaults>('/api/config/execution-defaults'),

  updateExecutionDefaults: (params: ExecutionDefaults) =>
    request<ExecutionDefaults>('/api/config/execution-defaults', {
      method: 'PUT',
      body: JSON.stringify(params),
    }),

  // Simulations
  createSimulation: (req: SimulationRequest) =>
    request<SimulationStatus>('/api/simulations', {
      method: 'POST',
      body: JSON.stringify(req),
    }),

  getSimulations: (status?: string, limit = 20, offset = 0) =>
    request<{ jobs: SimulationStatus[]; total: number }>(
      `/api/simulations?limit=${limit}&offset=${offset}${status ? `&status=${status}` : ''}`
    ),

  getSimulation: (jobId: string) =>
    request<SimulationStatus>(`/api/simulations/${jobId}`),

  cancelSimulation: (jobId: string) =>
    request<{ job_id: string; status: string }>(`/api/simulations/${jobId}`, {
      method: 'DELETE',
    }),

  // Results
  getResultMeta: (jobId: string) =>
    request<ResultMeta>(`/api/results/${jobId}`),

  getMetrics: (jobId: string) =>
    request<Record<string, unknown>>(`/api/results/${jobId}/metrics`),

  getTimeseries: (jobId: string, fields?: string, entity = 'Ego') =>
    request<{ data: Record<string, number>[]; entity: string; fields: string[] }>(
      `/api/results/${jobId}/timeseries?entity=${entity}${fields ? `&fields=${fields}` : ''}`
    ),

  // System
  getSystemInfo: () =>
    request<Record<string, unknown>>('/api/config/system'),
};
