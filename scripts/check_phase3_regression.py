#!/usr/bin/env python
"""check_phase3_regression.py -- per-scenario/per-matcher regression gate for the
VirtualDriver behavioral batch (tech-debt roadmap F4 / V4).

WHY (vs the raw batch exit code)
--------------------------------
`gt_sim_test.py batch` returns 0 for overall in {pass, needs-review} and 1 for
overall=fail. But the phase-3 batch is *expected* to carry a couple of
discriminating failures at the current development stage (e.g. red_stop_green_go
and green_no_stop). Gating on the aggregate "any fail -> fail" would either
always WARN (imprecise) or, if flipped to hard, block on a KNOWN state.

This script compares the batch output to a COMMITTED baseline
(GT_esmini/test/regression_baseline/phase3_expected.yaml) **per scenario and per
matcher**. It flags a deviation in EITHER direction:

  * a scenario/matcher that was pass and is now fail  (a regression), AND
  * a scenario/matcher that was fail and is now pass  (a silent fix / the
    baseline is stale and must be refreshed).

Both are real signal: the second has historically been how behavior changes slip
in unannounced. Exit is non-zero on any deviation, with the diff printed.

INPUT
-----
A gt_sim_test batch --out directory containing:
  batch_verdict.json        (per-scenario overall status + summary)
  <stem>/verdict.json       (per-matcher results: event + status + detail)

USAGE
-----
  # gate: compare an existing batch output against the committed baseline
  check_phase3_regression.py --batch-out test_results/regression/phase3

  # refresh the baseline after an INTENTIONAL behavior change (review the diff!)
  check_phase3_regression.py --batch-out test_results/regression/phase3 --update

  # override baseline / report location
  check_phase3_regression.py --batch-out <dir> --baseline <yaml> --report <md>

The last stdout line is machine-parseable:
  PHASE3_REGRESSION result=PASS|FAIL deviations=N scenarios=N baseline=<path>

Runs on stdlib + pyyaml only (no osi3 / no DLL): it reads the JSON the batch
already wrote. Run under DriverScript/.venv (pyyaml). The batch itself must have
been produced separately (regression-gate Step 2 / CI).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE = REPO_ROOT / "GT_esmini" / "test" / "regression_baseline" / "phase3_expected.yaml"
DEFAULT_MANIFEST = "resources/xosc/verification/phase3_batch.yaml"


# ---------------------------------------------------------------------------
# read a gt_sim_test batch output directory into a normalized dict
# ---------------------------------------------------------------------------

def _scenario_status(rec: dict) -> str:
    """Mirror gt_sim_test.batch._status: error > verdict.overall > needs-review."""
    if rec.get("error"):
        return "error"
    if rec.get("verdict"):
        return rec["verdict"]["overall"]
    return "needs-review"


def read_batch_output(batch_out: Path) -> dict:
    """-> {manifest, commit, overall, summary, scenarios: {stem: {...}}}.

    Each scenario carries its per-scenario status and an ORDERED matcher list
    [{event, status, detail}] read from <stem>/verdict.json (empty on error)."""
    bv_path = batch_out / "batch_verdict.json"
    if not bv_path.is_file():
        raise FileNotFoundError(
            f"{bv_path} not found - run `gt_sim_test.py batch ... --out {batch_out}` first")
    bv = json.loads(bv_path.read_text(encoding="utf-8"))

    scenarios: dict[str, dict] = {}
    order: list[str] = []
    for rec in bv.get("scenarios", []):
        stem = Path(rec["scenario"]).stem
        status = _scenario_status(rec)
        matchers: list[dict] = []
        vpath = batch_out / stem / "verdict.json"
        if vpath.is_file():
            try:
                vd = json.loads(vpath.read_text(encoding="utf-8"))
                for r in vd.get("results", []):
                    matchers.append({
                        "event": r.get("event"),
                        "status": r.get("status"),
                        "detail": r.get("detail", ""),
                    })
            except (OSError, json.JSONDecodeError):
                pass
        scenarios[stem] = {
            "scenario": rec["scenario"],
            "status": status,
            "error": rec.get("error"),
            "matchers": matchers,
        }
        order.append(stem)

    return {
        "manifest": bv.get("manifest", DEFAULT_MANIFEST),
        "commit": bv.get("commit", ""),
        "overall": bv.get("overall"),
        "summary": bv.get("summary", {}),
        "order": order,
        "scenarios": scenarios,
    }


# ---------------------------------------------------------------------------
# baseline (committed expected values)
# ---------------------------------------------------------------------------

def _norm_manifest(manifest: str) -> str:
    """Store the manifest as a repo-relative forward-slash path so the committed
    baseline is portable across machines/CI (batch_verdict.json records absolute)."""
    try:
        return _rel(Path(manifest))
    except (ValueError, OSError):
        return manifest


def build_baseline_doc(batch: dict, manifest_hint: str | None = None) -> dict:
    """Turn a batch output dict into the committed-baseline document shape."""
    scen_doc = {}
    for stem in batch["order"]:
        s = batch["scenarios"][stem]
        scen_doc[stem] = {
            "status": s["status"],
            "matchers": [{"event": m["event"], "status": m["status"]} for m in s["matchers"]],
        }
    return {
        "manifest": manifest_hint or _norm_manifest(batch["manifest"]),
        "note": ("Committed regression baseline for the VirtualDriver phase-3 behavioral "
                 "batch. Compared per-scenario/per-matcher by "
                 "scripts/check_phase3_regression.py. Any deviation (new fail OR a known "
                 "fail turning pass) fails the check. Refresh ONLY after an intentional "
                 "behavior change, with --update, and review the diff before committing."),
        "expected_summary": dict(batch["summary"]),
        "scenarios": scen_doc,
    }


def write_baseline(path: Path, doc: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # Emit scenarios in insertion order (Python dicts preserve it); matcher dicts
    # are written inline for readability.
    text = yaml.safe_dump(doc, sort_keys=False, default_flow_style=False, width=100)
    path.write_text(text, encoding="utf-8")


# ---------------------------------------------------------------------------
# comparison
# ---------------------------------------------------------------------------

def compare(baseline: dict, batch: dict) -> list[dict]:
    """Return a list of deviation records. Empty list = no regression."""
    devs: list[dict] = []
    base_scen = baseline.get("scenarios", {}) or {}
    out_scen = batch["scenarios"]

    base_keys = list(base_scen.keys())
    out_keys = batch["order"]

    # 1. structural: scenario set differs
    for stem in base_keys:
        if stem not in out_scen:
            devs.append({"scenario": stem, "kind": "scenario_missing",
                         "expected": base_scen[stem].get("status"), "actual": None,
                         "detail": "scenario in baseline but absent from batch output"})
    for stem in out_keys:
        if stem not in base_scen:
            devs.append({"scenario": stem, "kind": "scenario_new", "expected": None,
                         "actual": out_scen[stem]["status"],
                         "detail": "scenario in batch output but absent from baseline "
                                   "(add it via --update)"})

    # 2. per-scenario status + per-matcher status
    for stem in out_keys:
        if stem not in base_scen:
            continue
        b = base_scen[stem]
        o = out_scen[stem]
        if b.get("status") != o["status"]:
            devs.append({"scenario": stem, "kind": "status_change",
                         "expected": b.get("status"), "actual": o["status"],
                         "detail": o.get("error") or "scenario verdict changed"})

        b_matchers = b.get("matchers", []) or []
        o_matchers = o["matchers"]
        n = max(len(b_matchers), len(o_matchers))
        for i in range(n):
            bm = b_matchers[i] if i < len(b_matchers) else None
            om = o_matchers[i] if i < len(o_matchers) else None
            if bm is None:
                devs.append({"scenario": stem, "kind": "matcher_added",
                             "event": om["event"], "expected": None, "actual": om["status"],
                             "detail": om.get("detail", "")})
                continue
            if om is None:
                devs.append({"scenario": stem, "kind": "matcher_removed",
                             "event": bm["event"], "expected": bm["status"], "actual": None,
                             "detail": "matcher present in baseline but not in output"})
                continue
            if bm.get("event") != om.get("event"):
                devs.append({"scenario": stem, "kind": "matcher_reordered",
                             "event": f"{bm.get('event')} -> {om.get('event')}",
                             "expected": bm.get("status"), "actual": om.get("status"),
                             "detail": om.get("detail", "")})
                continue
            if bm.get("status") != om.get("status"):
                kind = ("regression" if (bm.get("status") == "pass" and om.get("status") == "fail")
                        else "unexpected_pass" if (bm.get("status") == "fail" and om.get("status") == "pass")
                        else "matcher_status_change")
                devs.append({"scenario": stem, "kind": kind, "event": om.get("event"),
                             "expected": bm.get("status"), "actual": om.get("status"),
                             "detail": om.get("detail", "")})
    return devs


# ---------------------------------------------------------------------------
# markdown report
# ---------------------------------------------------------------------------

_STATUS_MARK = {"pass": "PASS", "fail": "FAIL", "needs-review": "REVIEW",
                "error": "ERROR", None: "-"}


def render_report(baseline: dict, batch: dict, devs: list[dict], baseline_path: Path) -> str:
    base_scen = baseline.get("scenarios", {}) or {}
    result = "PASS" if not devs else "FAIL"
    lines: list[str] = []
    lines.append("# Phase-3 VirtualDriver behavioral regression report")
    lines.append("")
    lines.append(f"**Result: {result}**  "
                 f"(deviations={len(devs)}, scenarios={len(batch['order'])})  "
                 f"commit={batch['commit'] or 'n/a'}")
    lines.append("")
    lines.append(f"- manifest: `{_norm_manifest(batch['manifest'])}`")
    lines.append(f"- baseline: `{_rel(baseline_path)}`")
    s = batch["summary"]
    es = baseline.get("expected_summary", {}) or {}
    lines.append(f"- batch summary: pass={s.get('pass', 0)} fail={s.get('fail', 0)} "
                 f"needs-review={s.get('needs-review', 0)} error={s.get('error', 0)} "
                 f"(baseline expected: pass={es.get('pass', 0)} fail={es.get('fail', 0)} "
                 f"needs-review={es.get('needs-review', 0)} error={es.get('error', 0)})")
    lines.append("")

    # Deviations block
    if devs:
        lines.append("## Deviations (gate-failing)")
        lines.append("")
        lines.append("| scenario | matcher | kind | expected | actual | measured |")
        lines.append("| :-- | :-- | :-- | :-- | :-- | :-- |")
        for d in devs:
            ev = d.get("event", "-")
            lines.append(f"| {d['scenario']} | {ev} | **{d['kind']}** | "
                         f"{_STATUS_MARK.get(d.get('expected'), d.get('expected'))} | "
                         f"{_STATUS_MARK.get(d.get('actual'), d.get('actual'))} | "
                         f"{_md_cell(d.get('detail', ''))} |")
        lines.append("")
    else:
        lines.append("## Deviations")
        lines.append("")
        lines.append("None. Every scenario and matcher matches the committed baseline.")
        lines.append("")

    # Full scenario x matcher table
    lines.append("## Full per-scenario / per-matcher table")
    lines.append("")
    lines.append("| scenario | expected | actual | matcher | expected | actual | measured |")
    lines.append("| :-- | :-- | :-- | :-- | :-- | :-- | :-- |")
    for stem in batch["order"]:
        o = batch["scenarios"][stem]
        b = base_scen.get(stem, {})
        b_status = b.get("status")
        s_flag = "" if b_status == o["status"] else "  [!]"
        b_matchers = b.get("matchers", []) or []
        if not o["matchers"]:
            lines.append(f"| {stem} | {_STATUS_MARK.get(b_status, b_status)} | "
                         f"{_STATUS_MARK.get(o['status'])}{s_flag} | "
                         f"{o.get('error') or '(no matchers)'} |  |  |  |")
            continue
        first = True
        for i, m in enumerate(o["matchers"]):
            bm = b_matchers[i] if i < len(b_matchers) else None
            bm_status = bm.get("status") if bm else None
            m_flag = "" if (bm and bm_status == m["status"]) else "  [!]"
            if first:
                sc_cell = stem
                se_cell = _STATUS_MARK.get(b_status, b_status)
                sa_cell = f"{_STATUS_MARK.get(o['status'])}{s_flag}"
                first = False
            else:
                sc_cell = se_cell = sa_cell = ""
            lines.append(f"| {sc_cell} | {se_cell} | {sa_cell} | {m['event']} | "
                         f"{_STATUS_MARK.get(bm_status, bm_status)} | "
                         f"{_STATUS_MARK.get(m['status'])}{m_flag} | "
                         f"{_md_cell(m['detail'])} |")
    lines.append("")
    lines.append(f"_[!] marks a deviation from the committed baseline._")
    lines.append("")
    return "\n".join(lines)


def _md_cell(text: str) -> str:
    return (text or "").replace("|", "\\|").replace("\n", " ").strip()


def _rel(p: Path) -> str:
    try:
        return str(p.resolve().relative_to(REPO_ROOT)).replace("\\", "/")
    except ValueError:
        return str(p)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--batch-out", type=Path, required=True,
                    help="gt_sim_test batch --out directory (has batch_verdict.json)")
    ap.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE,
                    help=f"committed baseline yaml (default: {_rel(DEFAULT_BASELINE)})")
    ap.add_argument("--report", type=Path, default=None,
                    help="markdown report path (default: <batch-out>/regression_report.md)")
    ap.add_argument("--update", action="store_true",
                    help="regenerate the baseline from the batch output (intentional changes)")
    ap.add_argument("--manifest", default=None,
                    help="manifest path recorded in a freshly written baseline")
    args = ap.parse_args(argv)

    batch_out = args.batch_out.resolve()
    try:
        batch = read_batch_output(batch_out)
    except FileNotFoundError as e:
        print(f"[check_phase3] ERROR: {e}", file=sys.stderr)
        return 2

    report_path = args.report or (batch_out / "regression_report.md")

    if args.update:
        doc = build_baseline_doc(batch, args.manifest)
        write_baseline(args.baseline, doc)
        print(f"[check_phase3] baseline written: {_rel(args.baseline)} "
              f"({len(batch['order'])} scenarios, summary={batch['summary']})",
              file=sys.stderr)
        # still emit a report so the update is auditable
        rendered = render_report(doc, batch, [], args.baseline)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(rendered, encoding="utf-8")
        print(f"PHASE3_REGRESSION result=UPDATED deviations=0 "
              f"scenarios={len(batch['order'])} baseline={_rel(args.baseline)}")
        return 0

    if not args.baseline.is_file():
        print(f"[check_phase3] ERROR: baseline not found: {args.baseline}\n"
              f"    create it once with: check_phase3_regression.py --batch-out "
              f"{_rel(batch_out)} --update", file=sys.stderr)
        return 2
    baseline = yaml.safe_load(args.baseline.read_text(encoding="utf-8")) or {}

    devs = compare(baseline, batch)
    rendered = render_report(baseline, batch, devs, args.baseline)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(rendered, encoding="utf-8")

    if devs:
        print(f"[check_phase3] {len(devs)} DEVIATION(S) vs baseline "
              f"{_rel(args.baseline)}:", file=sys.stderr)
        for d in devs:
            ev = f" {d['event']}" if d.get("event") else ""
            print(f"   - {d['scenario']}{ev}: {d['kind']} "
                  f"(expected={d.get('expected')} actual={d.get('actual')})", file=sys.stderr)
        print(f"   report -> {report_path}", file=sys.stderr)
    else:
        print(f"[check_phase3] no deviations vs baseline {_rel(args.baseline)} "
              f"({len(batch['order'])} scenarios) -> {report_path}", file=sys.stderr)

    result = "PASS" if not devs else "FAIL"
    print(f"PHASE3_REGRESSION result={result} deviations={len(devs)} "
          f"scenarios={len(batch['order'])} baseline={_rel(args.baseline)}")
    return 0 if not devs else 1


if __name__ == "__main__":
    raise SystemExit(main())
