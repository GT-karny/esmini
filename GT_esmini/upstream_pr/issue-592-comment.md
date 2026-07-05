<!-- Design-sketch comment to post on esmini issue #592 (virtual junction support)
     BEFORE opening the PRs. Keep it concise; the goal is to surface the two
     interpretation questions as maintainer decisions and offer the fixture corpus. -->

We have a working implementation of native OpenDRIVE **virtual junction**
(`<junction type="virtual">`, ASAM OpenDRIVE 1.7/1.8 section 10.4) support and
would like to upstream it. Sharing the design here first to get maintainer input
on two interpretation points before opening the PRs.

**Approach.** The map is never transformed — the main road stays unsplit, all road
ids and s-coordinates are preserved, and the runtime learns the mid-road
connections directly from the junction. The guiding principle is *"straight-through
is free, turning is the feature"*: every change is an additive branch that only
activates when a route/path actually crosses a virtual connection. For maps with no
virtual junction the anchor registry is empty, so the cost is zero and behaviour is
unchanged. The parse and connectivity follow the existing **direct-junction**
precedent (typed parse → post-parse synthesis → `IsOsiIntersection == false` →
direct-style OSI lane pairing).

**Proposed split** — four small stacked PRs:

- **A — parse only**: `type="virtual"` junctions, span attributes, and the
  `@elementS`/`@elementDir` mid-road contact on road links. No behavioural change.
- **B — connectivity + OSI classification**: post-`CheckConnections` resolution,
  branch→main counter-connections, an anchor registry, and
  `IsOsiIntersection == false`.
- **C — position/routing**: `RoadPath`, `Route`, `MoveAlongS` /
  `MoveToConnectingRoad`, and `LaneIndependentRouter` traversal across the anchor.
- **D — OSI lane pairing**: the per-`<laneLink>` pairing in
  `UpdateOSIIntersection`.

**Two questions we'd like your call on** (surfaced in PR-B):

1. **Main-road-span membership.** Should a `Position` on the unsplit main road,
   inside the junction span, report the junction id (`GetJunctionId()`), or keep
   reporting −1? Reporting the id is arguably more correct, but it interacts with
   the junction-selector re-randomisation in the scenario engine. Our v1 keeps −1;
   we'd rather match your intended semantics.
2. **`@elementDir` reverse composition.** ASAM 10.4 does not pin how `@elementDir`
   composes when we *synthesize* the branch→main counter-connection (the reverse of
   the authored branch link). We currently map `+` → land heading s-increasing,
   `-` → s-decreasing, unknown → geometric fallback. Happy to follow your reading.

**Fixtures.** We have a corpus of hand-authored 1.7/1.8 virtual-junction fixtures
(including a connection-less pedestrian-crossing shape and an LHT lane-section
variant) plus a minimal upstream-style twin
(`Unittest/xodr/virtual_junction_simple.xodr`) that ships with PR-A. We're glad to
contribute more of these if useful.

Would this split and these choices work for you? Happy to adjust before opening
the PRs.
