import { useMemo, useRef, useState, type ReactElement } from 'react';
import type { RoutePlan, RoutePoint } from '../../api/client';
import { type RoadBoundary, flipY, getBoundaryStyle } from '../../lib/sceneGeometry';

/**
 * Whole-network road map with click-to-place route points.
 *
 * Deliberately NOT a reuse of LiveSceneView: that one's viewBox follows the ego
 * vehicle frame by frame, which is the opposite of what picking a route needs
 * (a stable overview you can click twice). What IS reused is the shared geometry
 * vocabulary in lib/sceneGeometry -- flipY and getBoundaryStyle -- so a road drawn
 * here looks identical to the same road drawn during a run.
 *
 * Coordinate handling: the SVG viewBox is set to the road network's own bounding
 * box in world units (with y flipped, since world y is up and SVG y is down). That
 * means a click can be converted back to world coordinates by inverting the CTM,
 * with no scale bookkeeping of our own -- and the backend snaps that world point
 * to a lane, so what the user clicks and what gets routed cannot drift apart.
 */

interface RouteMapViewProps {
  boundaries: RoadBoundary[];
  points: RoutePoint[];
  plan: RoutePlan | null;
  onAddPoint: (point: RoutePoint) => void;
  className?: string;
}

const PADDING_M = 20;

export function RouteMapView({
  boundaries,
  points,
  plan,
  onAddPoint,
  className = '',
}: RouteMapViewProps): ReactElement {
  const svgRef = useRef<SVGSVGElement>(null);
  const [hover, setHover] = useState<RoutePoint | null>(null);

  const bounds = useMemo(() => {
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
    if (!Number.isFinite(minX)) return { minX: -50, minY: -50, width: 100, height: 100 };
    return {
      minX: minX - PADDING_M,
      minY: minY - PADDING_M,
      width: maxX - minX + PADDING_M * 2,
      height: maxY - minY + PADDING_M * 2,
    };
  }, [boundaries]);

  // One rendered pixel expressed in world metres (assuming a ~400 px tall viewport).
  //
  // The viewBox is in metres, and getBoundaryStyle's widths are REAL widths (0.25 m
  // for a road edge) chosen for LiveSceneView's ~50 m view. Across a whole network
  // -- often hundreds of metres, sometimes kilometres -- those become sub-pixel
  // hairlines. So every stroke below takes the LARGER of its true width and a
  // pixel-based floor: close-up the road keeps its real proportions, zoomed out it
  // stays visible. Markers use the pixel unit alone, since a route point is a UI
  // affordance and has no physical size to be faithful to.
  const px = Math.max(bounds.width, bounds.height) / 400;

  const toWorld = (evt: React.MouseEvent<SVGSVGElement>): RoutePoint | null => {
    const svg = svgRef.current;
    if (!svg) return null;
    const ctm = svg.getScreenCTM();
    if (!ctm) return null;
    const pt = svg.createSVGPoint();
    pt.x = evt.clientX;
    pt.y = evt.clientY;
    const local = pt.matrixTransform(ctm.inverse());
    // Undo the y flip applied when drawing.
    return { x: local.x, y: flipY(local.y) };
  };

  const routeLine = useMemo(() => {
    if (!plan || plan.waypoints.length < 2) return '';
    return plan.waypoints.map((w) => `${w.x},${flipY(w.y)}`).join(' ');
  }, [plan]);

  return (
    <svg
      ref={svgRef}
      className={`h-full w-full cursor-crosshair bg-slate-950 ${className}`}
      viewBox={`${bounds.minX} ${bounds.minY} ${bounds.width} ${bounds.height}`}
      onClick={(evt) => {
        const world = toWorld(evt);
        if (world) onAddPoint(world);
      }}
      onMouseMove={(evt) => setHover(toWorld(evt))}
      onMouseLeave={() => setHover(null)}
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
          strokeWidth={px * 2.5}
          strokeLinejoin="round"
          strokeLinecap="round"
          opacity={0.9}
        />
      )}

      {/* Lane changes the route requires -- the thing a user cannot infer by eye. */}
      {plan?.lane_changes.map((lc, i) => {
        const wp = plan.waypoints.find((w) => w.road_id === lc.road_id);
        if (!wp) return null;
        return (
          <circle
            key={`lc-${i}`}
            cx={wp.x}
            cy={flipY(wp.y)}
            r={px * 6}
            fill="none"
            stroke="#fbbf24"
            strokeWidth={px * 1.5}
          />
        );
      })}

      {points.map((p, i) => {
        const isStart = i === 0;
        const isGoal = i === points.length - 1 && points.length > 1;
        const snapped = plan?.snapped?.[i];
        return (
          <g key={`p-${i}`}>
            {/* Line from the click to where it snapped, so a user can see the
                point was moved onto a lane rather than silently relocated. */}
            {snapped && (
              <line
                x1={p.x}
                y1={flipY(p.y)}
                x2={snapped.x}
                y2={flipY(snapped.y)}
                stroke="#94a3b8"
                strokeWidth={px * 0.8}
              />
            )}
            <circle
              cx={snapped ? snapped.x : p.x}
              cy={flipY(snapped ? snapped.y : p.y)}
              r={px * 5}
              fill={isStart ? '#22c55e' : isGoal ? '#ef4444' : '#a855f7'}
              stroke="#0f172a"
              strokeWidth={px * 1.2}
            />
          </g>
        );
      })}

      {hover && (
        <text
          x={bounds.minX + px * 6}
          y={bounds.minY + px * 16}
          fill="#64748b"
          fontSize={px * 12}
          fontFamily="monospace"
        >
          {hover.x.toFixed(1)}, {hover.y.toFixed(1)}
        </text>
      )}
    </svg>
  );
}
