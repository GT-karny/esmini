# Frozen: Python Verification Toolchain

## What this is

This directory contains the PythonDriverController comparison and verification
toolchain that was frozen at GT_esmini v0.8. It is preserved here for historical
reference only.

## Why it was frozen (audit SCR-2)

PythonDriver features were development-frozen at v0.8. Subsequently, upstream
esmini made breaking changes (v3.0+) and `scripts/dat.py` was lost, making the
toolchain non-functional in its original location. The scripts are kept here
because they document the original comparison methodology and KPI definitions.

**Do not run these scripts.** Imports are stale (dat.py is missing from upstream,
jinja2/matplotlib versions may have drifted). They are not maintained.

## Original file paths

| Archived path | Original repo path |
| :--- | :--- |
| `run_comparison_test.py` | `run_comparison_test.py` (repo root) |
| `scripts/compare_python_vs_default.py` | `scripts/compare_python_vs_default.py` |
| `scripts/comparison_kpis.py` | `scripts/comparison_kpis.py` |
| `scripts/comparison_report_template.html` | `scripts/comparison_report_template.html` |
| `scripts/plot_comparison.py` | `scripts/plot_comparison.py` |
| `scripts/validate_realdriver_feature_results.py` | `scripts/validate_realdriver_feature_results.py` |
| `test/comparison_matrix.yaml` | `GT_esmini/test/comparison_matrix.yaml` |

## Not moved: comparison_thresholds.yaml

`GT_esmini/test/comparison_thresholds.yaml` was NOT moved here. It is actively
read and written by the live web backend (`GT_esmini/web/backend/config.py`
`load_thresholds`/`save_thresholds`) and is packaged by the PyInstaller pipeline.
That file remains at its original location.

## Frozen since

v0.8 — June 2026. Moved to archive as part of tech-debt week-1 (audit SCR-2/WEB-5).
