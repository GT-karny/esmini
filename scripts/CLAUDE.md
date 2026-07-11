# Component Knowledge Graph: scripts/

Python utility scripts. Always execute via `DriverScript/.venv/Scripts/python.exe`.

## Script Categories

### Data Conversion
| Script | Role |
| :--- | :--- |
| `osi2csv.py` | OSI binary trace → CSV conversion |

> `dat.py` / `dat2csv.py` were removed upstream (commit 9ea6992e; `.dat`→CSV is now the C++
> `dat2csv` tool in `EnvironmentSimulator/Applications/replayer/`). The GT-vendored `DATFile`
> reader lives at `GT_esmini/scripts/dat.py`.

### Visualization
| Script | Role |
| :--- | :--- |
| `plot.py` | General plotting utilities |
| `plot_csv.py` | Plot from CSV data |
| `plot_dat.py` | Plot directly from `.dat` files |
| `osiviewer.py` | OSI trace viewer |

### Testing & Comparison

> Frozen toolchain moved to `archive/frozen_python_verification/scripts/`.
> See `archive/frozen_python_verification/README.md` for context (audit SCR-2).

### Test Gates (PowerShell)
| Script | Role |
| :--- | :--- |
| `run_gt_tests.ps1` | GT unit gate (ctest `test_ScenarioReaderParsing`; `-IncludeIntegration` for known-broken sets) |
| `run_regression_gate.ps1` | Pre-merge ladder: unit gate → ODR quick → VirtualDriver behavioral batch (see root CLAUDE.md §5) |
| `build_package.ps1` | Distribution package pipeline (single source of truth for `/package`) |

### Fork Governance (Clean Core / upstream sync)
| Script | Role |
| :--- | :--- |
| `check_core_census.py` | AUTHORITATIVE outbound check: every fork/2nd-class deviation attributed per marker id against the census manifest (`gt_roadmanager_patches.md`) |
| `check_fork_drift.py` | Legacy coarse outbound drift check (GT_RoadManager.cpp vs pristine, proximity attribution) |
| `check_fork_sync.py` | INBOUND sync gate (audit R4): upstream commits on the forked lineages (RoadManager/OSIReporter/roadgen) not yet ported — manifest `GT_esmini/docs/fork_sync_manifest.yaml`; WARN by default, `--strict` gates; runs in CI as a warning step |
| `gt_patch_manifest.py` | Loader/validator for the census manifest YAML block (shared by the checkers and ctest) |
| `check_resync_guards.py` | Resync guard checks (double-processing guards) |
| `odr_resync_rehearsal.py` | Upstream resync rehearsal automation |
| `strip_gt_markers.py` | Strip `[GT_ODR:]`/`[GT_LHT]` markers (upstream PR preparation) |

### OpenDRIVE Conformance (plan P0)

Baseline harness for the OpenDRIVE 1.6-1.9 support plan. See `GT_esmini/test/odr_fixtures/README.md`.

| Script | Role |
| :--- | :--- |
| `odr_fixture_setup.py` | Bootstrap ASAM 1.9 assets: verify the source zips against `asam_pins.json`, extract `.xodr`/`.xsd` into gitignored `official/`/`schema19/` (XSD decl bumped to 1.1). Idempotent; no-op SKIP when zips absent. Importable (`ensure_assets`). |
| `validate_xodr_schema.py` | Per-file OpenDRIVE schema validation. Wraps `run_schema_comply.py` unmodified, extends `SCHEMA_MAPPINGS["xodr"]` with the 1.9 XSDs at runtime. `PASS`/`FAIL`/`SKIP_NO_MAPPING`/`SKIP_NO_SCHEMA19`; exit 0 iff no FAIL (no SKIP under `--strict`). |
| `odr_feature_injector.py` | Recipe-driven generator for the `generated/` fixtures (g1-g6): injects targeted 1.6-1.9 DOM features into base xodr files. |
| `run_odr_conformance.py` | 3-layer conformance harness (schema / RM / OSI). `--profile quick\|full`, `--layers schema[,rm,osi]` (CI schema-only path), `--check-matrix`, `--update-golden`, `--smoke`. Exit 0 iff no FAIL/XPASS. Regression-gate Step 1.5 runs `--profile quick`. |

### Scenario Generation
| Script | Role |
| :--- | :--- |
| `scenario_generator.py` | Generate XOSC variants (default/python controller) |

### Analysis
| Script | Role |
| :--- | :--- |
| `collect_osi_light_metrics.py` | Collect OSI traffic light metrics |
| `count_osi_messages.py` | Count OSI messages in trace |
| `render_realdriver_report.py` | Generate RealDriver HTML report |

### Tools
| Script | Role |
| :--- | :--- |
| `esmini-launcher.py` | esmini scenario launcher |
| `fix_dae_materials.py` | Fix Collada (.dae) material issues |
| `move_to_origin.py` | Move 3D model to origin |
| `xodr_lines2curves.py` | Convert OpenDRIVE lines to curves |

### Migration
| Script | Role |
| :--- | :--- |
| `migrate_realdriver_to_pythondriver.py` | Migrate RealDriver → PythonDriver |
| `run_distribution.py` | Distribution packaging runner |
| `run_schema_comply.py` | OpenSCENARIO schema compliance check |

## Origin

The Data Conversion, Visualization, and Tools categories originate from upstream esmini. The remaining scripts (Test Gates, Fork Governance, Analysis, Migration, and OpenDRIVE Conformance categories) are GT_esmini-specific additions.
