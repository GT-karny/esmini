/** One route point as the map and the list both see it.
 *
 * `id` exists so React keys survive reordering and deletion, and so a point stays
 * the same object across a drag. Index alone was what made the first version
 * append-only: with no identity there was nothing to move or remove.
 */
export interface RoutePlanPoint {
  id: string;
  /** Where the user actually clicked / dragged to, in world metres. */
  x: number;
  y: number;
  /** Where it landed on the road. Undefined only until the first snap returns. */
  snap?: SnapResult;
}

/** Per-point snap status from POST /api/roads/snap.
 *
 * A missed point is NOT an error: it carries on_road=false so the map can mark
 * that one point while the others stay usable.
 */
export interface SnapResult {
  on_road: boolean;
  reason?: 'off_road' | 'not_routable';
  road_id?: number;
  lane_id?: number;
  s?: number;
  x?: number;
  y?: number;
  h?: number;
}

export function newPoint(x: number, y: number): RoutePlanPoint {
  return { id: `p${Date.now().toString(36)}${Math.random().toString(36).slice(2, 7)}`, x, y };
}

/** Short human label for a snapped point, e.g. "road 0 / lane +1 / s 50.0". */
export function describeSnap(snap?: SnapResult): string {
  if (!snap) return 'snapping…';
  if (!snap.on_road) {
    return snap.reason === 'not_routable' ? 'not a drivable lane' : 'off the road';
  }
  const lane = (snap.lane_id ?? 0) > 0 ? `+${snap.lane_id}` : `${snap.lane_id}`;
  return `road ${snap.road_id} / lane ${lane} / s ${snap.s?.toFixed(1)}`;
}
