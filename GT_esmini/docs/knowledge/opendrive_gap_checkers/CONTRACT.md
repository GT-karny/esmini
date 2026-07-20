# OpenDRIVE Annex F gap-rule checker suite — shared contract

You are implementing a slice of a larger effort: turning ASAM OpenDRIVE 1.9.0 Annex F
"Checker rules (normative)" that are **not** covered by the off-the-shelf `qc-opendrive`
tool ("gap" rules, 259 of 275 total) into lightweight, dependency-free Python checkers.
These catch XSD-valid-but-structurally/referentially-broken `.xodr` files that no current
tool flags. You have been assigned one category-group (a JSON file of rule_name +
description). Follow this contract exactly so 19 parallel groups integrate cleanly.

## Repo root
`e:/Repository/GT_esmini/esmini` — all paths below are relative to this unless absolute.

## Python environment (hard rule)
Never invoke bare `python`/`pip`/`py` — a PreToolUse guard hook denies it. Always use:
`DriverScript/.venv/Scripts/python.exe`
e.g. `DriverScript/.venv/Scripts/python.exe your_script.py`

## Your inputs
- Your assigned rules: `scratchpad/checks_gap/rules_<GROUP>.json` — a JSON list of
  `{rule_name, category, version, description}`. `description` is the verbatim Annex F
  rule text (already extracted from the spec HTML) — normally sufficient on its own.
  If genuinely ambiguous, cross-check the raw spec HTML:
  `thirdparty/opendrive/1.9/spec_html/16_annexes/map_rules.html` (search for the UID,
  e.g. `asam.net:xodr:1.4.0:road.lane.lane_order`) — open with UTF-8, never `print()`
  raw non-ASCII spec text to console (Windows console is cp932 → `UnicodeEncodeError`;
  write findings to a file instead, or use `errors='replace'`).
- Reference/style precedent: `scratchpad/gap_rule_check.py` — the existing checker (8
  rules already implemented: `ids.only_ref_defined_ids`, `ids.id_unique_in_class`,
  `road.linkage.both_sides_consistency`, `road.lane.lanes_numbered_correctly`,
  `lane_order_no_gaps`, `center_lane_id`, `geometry.elem_asc_order`, `refline_no_gaps`,
  `length_sum_geometries`, `rule_hand_uniformity`). Do **not** re-implement these — they
  are not in your rule list. Read it to see the parsing idiom (roads dict, junction_ids,
  origin bucketing) you must match.
- HOW-style precedent (semantics only — do NOT copy code, it depends on `lxml` +
  `esminiRMLib` road-geometry evaluation which we do not use here):
  `scratchpad/qcvenv/Lib/site-packages/qc_opendrive/checks/**/*.py`
  (basic/, geometry/, performance/, semantic/, smoothness/ subfolders — grep by rule
  name fragment to find a relevant file if one exists; most of yours won't have a
  qc counterpart, that's the point).

## Module contract (mandatory signature)

Write your module to `scratchpad/checks_gap/check_<GROUP>.py` with:

```python
def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    """
    file_path: str, path to the .xodr file
    root: xml.etree.ElementTree Element — the <OpenDRIVE> root
    roads: dict[str, Element] — road @id -> <road> element
    road_ids: set[str] — all road @id values
    junctions: dict[str, Element] — junction @id -> <junction> element
    junction_ids: set[str] — all junction @id values

    Returns: list[tuple[str, str, str]] = (rule_name, detail, location)
      rule_name : EXACT bare rule_name from your rules_<GROUP>.json (e.g.
                  "road.lane.lane_order"), never the full uid.
      detail    : short human-readable explanation of the specific violation found
                  (Japanese is fine, matches existing style — see gap_rule_check.py).
      location  : e.g. "road 5 s=12.3", "junction 2", "road 7 object id=3" — used
                  for report grouping, keep it short.
    """
    flags = []
    ...
    return flags
```

Pure `xml.etree.ElementTree`, stdlib only, no new pip dependencies. Do not read any
file except the one passed to you (`file_path`/`root` are already parsed once per file
by the integration layer — do not re-`ET.parse()` inside `run_checks` except in your own
standalone self-test harness).

Implement as many of your assigned rules as are genuinely checkable via **XML
structure/attributes only** (presence/absence/count of elements, attribute value
domains, cross-attribute consistency, s/id ordering and uniqueness, parent-child
consistency, referential integrity against `road_ids`/`junction_ids`). This is most of
your list — that's exactly why these are "gap" rules: XSD can't express them, but plain
structural inspection can.

## Non-implementable rules — classify, don't skip silently

For any assigned rule that is genuinely not implementable this way, classify it (do NOT
write a stub/half-attempt). Every rule in your JSON must end up in your structured
output with one of these statuses:

- `implemented_gt` — checker written, self-tested, no crashes, no obvious false
  positives on the official ASAM calibration set.
- `gap_geometry_math` — requires actual road-geometry computation beyond attribute
  reading: evaluating arc/spiral/paramPoly3 curvature/position along s, combining
  elevation+superelevation+shape into 3D inertial coordinates, polygon/bounding-box
  overlap tests, junction-boundary closed-polygon ordering, CRG grid math, etc. This is
  a legitimate, expected outcome for many geometry/junction.geometry/junction.elevation_grid/
  road.crg/corner_local/corner_road/curve_local/skeleton rules — do not force it.
- `gap_niche` — technically checkable, but concerns cosmetic/decorative detail with no
  bearing on GT's driving-behavior simulation (e.g. marking colour, board sub-signal
  decoration). Use sparingly — most rules are NOT niche just because they're tedious.
