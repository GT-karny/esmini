import { useCallback, useMemo, useRef, useState, type ReactElement } from 'react';
import type { TrafficLight } from '../hooks/useOsiStream';
import type { MidLongConstraint, MidLongProfile, VdTelemetryFrame } from '../api/client';

/* ---------- Types ---------- */

interface OsiObject {
  id: number;
  name: string;
  x: number;
  y: number;
  z: number;
  h: number;
  speed: number;
  head_light: string;
  indicator: string;
  brake_light: string;
  obj_type: string;
  vehicle_class: string;
  length: number;
  width: number;
}

export interface RoadBoundary {
  road_id: number;
  type: string;
  points: [number, number][];
}

export interface RoadSign {
  id: number;
  road_id: number;
  x: number;
  y: number;
  z: number;
  h: number;
  s: number;
  t: number;
  name: string;
  orientation: number;
  height: number;
  width: number;
}

export interface StopLine {
  road_id: number;
  sign_id: number;
  points: [number, number][];
}

export interface RoadGeometry {
  boundaries: RoadBoundary[];
  signs?: RoadSign[];
  stop_lines?: StopLine[];
}

interface LiveSceneViewProps {
  objects: OsiObject[];
  roadGeometry?: RoadGeometry | null;
  trafficLights?: TrafficLight[];
  /** Current VirtualDriver telemetry frame (replay or live) -> short-horizon preview overlay. */
  vdTelemetry?: VdTelemetryFrame | null;
  /** Mid/long planner output (Phase 2) -> v_target labels + maneuver markers. */
  midlong?: MidLongProfile | null;
  /** Baseline (Default) ego path for 2-run comparison, as world [x,y] points. */
  ghostPath?: [number, number][] | null;
  className?: string;
  viewRadius?: number;
}

type LayerKey = 'signs' | 'stopLines' | 'signals' | 'vTarget' | 'maneuverMarkers';

/* ---------- Constants ---------- */

const DEFAULT_VIEW_RADIUS = 50;
const MIN_VIEW_RADIUS = 10;
const MAX_VIEW_RADIUS = 500;
const ZOOM_FACTOR = 1.15; // per wheel tick
const GRID_SPACING = 10;

/**
 * esmini world (Y-up) → SVG (Y-down) using a *constant* mapping (svgY = -worldY).
 * The previous mapping (maxY - y) folded the camera position into every node's
 * coordinates, forcing a full re-render each frame. With a fixed flip, only the
 * SVG viewBox moves with the ego — static geometry keeps identical attributes.
 */
const flipY = (y: number) => -y;

/* ---------- Styling helpers ---------- */

function getBoundaryStyle(type: string): {
  stroke: string;
  strokeWidth: number;
  dasharray: string;
  opacity: number;
} {
  switch (type) {
    case 'center_line':
      return { stroke: 'rgba(220,200,50,0.7)', strokeWidth: 0.15, dasharray: '', opacity: 0.7 };
    case 'lane_divider':
      return { stroke: 'rgba(220,220,220,0.6)', strokeWidth: 0.12, dasharray: '3 2', opacity: 0.6 };
    case 'road_edge':
      return { stroke: 'rgba(220,220,220,0.8)', strokeWidth: 0.25, dasharray: '', opacity: 0.8 };
    case 'sidewalk_edge':
      return { stroke: 'rgba(160,160,160,0.6)', strokeWidth: 0.2, dasharray: '', opacity: 0.6 };
    default:
      return { stroke: 'rgba(180,180,180,0.4)', strokeWidth: 0.1, dasharray: '', opacity: 0.4 };
  }
}

function getObjectColors(obj: OsiObject, isEgo: boolean): { fill: string; stroke: string } {
  if (isEgo) {
    return { fill: 'var(--color-primary, #7B88E8)', stroke: 'var(--color-accent-vivid, #9B84E8)' };
  }
  switch (obj.obj_type) {
    case 'pedestrian':
      return { fill: 'rgba(240,160,80,0.7)', stroke: 'rgba(240,160,80,0.9)' };
    case 'animal':
      return { fill: 'rgba(150,150,150,0.5)', stroke: 'rgba(150,150,150,0.7)' };
    case 'vehicle': {
      const vc = obj.vehicle_class;
      if (vc === 'heavy_truck' || vc === 'semitrailer' || vc === 'semitractor' || vc === 'trailer')
        return { fill: 'rgba(200,120,120,0.5)', stroke: 'rgba(200,120,120,0.7)' };
      if (vc === 'bus' || vc === 'tram' || vc === 'train')
        return { fill: 'rgba(120,180,200,0.5)', stroke: 'rgba(120,180,200,0.7)' };
      if (vc === 'bicycle' || vc === 'motorbike' || vc === 'standup_scooter')
        return { fill: 'rgba(120,200,120,0.6)', stroke: 'rgba(120,200,120,0.8)' };
      return { fill: 'rgba(180,170,230,0.25)', stroke: 'rgba(180,170,230,0.45)' };
    }
    default:
      return { fill: 'rgba(150,150,150,0.3)', stroke: 'rgba(150,150,150,0.5)' };
  }
}

