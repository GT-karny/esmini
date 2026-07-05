# Route and move across virtual junctions

Stacked on top of PR-A (parse) and PR-B (connectivity). This PR makes vehicles
actually traverse a virtual junction: branch off the main road at a mid-road
anchor and merge back onto it.

## Motivation

After PR-B the connectivity graph and anchor registry are complete, but the
position/routing subsystems still treat road links as end-to-end contacts. A
virtual-junction anchor lies **mid-road**, so `RoadPath`, `Route`, the position
stepping (`MoveAlongS` / `MoveToConnectingRoad`) and the `LaneIndependentRouter`
need to learn how to enter and leave a road at an arbitrary s.

The guiding invariant is **"straight-through is free, turning is the feature"**:
every change is an additive branch that activates only when a route or path
actually selects the branch across the anchor. Driving straight along the unsplit
main road is untouched.

Refs ASAM OpenDRIVE 10.4; issue #592.

## What this PR does

- **`RoadPath`**: a link with `@elementS >= 0` anchors its node at the anchor s
  (no "opposite end" flip), the edge weight uses the partial traversal length, and
  the search seeds one node per registry anchor on the start road so a link-less
  main road can still expand onto its branches.
- **`Route`**: `GetRoadAtOtherEndOfIncomingRoad` disambiguates the two ends of the
  *same* main road by anchor s; the traversed-length accounting clamps track-s to
  the leg's anchor span so route-s is not extrapolated past the anchor.
- **`MoveAlongS`**: per step, if the current road has registry anchors, scan the
  step window `(s, s+ds]` for an anchor; when the route selects the branch, split
  the move at the anchor and continue onto the branch — otherwise pass through
  untouched.
- **`MoveToConnectingRoad`**: departure is callable from a mid-road anchor, and
  the entry landing places the position at the anchor s (heading per the
  `elementDir` merge rule) rather than at s=0 / length.
- **`LaneIndependentRouter`**: injects virtual-junction edges when expanding a
  main road with registry anchors, resolves anchored hops and the branch→main
  merge-back, and uses the anchor lane section for the connecting lanes.
- **`ControllerLooming`**: the road-chain lookahead ends gracefully at a mid-road
  anchor (the contact-point direction is undefined there).

## Test coverage

- `RoadManager_test.cpp`:
  - `VirtualJunctionPassThrough` (T2): with no route set,
    `SetLanePos(1,-1,50); MoveAlongS(150)` stays on road 1 and ends
    `ERROR_END_OF_ROAD` — the branch is never taken implicitly.
  - `VirtualJunctionPathViaBranch` (T3): a `RoadPath` from the main road to the
    continuation road traverses the branch across the anchor.
  - `VirtualJunctionMergeBack` (T4): a route main→continuation is valid (departs
    the main road at the anchor and merges back).
- `FollowRoute_test.cpp` `FollowRouteTestVirtualJunction.FindPathAcrossVirtualJunction`:
  the lane-independent router finds a path from the main road to the continuation
  road across the virtual junction.

## Behavioural invariance

Every branch added here is gated on a non-empty anchor registry
(`GetVirtualJunctionAtRoadS`), which is empty for all existing maps. Ordinary
`@elementS` links on non-virtual roads stay on their existing (parse-only) path.
The full upstream `RoadManager_test` and `FollowRoute_test` suites stay green.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
