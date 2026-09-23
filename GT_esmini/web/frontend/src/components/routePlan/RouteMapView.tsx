import { useCallback, useMemo, useRef, useState, type ReactElement } from 'react';
import type { RoutePlan } from '../../api/client';
import { type RoadBoundary, flipY, getBoundaryStyle } from '../../lib/sceneGeometry';
import type { RoutePlanPoint } from './types';

/**
 * Whole-network road map: place, move and remove route points directly on it.
 *
 * Deliberately NOT a reuse of LiveSceneView: that one's viewBox follows the ego
 * vehicle frame by frame, which is the opposite of what picking a route needs.
 * What IS reused is the shared geometry vocabulary in lib/sceneGeometry (flipY,
 * getBoundaryStyle), so a road drawn here looks like the same road during a run.
 *
 * INTERACTION MODEL -- points are objects, not events
 * ---------------------------------------------------
 * The first version treated a click as an append-only event: there was no way to
 * move or remove one point, and the only recovery was wiping every point. Here a
 * point has identity, so it can be selected, dragged and deleted individually,
 * and each action is reversible by the page's undo stack.
 *
 * Left button does two different things on the canvas, separated by DISTANCE
 * rather than by a mode toggle (which the user would have to find and remember):
 * release within DRAG_THRESHOLD_PX of the press = a click that adds a point;
 * anything further = a pan that ends without adding one. That is the convention
 * every map uses, so it needs no explanation.
 *
 * A dragged point follows the cursor RAW and snaps on release. Snapping every
 * mousemove would need a server round-trip per frame; more importantly, seeing
 * the point jump to the lane the moment you let go is what tells the user the
 * snap happened at all.
 */

const DRAG_THRESHOLD_PX = 4;
const ZOOM_STEP = 1.2;
const MIN_SPAN_M = 20; // stop zooming in once a lane is ~half the viewport
const PADDING_M = 30;

interface ViewBox {
  x: number;
  y: number;
  w: number;
  h: number;
}

interface RouteMapViewProps {
  boundaries: RoadBoundary[];
  points: RoutePlanPoint[];
  plan: RoutePlan | null;
  selectedIndex: number | null;
  onAddPoint: (world: { x: number; y: number }) => void;
  onMovePoint: (index: number, world: { x: number; y: number }) => void;
  onCommitPoint: (index: number) => void;
  onSelectPoint: (index: number | null) => void;
  onContextPoint: (index: number, clientX: number, clientY: number) => void;
  className?: string;
}

/** setPointerCapture throws (NotFoundError) for a pointer id that is no longer
 *  active -- a stale id, a pointer already released, or a synthetic event. It is
 *  an optimisation here, not a requirement, so a failure must not abort the
 *  handler and leave the gesture half-started. */
function capture(target: EventTarget | null, pointerId: number) {
  try {
    (target as Element | null)?.setPointerCapture?.(pointerId);
  } catch {
    /* pointer already gone; dragging still works via the svg-level handlers */
  }
}