/* ---------- Component ---------- */

export function LiveSceneView({
  objects,
  roadGeometry,
  trafficLights,
  vdTelemetry,
  midlong,
  ghostPath,
  className = '',
  viewRadius: initialRadius = DEFAULT_VIEW_RADIUS,
}: LiveSceneViewProps) {
  const svgRef = useRef<SVGSVGElement>(null);
  const [zoom, setZoom] = useState(1); // >1 = zoomed in, <1 = zoomed out
  const [layers, setLayers] = useState<Record<LayerKey, boolean>>({
    signs: true,
    stopLines: true,
    signals: true,
    vTarget: true,
    maneuverMarkers: true,
  });

  const viewRadius = initialRadius / zoom;

  const handleWheel = useCallback((e: React.WheelEvent<SVGSVGElement>) => {
    e.preventDefault();
    setZoom((prev) => {
      const next = e.deltaY < 0 ? prev * ZOOM_FACTOR : prev / ZOOM_FACTOR;
      const minZoom = initialRadius / MAX_VIEW_RADIUS;
      const maxZoom = initialRadius / MIN_VIEW_RADIUS;
      return Math.max(minZoom, Math.min(maxZoom, next));
    });
  }, [initialRadius]);

  // Ego = first MovingObject in OSI list (index 0)
  const ego = useMemo(() => (objects.length > 0 ? objects[0] : null), [objects]);

  // Ego-centered viewport
  const viewport = useMemo(() => {
    if (ego) return { cx: ego.x, cy: ego.y };
    return { cx: 0, cy: 0 };
  }, [ego]);

  const minX = viewport.cx - viewRadius;
  const maxX = viewport.cx + viewRadius;
  const minY = viewport.cy - viewRadius;
  const maxY = viewport.cy + viewRadius;
  const size = viewRadius * 2;

  // Grid is rebuilt only when the camera crosses a grid cell (hysteresis), not
  // every frame. Quantising the viewport bounds to GRID_SPACING keeps these
  // deps stable across the ~10m of travel between cells, so the grid memo (and
  // its SVG nodes) stay reconciliation-free while the ego moves smoothly.
  const gMinX = Math.floor(minX / GRID_SPACING) * GRID_SPACING;
  const gMaxX = Math.ceil(maxX / GRID_SPACING) * GRID_SPACING;
  const gMinY = Math.floor(minY / GRID_SPACING) * GRID_SPACING;
  const gMaxY = Math.ceil(maxY / GRID_SPACING) * GRID_SPACING;

  const gridLines = useMemo(() => {
    const lines: { x1: number; y1: number; x2: number; y2: number }[] = [];
    for (let x = gMinX; x <= gMaxX; x += GRID_SPACING) {
      lines.push({ x1: x, y1: flipY(gMaxY), x2: x, y2: flipY(gMinY) });
    }
    for (let y = gMinY; y <= gMaxY; y += GRID_SPACING) {
      lines.push({ x1: gMinX, y1: flipY(y), x2: gMaxX, y2: flipY(y) });
    }
    return lines;
  }, [gMinX, gMaxX, gMinY, gMaxY]);

  // Road boundaries: rendered once in a fixed world frame (constant Y-flip),
  // independent of the camera. Panning/zooming is done purely via the SVG
  // viewBox below, so these path nodes never change attributes frame-to-frame
  // and React skips reconciling them. (For very large networks, swap the full
  // render for a hysteresis-clipped subset — see plan risks.)
  const roadPaths = useMemo(() => {
    if (!roadGeometry) return null;
    return roadGeometry.boundaries.map((boundary, idx) => {
      if (boundary.points.length < 2) return null;
      const d = boundary.points
        .map(([px, py], i) => `${i === 0 ? 'M' : 'L'}${px},${flipY(py)}`)
        .join(' ');
      const style = getBoundaryStyle(boundary.type);

      return (
        <path
          key={`road-${idx}`}
          d={d}
          fill="none"
          stroke={style.stroke}
          strokeWidth={style.strokeWidth}
          strokeDasharray={style.dasharray || undefined}
          opacity={style.opacity}
        />
      );
    });
  }, [roadGeometry]);

  // Split objects: non-ego rendered first, ego (first object) on top
  const egoId = ego?.id;
  const nonEgo = useMemo(() => (egoId != null ? objects.slice(1) : objects), [objects, egoId]);

  // Static environment layers (camera-independent, constant Y-flip → stable nodes)
  const stopLinePaths = useMemo(() => {
    const lines = roadGeometry?.stop_lines;
    if (!lines || lines.length === 0) return null;
    return lines.map((line, idx) => {
      if (line.points.length < 2) return null;
      const d = line.points
        .map(([px, py], i) => `${i === 0 ? 'M' : 'L'}${px},${flipY(py)}`)
        .join(' ');
      return (
        <path
          key={`stop-${line.road_id}-${line.sign_id}-${idx}`}
          d={d}
          fill="none"
          stroke="rgba(245,245,245,0.85)"
          strokeWidth={0.35}
          strokeLinecap="round"
        />
      );
    });
  }, [roadGeometry]);

  const signMarkers = useMemo(() => {
    const signs = roadGeometry?.signs;
    if (!signs || signs.length === 0) return null;
    return signs.map((sign) => renderSign(sign, flipY));
  }, [roadGeometry]);

  // Traffic-light phase comes live from the OSI WS stream (per-frame).
  const signalNodes = useMemo(
    () => (trafficLights && trafficLights.length > 0 ? renderTrafficLights(trafficLights, flipY) : null),
    [trafficLights],
  );

  // VirtualDriver short-horizon preview (speed-coloured polyline + sample dots).
  const vdPreview = useMemo(
    () => (vdTelemetry?.preview?.points?.length ? renderVdPreview(vdTelemetry, flipY) : null),
    [vdTelemetry],
  );

  // v_target speed labels along the short-horizon preview (no A2 dependency:
  // the profile is in route-s with no XY, so the spatial readout reuses the
  // preview sample points which already carry XY + target speed v).
  const vTargetLabels = useMemo(() => {
    const pts = vdTelemetry?.preview?.points;
    if (!pts || pts.length < 2) return null;
    const step = Math.max(1, Math.floor(pts.length / 5));
    const out: ReactElement[] = [];
    for (let i = step; i < pts.length; i += step) {
      const p = pts[i];
      out.push(
        <text key={`vt${i}`} x={p.x + 0.6} y={flipY(p.y)} fontSize={1.6}
          fill="rgba(120,210,150,0.9)" fontFamily="var(--font-mono, monospace)">
          {p.v.toFixed(0)}
        </text>,
      );
    }
    return out;
  }, [vdTelemetry]);

  // Mid/long maneuver markers at constraint world XY (curve / junction / speed
  // limit). [A2] Driven by midlong.constraints, which carry real XY; null until
  // A2 emits them.
  const maneuverMarkersNode = useMemo(() => {
    const cs = midlong?.valid ? midlong.constraints : undefined;
    if (!cs || cs.length === 0) return null;
    return cs.map((c, i) => renderManeuverMarker(c, flipY, i));
  }, [midlong]);

  // Baseline (Default) ghost path for 2-run comparison.
  const ghostPathNode = useMemo(() => {
    if (!ghostPath || ghostPath.length < 2) return null;
    const d = ghostPath.map(([px, py], i) => `${i === 0 ? 'M' : 'L'}${px},${flipY(py)}`).join(' ');
    return <path d={d} fill="none" stroke="rgba(230,200,120,0.5)" strokeWidth={0.25} strokeDasharray="1.5 1.2" />;
  }, [ghostPath]);

  const hasSigns = (roadGeometry?.signs?.length ?? 0) > 0;
  const hasStopLines = (roadGeometry?.stop_lines?.length ?? 0) > 0;
  const hasSignals = (trafficLights?.length ?? 0) > 0;
  const hasVdPreview = (vdTelemetry?.preview?.points?.length ?? 0) > 0;
  const hasManeuver = !!(midlong?.valid && (midlong.constraints?.length ?? 0) > 0);
  const showLayerBar = hasSigns || hasStopLines || hasSignals || hasVdPreview || hasManeuver;

  return (
    <div className={`relative ${className}`}>
      {showLayerBar && (
        <div className="absolute top-2 left-2 z-10 flex gap-1 flex-wrap">
          {hasSignals && (
            <LayerToggle label="Signals" active={layers.signals}
              onClick={() => setLayers((l) => ({ ...l, signals: !l.signals }))} />
          )}
          {hasSigns && (
            <LayerToggle label="Signs" active={layers.signs}
              onClick={() => setLayers((l) => ({ ...l, signs: !l.signs }))} />
          )}
          {hasStopLines && (
            <LayerToggle label="Stop lines" active={layers.stopLines}
              onClick={() => setLayers((l) => ({ ...l, stopLines: !l.stopLines }))} />
          )}
          {hasVdPreview && (
            <LayerToggle label="v_target" active={layers.vTarget}
              onClick={() => setLayers((l) => ({ ...l, vTarget: !l.vTarget }))} />
          )}
          {hasManeuver && (
            <LayerToggle label="Maneuver" active={layers.maneuverMarkers}
              onClick={() => setLayers((l) => ({ ...l, maneuverMarkers: !l.maneuverMarkers }))} />
          )}
        </div>
      )}
      {renderScene()}
    </div>
  );

  function renderScene() {
    return (
    <svg
      ref={svgRef}
      viewBox={`${minX} ${flipY(maxY)} ${size} ${size}`}
      className="w-full h-full block"
      style={{ background: 'var(--color-glass-1, rgba(18,12,48,0.35))' }}
      onWheel={handleWheel}
    >
      {/* Road boundaries */}
      {roadPaths}

      {/* Grid */}
      {gridLines.map((line, i) => (
        <line
          key={`g${i}`}
          x1={line.x1}
          y1={line.y1}
          x2={line.x2}
          y2={line.y2}
          stroke="var(--color-glass-edge, rgba(180,170,230,0.07))"
          strokeWidth={0.3}
          opacity={0.3}
        />
      ))}

      {/* Stop lines (static, from road geometry) */}
      {layers.stopLines && stopLinePaths}

      {/* Non-ego objects */}
      {nonEgo.map((obj) => renderObject(obj, flipY, false))}

      {/* Ego vehicle (on top) */}
      {ego && renderObject(ego, flipY, true)}

      {/* Baseline ghost path (faint, under everything dynamic) */}
      {ghostPathNode}

      {/* VirtualDriver preview trajectory (under markers, over road) */}
      {vdPreview}
      {layers.vTarget && vTargetLabels}

      {/* Mid/long maneuver markers (curve / junction / speed-limit preview) */}
      {layers.maneuverMarkers && maneuverMarkersNode}

      {/* Signs + traffic-light heads (on top of vehicles) */}
      {layers.signs && signMarkers}
      {layers.signals && signalNodes}

      {/* Empty state */}
      {objects.length === 0 && (
        <text
          x={viewport.cx}
          y={flipY(viewport.cy)}
          textAnchor="middle"
          dominantBaseline="central"
          fill="var(--color-text-secondary, #aaa)"
          fontSize={3}
          fontFamily="var(--font-body, sans-serif)"
        >
          Waiting for data...
        </text>
      )}
    </svg>
    );
  }
}

