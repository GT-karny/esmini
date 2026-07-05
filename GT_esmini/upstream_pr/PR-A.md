# Add OpenDRIVE virtual junction parsing (ASAM 1.7+)

## Motivation

ASAM OpenDRIVE 1.7 introduced **virtual junctions** (`<junction type="virtual">`,
spec section 10.4). Unlike common or direct junctions, a virtual junction does
not split the road network: the main road continues uninterrupted and the
junction only *describes* where branch roads connect to it, at an s-coordinate
along the (unsplit) main road. This is the natural way to model, for example, a
minor road joining a through road mid-block, or the pedestrian-crossing junctions
shipped in the official example set.

esmini currently does not support them: `<junction type="virtual">` is downgraded
to a default junction with a "not supported yet" warning, and a virtual
`<connection>` without a `connectingRoad` aborts the whole parse. This is the
first of four stacked PRs that add native support. **This PR is parse-only** and
introduces no behavioural change for any non-virtual map.

Refs issue #592.

## What this PR does

- Parses `<junction type="virtual">` and its span attributes `@mainRoad`,
  `@sStart`, `@sEnd`, `@orientation` (a missing `@orientation` is tolerated — the
  official pedestrian-crossing example omits it — and defaults to *none*).
- Parses the virtual `<connection>` shape: the mandatory `<predecessor>` /
  `<successor>` children carrying `@elementId`, `@elementS` and the optional
  `@elementDir`, from which the incoming/outgoing roads and the anchor
  s-coordinates are derived. A `connectingRoad` is optional; a connection without
  one (a *kind-2* topological connection) is stored and skipped rather than
  aborting the parse.
- Reads the optional `@elementS` / `@elementDir` mid-road contact on a
  `<road><link><predecessor|successor>`; a link with `@elementS` no longer
  requires a `@contactPoint`.
- Defensively rejects a non-finite / negative `@elementS` (one official example
  file contains a denormal value there) with a warning and a legacy fallback.

## Data model

`RoadManager.hpp` gains additive-only members:

- `RoadLink`: `element_s_` (`< 0` = legacy end contact — no new `ContactPointType`
  value, which is exhaustively switched) and a 3-value `element_dir_`.
- `Junction`: a `VirtualJunctionAttributes` span struct + accessors (the
  `JunctionType::VIRTUAL` enumerator already exists).
- `Connection`: `incoming_contact_s_` / `outgoing_contact_s_` anchors and an
  `is_virtual_` flag for kind-2 connections.

The registry that consumes these anchors, connectivity synthesis, routing and OSI
output all follow in the later PRs (B/C/D). Parsing alone leaves every non-virtual
map byte-identical.

## Spec references

- ASAM OpenDRIVE 1.7 / 1.8 section 10.4 "Junctions", `type="virtual"`,
  `<connection>` with `<predecessor>`/`<successor>` and `@elementDir`.

## Test coverage

- New fixture `EnvironmentSimulator/Unittest/xodr/virtual_junction_simple.xodr`:
  a main road (unsplit, no links), a branch attaching mid-road via
  `<predecessor elementS="100" elementDir="+">`, a continuation road, and a
  `type="virtual"` junction (`mainRoad=1 sStart=95 sEnd=105 orientation="+"`) with
  one anchor-carrying connection and one connection-less virtual connection.
- New `RoadManager_test.cpp` test `VirtualJunctionTest.ParseVirtualJunction`
  asserts the junction type, span attributes, connection anchor s and the branch
  road's mid-road elementS/elementDir link — and that the load neither aborts nor
  warns about an unsupported junction.

## Behavioural invariance

No existing map contains a virtual junction, so no existing parse path changes.
The relaxed "missing contact point" diagnostic only fires differently when both
`@contactPoint` and `@elementS` are absent (unchanged for real assets). The full
upstream `RoadManager_test` suite stays green.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
