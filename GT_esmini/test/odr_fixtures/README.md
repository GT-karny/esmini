# OpenDRIVE 1.6-1.9 conformance fixtures

Machine-verifiable conformance baseline for the OpenDRIVE 1.6-1.9 support plan
(`GT_esmini/docs/opendrive_16_19_support_plan.md`, phase **P0**, section 3.3).

## Directory layout

| Dir | Committed? | Contents |
| :-- | :-- | :-- |
| `official/` | **NO** (gitignored) | ASAM 1.9.0 sample `.xodr` files (36), extracted from the ASAM zip preserving its `examples/` + `use_cases/` tree. |
| `schema19/` | **NO** (gitignored) | The 7 OpenDRIVE 1.9 `.xsd` files, extracted with their XML declaration bumped to `version="1.1"` (see below). |
| `reports/` | **NO** (gitignored) | Generated validation / probe reports (JSON). |
| `work/` | **NO** (gitignored) | Scratch space for harness runs. |
| `handauthored/` | **yes** | GT-authored fixtures 01-20 (crossing junctions, objectReference, bridge, lane rule/speed, repeat polynomials, removed-in-1.6 neighbor, etc.). |
| `generated/` | **yes** | Injector-produced fixtures g1-g6 (`scripts/odr_feature_injector.py`) + `recipes/`. |
| `golden/` | **yes** | RM (`golden/rm/`) + OSI (`golden/osi/`) golden extracts (tolerance-based; regen with `--update-golden`). |
| `asam_pins.json` | **yes** | Integrity pins (sha256/size/counts) for the source zips. |
| `manifest.yaml` | **yes** | Consolidated conformance manifest (`control_set` + `fixtures`) -- single source of truth for the harness. Merged from the four `manifest.frag.*.yaml` coordination fragments, which are now DELETED. |
| `matrix_requirements.yaml` | **yes** | Element-cluster -> required feature-pattern traceability matrix (`--check-matrix`). |
| `parser_coverage.yaml` | **yes** | What GT_RoadManager.cpp reads from the xodr DOM (P1 OdrCoverageAudit whitelist source). |
| `README.md` | **yes** | This file. |

## Conformance harness: `scripts/run_odr_conformance.py`

Manifest-driven, CWD-independent, deterministic 3-layer harness (plan P0 sec 3.3). Run under the
venv that has xmlschema/lxml/pyyaml (`DriverScript/.venv`); the OSI layer additionally needs an
interpreter with `osi3`+`protobuf` (auto-detected: running interpreter, then `GT_esmini/web/.venv`).

```
DriverScript/.venv/Scripts/python.exe scripts/run_odr_conformance.py \
    [--profile quick|full] [--update-golden] [--check-matrix] [--only <substr>] \
    [--dll <GT_esminiLib.dll>] [--rmdll <esminiRMLib.dll>] [--report-dir <dir>] [--smoke]
```

- **Layer 1 (schema)**: reuses `validate_xodr_schema.py` over control_set + fixtures; status vs the
  manifest `expected.schema` -> PASS / FAIL / XFAIL / XPASS / SKIP. **XPASS** (expected fail but passed)
  fails the run, forcing a manifest update.
- **Layer 2 (rm)**: isolated `esminiRMLib` RM_Init probes (each in its own 60s subprocess writing JSON
  to a file -- the DLL floods stdout and some files crash) -> deterministic extract (roads / lane
  samples / position probe / signs, floats rounded to 1e-6) compared with `golden/rm/<slug>.json`
  (abs tol 1e-6 on floats, exact otherwise).
- **Layer 3 (osi, profile `full`)**: control_set + fixtures opted in via manifest `osi: true`
  (P2: `13_lane_types_16_18`; P3: `03_dynamic_signal_demote_18`, `05_vms_boards`,
  `19_signal_reference_18`; a fixture may additionally set `osi_expect_no_unknown: true`
  to turn any `TYPE_UNKNOWN` lane classification into a FAIL -- plan P2 acceptance (i)).
  A minimal probe `.xosc` (Ego teleported to
  road[0] midpoint) drives `GT_esminiLib` `SE_Init`/`SE_StepDT(0.05)`/`SE_GetOSIGroundTruth`; the
  serialized `osi3::GroundTruth` is decoded with esmini's own `scripts/osi3` bindings and reduced to a
  deterministic lane/boundary/object/sign/light summary compared with `golden/osi/<slug>.json`.
  (`SE_GetOSIGroundTruth`'s `restype` MUST be `c_void_p` + `ctypes.string_at(ptr, size)`; `c_char_p`
  truncates the buffer at the first NUL and yields garbage.)
- **`--check-matrix`**: verifies every non-deferred cluster pattern in `matrix_requirements.yaml`
  matches >=1 fixture feature; prints the cluster x fixture coverage table ("no empty rows").
