---
name: esmini-pythondriver-validation
description: Validate esmini PythonDriver feature test artifacts and diagnose failures. Use when asked to run validation for a specific run directory under artifacts/pythondriver_features, re-evaluate summary/report outputs with scripts/validate_realdriver_feature_results.py, or explain KPI/pattern/trace/light mismatch failures from summary.json or report.html.
---

# esmini PythonDriver Validation

Run validation against an existing `artifacts/pythondriver_features/<run_id>` result and summarize why each feature passed or failed.

## Execute Validation

1. Move to repo root (`esmini`).
2. Ensure validation Python exists:

```powershell
Test-Path venv\Scripts\python.exe
```

3. Run validator for the target run directory:

```powershell
venv\Scripts\python.exe scripts\validate_realdriver_feature_results.py `
  --matrix GT_esmini/test/validation/pythondriver_feature_matrix.yaml `
  --thresholds GT_esmini/test/validation/kpi_thresholds.yaml `
  --run-dir artifacts/pythondriver_features/<run_id>
```

4. Read outputs:
- `artifacts/pythondriver_features/<run_id>/summary.json`
- `artifacts/pythondriver_features/<run_id>/report.html`

## Extract Failure Reasons

Use this PowerShell snippet to list major fail reasons per feature:

```powershell
$j = Get-Content artifacts\pythondriver_features\<run_id>\summary.json -Raw | ConvertFrom-Json
foreach($r in $j.results){
  if(-not $r.pass){
    $reasons=@()
    if((@($r.missing_required_patterns).Count) -gt 0){$reasons += "missing_required_patterns"}
    if((@($r.hit_forbidden_patterns).Count) -gt 0){$reasons += "hit_forbidden_patterns"}
    if($r.kpi_checks -and (-not $r.kpi_checks.pass)){$reasons += "kpi_checks"}
    if($r.kpi_checks_any -and (-not $r.kpi_checks_any.pass)){$reasons += "kpi_checks_any"}
    if($null -ne $r.trace_integrity -and (-not $r.trace_integrity)){$reasons += "trace_integrity"}
    if($null -ne $r.light_mapping_integrity -and (-not $r.light_mapping_integrity)){$reasons += "light_mapping_integrity"}
    if($null -ne $r.autolight_integrity -and (-not $r.autolight_integrity)){$reasons += "autolight_integrity"}
    "{0}: {1}" -f $r.id, ($reasons -join ", ")
  }
}
```

## Common Interpretation Rules

- `trace_result_key_missing`: trace schema/key mismatch or missing trace emission.
- `light_trace_missing`: light mapping trace missing; check Python-side trace output and light signal path.
- `autolight_trace_missing`: autolight trace missing; check auto-light logic instrumentation and output path.
- `missing_required_patterns`: expected log marker not found; verify scenario actions and log level.
- `kpi_checks` fail: behavior deviates from matrix thresholds; inspect per-scenario KPI details.

When reporting, always include:
- target run directory
- validator command
- overall pass/fail
- failing feature IDs and concrete fail reasons
