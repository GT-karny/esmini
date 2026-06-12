# scenario_authoring — Layer-1 Catalog for VirtualDriver Phase 3d/3e

This directory is the root of the parameterized scenario-authoring foundation described in
`GT_esmini/docs/virtualdriver/scenario_authoring_foundation.md`.

It targets VirtualDriver **Phase 3d** (oncoming-vehicle yield / right-turn gap judgement)
and **Phase 3e** (unsignalized junction priority) verification.

---

## Directory Layout

```
scenario_authoring/
  authoring_common.py          # shared helpers (repo_root, git_short_hash, vehicle/controller factories)
  requirements-authoring.txt  # pinned build-time tool deps (scenariogeneration, lxml, PyYAML)
  validate_catalog.py          # catalog validator — run after any generation step
  README.md                    # this file
  road_catalog/
    gen_t_junction.py          # G4: T-junction road generator (angle / lanes / length / signal / priority)
    gen_4way_priority.py       # G5+G13: 4-way priority junction generator (main pair / lanes / length / signage)
    priority_injector.py       # OpenDRIVE <priority> post-processor (3e; scenariogeneration cannot emit these)
    generated/                 # generated .xodr + .road.meta.yaml (COMMITTED)
  scenario_templates/
    gen_07_oncoming_yield.py   # Phase 3d: oncoming-yield / left-turn-across-traffic (24 variants)
    gen_08_unsignalized.py     # Phase 3e: unsignalized priority-junction judgement (12 variants)
    build_manifest.py          # generated/*.xosc -> catalog_batch.yaml (gt_sim_test batch manifest)
    generated/                 # generated .xosc + .meta.yaml + .annotation_required.yaml
                               #   + catalog_batch.yaml  (all COMMITTED)
```

---

## Generated Artifacts ARE Committed

Both `road_catalog/generated/` and `scenario_templates/generated/` are version-controlled.
This enables:
- diff-visible changes when generators evolve
- fresh-clone runnability without re-running generators
- catalog_id stability for annotation-result traceability

The `validate_report.md` produced by `validate_catalog.py` is NOT committed (see `.gitignore`).

---

## Regeneration Commands

All commands use the project venv — never system Python.

### T-junction road (G4) — default (90°, 1 lane, unsignalized)

```powershell
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/road_catalog/gen_t_junction.py
```

### T-junction with custom parameters

```powershell
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/road_catalog/gen_t_junction.py `
    --angle-deg 60 --leg-length 120 --lanes 2 --signal
```

### T-junction priority variant (G4 + G13) — through legs are the priority road

`--priority-main` injects OpenDRIVE `<priority>` records (through legs 0+1 = priority road)
and, with `--signage` (default ON), adds a YIELD sign on the minor leg. Output stem becomes
`t_junction_priority__a{angle}`.

```powershell
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/road_catalog/gen_t_junction.py --priority-main
```

### 4-way priority junction (G5 + G13)

Four legs at 0/90/180/270 deg. `--main {ns,ew}` selects which through-pair is the priority
road. `--signage`/`--no-signage` (default ON) toggles priority-road signs on the main approaches
and YIELD signs on the minor approaches. The pipeline runs `priority_injector` automatically.

```powershell
# Default: NS is the priority road, signage ON
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/road_catalog/gen_4way_priority.py

# EW priority, 2 lanes/direction, no signage
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/road_catalog/gen_4way_priority.py `
    --main ew --lanes 2 --no-signage
```

### Inject `<priority>` into an arbitrary generated xodr (standalone)

`scenariogeneration` cannot emit OpenDRIVE junction `<priority>` records, so this lean lxml
post-processor injects them. `--main-roads` lists the incoming road ids forming the priority road.
Idempotent (re-running replaces prior injected records). Upstream esmini ignores `<priority>`
(load-safe); Phase 3e adds GT-side extraction that reads these spec-conformant ground-truth records.

```powershell
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/road_catalog/priority_injector.py `
    resources/scenario_authoring/road_catalog/generated/4way_priority__main_ns.xodr --main-roads 1,3