export function RouteMapView({
  boundaries,
  points,
  plan,
  selectedIndex,
  onAddPoint,
  onMovePoint,
  onCommitPoint,
  onSelectPoint,
  onContextPoint,
  className = '',
}: RouteMapViewProps): ReactElement {
  const svgRef = useRef<SVGSVGElement>(null);
  const [view, setView] = useState<ViewBox | null>(null);
  // Cursor is STATE, not derived from the gesture ref: a ref change does not
  // re-render, so reading it during render left the cursor stuck on crosshair
  // for the whole pan (and violates react-hooks/refs).
  const [panning, setPanning] = useState(false);
  const gesture = useRef<
    | { kind: 'idle' }
    | { kind: 'maybe-pan'; startClient: { x: number; y: number }; startView: ViewBox }
    | { kind: 'panning'; startClient: { x: number; y: number }; startView: ViewBox }
    | { kind: 'point'; index: number; moved: boolean }
  >({ kind: 'idle' });

  const fitted = useMemo<ViewBox>(() => {
    let minX = Infinity;
    let maxX = -Infinity;
    let minY = Infinity;
    let maxY = -Infinity;
    for (const b of boundaries) {
      for (const [x, y] of b.points) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        const fy = flipY(y);
        if (fy < minY) minY = fy;
        if (fy > maxY) maxY = fy;
      }
    }
    if (!Number.isFinite(minX)) return { x: -50, y: -50, w: 100, h: 100 };
    return {
      x: minX - PADDING_M,
      y: minY - PADDING_M,
      w: maxX - minX + PADDING_M * 2,
      h: maxY - minY + PADDING_M * 2,
    };
  }, [boundaries]);

  // Re-fit whenever a different road loads, but never while the user is looking
  // around an unchanged one -- resetting someone's viewport is its own defect.
  // Adjusted during render rather than in an effect: an effect would paint one
  // frame of the new road through the OLD viewBox before correcting itself.
  const [fittedFor, setFittedFor] = useState(fitted);
  if (fittedFor !== fitted) {
    setFittedFor(fitted);
    setView(null); // null falls through to `fitted` below
  }

  const vb = view ?? fitted;

  // One rendered pixel in world metres. getBoundaryStyle's widths are REAL widths
  // (0.25 m for a road edge) sized for LiveSceneView's ~50 m view; across a whole
  // network they become sub-pixel hairlines. Every stroke takes the larger of its
  // true width and a pixel floor, so the road keeps real proportions when zoomed
  // in and stays visible when zoomed out. Recomputed from the live viewBox so it
  // tracks zoom.
  const px = Math.max(vb.w, vb.h) / 700;

  const toWorld = useCallback((clientX: number, clientY: number) => {
    const svg = svgRef.current;
    if (!svg) return null;
    const ctm = svg.getScreenCTM();
    if (!ctm) return null;
    const pt = svg.createSVGPoint();
    pt.x = clientX;
    pt.y = clientY;
    const local = pt.matrixTransform(ctm.inverse());
    return { x: local.x, y: flipY(local.y), localY: local.y };
  }, []);

  const handleWheel = useCallback(
    (evt: React.WheelEvent<SVGSVGElement>) => {
      evt.preventDefault();
      const at = toWorld(evt.clientX, evt.clientY);
      if (!at) return;
      const k = evt.deltaY > 0 ? ZOOM_STEP : 1 / ZOOM_STEP;
      setView((prev) => {
        const cur = prev ?? fitted;
        const w = cur.w * k;
        const h = cur.h * k;
        if (w < MIN_SPAN_M || w > fitted.w * 20) return cur;
        // Keep the world point under the cursor pinned to the cursor.
        return {
          x: at.x - (at.x - cur.x) * k,
          y: at.localY - (at.localY - cur.y) * k,
          w,
          h,
        };
      });
    },
    [fitted, toWorld],
  );

  const zoomBy = (k: number) =>
    setView((prev) => {
      const cur = prev ?? fitted;
      const w = cur.w * k;
      const h = cur.h * k;
      if (w < MIN_SPAN_M || w > fitted.w * 20) return cur;
      return { x: cur.x + (cur.w - w) / 2, y: cur.y + (cur.h - h) / 2, w, h };
    });

  const onPointerDown = (evt: React.PointerEvent<SVGSVGElement>) => {
    if (evt.button !== 0) return;
    capture(evt.target, evt.pointerId);
    gesture.current = {
      kind: 'maybe-pan',
      startClient: { x: evt.clientX, y: evt.clientY },
      startView: vb,
    };
  };

  const onPointerMove = (evt: React.PointerEvent<SVGSVGElement>) => {
    const g = gesture.current;
    if (g.kind === 'point') {
      const at = toWorld(evt.clientX, evt.clientY);
      if (at) {
        gesture.current = { ...g, moved: true };
        onMovePoint(g.index, { x: at.x, y: at.y });
      }
      return;
    }
    if (g.kind === 'idle') return;

    const dx = evt.clientX - g.startClient.x;
    const dy = evt.clientY - g.startClient.y;
    if (g.kind === 'maybe-pan' && Math.hypot(dx, dy) < DRAG_THRESHOLD_PX) return;

    const svg = svgRef.current;
    if (!svg) return;
    const rect = svg.getBoundingClientRect();
    const scaleX = g.startView.w / rect.width;
    const scaleY = g.startView.h / rect.height;
    gesture.current = { ...g, kind: 'panning' };
    setPanning(true);
    setView({
      ...g.startView,
      x: g.startView.x - dx * scaleX,
      y: g.startView.y - dy * scaleY,
    });
  };

  const onPointerUp = (evt: React.PointerEvent<SVGSVGElement>) => {
    const g = gesture.current;
    gesture.current = { kind: 'idle' };
    setPanning(false);
    if (g.kind === 'point') {
      if (g.moved) onCommitPoint(g.index);
      return;
    }
    // A press that never exceeded the threshold is a click, not a pan.
    if (g.kind === 'maybe-pan') {
      const at = toWorld(evt.clientX, evt.clientY);
      if (at) onAddPoint({ x: at.x, y: at.y });
    }
  };

  const routeLine = useMemo(() => {
    if (!plan) return '';
    const pts = plan.path?.length ? plan.path : plan.waypoints;
    if (pts.length < 2) return '';
    return pts.map((w) => `${w.x},${flipY(w.y)}`).join(' ');
  }, [plan]);

  return (
    <div className={`relative h-full w-full ${className}`}>
      <svg
        ref={svgRef}
        className="h-full w-full touch-none bg-slate-950"
        style={{ cursor: panning ? 'grabbing' : 'crosshair' }}
        viewBox={`${vb.x} ${vb.y} ${vb.w} ${vb.h}`}
        onWheel={handleWheel}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerLeave={() => {
          gesture.current = { kind: 'idle' };
          setPanning(false);
        }}
        onContextMenu={(e) => e.preventDefault()}
      >
        {boundaries.map((b, i) => {
          const style = getBoundaryStyle(b.type);
          return (
            <polyline
              key={`${b.road_id}-${i}`}
              points={b.points.map(([x, y]) => `${x},${flipY(y)}`).join(' ')}
              fill="none"
              stroke={style.stroke}
              strokeWidth={Math.max(style.strokeWidth, px * 1.2)}
              strokeDasharray={style.dasharray || undefined}
              opacity={style.opacity}
            />
          );
        })}

        {routeLine && (
          <polyline
            points={routeLine}
            fill="none"
            stroke="#38bdf8"
            strokeWidth={px * 3}
            strokeLinejoin="round"
            strokeLinecap="round"
            opacity={0.85}
          />
        )}

        {/* Lane changes the route requires -- not inferable from the line alone. */}
        {plan?.lane_changes.map((lc, i) => {
          const wp = plan.waypoints.find((w) => w.road_id === lc.road_id);
          if (!wp) return null;
          return (
            <circle
              key={`lc-${i}`}
              cx={wp.x}
              cy={flipY(wp.y)}
              r={px * 9}
              fill="none"
              stroke="#fbbf24"
              strokeWidth={px * 1.5}
            />
          );
        })}

        {points.map((p, i) => {
          const snap = p.snap;
          const at = snap?.on_road ? { x: snap.x!, y: snap.y! } : { x: p.x, y: p.y };
          const isStart = i === 0;
          const isGoal = i === points.length - 1 && points.length > 1;
          const selected = selectedIndex === i;
          const missed = snap != null && !snap.on_road;
          const fill = missed
            ? '#64748b'
            : isStart
              ? '#22c55e'
              : isGoal
                ? '#ef4444'
                : '#a855f7';
          return (
            <g key={p.id}>
              {/* Click -> snap connector: shows the point was MOVED onto a lane
                  rather than silently relocated. */}
              {snap?.on_road && (Math.abs(snap.x! - p.x) > 0.05 || Math.abs(snap.y! - p.y) > 0.05) && (
                <line
                  x1={p.x}
                  y1={flipY(p.y)}
                  x2={snap.x!}
                  y2={flipY(snap.y!)}
                  stroke="#475569"
                  strokeWidth={px * 0.8}
                  strokeDasharray={`${px * 2} ${px * 2}`}
                />
              )}
              {selected && (
                <circle cx={at.x} cy={flipY(at.y)} r={px * 12} fill="none" stroke="#38bdf8" strokeWidth={px * 1.5} />
              )}
              <circle
                cx={at.x}
                cy={flipY(at.y)}
                r={px * 7}
                fill={fill}
                stroke={missed ? '#f87171' : '#0f172a'}
                strokeWidth={px * 1.5}
                strokeDasharray={missed ? `${px * 2} ${px * 2}` : undefined}
                style={{ cursor: 'move' }}
                onPointerDown={(e) => {
                  e.stopPropagation();
                  if (e.button !== 0) return;
                  capture(e.target, e.pointerId);
                  gesture.current = { kind: 'point', index: i, moved: false };
                  onSelectPoint(i);
                }}
                onContextMenu={(e) => {
                  e.preventDefault();
                  e.stopPropagation();
                  onSelectPoint(i);
                  onContextPoint(i, e.clientX, e.clientY);
                }}
              />
              <text
                x={at.x}
                y={flipY(at.y) + px * 3}
                textAnchor="middle"
                fontSize={px * 8}
                fill="#0f172a"
                fontWeight="700"
                style={{ pointerEvents: 'none', userSelect: 'none' }}
              >
                {i + 1}
              </text>
            </g>
          );
        })}
      </svg>

      <div className="absolute right-3 top-3 flex flex-col gap-1">
        <MapButton label="Zoom in" onClick={() => zoomBy(1 / ZOOM_STEP)}>
          <path d="M5 12h14M12 5v14" />
        </MapButton>
        <MapButton label="Zoom out" onClick={() => zoomBy(ZOOM_STEP)}>
          <path d="M5 12h14" />
        </MapButton>
        <MapButton label="Fit road" onClick={() => setView(fitted)}>
          <path d="M4 9V5a1 1 0 0 1 1-1h4M15 4h4a1 1 0 0 1 1 1v4M20 15v4a1 1 0 0 1-1 1h-4M9 20H5a1 1 0 0 1-1-1v-4" />
        </MapButton>
      </div>
    </div>
  );
}

/** Square icon button for the map overlay. Labelled for screen readers and as a
 *  tooltip — an icon-only control still needs a name. */
function MapButton({
  label,
  onClick,
  children,
}: {
  label: string;
  onClick: () => void;
  children: ReactElement;
}) {
  return (
    <button
      type="button"
      aria-label={label}
      title={label}
      onClick={onClick}
      className="flex h-8 w-8 cursor-pointer items-center justify-center rounded border border-slate-700 bg-slate-900/90 text-slate-300 transition-colors hover:bg-slate-800 hover:text-slate-100 focus:outline-none focus-visible:ring-2 focus-visible:ring-sky-500"
    >
      <svg
        width="16"
        height="16"
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        strokeWidth="1.5"
        strokeLinecap="round"
        strokeLinejoin="round"
      >
        {children}
      </svg>
    </button>
  );
}
