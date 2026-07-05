# P6 Stage-0 (5): CheckLink / CheckConnectedRoad one-way-link trace

Budget-confirmation material for design doc `odr_p6_virtual_junction_design.md` §4 Stage-3 **[vj-synth]** short-circuit.
All line numbers refer to **pristine** `EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp` at branch `feature/odr1619-p6-vj` (HEAD of dev_v0.12 merge base, ~15375 lines). The design doc cites fork-offset numbers (`:6900`, `:6932-6952`); the pristine equivalents are `:6815` and `:6847-6867` respectively (fork carries +75 GT lines above these sites).

## 1. Scenario definition

- Road **B** (virtual-junction branch road) carries:
  `<link><predecessor elementType="road" elementId="MAIN" elementS="100" elementDir="+"/></link>`
  i.e. a road-element link with a mid-road anchor, **no `contactPoint` attribute**.
- Road **MAIN** is unsplit and has **no link at all mentioning B** (in the verified P6 fixtures the main road is entirely link-less; the general case where MAIN has its own end links is covered in §3 note).
- Pristine esmini ignores `@elementS`/`@elementDir` completely (the attributes are never read anywhere in RoadManager.cpp; `RoadLink` has no `element_s_` member in pristine — see `RoadManager.hpp:1218-1223`). The field `GetElementS()` referenced by the design is a P6 fork addition.

## 2. Code-path walk

### 2.0 Parse time (before any link checking)

`ParseOpenDriveXML` (:3684) builds the link objects at :3886-3896:

```cpp
r->AddLink(new RoadLink(SUCCESSOR, successor));   // :3889
r->AddLink(new RoadLink(PREDECESSOR, predecessor)); // :3895
```

`RoadLink::RoadLink(LinkType, pugi::xml_node)` (:2545) for `elementType="road"` with **absent** contactPoint hits:

```cpp
else if (contact_point_type.empty())
{
    LOG_ERROR("Missing contact point type");   // :2572
}
```

- `contact_point_type_` stays `CONTACT_POINT_UNDEFINED` (default, :1222). The link object is **created and attached anyway**; nothing is rejected and parsing continues.
- This ERROR fires **once per VJ branch-road link, at parse time, independently of CheckLink**. It is NOT killed by the [vj-synth] short-circuit — the Stage-2 **[vj-parse]** hook must suppress it when `elementS` is present (see §7).

### 2.1 Direction (a): CheckConnections iterating road B

Call chain: `ParseOpenDriveXML` :5473 → `CheckConnections()` (:6847) → for road B, predecessor link non-null → `CheckLink(B, link, CONTACT_POINT_START)` (:6858).

`CheckLink` (:6797):

1. `link->GetElementType() == ELEMENT_TYPE_ROAD` → true (:6800).
2. `connecting_road = GetRoadById(MAIN)` (:6802) → resolves (MAIN exists).
3. `CheckConnectedRoad(B, CONTACT_POINT_START, MAIN->GetLink(PREDECESSOR))` (:6805) — MAIN link-less → `link == nullptr` → **returns -1** (:6569-6572).
4. Same for `MAIN->GetLink(SUCCESSOR)` (:6809) → **-1**.
5. Falls into the `else` (:6813):

```cpp
LOG_WARN("Warning: Reversed road link {}->{} not found. Might be a flaw in the OpenDRIVE description.",
         road->GetId(), connecting_road->GetId());   // :6815-6817
```

6. **Falls through to `return 0;` (:6844).** No state is touched, no failure is propagated.

Notes on the UNDEFINED contact point:
- `CheckLink` never reads **B's own** link contactPoint. `expected_contact_point_type` is derived purely from which end of B the link sits on (START for predecessor :6858, END for successor :6862). The contact points inspected in `CheckConnectedRoad` (:6578) are those on **MAIN's** links. Hence `CONTACT_POINT_UNDEFINED` on B's link is completely inert in the link checker.

### 2.2 Direction (b): CheckConnections iterating road MAIN

`CheckConnections` :6855/:6860: both `GetLink()` calls return `nullptr` → both `if` bodies are skipped. **Nothing runs, nothing is logged.** The checker is driven solely by the road that *declares* a link; a link-less road is never inspected.

