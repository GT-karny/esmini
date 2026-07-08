# Resolve virtual junction connectivity and OSI classification

Stacked on top of PR-A (parse-only). This PR wires up the connectivity that the
parsed virtual-junction data describes, and classifies a virtual junction for OSI.

## Motivation

After PR-A the parser understands `<junction type="virtual">`, but the runtime
connectivity graph does not yet know that a branch road joins the main road at a
mid-road anchor. The existing `CheckConnections()` pass cannot resolve this: the
unsplit main road never references the junction, and the branch roads reference
the **main road** (via `@elementS`), not the junction. This PR adds the missing
resolution pass and the OSI classification.

Refs ASAM OpenDRIVE 10.4; issue #592.

## What this PR does

- Adds a junction-driven pass after `CheckConnections()`
  (`OpenDrive::EstablishVirtualJunctionConnections`) that, per virtual junction:
  validates the span against the main road length, binds each branch road's
  `@elementS` link to its connection, synthesizes the missing branch→main
  *counter-connections* (following the existing direct-junction auto-add
  template), and fills a per-main-road **anchor registry**.
- The anchor registry (`GetVirtualJunctionAtRoadS(road_id, s)` +
  `GetVirtualJunctionAnchors(road_id)`) is **empty for every map that has no
  virtual junction**, which is the zero-cost-when-absent guarantee that bounds the
  regression risk: the routing/motion consumers in PR-C are gated on a non-empty
  registry lookup.
- Classifies a virtual junction as a non-intersection
  (`Junction::IsOsiIntersection() == false`), placed beside the existing
  `DIRECT` branch — a virtual junction owns no junction area, so it must not
  synthesize an OSI intersection lane.
- Short-circuits the reverse-link scan for links carrying `@elementS` so virtual
  maps load without a spurious "reversed road link not found" warning.

## Membership on the main-road span (v1)

A `Position` on the main-road span keeps reporting **no** junction id
(`GetJunctionId() == -1`, `IsInJunction() == false`). This is a deliberate v1
choice: the connecting (branch) roads already carry the junction id, and leaving
the main-road span as "not in a junction" avoids re-randomising the junction
selector on a through-driving vehicle. See **Open questions** below.

## Test coverage

- `RoadManager_test.cpp` `VirtualJunctionTest.VirtualJunctionConnectivity`:
  registry hit inside the span (`GetVirtualJunctionAtRoadS(1, 100) == junction`)
  and miss outside it; a synthesized branch→main counter-connection with the
  expected outgoing anchor s; `IsOsiIntersection() == false`; and the main-road
  span reporting no junction id.

## Behavioural invariance

The new pass iterates only virtual junctions (none in existing assets), and the
registry stays empty, so connectivity, classification and membership are unchanged
for every existing map. The full upstream `RoadManager_test` suite stays green.

## Open questions (maintainer decisions)

1. **Main-road-span membership.** Should a position on the span report the
   virtual junction's id, or keep reporting −1 as here? Reporting the id is more
   "correct" but interacts with the junction-selector re-randomisation in the
   scenario engine. We chose −1 for v1 and would like the maintainers' preference
   before committing to it.
2. **`elementDir` reverse composition.** ASAM 10.4 does not pin how `@elementDir`
   composes when a branch→main counter-connection is *synthesized* (the reverse of
   the authored branch link). We map `+` → land heading s-increasing (contact
   START), `-` → s-decreasing (contact END), unknown → geometric fallback. This is
   an interpretation; if the spec intent differs we will follow the maintainers'
   reading.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
