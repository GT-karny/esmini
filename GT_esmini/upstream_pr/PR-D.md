# Pair virtual junction lanes in OSI output

Stacked on top of PR-A/B/C. This final PR completes the OSI ground-truth output
for virtual junctions.

## Motivation

A virtual junction has no junction area of its own: the unsplit main road keeps
its regular driving lanes across the span, and the branch roads are ordinary roads
already emitted as `TYPE_DRIVING`. So — exactly like a **direct** junction — there
must be **no** `TYPE_INTERSECTION` lane for a virtual junction. What is still owed
to OSI is the per-`<laneLink>` **lane pairing** that connects the branch's entry
lane to the main road's lane *at the anchor section*.

Refs ASAM OpenDRIVE 10.4; issue #592.

## What this PR does

`OSIReporter::UpdateOSIIntersection` gains a `JunctionType::VIRTUAL` branch,
placed beside the existing `DIRECT` branch and mirroring its structure. For each
kind-1 connection (main→branch) it:

- resolves the main-road lane at the connection's incoming contact s using
  `GetLaneSectionByS(anchor_s)` — the anchor is mid-road, so an *end* section
  would be wrong;
- resolves the branch entry lane at its first section;
- for each `<laneLink>`, registers the main-road lane as the branch lane's
  `antecessor` (contact START) or `successor` (contact END) lane pairing, using
  the same mutual-pairing convention as the direct branch;
- skips kind-2 connections (null connecting road) and the synthesized
  branch→main counter-connections (connecting road == main road) so each pairing
  is registered exactly once.

## Test coverage

OSI reporting has no dedicated gtest harness upstream, so this PR adds no new unit
test. The branch is exercised by the fixture from PR-A
(`virtual_junction_simple.xodr`, which carries a `<laneLink from="-1" to="-1">` on
its virtual connection) and was verified manually: the branch entry lane receives
a single `lane_pairing` referencing the main-road lane global id at s=100, and no
`TYPE_INTERSECTION` lane is emitted for junction 888. If the maintainers prefer a
harnessed check, we are happy to add one under `OSIReporter` following whatever
pattern they favour.

## Behavioural invariance

The new branch only runs for `JunctionType::VIRTUAL` junctions (none in existing
assets); all other junction OSI output is unchanged. The upstream unit-test suites
stay green.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
