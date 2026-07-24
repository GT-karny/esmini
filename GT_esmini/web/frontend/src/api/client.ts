/**
 * ============================================================================
 * FROZEN CONTRACT — unified UI migration (2026-07-13)
 * ============================================================================
 * This file is the SINGLE SOURCE OF TRUTH for the REST type contract that the
 * GT-OpenSCENARIOEditor port consumes. The editor's `packages/esmini` public
 * types are being derived from the request/response types and endpoint paths
 * declared here.
 *
 * While the frontend feature freeze is in effect:
 *   - Do NOT change types, function signatures, or endpoint paths here except
 *     to fix an outright bug.
 *   - If a contract change is genuinely required, it MUST be synchronized with
 *     the editor-side contract types in `packages/esmini` (the port depends on
 *     these staying in lockstep); coordinate the change on both sides in the
 *     same review — never diverge them silently.
 *
 * See GT-monorepo/unified-ui-migration-plan-2026-07-13.md for the migration
 * plan and the contract-freeze rationale (fork frontend = frozen, editor
 * `packages/esmini` = the ongoing home of this contract).
 * ============================================================================
 */

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

// --- OpenDRIVE side-model metadata (plan P9a) ---

export interface OdrAuditWarnings {
  version: { rev_major: number; rev_minor: number };
  unsupported_elements: number;
  unsupported_attributes: number;
  removed16_hits: number;
  entries: string[];
}

export interface OdrUserDataItem {
  owner_path: string;
  context_id: string;
  xml: string;
}

export interface OdrSignalSemantics {
  speeds: Array<{ type: string; value: number; unit: string }>;
  lane_types: string[];
  priority_types: string[];
  prohibited: Array<{ kind: string; category: string }>;
  warning_count: number;
}

export interface OdrSignal {
  road_id: string;
  signal_id: string;
  has_semantics: boolean;
  semantics: OdrSignalSemantics;
  dependencies: Array<{ id: string; type: string }>;
  references: Array<{ element_type: string; element_id: string; type: string }>;
  temporary: boolean;
  invalidated: boolean;
}

export interface OdrJunctionPriority {
  junction_id: string;
  type: string;
  priorities: Array<{ high: string; low: string }>;
}

export interface OdrCrosswalk {
  junction_id: string;
  id: string;
  crossing_road: string;
  road_at_start: string;
  road_at_end: string;
  synth_object_id: number;
}

export interface OdrRailTrackRef {
  id: string;
  s: number;
  dir: string;
}

export interface OdrRailSwitch {
  road_id: string;
  name: string;
  id: string;
  position: string;
  main_track: OdrRailTrackRef;
  side_track: OdrRailTrackRef;
  partner: { name: string; id: string } | null;
}

export interface OdrRailStation {
  id: string;
  name: string;
  type: string;
  platforms: Array<{
    id: string;
    name: string;
    segments: Array<{ road_id: string; s_start: number; s_end: number; side: string }>;
  }>;
}

// P9b: 1.9 lane layers (P8 shadow storage + process selection-mode latch)
export interface OdrLaneLayerSection {
  s: number;
  length: number;
  has_length: boolean;
  lane_count: number;
}

export interface OdrLaneLayer {
  name: string; // "permanent" | "temporary" (effective name; never empty)
  lane_offset_count: number;
  sections: OdrLaneLayerSection[];
}

export interface OdrRoadLaneLayers {
  road_id: string;
  active_mode: string; // mode resolved at parse time for this road
  has_temporary: boolean;
  temp_s_start: number;
  temp_s_end: number;
  layers: OdrLaneLayer[];
}

export interface OdrLaneLayers {
  mode: string; // process-wide GT_ODR_LANE_LAYERS latch: "permanent" | "temporary"
  roads: OdrRoadLaneLayers[];
}

// P9b: virtual-junction (P6) metadata
export interface OdrVirtualJunction {
  junction_id: string;
  name: string;
  main_road_id: string;
  main_road_length: number;
  s_start: number;
  s_end: number;
  orientation: string; // "+" | "-" | ""
  anchor_count: number;
  connection_count: number;
}