/* ---------- Layer toggle ---------- */

function LayerToggle({
  label,
  active,
  onClick,
}: {
  label: string;
  active: boolean;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      className={`px-2 py-0.5 rounded text-[10px] font-medium transition-colors backdrop-blur ${
        active
          ? 'bg-primary/80 text-white'
          : 'bg-glass-2/70 text-text-tertiary hover:bg-glass-2'
      }`}
    >
      {label}
    </button>
  );
}

/* ---------- Mid/long maneuver markers ---------- */

const MANEUVER_COLOR: Record<MidLongConstraint['kind'], string> = {
  curve: '#E8A24F',
  junction: '#E0568A',
  speed_limit: '#4F9DE8',
  stop: '#E03131',
};

// Diamond marker at the constraint's world XY with kind + target speed label.
function renderManeuverMarker(
  c: MidLongConstraint,
  toSvgY: (y: number) => number,
  key: number,
): ReactElement {
  const cx = c.x;
  const cy = toSvgY(c.y);
  const color = MANEUVER_COLOR[c.kind] ?? '#9B84E8';
  const r = 1.4;
  return (
    <g key={`mm${key}`}>
      <path
        d={`M${cx},${cy - r} L${cx + r},${cy} L${cx},${cy + r} L${cx - r},${cy} Z`}
        fill="none"
        stroke={color}
        strokeWidth={0.3}
      />
      <circle cx={cx} cy={cy} r={0.3} fill={color} />
      <text
        x={cx + r + 0.4}
        y={cy}
        fontSize={1.6}
        fill={color}
        dominantBaseline="central"
        fontFamily="var(--font-mono, monospace)"
      >
        {c.kind} {c.v.toFixed(0)}
      </text>
    </g>
  );
}

