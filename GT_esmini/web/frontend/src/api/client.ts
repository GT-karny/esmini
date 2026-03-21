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

export interface ManualDriveConfig {
  input_type: string;
  physics_type: string;
  ffb_enabled: boolean;
  domain: { lateral: string; longitudinal: string };
  sdl2: {
    device_index: number;
    deadzone: number;
    button_mapping: {
      upshift: number;
      downshift: number;
      override: number;
      indicator_left: number;
      indicator_right: number;
      headlight: number;
      high_beam: number;
      fog_light: number;
      hazard: number;
    };
  };
  input_network: { transport_type: string; port: number; level: string };
  physics_network: { transport_type: string; host: string; cmd_port: number; state_port: number };
  ffb: { spring_coefficient: number; damper_coefficient: number; constant_gain: number; max_force: number };
}

export interface ManualDrivePreset {
  name: string;
  builtin: boolean;
  config: ManualDriveConfig;
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
  manual_drive?: ManualDriveConfig;
}

export interface WindowConfig {
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface ExecutionDefaults {
  hz: number;
  headless: boolean;
  record: boolean;
  no_realtime: boolean;
  timeout: number;
  osi: { enabled: boolean; ip: string };
  autolight: boolean;
  vehicle_physics: boolean;
  threads: boolean;
  window: WindowConfig;
}

export interface SimulationRequest {
  scenario_id: string;
  project_id?: string;
  controller: ControllerConfig;
  execution: {
    headless: boolean;
    record: boolean;
    hz: number;
    no_realtime: boolean;
    timeout: number;
    osi: { enabled: boolean; ip: string };
    autolight: boolean;
    vehicle_physics: boolean;
    threads: boolean;
    window: WindowConfig;
    extra_args: string[];
  };
  param_overrides?: Record<string, string>;
}

export interface SimulationStatus {
  job_id: string;
  scenario_id: string;
  project_id: string | null;
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

// --- Project types ---

export interface Project {
  project_id: string;
  name: string;
  description: string;
  is_builtin: boolean;
  scenario_count: number;
  road_count: number;
  file_count: number;
  created_at: string;
  updated_at: string;
}

export interface ProjectDetail extends Project {
  root_path: string;
}

export interface ProjectFile {
  path: string;
  name: string;
  type: string;
  size: number;
  modified: string;
  is_dir: boolean;
}

export interface ScenarioParam {
  name: string;
  type: string;
  value: string;
}

export interface ScenarioInfo {
  file: string;
  filename: string;
  road_file: string | null;
  entities: Array<{ name: string; model: string | null; controller: string | null }>;
  params: ScenarioParam[];
  has_controller: boolean;
}

export interface ParameterPreset {
  preset_id: string;
  project_id: string;
  scenario_file: string;
  name: string;
  values: Record<string, string>;
  created_at: string;
}

// --- API functions ---

export const api = {
  // Projects
  getProjects: () =>
    request<Project[]>('/api/projects'),

  getProject: (projectId: string) =>
    request<ProjectDetail>(`/api/projects/${projectId}`),

  createProject: (name: string, description = '') =>
    request<ProjectDetail>('/api/projects', {
      method: 'POST',
      body: JSON.stringify({ name, description }),
    }),

  getProjectTemplateUrl: () => '/api/projects/template/download',

  uploadProject: async (file: File, name: string, description = '') => {
    const form = new FormData();
    form.append('file', file);
    form.append('name', name);
    form.append('description', description);
    const res = await fetch('/api/projects/upload', { method: 'POST', body: form });
    if (!res.ok) throw new Error(`${res.status}: ${await res.text()}`);
    return res.json() as Promise<ProjectDetail>;
  },

  updateProject: (projectId: string, data: { name?: string; description?: string }) =>
    request<{ status: string }>(`/api/projects/${projectId}`, {
      method: 'PUT',
      body: JSON.stringify(data),
    }),

  deleteProject: (projectId: string) =>
    request<{ status: string }>(`/api/projects/${projectId}`, { method: 'DELETE' }),

  // Project files
  getProjectFiles: (projectId: string) =>
    request<ProjectFile[]>(`/api/projects/${projectId}/files`),

  uploadProjectFile: async (projectId: string, file: File, path?: string) => {
    const form = new FormData();
    form.append('file', file);
    if (path) form.append('path', path);
    const res = await fetch(`/api/projects/${projectId}/files`, { method: 'POST', body: form });
    if (!res.ok) throw new Error(`${res.status}: ${await res.text()}`);
    return res.json() as Promise<{ status: string; path: string }>;
  },

  downloadProjectFile: (projectId: string, filePath: string) =>
    `/api/projects/${projectId}/files/${filePath}`,

  deleteProjectFile: (projectId: string, filePath: string) =>
    request<{ status: string }>(`/api/projects/${projectId}/files/${filePath}`, { method: 'DELETE' }),

  // Project scenarios
  getProjectScenarios: (projectId: string) =>
    request<ScenarioInfo[]>(`/api/projects/${projectId}/scenarios`),

  getScenarioParams: (projectId: string, scenarioFile: string) =>
    request<ScenarioParam[]>(`/api/projects/${projectId}/scenarios/${scenarioFile}/params`),

  getRoadGeometry: (projectId: string, scenarioFile: string) =>
    request<{ boundaries: Array<{ road_id: number; type: string; points: [number, number][] }> }>(
      `/api/projects/${projectId}/scenarios/${scenarioFile}/road-geometry`,
    ),

  getScenarioDocs: async (projectId: string, scenarioFile: string): Promise<string | null> => {
    const res = await fetch(`${BASE}/api/projects/${projectId}/scenarios/${scenarioFile}/docs`);
    return res.ok ? res.text() : null;
  },

  // Parameter presets
  getPresets: (projectId: string, scenarioFile: string) =>
    request<ParameterPreset[]>(`/api/projects/${projectId}/scenarios/${scenarioFile}/presets`),

  createPreset: (projectId: string, scenarioFile: string, name: string, values: Record<string, string>) =>
    request<ParameterPreset>(`/api/projects/${projectId}/scenarios/${scenarioFile}/presets`, {
      method: 'POST',
      body: JSON.stringify({ name, values }),
    }),

  updatePreset: (projectId: string, presetId: string, data: { name?: string; values?: Record<string, string> }) =>
    request<{ status: string }>(`/api/projects/${projectId}/presets/${presetId}`, {
      method: 'PUT',
      body: JSON.stringify(data),
    }),

  deletePreset: (projectId: string, presetId: string) =>
    request<{ status: string }>(`/api/projects/${projectId}/presets/${presetId}`, { method: 'DELETE' }),

  // Scenarios (legacy)
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

  getSimulations: (status?: string, limit = 20, offset = 0, projectId?: string, scenarioId?: string) =>
    request<{ jobs: SimulationStatus[]; total: number }>(
      `/api/simulations?limit=${limit}&offset=${offset}${status ? `&status=${status}` : ''}${projectId ? `&project_id=${projectId}` : ''}${scenarioId ? `&scenario_id=${scenarioId}` : ''}`
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

  // Manual Drive config
  getManualDriveConfig: () =>
    request<ManualDriveConfig>('/api/manual-drive/config'),

  updateManualDriveConfig: (config: ManualDriveConfig) =>
    request<ManualDriveConfig>('/api/manual-drive/config', {
      method: 'PUT',
      body: JSON.stringify(config),
    }),

  getManualDrivePresets: () =>
    request<ManualDrivePreset[]>('/api/manual-drive/presets'),

  saveManualDrivePreset: (name: string, config: ManualDriveConfig) =>
    request<ManualDrivePreset>('/api/manual-drive/presets', {
      method: 'POST',
      body: JSON.stringify({ name, config }),
    }),

  deleteManualDrivePreset: (name: string) =>
    request<{ status: string }>(`/api/manual-drive/presets/${encodeURIComponent(name)}`, {
      method: 'DELETE',
    }),

  // System
  getSystemInfo: () =>
    request<Record<string, unknown>>('/api/config/system'),
};
