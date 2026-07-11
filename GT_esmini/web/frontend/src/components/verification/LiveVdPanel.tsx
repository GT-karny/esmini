import { useMemo } from 'react';
import { useQuery } from '@tanstack/react-query';
import { LiveSceneView, type RoadGeometry } from '../LiveSceneView';
import { ErrorChart, TelemetryInfoRows } from './TelemetryPanels';
import { VTargetProfileChart } from './VTargetProfileChart';
import { PolicyTimelinePanel } from './PolicyTimelinePanel';
import { ActivePolicyPanel } from './ActivePolicyPanel';
import { VdManualOverridePanel } from './VdManualOverridePanel';
import { useVdStream } from '../../hooks/useVdStream';
import { useOsiStream, type OsiObject } from '../../hooks/useOsiStream';
import { api } from '../../api/client';

/**
 * Live VirtualDriver telemetry view. Renders the full scene (road geometry,
 * other traffic, signals via the OSI stream) and overlays the VirtualDriver
 * short-horizon preview + driver readouts (VD telemetry stream). Shared by the
 * embedded panel (SimulationDetailPage) and the standalone window (VdLivePage).
 *
 * `jobId` may be the literal "current": the panel then resolves the active
 * running simulation, so the standalone window can be opened *before* a run and
 * auto-populate once a VirtualDriver simulation starts. The rich scene (other
 * cars / signals) requires the run to have OSI streaming enabled.
 */
export function LiveVdPanel({
  jobId,
  projectId,
  scenarioFile,
  showOverride = false,
}: {
  jobId: string | null;
  projectId?: string;
  scenarioFile?: string;
  showOverride?: boolean;
}) {
  // Resolve "current" to the active running job (single-instance enforced).
  const isCurrent = jobId === 'current';
  const { data: simsData } = useQuery({
    queryKey: ['active-sim'],
    queryFn: () => api.getSimulations('running', 1),
    enabled: isCurrent,
    refetchInterval: 2000,
  });
  const activeSim = isCurrent ? simsData?.jobs?.[0] ?? null : null;

  const effectiveJobId = isCurrent ? activeSim?.job_id ?? null : jobId;
  const effectiveProjectId = projectId ?? activeSim?.project_id ?? undefined;

  // VD telemetry (preview overlay + driver readouts) and OSI scene (cars/signals).
  const { frame, history, status: vdStatus, frameCount } = useVdStream(effectiveJobId);
  const { objects: osiObjects, trafficLights } = useOsiStream(effectiveJobId);

  // Road geometry (road network + signs + stop lines). Optional — needs the
  // scenario filename, which the launcher passes through as a query param.
  const { data: roadGeometryData } = useQuery({
    queryKey: ['road-geometry', effectiveProjectId, scenarioFile],
    queryFn: () => api.getRoadGeometry(effectiveProjectId!, scenarioFile!),
    enabled: !!effectiveProjectId && !!scenarioFile,
    retry: false, // road overlay is optional
  });
  const roadGeometry = (roadGeometryData as RoadGeometry | undefined) ?? null;

  // Prefer the OSI objects (all traffic incl. ego). Until OSI streams (or if it
  // is disabled for the run), fall back to a single ego marker from VD telemetry
  // so the preview still has context.
  const sceneObjects: OsiObject[] = useMemo(() => {
    if (osiObjects.length > 0) return osiObjects;
    if (!frame) return [];
    const e = frame.ego;
    return [{
      id: 0, name: 'ego', x: e.x, y: e.y, z: e.z, h: e.h, speed: e.speed,
      head_light: 'off',
      indicator: frame.indicator.left ? 'left' : frame.indicator.right ? 'right' : 'off',
      brake_light: frame.driver.brake > 0.05 ? 'normal' : 'off',
      obj_type: 'vehicle', vehicle_class: 'medium_car', length: 5, width: 2,
    }];
  }, [osiObjects, frame]);

  const statusColor =
    vdStatus === 'connected' ? 'text-success'
    : vdStatus === 'error' ? 'text-destructive'
    : 'text-warning';

  return (
    <div className="h-full flex flex-col gap-3 min-h-0">
      <div className="flex items-center gap-3 text-xs">
        <span className="font-display text-sm text-foreground">VirtualDriver Live</span>
        <span className={statusColor}>● {vdStatus}</span>
        <span className="text-text-tertiary font-mono">{frameCount} frames</span>
        {frame && <span className="text-text-tertiary font-mono">t = {frame.sim_time.toFixed(2)}s</span>}
        {isCurrent && !effectiveJobId && (
          <span className="text-text-tertiary">waiting for a running simulation…</span>
        )}
      </div>

      <div className="flex-1 min-h-0 flex gap-3">
        <div className="flex-1 min-h-0 rounded overflow-hidden border border-glass-edge">
          <LiveSceneView
            objects={sceneObjects}
            roadGeometry={roadGeometry}
            trafficLights={trafficLights}
            vdTelemetry={frame}
            midlong={frame?.midlong ?? null}
            className="h-full"
            viewRadius={40}
          />
        </div>

        <div className="w-72 shrink-0 flex flex-col gap-3 overflow-y-auto">
          {frame ? (
            <>
              <TelemetryInfoRows frame={frame} />
              <ErrorChart frames={history} idx={history.length - 1} />
              <VTargetProfileChart frames={history} idx={history.length - 1} midlong={frame.midlong} />
              <ActivePolicyPanel frames={history} />
              <PolicyTimelinePanel frames={history} idx={history.length - 1} />
            </>
          ) : (
            <div className="rounded border border-glass-edge p-3 text-xs text-text-tertiary">
              Waiting for VirtualDriver telemetry… Start a simulation with the
              Virtual Driver controller.
            </div>
          )}
          {showOverride && effectiveJobId && <VdManualOverridePanel jobId={effectiveJobId} />}
        </div>
      </div>
    </div>
  );
}
