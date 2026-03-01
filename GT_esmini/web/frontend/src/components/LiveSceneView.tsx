import { useCallback, useMemo, useRef, useState } from 'react';

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

export interface RoadGeometry {
  boundaries: RoadBoundary[];
}

interface LiveSceneViewProps {
  objects: OsiObject[];
  roadGeometry?: RoadGeometry | null;
  className?: string;
  viewRadius?: number;
}

/* ---------- Constants ---------- */

const DEFAULT_VIEW_RADIUS = 50;
const MIN_VIEW_RADIUS = 10;
const MAX_VIEW_RADIUS = 500;
const ZOOM_FACTOR = 1.15; // per wheel tick
const GRID_SPACING = 10;
const CLIP_MARGIN = 50; // extra margin for road clipping (meters)

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
  className = '',
  viewRadius: initialRadius = DEFAULT_VIEW_RADIUS,
}: LiveSceneViewProps) {
  const svgRef = useRef<SVGSVGElement>(null);
  const [zoom, setZoom] = useState(1); // >1 = zoomed in, <1 = zoomed out

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

  // esmini Y-up → SVG Y-down: world maxY maps to SVG 0, world minY maps to SVG size
  const toSvgY = (y: number) => maxY - y;

  // Grid lines
  const gridLines = useMemo(() => {
    const lines: { x1: number; y1: number; x2: number; y2: number }[] = [];
    const startX = Math.ceil(minX / GRID_SPACING) * GRID_SPACING;
    const startY = Math.ceil(minY / GRID_SPACING) * GRID_SPACING;
    for (let x = startX; x <= maxX; x += GRID_SPACING) {
      lines.push({ x1: x, y1: 0, x2: x, y2: size });
    }
    for (let y = startY; y <= maxY; y += GRID_SPACING) {
      const svgY = toSvgY(y);
      lines.push({ x1: minX, y1: svgY, x2: maxX, y2: svgY });
    }
    return lines;
  }, [minX, maxX, minY, maxY, size, toSvgY]);

  // Road boundaries (memoised — only recalculate on geometry or viewport change)
  const roadPaths = useMemo(() => {
    if (!roadGeometry) return null;
    return roadGeometry.boundaries.map((boundary, idx) => {
      // Clip: only include points near viewport
      const clipped = boundary.points.filter(
        ([px, py]) =>
          px >= minX - CLIP_MARGIN && px <= maxX + CLIP_MARGIN &&
          py >= minY - CLIP_MARGIN && py <= maxY + CLIP_MARGIN,
      );
      if (clipped.length < 2) return null;

      const d = clipped
        .map(([px, py], i) => `${i === 0 ? 'M' : 'L'}${px},${toSvgY(py)}`)
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
  }, [roadGeometry, minX, maxX, minY, maxY, toSvgY]);

  // Split objects: non-ego rendered first, ego (first object) on top
  const egoId = ego?.id;
  const nonEgo = useMemo(() => (egoId != null ? objects.slice(1) : objects), [objects, egoId]);

  return (
    <svg
      ref={svgRef}
      viewBox={`${minX} 0 ${size} ${size}`}
      className={`w-full ${className}`}
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

      {/* Non-ego objects */}
      {nonEgo.map((obj) => renderObject(obj, toSvgY, false))}

      {/* Ego vehicle (on top) */}
      {ego && renderObject(ego, toSvgY, true)}

      {/* Empty state */}
      {objects.length === 0 && (
        <text
          x={viewport.cx}
          y={size / 2}
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
): JSX.Element {
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