- `gap_ambiguous` — the rule's prose is genuinely too vague to encode a deterministic
  pass/fail without an interpretive judgment call the spec text doesn't resolve.
- `gap_deferred` — checkable and relevant, but out of scope for this pass for a
  concrete infrastructural reason (say what, precisely, in `reason`). Use rarely — this
  is not a generic "ran out of time" bucket; prefer actually implementing it.

**Bar for classifying away from `implemented_gt`**: would a competent engineer looking
at just the XML agree this genuinely needs geometry/is cosmetic/is ambiguous? If you can
check it with element/attribute inspection, implement it — don't take the easy way out.

## Corpus & validation

Corpus definition (reuse exactly, for consistent counts — see `gap_rule_check.py`):
```python
import glob
from pathlib import Path
ROOT = Path(r"e:/Repository/GT_esmini/esmini")
files = []
for f in glob.glob(str(ROOT / "**/*.xodr"), recursive=True):
    rp = str(Path(f).relative_to(ROOT)).replace("\\", "/")
    if rp.startswith(("thirdparty/", "dist/", "build/")) or "/build/" in rp:
        continue
    files.append(f)
files = sorted(set(files))
```
208 files, 3 fail to parse (intentionally-broken handauthored fixtures — skip those,
don't crash). Element prevalence in this corpus (so a rule finding 0 applicable elements
across all 208 files is plausible, not necessarily a bug): `object` 45 files, `signal` 48,
`railroad` 2, `CRG` 5, `cornerRoad` 15, `cornerLocal` 9, `elevationProfile` 121,
`laneOffset` 54, `shape` 2, `type` 145, `junctionGroup` 2, `elevationGrid` 2, `boundary` 3,
`controller` 11.

Origin buckets (methodology — critical for avoiding false-positive storms):
- `test/odr_fixtures/official/` = **official ASAM calibration set**. A correct checker
  should produce **zero or near-zero** flags here. If your checker fires a lot on
  official fixtures, your logic is probably wrong (over-broad), not the fixture —
  investigate before shipping.
- `resources/xodr/`, `GT_esmini/test/` (incl. `handauthored/`), `**/generated/**`,
  `scenario_authoring` = **GT-authored, the actual audit target**.
- `EnvironmentSimulator/`, `OSMP_FMU/` = upstream, out of scope (already excluded from
  corpus glob above via nothing special — just don't specifically target them).
- **Junction context is the single biggest false-positive source**: a connecting road
  (`@junction != "-1"`) has different link semantics than a normal road — it links
  through the junction's `<connection>` elements, not via direct road↔road
  predecessor/successor reciprocity. Any rule about road linkage, lane linkage, or
  geometry continuity must special-case connecting roads, or it will over-fire. Likewise
  consider lane `type` (e.g. `border`/`shoulder` vs `driving`), and width=0 as a
  legitimate lane-taper (not automatically a violation) where relevant to your rules.

`test/odr_fixtures/handauthored/` (`GT_esmini/test/odr_fixtures/handauthored/`) contains
~29 GT-authored fixtures with descriptive filenames, several intentionally violating
specific rule families (e.g. `02_invalid_junction_connection_14.xodr`,
`07_license_default_regulations_18.xodr`, `08_lane_rule_speed_access_15.xodr`,
`14_crg_offsets_19.xodr`, `19_signal_reference_18.xodr`, `21_boards_vmsgroup_19.xodr`,
`23_virtual_junction_17.xodr`, `24_cross_section_surface_18.xodr`,
`26_object_details_19.xodr`, etc.). If any filename/content is plausibly relevant to one
of your assigned rules, use it as a positive-detection sanity check (your checker SHOULD
flag something there). Not finding a relevant one for a given rule is fine — the
official-set-stays-clean + no-crash bar is the main gate.

Flags are advisory review items, not hard errors (severity = "review" throughout this
alpha) — don't editorialize that in code, just keep detail strings factual.

## Self-test (mandatory before you report `implemented_gt` for any rule)

Write a small throwaway driver (inline `python -c` via Bash, or a temp script under
`scratchpad/checks_gap/`) that: globs the corpus per the definition above, parses each
file, builds `roads`/`road_ids`/`junctions`/`junction_ids` the same way
`gap_rule_check.py` does, calls your `run_checks`, and confirms:
1. No exceptions across all 208 files (catch-and-report per file is fine, but your
   `run_checks` itself must not throw on well-formed XML it wasn't specifically written
   for — guard with `.get()`/`.find()` None-checks, not bare indexing).
2. Flags-per-rule counts, and specifically the count within `test/odr_fixtures/official/`
   — should be 0 or you've investigated and are confident it's a genuine finding.
3. For rules where you identified a relevant handauthored negative fixture, confirm the
   expected flag actually fires.

## Output

Report back via the required structured schema: one entry per rule in your assigned
JSON (all of them — none may be silently dropped), each with `status`, a one-sentence
`reason` (why this status — for `implemented_gt` this can be short, e.g. "structural
attribute check, validated clean on official set"), and `function` (your module's
function name, always `run_checks`, or null if not implemented). Also report your
module's file path and a short self-test summary (files scanned, total flags, any
exceptions hit and how you fixed them).