/* ---------- Environment renderers ---------- */

// Best-effort sign classification from the OpenDRIVE name (RM_RoadSign has no
// OSI type). Falls back to a neutral marker.
function classifySign(name: string): { kind: 'stop' | 'yield' | 'other'; color: string } {
  const n = name.toLowerCase();
  if (n.includes('stop')) return { kind: 'stop', color: 'rgb(220,60,60)' };
  if (n.includes('yield') || n.includes('give') || n.includes('giveway')) {
    return { kind: 'yield', color: 'rgb(235,180,60)' };
  }
  return { kind: 'other', color: 'rgb(150,160,200)' };
}

function renderSign(sign: RoadSign, toSvgY: (y: number) => number): ReactElement {
  const sx = sign.x;
  const sy = toSvgY(sign.y);
  const { kind, color } = classifySign(sign.name);
  const r = 1.0;

  let shape: ReactElement;
  if (kind === 'stop') {
    // Octagon
    const pts = Array.from({ length: 8 }, (_, i) => {
      const a = (Math.PI / 8) + (i * Math.PI) / 4;
      return `${(r * Math.cos(a)).toFixed(2)},${(r * Math.sin(a)).toFixed(2)}`;
    }).join(' ');
    shape = <polygon points={pts} fill={color} stroke="white" strokeWidth={0.12} />;
  } else if (kind === 'yield') {
    // Inverted triangle
    shape = (
      <polygon
        points={`${-r},${-r * 0.8} ${r},${-r * 0.8} 0,${r}`}
        fill={color}
        stroke="white"
        strokeWidth={0.12}
      />
    );
  } else {
    shape = <circle r={r * 0.8} fill={color} stroke="white" strokeWidth={0.12} />;
  }

  return (
    <g key={`sign-${sign.road_id}-${sign.id}`} transform={`translate(${sx}, ${sy})`}>
      {shape}
      {sign.name && (
        <text
          x={0}
          y={-r - 0.4}
          textAnchor="middle"
          fill="var(--color-text-secondary, #aaa)"
          fontSize={1.0}
          fontFamily="var(--font-body, sans-serif)"
        >
          {sign.name}
        </text>
      )}
    </g>
  );
}