export interface OdrMetadata {
  warnings: OdrAuditWarnings;
  user_data: OdrUserDataItem[];
  data_quality: OdrUserDataItem[];
  signals: OdrSignal[];
  junction_priorities: OdrJunctionPriority[];
  crosswalks: OdrCrosswalk[];
  railroad: { switches: OdrRailSwitch[]; stations: OdrRailStation[] };
  lane_layers: OdrLaneLayers;
  virtual_junctions: OdrVirtualJunction[];
}

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
  keyboard: {
    steer_left: string;
    steer_right: string;
    throttle: string;
    brake: string;
    clutch: string;
    upshift: string;
    downshift: string;
    override_key: string;
    indicator_left: string;
    indicator_right: string;
    headlight: string;
    high_beam: string;
    fog_light: string;
    hazard: string;
    steer_rate: number;
    centering_rate: number;
    pedal_press_rate: number;
    pedal_release_rate: number;
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

/**
 * AutoLight (F6) environment-driven headlight config — the editable keys of
 * GT_esmini/config/auto_light.json. The GET payload also carries "// ..." comment
 * keys (spec docs); those are not modeled here and are preserved server-side.
 */
export interface AutoLightConfig {
  headlight_enabled: boolean;
  headlight_illuminance_lux_threshold: number;
  headlight_sun_elevation_deg: number;
  headlight_use_time_of_day: boolean;
  headlight_dusk_hour: number;
  headlight_dawn_hour: number;
  headlight_tunnel_enabled: boolean;
  highbeam_enabled: boolean;
  highbeam_range_m: number;
  highbeam_range_hysteresis_m: number;
  highbeam_corridor_half_width_m: number;
  highbeam_on_delay_s: number;
  highbeam_off_delay_s: number;
}

// VirtualDriver (Phase 1-3) runtime config — mirrors config/virtual_driver.json's
// editable keys (issue #33). All fields optional to tolerate partial payloads,
// matching how the shared file is read (GET returns the on-disk file verbatim,
// including "_..." comment keys and runner-owned input_* fields not listed here).
export interface VirtualDriverConfig {
  // Phase 3 traffic policies
  policy_lead_enabled?: boolean;
  policy_traffic_light_enabled?: boolean;
  policy_stop_yield_enabled?: boolean;
  policy_conflict_enabled?: boolean;
  policy_crosswalk_enabled?: boolean;
  policy_junction_priority_enabled?: boolean;
  policy_aeb_enabled?: boolean;
  // Planner
  horizon_s?: number;
  short_dt?: number;
  max_lateral_accel?: number;
  comfort_decel?: number;
  emergency_decel?: number;
  comfort_jerk?: number;
  scan_distance?: number;
  scan_step?: number;
  turn_speed?: number;
  min_turn_speed?: number;
  stop_band?: number;
  respect_speed_limit?: boolean;
  // Driver model (PID + Pure Pursuit)
  lookahead_gain?: number;
  min_lookahead?: number;
  max_lookahead?: number;
  max_steer_angle?: number;
  steering_sign?: number;
  speed_kp?: number;
  speed_ki?: number;
  speed_kd?: number;
  control_point_offset?: number;
  control_point_min_speed?: number;
  // Indicator
  indicator_lead_time?: number;
  indicator_min_on_time?: number;
  // 3a lead-vehicle IDM follow
  idm_time_headway?: number;
  idm_min_gap?: number;
  idm_max_accel?: number;
  idm_comfort_decel?: number;
  idm_desired_speed?: number;
  idm_lookahead?: number;
  idm_lateral_tol?: number;
  idm_target_horizon?: number;
  // 3b traffic light
  tl_lookahead?: number;
  tl_yellow_decel?: number;
  tl_stop_margin?: number;
  // 3c stop / yield sign
  sign_lookahead?: number;
  stop_hold_time?: number;
  stop_detect_speed?: number;
  stop_line_tol?: number;
  creep_speed?: number;
  creep_advance?: number;
  yield_creep_speed?: number;
  sign_stop_margin?: number;
  // 3d conflict-corridor resolver
  conflict_lookahead?: number;
  conflict_step?: number;
  conflict_lane_margin?: number;
  conflict_standoff?: number;
  conflict_release_buffer?: number;
  conflict_pet?: number;
  conflict_nominal_speed?: number;
  conflict_min_cross_angle_deg?: number;
  conflict_other_min_speed?: number;
  conflict_area_eps?: number;
  // 3d ext crosswalk pedestrian yield
  crosswalk_lookahead?: number;
  crosswalk_step?: number;
  crosswalk_standoff?: number;
  crosswalk_wait_margin?: number;
  crosswalk_yield_to_waiting?: boolean;
  crosswalk_ped_signal_aware?: boolean;
  crosswalk_signal_link_radius?: number;
  crosswalk_release_lateral_margin?: number;
  // AEB (autonomous emergency braking) — forward-collision guardian
  aeb_ttc_threshold?: number;
  aeb_lateral_tol?: number;
  aeb_min_a_req?: number;
  aeb_stop_margin?: number;
  // Manual override (reuses ManualDrive OverrideManager)
  override_enabled?: boolean;
  override_button?: boolean;
  steering_threshold?: number;
  throttle_threshold?: number;
  brake_threshold?: number;
  auto_return_timeout?: number;
  override_lateral?: 'manual' | 'scenario';
  override_longitudinal?: 'manual' | 'scenario';

  // SDL2 wheel button bindings (integer joystick button IDs; -1 = unassigned).
  // Only consumed when input_type=="sdl2_wheel"; sdl2_auto_resume_button is
  // feature:F7's manual->auto RESUME.
  sdl2_override_button?: number;
  sdl2_indicator_left_button?: number;
  sdl2_indicator_right_button?: number;
  sdl2_upshift_button?: number;
  sdl2_downshift_button?: number;
  sdl2_headlight_button?: number;
  sdl2_high_beam_button?: number;
  sdl2_fog_light_button?: number;
  sdl2_hazard_button?: number;
  sdl2_auto_resume_button?: number;
}

export interface ControllerConfig {
  controller_type: string;
  python?: {
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
  kinematic_mode: boolean;
  route_drive_mode: boolean;
  route_drive_timing?: 'late' | 'normal' | 'early';
  route_drive_gap?: 'wide' | 'normal' | 'tight';
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
    /** F6: pass --autolight-headlights (env-driven headlights, overrides config master switch). */
    autolight_headlights: boolean;
    vehicle_physics: boolean;
    kinematic_mode: boolean;
    route_drive_mode: boolean;
    route_drive_timing?: 'late' | 'normal' | 'early';
    route_drive_gap?: 'wide' | 'normal' | 'tight';
    threads: boolean;
    window: WindowConfig;
    extra_args: string[];
    drive_mode?: 'comfort' | 'sport';
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
  name: string;
  description?: string;
  values: Record<string, string>;
}

// --- VirtualDriver verification (replay) ---

export interface VdPreviewPoint {
  x: number;
  y: number;
  v: number;
  t: number;
}

/* Mid/long planner output (Phase 2). Optional on the telemetry frame: emitted
 * once A2's VirtualDriverTelemetryJson.cpp serializes the `midlong` section.
 * Until then it is undefined and the v_target chart / maneuver-marker layers
 * degrade gracefully. Keys/shape must match the C++ serializer — this interface
 * is the single frontend reconciliation point (see plan §2a). */
export type MidLongConstraintKind = 'curve' | 'junction' | 'speed_limit' | 'stop';

export interface MidLongConstraint {
  s: number;     // route s the constraint applies at [m]
  x: number;     // world position [m]
  y: number;     // world position [m]
  v: number;     // target speed at the constraint [m/s]
  kind: MidLongConstraintKind;
}

export interface MidLongProfile {
  v_target_profile: [number, number][];  // (s [m], v_max [m/s]) pairs along the route
  constraints?: MidLongConstraint[];       // [A2] labelled constraint points (with XY)
  valid: boolean;
}

/* [A3 / Phase 3] Traffic-policy output. Each enabled policy (lead-vehicle /
 * traffic-light / stop-yield sign) emits PolicyConstraints; the mid/long planner
 * folds them into the v_target ceiling. Shape mirrors the C++ serializer
 * (VirtualDriverTelemetryJson.cpp `policy` block). PolicyConstraint carries no
 * world XY (kind/s/value/source only) — the planner echoes stop/yield points into
 * `midlong.constraints` (with XY, kind 'stop') for scene markers, so the scene
 * layer reuses maneuverMarkers; this `policy` block drives the timeline panel. */
export type PolicyConstraintKind =
  | 'none' | 'stop_at_s' | 'max_speed' | 'max_speed_to_s' | 'yield' | 'wait_until';

/** Arbitration tier (AEB phase 1). Only AebSafety emits 'safety'; every other
 * policy leaves it at 'comfort'. Optional because telemetry recorded before the
 * field was serialized (W2) has no `tier`. */
export type PolicyConstraintTier = 'comfort' | 'courtesy' | 'compliance' | 'safety';

export interface PolicyConstraint {
  kind: PolicyConstraintKind;
  s: number;        // route s ahead of the ego the constraint applies at/until [m]
  value: number;    // speed [m/s] or time [s] depending on kind
  source: string;   // "lead_vehicle" | "traffic_light" | "stop_sign" | "yield_sign" | ...
  tier?: PolicyConstraintTier;
}

export interface TrafficPolicySnapshot {
  valid: boolean;
  constraints: PolicyConstraint[];
}

export interface VdTelemetryFrame {
  sim_time: number;
  ego: {
    x: number; y: number; z: number; h: number; speed: number;
    track?: number; lane?: number; offset?: number; s?: number;
  };
  // feature:F7 — manual_transition / auto_transition are the single-frame
  // edges of the AUTO<->MANUAL flip. Optional so older telemetry sources still
  // deserialize; the panel only uses them when present.
  override: {
    lateral: boolean;
    longitudinal: boolean;
    manual_transition?: boolean;
    auto_transition?: boolean;
  };
  driver: {
    throttle: number; brake: number; steer: number;
    lateral_error: number; heading_error: number; speed_error: number;
    lookahead: number; valid: boolean;
  };
  indicator: { left: boolean; right: boolean };
  preview: { dt: number; valid: boolean; points: VdPreviewPoint[] };
  midlong?: MidLongProfile;  // Phase 2+ (optional; see MidLongProfile)
  policy?: TrafficPolicySnapshot;  // Phase 3+ (optional; see TrafficPolicySnapshot)
}

export interface VerificationRun {
  id: string;
  meta: Record<string, unknown> & { scenario?: string; frames?: number; sim_duration_s?: number };
  has_compare: boolean;
  has_verdict: boolean;
}

export interface BaselinePoint {
  t: number;
  x: number;
  y: number;
  speed: number;
}

export interface VerdictResult {
  event: string;
  status: 'pass' | 'fail' | 'skip';
  detail?: string;
  reason?: string;
  t?: number;
  idx?: number;
}

/** One recorded OSI scene frame (other traffic + signal phases) for replay. */
export interface SceneFrame {
  sim_time: number;
  objects: Record<string, unknown>[];
  traffic_lights: Record<string, unknown>[];
}

export interface VerificationTelemetry {
  id: string;
  meta: Record<string, unknown> & { project_id?: string | null; scenario_file?: string };
  frames: VdTelemetryFrame[];
  scene: SceneFrame[];
  compare: { xy_rmse_m?: number; speed_rmse_mps?: number; xy_max_dev_m?: number } | null;
  verdict: { overall?: string; summary?: { pass: number; fail: number; skip: number }; results?: VerdictResult[] } | null;
  baseline_track: BaselinePoint[] | null;
}

// --- VirtualDriver verification (annotation) ---

export type AnnotationLabel = 'pass' | 'fail' | 'needs-discussion';

/** A run from the verification_runs registry, joined with its human annotation. */
export interface AnnotationRun {
  run_id: string;
  source: 'toplevel' | 'batch' | 'gui';
  batch_id: string | null;
  scenario: string | null;
  scenario_stem: string | null;
  project_id: string | null;
  scenario_file: string | null;
  frames: number | null;
  sim_duration_s: number | null;
  verdict_overall: string | null;  // pass|fail|needs-review|error (auto)
  verdict_summary: { pass: number; fail: number; skip: number } | null;
  has_compare: boolean;
  has_verdict: boolean;
  label: AnnotationLabel | null;    // human label
  comment: string | null;
  labeled: boolean;
  updated_at: string | null;
}

export interface Annotation {
  run_id: string;
  label: AnnotationLabel;
  comment: string;
  labeler: string;
  scenario?: string | null;
  scenario_stem?: string | null;
  created_at: string | null;
  updated_at: string | null;
}

export interface AnnotationInput {
  label: AnnotationLabel;
  comment?: string;
  labeler?: string;
}

export interface MatchResult {
  run_id: string;
  label: AnnotationLabel;
  comment: string;
  score: number;
  reasons: string[];
}

export interface AnnotationRunFilters {
  status?: string;
  batch_id?: string;
  labeled?: boolean;
  source?: string;
}

function annotationQuery(params?: AnnotationRunFilters): string {
  if (!params) return '';
  const q = new URLSearchParams();
  if (params.status) q.set('status', params.status);
  if (params.batch_id) q.set('batch_id', params.batch_id);
  if (params.source) q.set('source', params.source);
  if (params.labeled != null) q.set('labeled', String(params.labeled));
  const s = q.toString();
  return s ? `?${s}` : '';
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

  openProjectFolder: (projectId: string) =>
    request<{ status: string }>(`/api/projects/${projectId}/open-folder`, { method: 'POST' }),

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

  getOdrMetadata: (projectId: string, scenarioFile: string) =>
    request<OdrMetadata>(
      `/api/projects/${projectId}/scenarios/${scenarioFile}/odr-metadata`,
    ),

  // VirtualDriver verification (replay)
  getVerificationRuns: () =>
    request<{ runs: VerificationRun[] }>(`/api/verification/runs`),
  getVerificationTelemetry: (runId: string) =>
    request<VerificationTelemetry>(`/api/verification/runs/${encodeURIComponent(runId)}/telemetry`),
  runBaselineCompare: (runId: string) =>
    request<Record<string, unknown>>(
      `/api/verification/runs/${encodeURIComponent(runId)}/baseline-compare`,
      { method: 'POST' },
    ),
  runAssertions: (runId: string) =>
    request<Record<string, unknown>>(
      `/api/verification/runs/${encodeURIComponent(runId)}/assert`,
      { method: 'POST' },
    ),

  // VirtualDriver verification (annotation) — registry-backed runs + human labels
  getAnnotationRuns: (params?: AnnotationRunFilters) =>
    request<{ runs: AnnotationRun[] }>(
      `/api/verification/runs2${annotationQuery(params)}`,
    ),
  getRunDetail: (runId: string) =>
    request<AnnotationRun>(`/api/verification/run-detail/${encodeURIComponent(runId)}`),
  getAnnotation: async (runId: string): Promise<Annotation | null> => {
    const res = await fetch(`${BASE}/api/verification/annotation/${encodeURIComponent(runId)}`);
    if (res.status === 404) return null;
    if (!res.ok) throw new Error(`${res.status}: ${await res.text()}`);
    return res.json();
  },
  setAnnotation: (runId: string, body: AnnotationInput) =>
    request<Annotation>(`/api/verification/annotation/${encodeURIComponent(runId)}`, {
      method: 'POST',
      body: JSON.stringify(body),
    }),
  matchAnnotations: (runId: string, k = 5) =>
    request<{ target: { run_id: string; scenario_stem: string | null }; matches: MatchResult[] }>(
      `/api/verification/match`,
      { method: 'POST', body: JSON.stringify({ run_id: runId, k }) },
    ),
  scanRegistry: (force = true) =>
    request<{ count: number; scanned: number; skipped: boolean }>(
      `/api/verification/registry/scan?force=${force}`,
      { method: 'POST' },
    ),

  getScenarioDocs: async (projectId: string, scenarioFile: string): Promise<string | null> => {
    const res = await fetch(`${BASE}/api/projects/${projectId}/scenarios/${scenarioFile}/docs`);
    if (!res.ok || res.status === 204) return null;
    const text = await res.text();
    return text || null;
  },

  // Parameter presets
  getPresets: (projectId: string, scenarioFile: string) =>
    request<ParameterPreset[]>(`/api/projects/${projectId}/scenarios/${scenarioFile}/presets`),

  createPreset: (projectId: string, scenarioFile: string, name: string, values: Record<string, string>) =>
    request<ParameterPreset>(`/api/projects/${projectId}/scenarios/${scenarioFile}/presets`, {
      method: 'POST',
      body: JSON.stringify({ name, values }),
    }),

  updatePreset: (projectId: string, scenarioFile: string, presetId: string, data: { name?: string; values?: Record<string, string> }) =>
    request<{ status: string }>(`/api/projects/${projectId}/scenarios/${scenarioFile}/presets/${presetId}`, {
      method: 'PUT',
      body: JSON.stringify(data),
    }),

  deletePreset: (projectId: string, scenarioFile: string, presetId: string) =>
    request<{ status: string }>(`/api/projects/${projectId}/scenarios/${scenarioFile}/presets/${presetId}`, { method: 'DELETE' }),

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

  setSimulationSpeed: (jobId: string, speedFactor: number) =>
    request<{ job_id: string; speed_factor: number }>(`/api/simulations/${jobId}/speed`, {
      method: 'PUT',
      body: JSON.stringify({ speed_factor: speedFactor }),
    }),

  setDriveMode: (jobId: string, mode: 'comfort' | 'sport') =>
    request<{ job_id: string; mode: string }>(`/api/simulations/${jobId}/drive_mode`, {
      method: 'PUT',
      body: JSON.stringify({ mode }),
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

  // AutoLight (F6) config — GET returns "// ..." comment keys too (ignored by UI).
  getAutoLightConfig: () =>
    request<AutoLightConfig>('/api/auto-light/config'),

  updateAutoLightConfig: (config: Partial<AutoLightConfig>) =>
    request<AutoLightConfig>('/api/auto-light/config', {
      method: 'PUT',
      body: JSON.stringify(config),
    }),

  getAutoLightDefaults: () =>
    request<AutoLightConfig>('/api/auto-light/defaults'),

  // Virtual Driver config
  getVirtualDriverConfig: () =>
    request<VirtualDriverConfig>('/api/virtual-driver/config'),

  updateVirtualDriverConfig: (config: Partial<VirtualDriverConfig>) =>
    request<VirtualDriverConfig>('/api/virtual-driver/config', {
      method: 'PUT',
      body: JSON.stringify(config),
    }),

  getVirtualDriverDefaults: () =>
    request<VirtualDriverConfig>('/api/virtual-driver/defaults'),

  // Projects root
  getProjectsRoot: () =>
    request<{ projects_root: string | null; effective_dir: string; is_custom: boolean }>(
      '/api/config/projects-root',
    ),

  setProjectsRoot: (projectsRoot: string | null) =>
    request<{ projects_root: string | null; effective_dir: string }>(
      '/api/config/projects-root',
      { method: 'PUT', body: JSON.stringify({ projects_root: projectsRoot }) },
    ),

  // System
  getSystemInfo: () =>
    request<Record<string, unknown>>('/api/config/system'),
};
