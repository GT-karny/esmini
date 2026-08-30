import { useEffect, useMemo, useState } from 'react';
import {
  api,
  type BuildFromRouteResult,
  type RoadListItem,
  type RoutePlan,
  type RoutePoint,
} from '../api/client';
import { RouteMapView } from '../components/routePlan/RouteMapView';
import type { RoadBoundary } from '../lib/sceneGeometry';

/**
 * Pick a road, click a start and a goal, get a runnable scenario.
 *
 * The route itself is solved server-side by the lane-aware router, not here: the
 * user clicks world points and the backend decides which lanes those are and
 * whether a drivable path connects them. Everything this page adds is the
 * affordance and the readback -- notably WHY a click was refused, since "not on a
 * lane" and "no route from here" are different problems with different fixes.
 */
export function RoutePlanPage() {
  const [roads, setRoads] = useState<RoadListItem[]>([]);
  const [roadId, setRoadId] = useState<string>('');
  const [boundaries, setBoundaries] = useState<RoadBoundary[]>([]);
  const [points, setPoints] = useState<RoutePoint[]>([]);
  const [plan, setPlan] = useState<RoutePlan | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [built, setBuilt] = useState<BuildFromRouteResult | null>(null);
  const [egoSpeed, setEgoSpeed] = useState(13.889);
  const [laneChangeEnabled, setLaneChangeEnabled] = useState(true);
  const [backgroundTraffic, setBackgroundTraffic] = useState(false);

  useEffect(() => {
    api
      .listRoads()
      .then((list) => {
        setRoads(list);
        if (list.length && !roadId) setRoadId(list[0].road_id);
      })
      .catch((e) => setError(String(e)));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (!roadId) return;
    setPoints([]);
    setPlan(null);
    setBuilt(null);
    setError(null);
    setBoundaries([]);
    api
      .getRoadGeometryById(roadId)
      .then((geo) => setBoundaries(geo.boundaries as RoadBoundary[]))
      .catch((e) => setError(String(e)));
  }, [roadId]);

  const selectedRoad = useMemo(
    () => roads.find((r) => r.road_id === roadId) ?? null,
    [roads, roadId],
  );

  // A road without a SUMO network cannot host traffic; clear the request rather
  // than carrying a tick over to a road where it would only produce an error.
  // Kept as its own effect keyed on selectedRoad so the geometry fetch above does
  // not have to depend on `roads` and re-run when the road list loads.
  useEffect(() => {
    if (!selectedRoad?.sumocfg) setBackgroundTraffic(false);
  }, [selectedRoad]);

  // Re-plan whenever the point list changes and there are at least two points.
  useEffect(() => {
    if (points.length < 2) {
      setPlan(null);
      return;
    }
    let cancelled = false;
    setBusy(true);
    setError(null);
    api
      .planRoute(roadId, points)
      .then((p) => {
        if (!cancelled) setPlan(p);
      })
      .catch((e) => {
        if (!cancelled) {
          setPlan(null);
          setError(describeError(e));
        }
      })
      .finally(() => {
        if (!cancelled) setBusy(false);
      });
    return () => {
      cancelled = true;
    };
  }, [points, roadId]);

  const laneChangeSummary = useMemo(() => {
    if (!plan?.lane_changes.length) return null;
    return plan.lane_changes
      .map((lc) => `road ${lc.road_id}: ${lc.from_lane_id} → ${lc.to_lane_id}`)
      .join(', ');
  }, [plan]);

  const build = async () => {
    setBusy(true);
    setError(null);
    try {
      const result = await api.buildScenarioFromRoute({
        road_id: roadId,
        points,
        ego_speed: egoSpeed,
        policies: laneChangeEnabled ? ['lane_change_initiation'] : [],
        background_traffic: backgroundTraffic,
      });
      setBuilt(result);
    } catch (e) {
      setError(describeError(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="flex h-full flex-col gap-4 px-6 py-6">
      <header className="flex flex-wrap items-end gap-4">
        <div>
          <label className="block text-xs uppercase tracking-wide text-slate-400">Road</label>
          <select
            className="mt-1 rounded border border-slate-700 bg-slate-900 px-3 py-1.5 text-slate-100"
            value={roadId}
            onChange={(e) => setRoadId(e.target.value)}
          >
            {roads.map((r) => (
              <option key={r.road_id} value={r.road_id}>
                {r.name}
                {r.source === 'upload' ? ' (uploaded)' : ''}
              </option>
            ))}
          </select>
        </div>

        <div>
          <label className="block text-xs uppercase tracking-wide text-slate-400">
            Cruise speed (m/s)
          </label>
          <input
            type="number"
            step="0.1"
            min="0.1"
            className="mt-1 w-28 rounded border border-slate-700 bg-slate-900 px-3 py-1.5 text-slate-100"
            value={egoSpeed}
            onChange={(e) => setEgoSpeed(Number(e.target.value))}
          />
        </div>

        <label className="flex items-center gap-2 pb-1.5 text-sm text-slate-300">
          <input
            type="checkbox"
            checked={laneChangeEnabled}
            onChange={(e) => setLaneChangeEnabled(e.target.checked)}
          />
          {/* Default OFF in virtual_driver.json; without it the vehicle records the
              route deviation instead of driving into the lane the route needs. */}
          Self-initiated lane changes
        </label>

        <label
          className={`flex items-center gap-2 pb-1.5 text-sm ${
            selectedRoad?.sumocfg ? 'text-slate-300' : 'cursor-not-allowed text-slate-600'
          }`}
          title={
            selectedRoad?.sumocfg
              ? 'Spawn SUMO background traffic on this road'
              : 'This road has no SUMO network. Generate one with scripts/xodr_to_sumo_net.py --demand N.'
          }
        >
          <input
            type="checkbox"
            checked={backgroundTraffic}
            disabled={!selectedRoad?.sumocfg}
            onChange={(e) => setBackgroundTraffic(e.target.checked)}
          />
          Background traffic
        </label>

        <div className="ml-auto flex items-center gap-2">
          <button
            className="rounded border border-slate-700 px-3 py-1.5 text-sm text-slate-300 hover:bg-slate-800 disabled:opacity-40"
            onClick={() => {
              setPoints([]);
              setPlan(null);
              setBuilt(null);
              setError(null);
            }}
            disabled={!points.length}
          >
            Clear points
          </button>
          <button
            className="rounded bg-sky-600 px-4 py-1.5 text-sm font-medium text-white hover:bg-sky-500 disabled:opacity-40"
            onClick={build}
            disabled={busy || !plan}
          >
            Create scenario
          </button>
        </div>
      </header>

      <div className="min-h-0 flex-1 overflow-hidden rounded border border-slate-800">
        <RouteMapView
          boundaries={boundaries}
          points={points}
          plan={plan}
          onAddPoint={(p) => setPoints((prev) => [...prev, p])}
        />
      </div>

      <footer className="space-y-1 text-sm">
        <p className="text-slate-400">
          Click a start, then a goal. Extra clicks in between become via points.
          {busy && <span className="ml-2 text-sky-400">planning…</span>}
        </p>

        {plan && (
          <p className="text-slate-300">
            {plan.waypoints.length} waypoints · {plan.length.toFixed(1)} m
            {laneChangeSummary && (
              <span className="text-amber-400"> · lane changes: {laneChangeSummary}</span>
            )}
          </p>
        )}

        {error && <p className="text-red-400">{error}</p>}

        {built && (
          <p className="text-emerald-400">
            Scenario <code className="font-mono">{built.scenario_id}</code> created — pick it on the
            simulation page to run it.
          </p>
        )}
      </footer>
    </div>
  );
}

/**
 * Turn an API failure into something a user can act on.
 *
 * The backend distinguishes these deliberately: a point that missed the road is
 * fixed by clicking elsewhere, while "no route" means the two lanes genuinely do
 * not connect in the driving direction -- often because the goal is on the
 * opposite carriageway. Collapsing them into "request failed" would send people
 * hunting for the wrong problem.
 */
function describeError(err: unknown): string {
  // api/client.ts's request() throws Error("<status>: <raw body>"), so the
  // structured detail has to be recovered from the message text rather than read
  // off the error object.
  const raw = err instanceof Error ? err.message : String(err);
  const jsonStart = raw.indexOf('{');
  let info: { code?: string; message?: string; index?: number } = {};
  if (jsonStart >= 0) {
    try {
      const parsed = JSON.parse(raw.slice(jsonStart)) as { detail?: unknown };
      if (typeof parsed.detail === 'object' && parsed.detail !== null) {
        info = parsed.detail as typeof info;
      } else if (typeof parsed.detail === 'string') {
        info = { message: parsed.detail };
      }
    } catch {
      // Not JSON (proxy error, HTML page): fall through to the raw text.
    }
  }
  switch (info.code) {
    case 'point_not_routable':
    case 'point_off_road':
      return `Point ${(info.index ?? 0) + 1} is not on a drivable lane — click on a road.`;
    case 'no_route':
      return 'No drivable route between those points. Check the goal is on a lane reachable in the direction of travel (the opposite carriageway will not connect).';
    case 'route_direction_api_missing':
    case 'route_api_missing':
    case 'library_unavailable':
      return `${info.message ?? 'Routing library unavailable'} — this needs a Release build.`;
    default:
      return info.message ?? raw;
  }
}
