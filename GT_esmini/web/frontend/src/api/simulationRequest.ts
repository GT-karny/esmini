import type { ControllerConfig, SimulationRequest } from './client';

type SimulationExecution = SimulationRequest['execution'];

export type SimulationExecutionOverrides = Partial<
  Omit<SimulationExecution, 'extra_args' | 'osi' | 'window'>
> & {
  extra_args?: string[];
  osi?: Partial<SimulationExecution['osi']>;
  window?: Partial<SimulationExecution['window']>;
};

export interface BuildSimulationRequestOptions {
  scenarioId: string;
  projectId?: string;
  controller?: ControllerConfig;
  controllerType?: string;
  execution?: SimulationExecutionOverrides;
  paramOverrides?: Record<string, string>;
}

const DEFAULT_EXECUTION: SimulationExecution = {
  headless: true,
  record: false,
  hz: 100,
  no_realtime: false,
  timeout: 120,
  osi: { enabled: false, ip: '127.0.0.1' },
  autolight: false,
  vehicle_physics: false,
  kinematic_mode: false,
  route_drive_mode: false,
  threads: false,
  window: { x: 60, y: 60, w: 1280, h: 720 },
  extra_args: [],
  drive_mode: 'comfort',
};

export function buildSimulationRequest({
  scenarioId,
  projectId,
  controller,
  controllerType = 'virtual_driver',
  execution,
  paramOverrides,
}: BuildSimulationRequestOptions): SimulationRequest {
  const mergedExecution = mergeExecution(execution);
  const request: SimulationRequest = {
    scenario_id: scenarioId,
    ...(projectId ? { project_id: projectId } : {}),
    controller: controller ?? { controller_type: controllerType },
    execution: mergedExecution,
  };

  if (paramOverrides !== undefined) {
    request.param_overrides = paramOverrides;
  }

  return request;
}

function mergeExecution(overrides: SimulationExecutionOverrides = {}): SimulationExecution {
  const { extra_args, osi, window, ...rest } = overrides;
  return {
    ...DEFAULT_EXECUTION,
    ...rest,
    osi: { ...DEFAULT_EXECUTION.osi, ...osi },
    window: { ...DEFAULT_EXECUTION.window, ...window },
    extra_args: extra_args ? [...extra_args] : [],
  };
}