function trafficLightFill(color: string): string {
  switch (color) {
    case 'red':
      return 'rgb(235,50,50)';
    case 'yellow':
      return 'rgb(240,200,50)';
    case 'green':
      return 'rgb(60,210,90)';
    default:
      return 'rgb(160,160,160)';
  }
}

// Each lamp is a separate TrafficLight at (nearly) the same x,y. Render inactive
// lamps first as dim rings, then active (mode != off/unknown) on top so the live
// phase colour is always visible regardless of array order.
function renderTrafficLights(
  lights: TrafficLight[],
  toSvgY: (y: number) => number,
): ReactElement {
  const isActive = (l: TrafficLight) => l.mode === 'constant' || l.mode === 'flashing' || l.mode === 'counting';
  const inactive = lights.filter((l) => !isActive(l));
  const active = lights.filter(isActive);

  return (
    <g>
      {inactive.map((l) => (
        <circle
          key={`tl-off-${l.id}`}
          cx={l.x}
          cy={toSvgY(l.y)}
          r={0.6}
          fill="none"
          stroke="rgba(150,150,150,0.5)"
          strokeWidth={0.15}
        />
      ))}
      {active.map((l) => (
        <circle
          key={`tl-on-${l.id}`}
          cx={l.x}
          cy={toSvgY(l.y)}
          r={0.8}
          fill={trafficLightFill(l.color)}
          stroke="white"
          strokeWidth={0.12}
          opacity={l.mode === 'flashing' ? 0.7 : 0.95}
        />
      ))}
    </g>
  );
}

function speedColor(frac: number): string {
  const f = Math.max(0, Math.min(1, frac));
  // slow (green) -> fast (red)
  const r = Math.round(80 + (235 - 80) * f);
  const g = Math.round(210 + (90 - 210) * f);
  const b = Math.round(140 + (90 - 140) * f);
  return `rgb(${r},${g},${b})`;
}

