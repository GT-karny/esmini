import { useMemo } from 'react';

interface OsiObject {
  id: number;
  x: number;
  y: number;
  z: number;
  h: number;
  speed: number;
  head_light: string;
  indicator: string;
  brake_light: string;
}

interface LiveSceneViewProps {
  objects: OsiObject[];
  className?: string;
}

const VEHICLE_LENGTH = 4;
const VEHICLE_WIDTH = 2;
const PADDING = 20;
const GRID_SPACING = 10;

export function LiveSceneView({ objects, className = '' }: LiveSceneViewProps) {
  const bounds = useMemo(() => {
    if (objects.length === 0) {
      return { minX: -50, maxX: 50, minY: -50, maxY: 50 };
    }
    let minX = Infinity, maxX = -Infinity;
    let minY = Infinity, maxY = -Infinity;
    for (const obj of objects) {
      if (obj.x < minX) minX = obj.x;
      if (obj.x > maxX) maxX = obj.x;
      if (obj.y < minY) minY = obj.y;
      if (obj.y > maxY) maxY = obj.y;
    }
    return {
      minX: minX - PADDING,
      maxX: maxX + PADDING,
      minY: minY - PADDING,
      maxY: maxY + PADDING,
    };
  }, [objects]);

  const width = bounds.maxX - bounds.minX;
  const height = bounds.maxY - bounds.minY;

  // Grid lines at 10m intervals
  const gridLines = useMemo(() => {
    const lines: { x1: number; y1: number; x2: number; y2: number; vertical: boolean }[] = [];
    const startX = Math.ceil(bounds.minX / GRID_SPACING) * GRID_SPACING;
    const startY = Math.ceil(bounds.minY / GRID_SPACING) * GRID_SPACING;
    for (let x = startX; x <= bounds.maxX; x += GRID_SPACING) {
      lines.push({ x1: x, y1: bounds.minY, x2: x, y2: bounds.maxY, vertical: true });
    }
    for (let y = startY; y <= bounds.maxY; y += GRID_SPACING) {
      lines.push({ x1: bounds.minX, y1: y, x2: bounds.maxX, y2: y, vertical: false });
    }
    return lines;
  }, [bounds]);

  // esmini Y-up → SVG Y-down: flip y
  const toSvgY = (y: number) => bounds.maxY - (y - bounds.minY);

  return (
    <svg
      viewBox={`${bounds.minX} 0 ${width} ${height}`}
      className={`w-full ${className}`}
      style={{ background: 'var(--color-glass-1, rgba(18,12,48,0.35))' }}
    >
      {/* Grid */}
      {gridLines.map((line, i) => (
        <line
          key={i}
          x1={line.x1}
          y1={toSvgY(line.y1)}
          x2={line.x2}
          y2={toSvgY(line.y2)}
          stroke="var(--color-glass-edge, rgba(180,170,230,0.07))"
          strokeWidth={0.3}
          opacity={0.3}
        />
      ))}

      {/* Objects */}
      {objects.length === 0 ? (
        <text
          x={bounds.minX + width / 2}
          y={height / 2}
          textAnchor="middle"
          dominantBaseline="central"
          fill="var(--color-text-secondary, #aaa)"
          fontSize={3}
          fontFamily="var(--font-body, sans-serif)"
        >
          Waiting for data...
        </text>
      ) : (
        objects.map((obj) => {
          const svgY = toSvgY(obj.y);
          // heading: esmini heading (rad), 0 = east, CCW positive
          // SVG rotation: CW positive from east, but Y is flipped so negate
          const angleDeg = (-obj.h * 180) / Math.PI;
          const isEgo = obj.id === 0;

          return (
            <g key={obj.id} transform={`translate(${obj.x}, ${svgY}) rotate(${angleDeg})`}>
              {/* Vehicle body */}
              <rect
                x={-VEHICLE_LENGTH / 2}
                y={-VEHICLE_WIDTH / 2}
                width={VEHICLE_LENGTH}
                height={VEHICLE_WIDTH}
                rx={0.3}
                fill={isEgo ? 'var(--color-primary, #7B88E8)' : 'var(--color-glass-edge, rgba(180,170,230,0.15))'}
                stroke={isEgo ? 'var(--color-accent-vivid, #9B84E8)' : 'var(--color-glass-edge-mid, rgba(180,170,230,0.14))'}
                strokeWidth={0.15}
                opacity={isEgo ? 0.9 : 0.7}
              />
              {/* Heading triangle (front of vehicle) */}
              <polygon
                points={`${VEHICLE_LENGTH / 2},0 ${VEHICLE_LENGTH / 2 - 0.8},-0.6 ${VEHICLE_LENGTH / 2 - 0.8},0.6`}
                fill={isEgo ? 'var(--color-accent-bright, #D0C6F2)' : 'var(--color-glass-edge-mid, rgba(180,170,230,0.14))'}
              />
              {/* ID label */}
              <text
                x={0}
                y={-VEHICLE_WIDTH / 2 - 0.5}
                textAnchor="middle"
                fill="var(--color-text-secondary, #aaa)"
                fontSize={1.2}
                fontFamily="var(--font-body, sans-serif)"
              >
                {obj.id}
              </text>
            </g>
          );
        })
      )}
    </svg>
  );
}