### 2.3 Contrast: junction-element links (`CheckJunctionConnection`, :6590)

Only reached from the `ELEMENT_TYPE_JUNCTION` branch of CheckLink (:6821-6841) — **never** for the VJ road-to-road form. Note for contrast that this path **does mutate**: it auto-creates missing counter `Connection` objects via `junction->AddConnection(...)` (:6665, :6735, :6781). The road-element path performs no analogous auto-repair — the asymmetry is exactly why [vj-synth] must synthesize counter-connections itself.

### 2.4 CheckConnectedRoad leniency (affects when the WARN fires at all)

`CheckConnectedRoad` (:6567-6588) returns **-1 only** when (i) the inspected link is `nullptr`, or (ii) it is a road link pointing back at B's id with the wrong contact point (:6576-6583). **Every other case returns 0** — including a link that points at a *different* road or at a junction. Consequence: the "Reversed road link" WARN fires **only if BOTH of MAIN's end links fail**, i.e. in practice only when MAIN is fully link-less (the P6 fixture case). If MAIN has any predecessor or successor link to some third road/junction, the reverse scan silently "passes" (false negative) and no WARN appears for that branch road. The short-circuit is still correct to add (semantic honesty + fixture reality), but on richer maps the WARN count per branch road can already be 0 in pristine.

## 3. WARN/ERROR inventory (per branch road, VJ fixture: one elementS link, MAIN link-less)

| # | Level | Line | Text | Phase | Fires |
|---|-------|------|------|-------|-------|
| 1 | ERROR | :2572 | `Missing contact point type` | parse (`RoadLink` ctor) | 1× per elementS link (contactPoint absent) |
| 2 | WARN | :6815 | `Warning: Reversed road link {B}->{MAIN} not found. Might be a flaw in the OpenDRIVE description.` | `CheckConnections` → `CheckLink` | 1× per elementS link, **iff** both MAIN end-links fail the :6567 scan (always true for link-less MAIN) |

A branch road anchoring **both** ends to main roads (crossing branch) fires 2× of each. No other WARN/ERROR exists on the road-element path: :6828 (`Junction ... does not exist`) and the errors inside `CheckJunctionConnection` (:6601, :6621, :6642, :6679, :6752) are junction-branch only; :5444 (`Unsupported contact point`) is junction-connection parsing, unrelated.

## 4. Mutation / abort analysis — the critical question

**Verdict: WARN-only. No mutation, no load failure.**

- `CheckLink`'s road-element branch (:6800-6820) contains **no assignment, no link creation/removal, no repair** — it is read-only plus one `LOG_WARN`, then falls to `return 0;` (:6844). It cannot even return non-zero from that branch.
- `CheckConnectedRoad` (:6567) is pure read-only.
- `CheckConnections` (:6847-6867) declares `int counter = 0;` (:6849), never increments it, ignores CheckLink's return, and returns 0. Its own return value is **discarded** at the single call site:

```cpp
    CheckConnections();          // :5473, inside ParseOpenDriveXML

    if (!SetRoadOSI()) { ... }   // :5475
    return true;                 // :5480 — unconditional
```

- `ParseOpenDriveXML` therefore returns `true` regardless of any one-way link; `LoadOpenDriveFile`/`LoadOpenDriveFromXMLString` (:5504/:5483) succeed. The parse-time ERROR :2572 likewise does not abort (LOG only).
- Mutation *does* exist on the sibling junction path (`CheckJunctionConnection` auto-adds counter connections, :6665/:6735/:6781) but is unreachable for elementS road links.

Consequence for the budget: the [vj-synth] short-circuit stays a **log-hygiene item** ("virtual maps must load warning-clean"), not a load-success prerequisite. No escalation of the estimate.

## 5. Runtime reverse-link consumers (quick scan, non-exhaustive)

`grep Reversed` over EnvironmentSimulator hits only :6815 — the warning text exists nowhere else. Reverse-link *patterns* that a link-less MAIN will starve at runtime:

