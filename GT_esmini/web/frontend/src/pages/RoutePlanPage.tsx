import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  api,
  type BuildFromRouteResult,
  type RoadListItem,
  type RoutePlan,
} from '../api/client';
import { RouteMapView } from '../components/routePlan/RouteMapView';
import {
  describeSnap,
  newPoint,
  type RoutePlanPoint,
  type SnapResult,
} from '../components/routePlan/types';
import { Button } from '../components/ui/Button';
import { Checkbox, NumberInput, SelectInput } from '../components/ui/Input';
import { ContextMenu, type ContextMenuItem } from '../components/ui/ContextMenu';
import { EmptyState } from '../components/ui/EmptyState';
import type { RoadBoundary } from '../lib/sceneGeometry';

/**
 * Pick a road, place route points on it, get a runnable scenario.
 *
 * The route itself is solved server-side by the lane-aware router: the user
 * places world points and the backend decides which lanes those are and whether
 * a drivable path connects them. This page owns the points, their snap status,
 * and undo.
 *
 * Two things are deliberately separate, because folding them together was the
 * original usability defect:
 *   - SNAPPING runs per point, as soon as a point is placed or moved. It does not
 *     wait for a route, so a lone first point already shows where it landed.
 *   - ROUTING runs only with two or more points and may legitimately fail; that
 *     failure must not erase the snap feedback the user is relying on.
 */

const MAX_UNDO = 50;
// Long enough to collapse a drag into one request, short enough to feel immediate.
const SNAP_DEBOUNCE_MS = 120;
const PLAN_DEBOUNCE_MS = 260;