- **`--smoke`**: 3 end-to-end app smokes (esmini `--record` -> replayer `--file` -> odrviewer `--odr`).
- **Audit plumbing (P1-ready, inert in P0)**: workers capture the DLL log and count `[ODR-UNSUPPORTED]`
  / `[ODR-REMOVED-1.6]`; a fixture may later carry `expected_unsupported{}` / `expected_unsupported_entries`
  and the harness will PASS/FAIL against it (unit-smoked with a fake log each run).

Exit 0 iff zero FAIL and zero XPASS (SKIP / XFAIL are fine). Goldens are byte-stable across runs
(verified by double-generation); `golden/` is committed, `reports/` and `work/` are gitignored.

### `--update-golden` review convention

`--update-golden` rewrites `golden/{rm,osi}/*.json` (sorted keys, indent 1, trailing newline). Review
every changed golden in a **single** commit -- the goldens deliberately freeze the CURRENT (pre-P2)
degraded parse behavior; that "red baseline" is the point. A golden diff means real behavior changed.

## ASAM local-only policy + CI skip semantics

The ASAM OpenDRIVE 1.9.0 package is redistribution-restricted, so **no extracted
ASAM byte is ever committed**. The `official/` and `schema19/` trees are gitignored
and are (re)materialised on demand from the source zips in
`thirdparty/opendrive/1.9/` (itself gitignored).

If the zips are absent (fresh clone / CI without the ASAM package), setup is a clean
no-op: it prints `ODR-SETUP: SKIP (...)` and exits 0, and the schema validator
reports revMinor-9 files as `SKIP_NO_SCHEMA19` (a failure only under `--strict`).
This lets CI run the harness unconditionally and simply skip the ASAM-derived parts
when the assets are unavailable.

## Gates & CI

Where each layer runs (conservative split: DLL golden layers local, deterministic checks in CI):

| Layer / check | Regression gate (local) | CI (`ci.yml`) |
| :-- | :-- | :-- |
| schema | Step 1.5 (`--profile quick`) | `test-no-external-modules`, ubuntu/Release: `--layers schema --check-matrix` |
| RM probe + `golden/rm/` | Step 1.5 (`--profile quick`) | -- (cross-OS float risk; local only) |
| OSI probe + `golden/osi/` | manual `--profile full` | -- (local only) |
| matrix coverage | (via `--check-matrix`, optional) | yes (in the schema step above) |
| app smoke | manual `--smoke` | -- |

- **Regression gate** (`scripts/run_regression_gate.ps1`): **Step 1.5** runs `run_odr_conformance.py --profile quick` (schema + RM) as a HARD gate, using the `DriverScript/.venv` python. Skip with `-SkipOdr`. RM needs the built `esminiRMLib.dll`; official ASAM fixtures auto-SKIP if the thirdparty zips are absent.
- **CI**: the ubuntu/Release `test-no-external-modules` job installs `xmlschema lxml pyyaml` and runs `--layers schema --check-matrix` only. No DLL is loaded (build-tree independent) and the ASAM layer SKIPs (zips never present in CI). The RM/OSI golden layers are deliberately kept out of the 3-OS matrix to avoid cross-OS float divergence in the goldens.
- **After a parser change**: run `--profile full` (adds OSI) with `--smoke` locally, and regenerate/review goldens in a single commit (see `--update-golden` above).

## Setup (extraction bootstrap)

```
# From any CWD; run under the venv that has xmlschema/lxml/pyyaml:
DriverScript/.venv/Scripts/python.exe scripts/odr_fixture_setup.py
```

- Verifies the two zips against `asam_pins.json` (sha256 + size + `.xodr`/`.xsd` counts) before extracting; on mismatch it exits non-zero (override with `--allow-pin-mismatch`).
- Extracts only `.xodr` entries into `official/` (preserving zip-internal paths) and the 7 `.xsd` files into `schema19/`.
- **XSD transform**: each 1.9 XSD declares `<?xml version="1.0"?>` but uses XSD-1.1 constructs (`vc:minVersion="1.1"`). `scripts/run_schema_comply.py` picks the validation processor from the *schema file's* XML declaration (`lxml.etree.parse(schema).docinfo.xml_version`): `"1.0"` -> `xmlschema.XMLSchema` (XSD 1.0, misreads the schema -> every file fails), `"1.1"` -> `xmlschema.XMLSchema11` (correct). Setup rewrites each declaration to `version="1.1"` and asserts `docinfo.xml_version == "1.1"` post-transform. This mirrors upstream esmini's own 1.8 precedent (`resources/schema/OpenDRIVE_1.8/local_schema/*.xsd`).
- Idempotent: writes an `.extracted.json` stamp (pin sha + file list) into each target dir and skips on match. Use `--force` to re-extract.
- `--status-json <path>` writes `{"official": "ok"|"skipped", "schema19": ...}` for callers.
- Importable: `from odr_fixture_setup import ensure_assets; ensure_assets(repo_root)`.