- **`RoadPath::CheckRoad` (:5964)** — route/distance search (`Position::Delta`, `Distance`). Two exposures: (i) :5979 reads the *source* link's contact point — `UNDEFINED != CONTACT_POINT_END` falls into the `else` (:5984-5988), i.e. UNDEFINED is silently treated as START, so entering MAIN from B follows MAIN's SUCCESSOR — direction is a coin toss for a mid-road anchor; (ii) :6012-6025 expands the graph by scanning `checkRoad`'s links for `fromRoad->GetId()` — MAIN never links to B, so path search cannot expand from MAIN back into B. Both are Stage-3 runtime-hook territory ([vj-route]/registry-backed traversal), silent (no WARN), out of scope for the CheckLink short-circuit.
- **`Road::IsDirectlyConnected` / `IsSuccessor` / `IsPredecessor` (:3180/:3240/:3245)** — asymmetric by construction: `B.IsPredecessor(MAIN)` returns true but hands out `contact_point = CONTACT_POINT_UNDEFINED` (:3199); `MAIN.Is*(B)` is false. Callers receiving UNDEFINED must be handled by the Stage-3 runtime hooks, not here.
- `OpenDrive::IsIndirectlyConnected` (:6420) walks junction connections only; unaffected at load time.

## 6. Short-circuit patch size and placement

Placement: **`OpenDrive::CheckLink`, first statement inside the `ELEMENT_TYPE_ROAD` branch** — pristine :6800-6804 (fork-offset ≈ :6885), i.e. right after (or instead of) the `GetRoadById` resolution:

```cpp
if (link->GetElementType() == RoadLink::ElementType::ELEMENT_TYPE_ROAD)
{
    // GT [ODR:vj-synth] elementS links are mid-road virtual-junction anchors:
    // no reciprocal link exists by design - validate against the VJ registry
    if (link->GetElementS() >= 0.0)
    {
        return <registry lookup / validation call> ? 0 : -1;
    }
    Road* connecting_road = GetRoadById(link->GetElementId());  // existing :6802
    ...
```

- **CheckConnectedRoad itself needs NO change** — it is reached only via CheckLink for road links, so guarding CheckLink alone fully suppresses the WARN. This narrows the design wording "short-circuit CheckLink/CheckConnectedRoad" to one hunk.
- **Estimate: 4-12 non-blank lines.** Min 4 (comment + guard + bare `return 0;` when validation lives entirely in the [vj-synth] registry pass, which has already vetted every elementS link before/after CheckConnections). Max 12 if the hunk itself does the registry lookup plus an in-situ WARN for an elementS link that resolves to no registered virtual junction (defensive path). Recommended: keep validation in the registry pass (PRISTINE-ONLY GT module where possible), hunk at ~5-7 lines against the FORK budget.
- Reminder: the return value is discarded by CheckConnections either way, so `-1` vs `0` on the failure path is diagnostic only.

## 7. Adjacent finding (flag, budgeted elsewhere)

"Warning-clean load" additionally requires killing the **parse-time ERROR :2572** (`Missing contact point type`), which fires before CheckConnections ever runs and is untouched by this short-circuit. The natural home is the Stage-2 **[vj-parse]** hook (when `elementS` attribute present → set `element_s_`, skip the empty-contactPoint error), ~3-5 extra lines inside `RoadLink::RoadLink` :2570-2573. This should be booked against the Stage-2 budget line, not [vj-synth].

## 8. Conclusion

The pristine one-way-link path is strictly WARN-only: `CheckLink` logs :6815 once per elementS link (when MAIN is link-less), mutates nothing, and `CheckConnections`' return value is discarded at :5473 — load success is never at stake, so the short-circuit remains cosmetic/log-hygiene as the design assumed. The short-circuit itself is a single 4-12 line hunk at the top of `CheckLink`'s road branch (:6800), with `CheckConnectedRoad` untouched; the remaining [vj-synth] scope (registry, elementS validation, counter-connection synthesis per the :6659/:6716 templates) comfortably fits the remaining ~40-65 lines. **The §4 Stage-3 [vj-synth] budget line of ~50-70 ln including the short-circuit holds**, with one carve-out: the parse-time `Missing contact point type` ERROR (:2572) must be handled by the Stage-2 [vj-parse] hook (~3-5 ln there) to reach a warning-clean load — it is not addressable from CheckLink at all.