// VirtualDriver short-horizon trajectory preview: polyline coloured by target
// speed, with sample dots. (x,y,v,t) come straight from GT_GetVirtualDriverTelemetry.
function renderVdPreview(tel: VdTelemetryFrame, toSvgY: (y: number) => number): ReactElement {
  const pts = tel.preview.points;
  const vmax = Math.max(0.001, ...pts.map((p) => p.v));
  const segs: ReactElement[] = [];
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1];
    const b = pts[i];
    segs.push(
      <line
        key={`vdseg-${i}`}
        x1={a.x} y1={toSvgY(a.y)} x2={b.x} y2={toSvgY(b.y)}
        stroke={speedColor((a.v + b.v) / 2 / vmax)}
        strokeWidth={0.4}
        strokeLinecap="round"
        opacity={0.9}
      />,
    );
  }
  return (
    <g>
      {segs}
      {pts.map((p, i) => (
        <circle key={`vdpt-${i}`} cx={p.x} cy={toSvgY(p.y)} r={0.16} fill="rgba(255,255,255,0.75)" />
      ))}
    </g>
  );
}

/* ---------- Object renderers ---------- */

function renderObject(
  obj: OsiObject,
  toSvgY: (y: number) => number,
  isEgo: boolean,
) {
  const svgY = toSvgY(obj.y);
  const angleDeg = (-obj.h * 180) / Math.PI;
  const { fill, stroke } = getObjectColors(obj, isEgo);

  // Actual dimensions from OSI (with fallbacks)
  const length = obj.length > 0 ? obj.length : 4.0;
  const width = obj.width > 0 ? obj.width : 2.0;

  return (
    <g key={obj.id} transform={`translate(${obj.x}, ${svgY}) rotate(${angleDeg})`}>
      {renderShape(obj, length, width, fill, stroke, isEgo)}

      {/* Heading indicator for vehicles */}
      {(obj.obj_type === 'vehicle' || obj.obj_type === '' || obj.obj_type === 'unknown') && (
        <polygon
          points={`${length / 2},0 ${length / 2 - 0.8},-0.6 ${length / 2 - 0.8},0.6`}
          fill={isEgo ? 'var(--color-accent-bright, #D0C6F2)' : 'rgba(180,170,230,0.3)'}
        />
      )}

      {/* Name label */}
      <text
        x={0}
        y={-Math.max(width, 1) / 2 - 0.6}
        textAnchor="middle"
        fill="var(--color-text-secondary, #aaa)"
        fontSize={1.2}
        fontFamily="var(--font-body, sans-serif)"
      >
        {obj.name || obj.id}
      </text>
    </g>
  );
}

function renderShape(
  obj: OsiObject,
  length: number,
  width: number,
  fill: string,
  stroke: string,
  isEgo: boolean,
): ReactElement {
  switch (obj.obj_type) {
    case 'pedestrian': {
      const r = Math.max(0.3, width / 2);
      return (
        <circle
          cx={0}
          cy={0}
          r={r}
          fill={fill}
          stroke={stroke}
          strokeWidth={isEgo ? 0.15 : 0.1}
        />
      );
    }

    case 'vehicle': {
      const vc = obj.vehicle_class;
      const isTwoWheeler =
        vc === 'bicycle' || vc === 'motorbike' || vc === 'standup_scooter';
      if (isTwoWheeler) {
        return (
          <ellipse
            cx={0}
            cy={0}
            rx={length / 2}
            ry={width / 2}
            fill={fill}
            stroke={stroke}
            strokeWidth={0.1}
          />
        );
      }
      return (
        <rect
          x={-length / 2}
          y={-width / 2}
          width={length}
          height={width}
          rx={0.3}
          fill={fill}
          stroke={stroke}
          strokeWidth={isEgo ? 0.2 : 0.12}
          opacity={isEgo ? 0.9 : 0.75}
        />
      );
    }

    case 'animal': {
      const s = Math.max(0.5, length / 2);
      return (
        <polygon
          points={`${s},0 0,${s * 0.6} ${-s},0 0,${-s * 0.6}`}
          fill={fill}
          stroke={stroke}
          strokeWidth={0.1}
        />
      );
    }

    default:
      // Unknown / other / legacy (no obj_type field)
      return (
        <rect
          x={-length / 2}
          y={-width / 2}
          width={length}
          height={width}
          rx={0.3}
          fill={fill}
          stroke={stroke}
          strokeWidth={isEgo ? 0.2 : 0.12}
          opacity={isEgo ? 0.9 : 0.7}
        />
      );
  }
}
