# RealDriver Validation Suite

This document describes the RealDriver validation framework introduced for GT_esmini.

## Layers

- Gate A: module tests (`scripts/run_realdriver_module_tests.ps1`)
- Gate B: feature E2E tests (`scripts/run_realdriver_feature_tests.ps1`)
- Gate C: report generation (`report.html` under artifacts)

## Runtime baseline

- Feature tests run on `GT_Sim` (not `GT_Loader`)
- Default execution frequency is `--hz 100`
- Each feature run stores:
  - `sim.dat` / `sim.csv` (extended road coordinates)
  - `result.mp4` (when ffmpeg is available)
  - `stdout.txt` and optional `video_error.txt`

## Report readability enhancements

- `report.html` now shows these columns per scenario:
  - `Video`: link to `result.mp4`
  - `検証観点`: human-readable validation points from feature matrix
  - `Road KPI要約`: lane/s/t based KPI summary
- Validation points are defined in:
  - `GT_esmini/test/validation/realdriver_feature_matrix.yaml` (`validation_points`)

## Configuration

- Feature matrix: `GT_esmini/test/validation/realdriver_feature_matrix.yaml`
- KPI thresholds: `GT_esmini/test/validation/kpi_thresholds.yaml`
- Native reference policy: `GT_esmini/test/validation/native_reference_policy.yaml`

## Golden policy

- Golden data is updated only with explicit `-UpdateGolden`.
- Native controller output is only a reference during golden creation.
- Daily pass/fail is determined by feature KPI and approved golden values.
- Road-coordinate KPI (`roadId/laneId/s/t`) is treated as the primary KPI axis.

## Typical usage

```powershell
./scripts/run_realdriver_module_tests.ps1 -BuildDir build
./scripts/run_realdriver_feature_tests.ps1 -BuildDir build
./scripts/run_realdriver_feature_tests.ps1 -BuildDir build -UpdateGolden
./scripts/run_realdriver_feature_tests.ps1 -BuildDir build -Hz 100 -EnableVideo $true
```
