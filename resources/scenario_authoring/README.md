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
    gen_t_junction.py          # G4: T-junction road generator (angle / lanes / length / signal)
    generated/                 # generated .xodr + .road.meta.yaml (COMMITTED)
  scenario_templates/
    generated/                 # generated .xosc + .meta.yaml (COMMITTED)
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

### Validate all generated artifacts

```powershell
DriverScript/.venv/Scripts/python.exe resources/scenario_authoring/validate_catalog.py
```

---

## Naming Conventions (catalog_id — §6.3)

| Type | Pattern | Example |
|---|---|---|
| Road | `<geometry>__<key params>` | `t_junction__a90`, `t_junction__a60_l2` |
| Scenario | `<NN_scene>__p<NNN>` | `07_oncoming_yield__p012` |

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
