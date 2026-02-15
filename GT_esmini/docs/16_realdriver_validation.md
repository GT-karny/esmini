# RealDriver Validation Suite

This document describes the RealDriver validation framework introduced for GT_esmini.

## Layers

- Gate A: module tests (`scripts/run_realdriver_module_tests.ps1`)
- Gate B: feature E2E tests (`scripts/run_realdriver_feature_tests.ps1`)
- Gate C: report generation (`report.html` under artifacts)

## Configuration

- Feature matrix: `GT_esmini/test/validation/realdriver_feature_matrix.yaml`
- KPI thresholds: `GT_esmini/test/validation/kpi_thresholds.yaml`
- Native reference policy: `GT_esmini/test/validation/native_reference_policy.yaml`

## Golden policy

- Golden data is updated only with explicit `-UpdateGolden`.
- Native controller output is only a reference during golden creation.
- Daily pass/fail is determined by feature KPI and approved golden values.

## Typical usage

```powershell
./scripts/run_realdriver_module_tests.ps1 -BuildDir build
./scripts/run_realdriver_feature_tests.ps1 -BuildDir build
./scripts/run_realdriver_feature_tests.ps1 -BuildDir build -UpdateGolden
```
