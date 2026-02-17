# RealDriver Validation Suite

This document describes the RealDriver validation framework introduced for GT_esmini.

## Layers

- Gate A: module tests (`scripts/run_realdriver_module_tests.ps1`)
- Gate B: feature E2E tests (`scripts/run_realdriver_feature_tests.ps1`)
- Gate C: report generation (`report.html` under artifacts)

## Runtime baseline

- Feature tests run on `GT_Sim` (not `GT_Loader`)
- Default execution frequency is `--hz 100`
- Python execution is expected to use validation `venv` (`venv/Scripts/python.exe`)
- Feature tests launch DriverScript controller in parallel (UDP input to RealDriverController)
  - `scenario_drive_example.py --mode udp` is launched per feature
  - `GT_Sim` is started with `--osi 127.0.0.1` so DriverScript receives OSI updates
- Each feature run stores:
  - `sim.dat` / `sim.csv` (extended road coordinates)
  - `result.mp4` (when ffmpeg is available)
  - `stdout.txt` and optional `video_error.txt`

## Report readability enhancements

- `report.html` now shows these columns per scenario:
  - `Video`: link to `result.mp4`
  - `期待挙動（自然言語）`: timeline-based behavior description per scenario
  - `判定基準（自然言語+数値）`: pass/fail criteria in plain language with KPI thresholds
  - `検証観点`: human-readable validation points from feature matrix
  - `Road KPI要約`: lane/s/t based KPI summary
- Scenario narratives and criteria are defined in:
  - `GT_esmini/test/validation/realdriver_feature_matrix.yaml`
    - `expected_behavior_nl` (timeline natural language)
    - `judgement_criteria_nl` (natural language + KPI thresholds)
    - `validation_points` (legacy compatibility field)

## Configuration

- Feature matrix: `GT_esmini/test/validation/realdriver_feature_matrix.yaml`
- KPI thresholds: `GT_esmini/test/validation/kpi_thresholds.yaml`
- Native reference policy: `GT_esmini/test/validation/native_reference_policy.yaml`
- KPI-first validation:
  - `kpi_thresholds.yaml`:
    - `baseline_kpi_checks`: common checks applied to all features
  - `realdriver_feature_matrix.yaml`:
    - `kpi_checks`: scenario-specific checks (distance, speed profile, arrival conditions)
    - `kpi_checks_any`: OR-based checks where any one condition is sufficient
    - `expected_behavior_nl`: timeline-based narrative (e.g. "開始5秒で停止。3秒後、4秒かけて40km/hへ加速")
    - `judgement_criteria_nl`: natural-language pass criteria with numeric thresholds
  - `required_patterns` are treated as advisory when KPI checks are defined for a feature.

## Golden policy

- Golden data is updated only with explicit `-UpdateGolden`.
- Native controller output is only a reference during golden creation.
- Daily pass/fail is determined by feature KPI and approved golden values.
- Road-coordinate KPI (`roadId/laneId/s/t`) is treated as the primary KPI axis.
- Additional KPI checks from `sim.csv` (e.g. `s_stall_ratio`, `lead_gap_end_m`) are primary pass/fail criteria.
- Additional derived KPI examples:
  - `t_span_m` (`t_max_m - t_min_m`)
  - `speed_end_mps`
  - `signed_speed_min_mps` (signed speed from `ds/dt`, used for reverse detection)
  - `lead_gap_start_m`

## F03 policy (brake/reverse lights)

- `F03` validates both behavior and reflection:
  - behavior: vehicle transitions to reverse (`signed_speed_min_mps <= -0.5`)
  - reflection: OSI `light_state` reports brake/reversing lights as expected
- F03 run enables DriverScript reverse profile handling only for this scenario:
  - `driverscript_extra_args: --allow_reverse_from_profile`
- F03 also stores OSI light metrics under feature output:
  - `osi_light_metrics.json` (required KPI source)
  - `osi_lights.csv` (sample-level debug log)
- Ego identification rule for light metrics:
  - Must use `GroundTruth.host_vehicle_id` to select ego `moving_object`
  - Do not assume `moving_object` index `0` or `id=0`
  - `moving_object.id` can change per scenario/run

## Typical usage

```powershell
./scripts/setup_realdriver_validation_venv.ps1 -VenvDir venv
./scripts/run_realdriver_module_tests.ps1 -BuildDir build
./scripts/run_realdriver_feature_tests.ps1 -BuildDir build
./scripts/run_realdriver_feature_tests.ps1 -BuildDir build -UpdateGolden
./scripts/run_realdriver_feature_tests.ps1 -BuildDir build -Hz 100 -EnableVideo $true
```

## Test procedure (recommended)

1. Build `GT_Sim` (Release):

```powershell
cmake --build build --config Release --target GT_Sim
```

2. Run RealDriver feature tests with 100Hz baseline and video enabled:

```powershell
./scripts/setup_realdriver_validation_venv.ps1 -VenvDir venv

pwsh -NoProfile -File ./scripts/run_realdriver_feature_tests.ps1 `
  -BuildDir build `
  -SimPath ./build/GT_esmini/Release/GT_Sim.exe `
  -VenvDir venv `
  -Hz 100 `
  -EnableVideo:$true
```

3. Open the generated report:

- `artifacts/realdriver_features/<run_id>/report.html`
- `artifacts/realdriver_features/<run_id>/summary.json`

4. Check each scenario row in `report.html`:

- `PASS/FAIL`
- `期待挙動（自然言語）` (timeline narrative of intended behavior)
- `判定基準（自然言語+数値）` (human-readable criteria with thresholds)
- `検証観点` (expected behavior being validated)
- `Road KPI要約` (`roadId/laneId/s/t` based summary)
- `Video` (`result.mp4` link, or first-line error reason)

## Prerequisites and fallback behavior

- PowerShell 7 is recommended.
- Validation venv should exist (`venv/Scripts/python.exe`).
- If missing, run `./scripts/setup_realdriver_validation_venv.ps1 -VenvDir venv`.
- `ffmpeg` is required for MP4 conversion.
  - If `ffmpeg` is unavailable, test execution continues and `video_error.txt` is written.
- Feature matrix loading supports JSON first, then YAML.
  - If `ConvertFrom-Yaml` is unavailable, JSON format still works.
- Resources are resolved with absolute paths and catalog paths are auto-added from:
  - `resources/xosc/Catalogs/*`

## Output files per feature

Each feature directory (`artifacts/realdriver_features/<run_id>/Fxx/`) contains:

- `stdout.txt` (GT_Sim log)
- `sim.dat`, `sim.csv` (simulation and extended CSV)
- `gtsim_exit_code.txt`
- `frame_count.txt`
- `result.mp4` (when conversion succeeded)
- `video_encode.log` / `video_error.txt` (when relevant)
- `osi_light_metrics.json` / `osi_lights.csv` (when scenario enables OSI light metric collection, e.g. F03)

## Common troubleshooting

- `Video` shows N/A and `frame_count=0`:
  - Check `stdout.txt` and `video_error.txt` first.
  - Verify OSG-enabled `GT_Sim` build and `--video_capture` execution path.
- `result.mp4` missing but frames exist:
  - Check `video_encode.log`.
  - Verify `ffmpeg` executable is available in `PATH`.