export function RoutePlanPage() {
  const [roads, setRoads] = useState<RoadListItem[]>([]);
  const [roadId, setRoadId] = useState<string>('');
  const [boundaries, setBoundaries] = useState<RoadBoundary[]>([]);
  const [loadingRoad, setLoadingRoad] = useState(false);

  const [points, setPoints] = useState<RoutePlanPoint[]>([]);
  const [selected, setSelected] = useState<number | null>(null);
  const [menu, setMenu] = useState<{ index: number; x: number; y: number } | null>(null);

  const [plan, setPlan] = useState<RoutePlan | null>(null);
  const [planning, setPlanning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [built, setBuilt] = useState<BuildFromRouteResult | null>(null);

  const [egoSpeed, setEgoSpeed] = useState(13.889);
  const [laneChange, setLaneChange] = useState(true);
  const [traffic, setTraffic] = useState(false);

  const undoStack = useRef<RoutePlanPoint[][]>([]);
  // Sequence guards: a response that has been superseded must not overwrite newer
  // state. Without this a late reply re-applies a stale position after the user
  // has already moved on.
  const snapSeq = useRef(0);
  const planSeq = useRef(0);

  const selectedRoad = useMemo(
    () => roads.find((r) => r.road_id === roadId) ?? null,
    [roads, roadId],
  );

  /** Every mutation goes through here so undo is never forgotten at a call site. */
  const commit = useCallback((next: RoutePlanPoint[]) => {
    setPoints((prev) => {
      undoStack.current = [...undoStack.current.slice(-(MAX_UNDO - 1)), prev];
      return next;
    });
    setBuilt(null);
  }, []);

  const undo = useCallback(() => {
    const prev = undoStack.current.pop();
    if (!prev) return;
    setPoints(prev);
    setSelected(null);
    setBuilt(null);
  }, []);

  useEffect(() => {
    api
      .listRoads()
      .then((list) => {
        setRoads(list);
        setRoadId((cur) => cur || (list[0]?.road_id ?? ''));
      })
      .catch((e) => setError(describeError(e)));
  }, []);

  useEffect(() => {
    if (!roadId) return;
    setPoints([]);
    undoStack.current = [];
    setSelected(null);
    setPlan(null);
    setBuilt(null);
    setError(null);
    setBoundaries([]);
    setLoadingRoad(true);
    let cancelled = false;
    api
      .getRoadGeometryById(roadId)
      .then((geo) => {
        if (!cancelled) setBoundaries(geo.boundaries as RoadBoundary[]);
      })
      .catch((e) => {
        if (!cancelled) setError(describeError(e));
      })
      .finally(() => {
        if (!cancelled) setLoadingRoad(false);
      });
    return () => {
      cancelled = true;
    };
  }, [roadId]);

  // A road with no SUMO network cannot host traffic; clear the request rather
  // than carrying a tick over to a road where it would only produce an error.
  useEffect(() => {
    if (!selectedRoad?.sumocfg) setTraffic(false);
  }, [selectedRoad]);

  /** Snap every point. Runs on any change to the point positions, independent of
   *  routing, so feedback never waits on a route that may not exist. */
  const snapAll = useCallback(
    async (pts: RoutePlanPoint[]) => {
      if (!roadId || pts.length === 0) return;
      const seq = ++snapSeq.current;
      try {
        const res = await api.snapPoints(
          roadId,
          pts.map((p) => ({ x: p.x, y: p.y })),
        );
        if (seq !== snapSeq.current) return; // superseded by a newer request
        // Keyed by point id, NOT by array index. Index matching breaks the moment
        // a point is deleted or inserted between request and response: every
        // surviving point shifts, the ids stop lining up, and each result is
        // discarded -- which looks like "it never snaps again".
        const byId = new Map(pts.map((p, i) => [p.id, res.snapped[i] as SnapResult]));
        setPoints((cur) =>
          cur.map((p) => (byId.has(p.id) ? { ...p, snap: byId.get(p.id) } : p)),
        );
      } catch (e) {
        if (seq === snapSeq.current) setError(describeError(e));
      }
    },
    [roadId],
  );

  const positionsKey = points.map((p) => `${p.id}:${p.x.toFixed(2)},${p.y.toFixed(2)}`).join('|');

  // DEBOUNCED, not gated on a "is dragging" flag.
  //
  // A drag changes positions every pointermove, and snap (~30 ms) and route
  // (~35 ms) both serialise behind one process-global OpenDrive lock on the
  // server: firing per frame queues ~120 requests per second and the map never
  // catches up. The first fix gated both on a dragging flag, but any missed
  // release path (pointer leaving the map, a cancelled pointer, a capture that
  // failed) left the flag stuck true and NOTHING ever snapped again -- turning a
  // slowness bug into a dead UI. A debounce has no such state: the timer resets
  // while positions keep changing and fires once they stop, so there is nothing
  // that can be left latched.
  useEffect(() => {
    const t = setTimeout(() => void snapAll(points), SNAP_DEBOUNCE_MS);
    return () => clearTimeout(t);
    // positionsKey collapses the array into a value so this runs on real position
    // changes only -- not on every snap result landing (which would loop).
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [positionsKey, roadId]);

  useEffect(() => {
    if (points.length < 2) {
      setPlan(null);
      setPlanning(false);
      return;
    }
    let cancelled = false;
    // Longer than the snap debounce: routing is the heavier call and its result
    // is less urgent than seeing where a point landed.
    const timer = setTimeout(() => {
      const seq = ++planSeq.current;
      setPlanning(true);
      setError(null);
      api
        .planRoute(
          roadId,
          points.map((p) => ({ x: p.x, y: p.y })),
        )
        .then((p) => {
          if (!cancelled && seq === planSeq.current) setPlan(p);
        })
        .catch((e) => {
          if (!cancelled && seq === planSeq.current) {
            setPlan(null);
            setError(describeError(e));
          }
        })
        .finally(() => {
          if (!cancelled && seq === planSeq.current) setPlanning(false);
        });
    }, PLAN_DEBOUNCE_MS);
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [positionsKey, roadId]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
        e.preventDefault();
        undo();
      }
      if ((e.key === 'Delete' || e.key === 'Backspace') && selected != null) {
        e.preventDefault();
        commit(points.filter((_, i) => i !== selected));
        setSelected(null);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [undo, selected, points, commit]);

  const build = async () => {
    setError(null);
    try {
      setBuilt(
        await api.buildScenarioFromRoute({
          road_id: roadId,
          points: points.map((p) => ({ x: p.x, y: p.y })),
          ego_speed: egoSpeed,
          policies: laneChange ? ['lane_change_initiation'] : [],
          background_traffic: traffic,
        }),
      );
    } catch (e) {
      setError(describeError(e));
    }
  };

  const menuItems: ContextMenuItem[] = menu
    ? [
        {
          label: 'Delete point',
          danger: true,
          onClick: () => {
            commit(points.filter((_, i) => i !== menu.index));
            setSelected(null);
          },
        },
        {
          label: 'Insert point after',
          disabled: menu.index === points.length - 1,
          onClick: () => {
            const a = points[menu.index];
            const b = points[menu.index + 1];
            const mid = newPoint((a.x + b.x) / 2, (a.y + b.y) / 2);
            commit([...points.slice(0, menu.index + 1), mid, ...points.slice(menu.index + 1)]);
          },
        },
      ]
    : [];

  const laneChanges = plan?.lane_changes ?? [];
  // A connector may not feed the lane that was clicked, in which case the route
  // arrives on a neighbour. The point row still shows the clicked lane, so
  // without this the list quietly disagrees with the route it drew.
  const laneAdjustments = new Map(
    (plan?.lane_adjustments ?? []).map((a) => [a.index, a] as const),
  );

  return (
    <div className="flex h-full min-h-0 flex-col gap-4 px-6 py-6">
      <header className="flex flex-wrap items-end gap-4">
        <SelectInput
          label="Road"
          value={roadId}
          onChange={(e) => setRoadId(e.target.value)}
          wrapperClassName="w-64"
        >
          {roads.map((r) => (
            <option key={r.road_id} value={r.road_id}>
              {r.name}
              {r.source === 'upload' ? ' (uploaded)' : ''}
            </option>
          ))}
        </SelectInput>

        <NumberInput
          label="Cruise speed (m/s)"
          step="0.1"
          min="0.1"
          value={egoSpeed}
          onChange={(e) => setEgoSpeed(Number(e.target.value))}
          wrapperClassName="w-36"
        />

        <div className="flex flex-col gap-1 pb-1">
          <Checkbox
            label="Self-initiated lane changes"
            checked={laneChange}
            onChange={(e) => setLaneChange(e.target.checked)}
          />
          <Checkbox
            label="Background traffic"
            checked={traffic}
            disabled={!selectedRoad?.sumocfg}
            title={
              selectedRoad?.sumocfg
                ? undefined
                : 'This road has no SUMO network. Generate one with scripts/xodr_to_sumo_net.py --demand N.'
            }
            onChange={(e) => setTraffic(e.target.checked)}
          />
        </div>

        <div className="ml-auto">
          <Button onClick={build} disabled={!plan || planning}>
            Create scenario
          </Button>
        </div>
      </header>

      <div className="flex min-h-0 flex-1 gap-4">
        <div className="min-w-0 flex-1 overflow-hidden rounded border border-slate-800">
          {loadingRoad ? (
            <div className="flex h-full items-center justify-center text-sm text-slate-500">
              Loading road…
            </div>
          ) : (
            <RouteMapView
              boundaries={boundaries}
              points={points}
              plan={plan}
              selectedIndex={selected}
              onAddPoint={(w) => commit([...points, newPoint(w.x, w.y)])}
              onMovePoint={(i, w) =>
                setPoints((cur) => cur.map((p, j) => (j === i ? { ...p, ...w } : p)))
              }
              onCommitPoint={() => {
                // The drag already mutated position; record the pre-drag list once
                // here so undo steps back over the whole drag, not each frame.
                undoStack.current = [...undoStack.current.slice(-(MAX_UNDO - 1)), points];
              }}
              onSelectPoint={setSelected}
              onContextPoint={(index, x, y) => setMenu({ index, x, y })}
            />
          )}
        </div>

        <aside className="flex w-80 shrink-0 flex-col gap-3">
          <div className="flex items-center justify-between">
            <h2 className="text-xs font-semibold uppercase tracking-wide text-slate-400">
              Route points
            </h2>
            <div className="flex gap-2">
              <Button size="sm" variant="ghost" onClick={undo} disabled={!undoStack.current.length}>
                Undo
              </Button>
              <Button
                size="sm"
                variant="ghost"
                onClick={() => commit([])}
                disabled={!points.length}
              >
                Clear
              </Button>
            </div>
          </div>

          <div className="min-h-0 flex-1 overflow-y-auto rounded border border-slate-800">
            {points.length === 0 ? (
              <EmptyState message="Click the map to place a start, then a goal. Extra clicks in between become via points." />
            ) : (
              <ul className="divide-y divide-slate-800">
                {points.map((p, i) => {
                  const role = i === 0 ? 'Start' : i === points.length - 1 ? 'Goal' : 'Via';
                  const missed = p.snap && !p.snap.on_road;
                  const adjusted = laneAdjustments.get(i);
                  return (
                    <li
                      key={p.id}
                      onClick={() => setSelected(i)}
                      className={`flex cursor-pointer items-center gap-3 px-3 py-2 text-sm transition-colors ${
                        selected === i ? 'bg-slate-800' : 'hover:bg-slate-900'
                      }`}
                    >
                      <span
                        className={`flex h-5 w-5 shrink-0 items-center justify-center rounded-full text-xs font-bold text-slate-900 ${
                          missed
                            ? 'bg-slate-500'
                            : i === 0
                              ? 'bg-emerald-500'
                              : i === points.length - 1
                                ? 'bg-red-500'
                                : 'bg-purple-500'
                        }`}
                      >
                        {i + 1}
                      </span>
                      <span className="min-w-0 flex-1">
                        <span className="block text-slate-300">{role}</span>
                        <span
                          className={`block truncate font-mono text-xs ${
                            missed ? 'text-red-400' : 'text-slate-500'
                          }`}
                        >
                          {describeSnap(p.snap)}
                        </span>
                        {adjusted && (
                          <span className="block truncate font-mono text-xs text-amber-400">
                            arrives on lane{' '}
                            {adjusted.arrived_lane > 0
                              ? `+${adjusted.arrived_lane}`
                              : adjusted.arrived_lane}
                          </span>
                        )}
                      </span>
                      <Button
                        size="sm"
                        variant="ghost"
                        aria-label={`Delete point ${i + 1}`}
                        onClick={(e) => {
                          e.stopPropagation();
                          commit(points.filter((_, j) => j !== i));
                          setSelected(null);
                        }}
                      >
                        Delete
                      </Button>
                    </li>
                  );
                })}
              </ul>
            )}
          </div>

          <div className="space-y-1 text-sm">
            {planning && <p className="text-sky-400">Planning route…</p>}
            {plan && !planning && (
              <p className="text-slate-300">
                {plan.waypoints.length} waypoints · {plan.length.toFixed(1)} m
              </p>
            )}
            {laneChanges.length > 0 && (
              <p className="text-amber-400">
                Lane changes:{' '}
                {laneChanges
                  .map((lc) => `road ${lc.road_id} ${lc.from_lane_id}→${lc.to_lane_id}`)
                  .join(', ')}
              </p>
            )}
            {error && <p className="text-red-400">{error}</p>}
            {built && (
              <p className="text-emerald-400">
                Scenario <code className="font-mono">{built.scenario_id}</code> created — pick it
                on the simulation page to run it.
              </p>
            )}
          </div>
        </aside>
      </div>

      {menu && (
        <ContextMenu
          items={menuItems}
          position={{ x: menu.x, y: menu.y }}
          onClose={() => setMenu(null)}
        />
      )}
    </div>
  );
}

/**
 * Turn an API failure into something a user can act on.
 *
 * The backend distinguishes these deliberately: a point that missed the road is
 * fixed by moving that point, while "no route" means the two lanes genuinely do
 * not connect in the driving direction -- often because the goal is on the
 * opposite carriageway. Collapsing them into "request failed" would send people
 * hunting for the wrong problem.
 */
function describeError(err: unknown): string {
  // api/client.ts's request() throws Error("<status>: <raw body>"), so the
  // structured detail has to be recovered from the message text.
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
      return `Point ${(info.index ?? 0) + 1} is not on a drivable lane — drag it onto a road.`;
    case 'no_route':
      return 'No drivable route between those points. Check the goal is reachable in the direction of travel (the opposite carriageway will not connect).';
    case 'no_sumocfg':
      return info.message ?? 'This road has no SUMO network.';
    case 'route_direction_api_missing':
    case 'route_api_missing':
    case 'library_unavailable':
      return `${info.message ?? 'Routing library unavailable'} — this needs a Release build.`;
    default:
      return info.message ?? raw;
  }
}
