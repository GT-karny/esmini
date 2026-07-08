#!/usr/bin/env python
"""telemetry_golden.py -- per-scenario telemetry tolerance goldens (P6 S0 oracle upgrade).

Value-level motion-invariance oracle for the VirtualDriver batches
(odr_p6_virtual_junction_design.md sec 6): freezes the ego's per-frame
(t, x, y, h, speed) series for every scenario of a gt_sim_test batch, then re-runs
the batch and diffs PER FRAME against the goldens. Unlike the regression-gate Step 2
verdict (matcher pass/fail, WARN-by-default) this measures the raw trajectory, so any
behavior drift on existing scenarios is caught even while every matcher still passes.

USAGE
-----
  telemetry_golden.py capture --batch <batch.yaml> [--label <name>] [--dll <path>]
                              [--nondet stem1,stem2]
  telemetry_golden.py diff    --batch <batch.yaml> [--label <name>] [--dll <path>]
                              [--tol-pos 1e-6] [--tol-h 1e-6] [--tol-v 1e-6]

  capture: run the batch (gt_sim_test.py batch, in-process via GT_esminiLib.dll) into
           test_results/telemetry_golden/<label>/capture, extract each scenario's ego
           series from telemetry.jsonl, and write
           GT_esmini/test/telemetry_goldens/<label>/<stem>.json
           (floats rounded 1e-6, sorted keys, indent 1, trailing newline -- the same
           conventions as the ODR conformance goldens). --nondet marks the listed
           scenario stems "nondeterministic": true (diff skips them; use only after a
           demonstrated same-build re-run deviation).
  diff:    re-run the batch into test_results/telemetry_golden/<label>/diff and compare
           per frame vs the goldens: frame counts must match EXACTLY; per-frame abs
           deviation vs --tol-pos (x, y), --tol-h (heading, wrap-aware), --tol-v (speed);
           t is compared at a fixed 1e-6 (fixed timestep). Tolerances are CLI-tunable
           because later stages may legitimately need loosening for float noise --
           defaults are tight. Exit nonzero on any FAIL or missing golden.

  Default --label is derived from the batch file stem with a trailing '_batch' stripped
  (phase3_batch.yaml -> phase3, catalog_batch.yaml -> catalog).

SAME-BUILD NOISE FLOOR (measured 2026-07-04, post-P5 Release build)
-------------------------------------------------------------------
The simulation is NOT bit-reproducible across process runs: same build + same
scenario + --fixed_timestep re-runs deviate by up to ~1e-3 m in x/y and, at
stop/brake transition frames, up to ~a*dt in speed (a one-frame offset in a
discrete controller decision; observed 0.0098 m/s). esmini's --seed does NOT
remove it (verified). Frame counts and t match exactly. The strict 1e-6 defaults
therefore only PASS on byte-identical telemetry; for gate / stage-exit usage run
with the noise floor + margin, which run_regression_gate.ps1 -TelemetryGolden uses:
  --tol-pos 5e-3 --tol-h 1e-3 --tol-v 5e-2
Position stays the tight discriminator (observed same-build max 8e-4 m; a real
regression moves the trajectory far beyond 5 mm). Prefer these tolerances over
the "nondeterministic": true escape hatch, which drops a scenario entirely.

Run under DriverScript/.venv (pyyaml; the batch itself needs osi3 + matplotlib and a
completed Release build: build/GT_esmini/Release/GT_esminiLib.dll). Stage-exit usage is
one command per batch; the last stdout line is machine-parseable:
  TELEMETRY_GOLDEN <cmd> label=<label> total=N ok=N fail=N missing=N skipped=N result=PASS|FAIL
"""
from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
GT_SIM_TEST = REPO_ROOT / "GT_esmini" / "scripts" / "verification" / "gt_sim_test.py"
GOLDEN_ROOT = REPO_ROOT / "GT_esmini" / "test" / "telemetry_goldens"
SCRATCH_ROOT = REPO_ROOT / "test_results" / "telemetry_golden"  # gitignored (test_results/)

CHANNELS = ("t", "x", "y", "h", "speed")
BATCH_TIMEOUT = 7200  # seconds; the catalog batch is ~56 scenarios x <=40 s sim
HUGE_FRAMES = 20000   # loud warning threshold; no downsampling implemented (dt=0.05 x 40 s = 800)


def _round6(v: float) -> float:
    r = round(float(v), 6)
    return 0.0 if r == 0.0 else r


def _label_from_batch(batch: Path) -> str:
    stem = batch.stem
    return stem[: -len("_batch")] if stem.endswith("_batch") else stem