```

### Scenario catalog — Phase 3d oncoming-yield (07, 24 variants)

Ego approaches the T-junction (`t_junction__a90`) on the east main leg and turns LEFT
across the oncoming through-stream into the south minor leg (RHT left-turn-across-traffic).
Param grid: `oncoming_speed ∈ {8,11,14}` × `first_gap_s ∈ {1.5,2.5,3.5,4.5}` ×
`oncoming_count ∈ {1,3}` = 24 variants `07_oncoming_yield__p001..p024`. Each variant emits
`<id>.xosc`, `<id>.meta.yaml`, `<id>.annotation_required.yaml`. Evaluation is annotation.

```powershell
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/scenario_templates/gen_07_oncoming_yield.py
```

### Scenario catalog — Phase 3e unsignalized junction (08, 12 variants)

Ego drives through an unsignalized priority junction with one cross vehicle timed to
the conflict point. Param grid: `junction ∈ {4way_priority__main_ns, t_junction_priority__a90}`
× `ego_on_priority ∈ {true,false}` × `cross_arrival_offset_s ∈ {-2,0,+2}` = 12 variants
`08_unsignalized_junction__p001..p012`. Same artifact trio per variant; evaluation is annotation.

```powershell
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/scenario_templates/gen_08_unsignalized.py
```

### Build the batch manifest (generated/*.xosc -> catalog_batch.yaml)

Globs the generated scenarios + meta and writes `scenario_templates/generated/catalog_batch.yaml`
in the `phase3_batch.yaml` format that `gt_sim_test batch` consumes. Deterministic ordering
(sorted by catalog_id). DO NOT hand-edit the manifest — regenerate it instead. It IS committed.

```powershell
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/scenario_templates/build_manifest.py
```

Run the whole catalog through the in-process VirtualDriver batch harness (telemetry +
snapshots; verdicts are null/absent because evaluation is annotation):

```powershell
DriverScript/.venv/Scripts/python.exe GT_esmini/scripts/verification/gt_sim_test.py batch `
    resources/scenario_authoring/scenario_templates/generated/catalog_batch.yaml `
    --out test_results/scenario_authoring_catalog
```

### Validate all generated artifacts

Static checks (XML well-formed, meta fields, catalog_id == stem, annotation_required present)
plus execution checks: roads run via `esmini --headless` (EXIT==0), scenarios run via
`gt_sim_test run` through the GT DLL (VirtualDriver telemetry frames > 0). Use `--skip-run`
for static-only. Writes `validate_report.md` (NOT committed).

```powershell
# Full validation (static + execution)
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/validate_catalog.py

# Static checks only (no esmini / DLL)
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/validate_catalog.py --skip-run

# Override the esmini / DLL paths (e.g. an isolated build copy)
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/validate_catalog.py `
    --dll build/GT_esmini/Release/GT_esminiLib.dll `
    --esmini build/EnvironmentSimulator/Applications/esmini/Release/esmini.exe
```

---

## Naming Conventions (catalog_id — §6.3)

| Type | Pattern | Example |
|---|---|---|
| Road | `<geometry>__<key params>` | `t_junction__a90`, `t_junction__a60_l2` |
| Scenario | `<NN_scene>__p<NNN>` | `07_oncoming_yield__p012`, `08_unsignalized_junction__p005` |

Each scenario variant carries three companion files sharing the `catalog_id` stem:
`<catalog_id>.xosc` (the scenario), `<catalog_id>.meta.yaml` (generation params + intent,
schema §6.2), and `<catalog_id>.annotation_required.yaml` (the labels a human must provide).

Rules:
- `catalog_id` == filename stem (enforced by `validate_catalog.py`)
- Lanes suffix `_l{N}` is omitted when `N == 1` (canonical default)
- Angle is rounded to nearest integer degree
- Scenario param indices are zero-padded to 3 digits

---

## Design Reference

Full rationale, taxonomy, and milestone roadmap:
`GT_esmini/docs/virtualdriver/scenario_authoring_foundation.md`

Tool dependencies and dev-freeze non-applicability: see §3.2 of that document.