## Schema validation

```
DriverScript/.venv/Scripts/python.exe scripts/validate_xodr_schema.py <paths...> [--report out.json] [--strict]
```

- Wraps `scripts/run_schema_comply.py` **without modifying it**: imports its `XmlValidation` class and extends the module-level `SCHEMA_MAPPINGS["xodr"]` at runtime with `"9" -> <abs path to schema19/OpenDRIVE_Core.xsd>` (an absolute mapping value passes through `os.path.join` unchanged).
- Auto-bootstraps the 1.9 XSDs (calls `ensure_assets`) when a revMinor-9 file is present and `schema19/` is missing.
- Per-file result: `PASS` / `FAIL` (first 3 error lines) / `SKIP_NO_MAPPING` (revMinor not mapped) / `SKIP_NO_SCHEMA19` (1.9 XSDs unavailable). Exit 0 iff no `FAIL` (and no `SKIP` under `--strict`).
- Paths may be files or directories (dirs recurse for `*.xodr`).

## Pin update procedure

When the ASAM source zips are (legitimately) updated:

1. Place the new zips under `thirdparty/opendrive/1.9/`.
2. Recompute pins: `DriverScript/.venv/Scripts/python.exe scripts/odr_fixture_setup.py --print-pins`.
3. Copy the printed `sha256` / `size` / `expected_xodr` / `expected_xsd` values into `asam_pins.json` (keep the `_comment` / `source_dir` / `filename` / `target` fields).
4. Re-extract: `... scripts/odr_fixture_setup.py --force`.
5. Re-capture the baseline: re-run `scripts/run_odr_conformance.py --profile full --update-golden`, reviewing every changed golden / `expected` / `observed` / `expected_notes` in a single commit.

## P0 baseline (frozen in `manifest.yaml` + `golden/`)

`manifest.yaml` = **31 control_set** (26 `resources/xodr/*.xodr` + 5 `resources/scenario_authoring/road_catalog/generated/*.xodr`) + **62 fixtures** (36 official + 20 handauthored + 6 generated; 19/20 added in P3). `run_odr_conformance.py --profile full` is green: schema 82 PASS / 11 XFAIL, rm 90 PASS / 3 XFAIL, osi 35 PASS (31 control + fixtures 13/03/05/19 opt-in; 0 FAIL / 0 XPASS). Goldens: 90 RM + 35 OSI.

P2 (2026-07-03) intentionally regenerated 15 RM goldens (lane-type NONE->SIDEWALK/CURB/BIDIRECTIONAL/CONNECTING_RAMP flips in fixtures using walking/curb/shared/slipLane + the Ex_Lane-Border border->width normalization) and added the fixture-13 OSI golden; every control_set golden stayed byte-identical (legacy invariance proof).

P3 (2026-07-03) refroze `golden/trafficlight_classification.json` atomically with the `[GT_ODR:tl-gate]`
relaxation (reviewed diff: `GT_esmini/docs/odr_p3_tl_gate_audit.md`), added fixtures 19/20 (+ RM/OSI
goldens) and regenerated 3 official RM goldens whose `<signalReference>` elements now materialize as
clone signs (UC_Motorway-Exit-Entry x2, UC_5Road_Junction); control_set goldens stayed byte-identical.

The frozen known-broken baselines (XFAIL) are:

- **Schema XFAIL (11)** = 5 fixtures + 6 control_set. Fixtures: `07_license_default_regulations` (ASAM `_OpenDriveElement`-abstract XSD defect, both 1.8/1.9), `18_removed16_neighbor` (element removed in 1.6 -- P1 `[ODR-REMOVED-1.6]` target), and 3 official mislabels (`Ex_Objects`, `Ex_Parkingspace_rhomboid`, `Ex_SmoothObjectOutline_traffic_island`). Control_set: `fabriksgatan_traffic_lights_ctrl.xodr` (revMinor=4 + top-level `<controller>`) and the 5 `road_catalog/generated/*.xodr` (empty `<elevationProfile>` / crosswalk object missing `@validLength`/`@height` -- scenariogeneration artifacts; `resources/` is off-limits so these are frozen, not fixed).
- **RM_Init XFAIL (3)**: `02_invalid_junction_connection_14` (dangling `connectingRoad="99"` aborts the parse -- relabeled to 1.4H so schema PASSes while the RM abort survives) + 2 official junction crashes (`Ex_Slip_Lane`, `UC_T_Junction`; unhandled C++ exception across the FFI, WinError `0xe06d7363`). All frozen as expected `rm_init: fail` until plan phase **P1** (abort hardening).

Fixture `16_include_error_15` currently PASSes rm_init (`<include>` is silently ignored today); P1 turns it into a hard error and its expectation flips then.
