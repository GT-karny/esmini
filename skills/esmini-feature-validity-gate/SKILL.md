---
name: esmini-feature-validity-gate
description: Validate whether each esmini feature scenario (Fxx) is functionally correct, not just KPI-pass. Use when designing or reviewing GT_esmini/test/scenarios/* and GT_esmini/test/validation/pythondriver_feature_matrix.yaml, when users report "pass but behavior is wrong", or when route/lane/action behavior must be proven from logs and traces.
---

# esmini Feature Validity Gate

Treat `test pass` and `functional validity` as separate claims. Prove both.
Also treat `test design validity` as a third independent claim. A failing feature can be product-bug, test-bug, or both.

## Workflow

1. Define feature intent and test intent in falsifiable sentences.
- Example: `F06 must follow AssignRoute through a turning path across a junction and remain route-valid.`
- Also define what the test is allowed to prove:
  - `Test intent`: `F06 validation can distinguish "route-following succeeded" from "controller fallback happened".`

2. Define required evidence before editing anything.
- `E1` Scenario-level intent evidence: expected road transition sequence or waypoint-hit sequence.
- `E2` Control-path evidence: required action log markers (for example `AssignRoute`).
- `E3` Safety/quality evidence: KPI bounds (`s_progress`, lateral error, lane-change count).
- `E4` Invalidity guards: patterns that must not appear (for example `Invalid waypoint`, `Route .* is not valid`).
- `E5` Test validity evidence: evidence that checks are specific enough to fail the wrong behavior and robust enough to pass the right behavior.

3. Read OpenDRIVE and derive reachable transitions.
- Parse `resources/xodr/*.xodr` junction connections.
- Use only transitions that are actually connected by `incomingRoad -> connectingRoad -> successor road`.
- Do not claim route-following if route definition is invalid or ignored by controller.

4. Validate the test logic itself before trusting outcomes.
- Check observability:
  - Required behavior must have at least one direct positive signal (not only KPI proxy).
  - Forbidden behavior must have at least one direct negative signal.
- Check specificity:
  - `required_patterns` must be behavior-specific, not generic startup/runtime lines.
  - KPI thresholds must discriminate intended behavior from known fallback/shortcut behavior.
- Check falsifiability:
  - You must be able to describe at least one realistic wrong behavior that would fail this test.
  - If no such counterexample exists, classify the test as weak and revise matrix/scenario.
- Check dependency consistency:
  - If the same pass/fail depends on simulator logging level or unstable text, add a more stable signal.
  - If route/path assertions depend on columns absent in `sim.csv`, add trace collection first.

5. Encode matrix checks so they can fail for the right reason.
- Keep `required_patterns` for action execution proof.
- Add `forbidden_patterns` for route-invalid and controller-fallback signatures.
- Set KPI thresholds that reflect the intended maneuver window, not unrelated historical values.
- Ensure at least one check explicitly detects intended path adherence.

6. Execute and verify with artifacts, not assumptions.
- Run feature tests.
- Inspect `summary.json`, `log.txt`, and `sim.csv`.
- Extract actual road sequence from `sim.csv`:

```powershell
@'
import pandas as pd
df = pd.read_csv("artifacts/pythondriver_features/<run_id>/Fxx/sim.csv", skiprows=1)
df.columns=[c.strip() for c in df.columns]
roads=df["roadId"].astype(int).tolist()
seq=[roads[0]]
for r in roads[1:]:
    if r!=seq[-1]:
        seq.append(r)
print(seq)
'@ | venv\Scripts\python.exe -
```

7. Classify result with strict labels.
- `functionally_valid`: intent evidence + action evidence + KPI + invalidity guards all pass.
- `metric_only_pass`: KPI pass but intent evidence missing or ambiguous.
- `invalid`: route invalid, required behavior missing, or forbidden pattern hit.
- `test_invalid`: test design cannot reliably distinguish correct vs incorrect behavior, regardless of current pass/fail.

## Mandatory review rules

- Reject any result where route invalid warnings exist, even if KPI passes.
- Reject any result where expected action markers are missing.
- Reject any result where observed road sequence contradicts feature intent.
- Reject any validation conclusion when the test lacks direct signals for both success and failure modes.
- Reject KPI-only evidence when KPI could be satisfied by fallback behavior.
- If test is weak/ambiguous, report `test_invalid` first, then propose how to strengthen it.
- Report concrete mismatch with file paths and observed values.

## Response template

Use this exact structure when reporting:

1. `Intent`: one sentence
2. `Test validity check`: what this test can/cannot prove, and why
3. `Expected evidence`: E1-E5 bullets
4. `Observed evidence`: run id + concrete values
5. `Decision`: `functionally_valid` / `metric_only_pass` / `invalid` / `test_invalid`
6. `Fix plan`: scenario changes, matrix changes, and re-validation command