def _scenario_stems(batch: Path) -> list[str]:
    import yaml
    spec = yaml.safe_load(batch.read_text(encoding="utf-8"))
    stems = []
    for entry in spec.get("scenarios", []):
        stems.append(Path(entry["scenario"]).stem)
    dup = {s for s in stems if stems.count(s) > 1}
    if dup:
        raise SystemExit(f"FATAL: duplicate scenario stems in {batch.name}: {sorted(dup)} "
                         "(per-stem goldens would collide)")
    return stems


def _run_batch(batch: Path, out_dir: Path, dll: str | None) -> None:
    """Run gt_sim_test batch as a subprocess (the DLL floods stdout; keep it in a log)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / "batch_stdout.log"
    cmd = [sys.executable, str(GT_SIM_TEST), "batch", str(batch), "--out", str(out_dir)]
    if dll:
        cmd += ["--dll", dll]
    print(f"[batch] {' '.join(cmd)}")
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT,
                              timeout=BATCH_TIMEOUT, cwd=str(REPO_ROOT))
    # Batch exit 1 = matcher-verdict fail, which is IRRELEVANT here (we consume raw
    # telemetry, not verdicts). Only a missing batch_verdict.json means the run broke.
    if not (out_dir / "batch_verdict.json").is_file():
        raise SystemExit(f"FATAL: batch produced no batch_verdict.json (exit {proc.returncode}); "
                         f"see {log_path}")


def _extract_series(run_dir: Path) -> dict:
    """telemetry.jsonl -> {'frames': N, 'channels': {t/x/y/h/speed: [...]}} rounded 1e-6."""
    jsonl = run_dir / "telemetry.jsonl"
    ch: dict[str, list[float]] = {k: [] for k in CHANNELS}
    n = 0
    if jsonl.is_file():
        for line in jsonl.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            fr = json.loads(line)
            ego = fr["ego"]
            ch["t"].append(_round6(fr["sim_time"]))
            ch["x"].append(_round6(ego["x"]))
            ch["y"].append(_round6(ego["y"]))
            ch["h"].append(_round6(ego["h"]))
            ch["speed"].append(_round6(ego["speed"]))
            n += 1
    if n > HUGE_FRAMES:
        print(f"WARNING: {run_dir.name} has {n} frames (> {HUGE_FRAMES}); "
              "golden is stored in full (no downsampling implemented)", file=sys.stderr)
    return {"frames": n, "channels": ch}


def _golden_path(label: str, stem: str) -> Path:
    return GOLDEN_ROOT / label / f"{stem}.json"


def _write_golden(path: Path, obj: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(obj, fh, sort_keys=True, indent=1)
        fh.write("\n")


def _summary(cmd: str, label: str, total: int, ok: int, fail: int,
             missing: int, skipped: int) -> int:
    result = "PASS" if (fail == 0 and missing == 0) else "FAIL"
    print(f"TELEMETRY_GOLDEN {cmd} label={label} total={total} ok={ok} fail={fail} "
          f"missing={missing} skipped={skipped} result={result}")
    return 0 if result == "PASS" else 1


# ---------------------------------------------------------------------------
# capture
# ---------------------------------------------------------------------------
def cmd_capture(args) -> int:
    batch = args.batch.resolve()
    label = args.label or _label_from_batch(batch)
    stems = _scenario_stems(batch)
    nondet = {s.strip() for s in (args.nondet or "").split(",") if s.strip()}
    unknown_nondet = nondet - set(stems)
    if unknown_nondet:
        raise SystemExit(f"FATAL: --nondet stem(s) not in batch: {sorted(unknown_nondet)}")

    out_dir = SCRATCH_ROOT / label / "capture"
    t0 = time.monotonic()
    _run_batch(batch, out_dir, args.dll)
    wall = time.monotonic() - t0

    ok = fail = 0
    for stem in stems:
        series = _extract_series(out_dir / stem)
        golden = {
            "batch": batch.relative_to(REPO_ROOT).as_posix()
            if str(batch).startswith(str(REPO_ROOT)) else str(batch),
            "label": label,
            "scenario": stem,
            **series,
        }
        if stem in nondet:
            golden["nondeterministic"] = True
            print(f"[capture] {stem}: marked nondeterministic (diff will SKIP it)")
        _write_golden(_golden_path(label, stem), golden)
        status = "ok" if series["frames"] > 0 else "ZERO-FRAMES"
        if series["frames"] > 0:
            ok += 1
        else:
            fail += 1
        print(f"[capture] {stem}: frames={series['frames']} ({status}) "
              f"-> {_golden_path(label, stem).relative_to(REPO_ROOT).as_posix()}")

    print(f"[capture] batch wall time: {wall:.1f}s ({len(stems)} scenarios)")
    return _summary("capture", label, len(stems), ok, fail, 0, 0)


# ---------------------------------------------------------------------------
# diff
# ---------------------------------------------------------------------------
def _h_dev(a: float, b: float) -> float:
    """Wrap-aware heading deviation (radians): min distance on the circle."""
    d = abs(a - b) % (2.0 * math.pi)
    return min(d, 2.0 * math.pi - d)


def cmd_diff(args) -> int:
    batch = args.batch.resolve()
    label = args.label or _label_from_batch(batch)
    stems = _scenario_stems(batch)
    tol = {"t": 1e-6, "x": args.tol_pos, "y": args.tol_pos, "h": args.tol_h,
           "speed": args.tol_v}
    eps = 1e-12  # absorb float noise at exactly-tol boundaries

    out_dir = SCRATCH_ROOT / label / "diff"
    _run_batch(batch, out_dir, args.dll)

    ok = fail = missing = skipped = 0
    for stem in stems:
        gpath = _golden_path(label, stem)
        if not gpath.is_file():
            print(f"[diff] {stem}: MISSING golden "
                  f"({gpath.relative_to(REPO_ROOT).as_posix()}; run capture first)")
            missing += 1
            continue
        golden = json.loads(gpath.read_text(encoding="utf-8"))
        if golden.get("nondeterministic"):
            print(f"[diff] {stem}: SKIP (marked nondeterministic in golden)")
            skipped += 1
            continue
        observed = _extract_series(out_dir / stem)
        if observed["frames"] != golden["frames"]:
            print(f"[diff] {stem}: FAIL frame count {observed['frames']} != "
                  f"golden {golden['frames']}")
            fail += 1
            continue
        max_dev = {k: 0.0 for k in CHANNELS}
        first_bad = None  # (channel, frame_idx, obs, gold)
        for k in CHANNELS:
            go, ob = golden["channels"][k], observed["channels"][k]
            devf = _h_dev if k == "h" else (lambda a, b: abs(a - b))
            for i in range(golden["frames"]):
                d = devf(ob[i], go[i])
                if d > max_dev[k]:
                    max_dev[k] = d
                if d > tol[k] + eps and first_bad is None:
                    first_bad = (k, i, ob[i], go[i])
        devs = " ".join(f"max_d{k}={max_dev[k]:.3g}" for k in CHANNELS)
        if first_bad is None:
            print(f"[diff] {stem}: PASS frames={golden['frames']} {devs}")
            ok += 1
        else:
            k, i, ob, go = first_bad
            print(f"[diff] {stem}: FAIL frames={golden['frames']} {devs} "
                  f"(first: {k}[{i}] observed={ob} golden={go} tol={tol[k]})")
            fail += 1

    stale = sorted(p.stem for p in (GOLDEN_ROOT / label).glob("*.json")
                   if p.stem not in stems) if (GOLDEN_ROOT / label).is_dir() else []
    if stale:
        print(f"[diff] WARNING: stale golden(s) not in batch: {stale}")

    return _summary("diff", label, len(stems), ok, fail, missing, skipped)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    pc = sub.add_parser("capture", help="run a batch and (re)write its telemetry goldens")
    pc.add_argument("--batch", type=Path, required=True, help="gt_sim_test batch yaml")
    pc.add_argument("--label", default=None, help="golden set name (default: batch stem minus _batch)")
    pc.add_argument("--dll", default=None, help="GT_esminiLib.dll override (forwarded to gt_sim_test)")
    pc.add_argument("--nondet", default=None,
                    help="comma list of scenario stems to flag nondeterministic (diff skips)")

    pd = sub.add_parser("diff", help="re-run a batch and diff per-frame telemetry vs goldens")
    pd.add_argument("--batch", type=Path, required=True)
    pd.add_argument("--label", default=None)
    pd.add_argument("--dll", default=None)
    pd.add_argument("--tol-pos", type=float, default=1e-6, help="abs tolerance for x/y [m]")
    pd.add_argument("--tol-h", type=float, default=1e-6, help="abs tolerance for heading [rad], wrap-aware")
    pd.add_argument("--tol-v", type=float, default=1e-6, help="abs tolerance for speed [m/s]")

    args = p.parse_args(argv)
    if not args.batch.is_file():
        print(f"ERROR: batch manifest not found: {args.batch}", file=sys.stderr)
        return 2
    if args.cmd == "capture":
        return cmd_capture(args)
    return cmd_diff(args)


if __name__ == "__main__":
    raise SystemExit(main())
