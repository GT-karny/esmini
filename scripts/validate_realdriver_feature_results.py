#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path
from typing import Any, Dict

try:
    import yaml  # type: ignore
except ImportError:  # pragma: no cover - fallback path for minimal env
    yaml = None


def load_yaml(path: Path) -> Dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        pass

    if yaml is not None:
        return yaml.safe_load(text)

    raise RuntimeError(
        f"Cannot parse {path}. Install pyyaml or use JSON-compatible YAML content."
    )


def try_compute_basic_kpis(feature_dir: Path) -> Dict[str, Any]:
    k = {}
    csv_path = feature_dir / "sim.csv"
    if not csv_path.exists():
        return k

    times = []
    speeds = []
    with csv_path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 10:
                continue
            try:
                t = float(parts[0])
                v = float(parts[9])
            except ValueError:
                continue
            times.append(t)
            speeds.append(v)

    if times:
        k["duration_s"] = max(times)
    if speeds:
        k["speed_min_mps"] = min(speeds)
        k["speed_max_mps"] = max(speeds)
    return k


def compare_with_golden(actual_kpi: Dict[str, Any], golden_file: Path, thresholds: Dict[str, Any]) -> Dict[str, Any]:
    if not golden_file.exists():
        return {"golden_available": False, "pass": True, "details": []}

    with golden_file.open("r", encoding="utf-8") as f:
        golden = json.load(f)

    details = []
    ok = True
    spd_thr = float(thresholds.get("speed_rmse_mps", 0.30))

    if "speed_max_mps" in actual_kpi and "speed_max_mps" in golden:
        d = abs(actual_kpi["speed_max_mps"] - golden["speed_max_mps"])
        details.append({"metric": "speed_max_mps", "delta": d, "limit": spd_thr})
        ok = ok and (d <= spd_thr)

    if "speed_min_mps" in actual_kpi and "speed_min_mps" in golden:
        d = abs(actual_kpi["speed_min_mps"] - golden["speed_min_mps"])
        details.append({"metric": "speed_min_mps", "delta": d, "limit": spd_thr})
        ok = ok and (d <= spd_thr)

    return {"golden_available": True, "pass": ok, "details": details}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", required=True)
    parser.add_argument("--thresholds", required=True)
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--golden-root", default="golden/realdriver_features")
    parser.add_argument("--update-golden", action="store_true")
    args = parser.parse_args()

    matrix = load_yaml(Path(args.matrix))
    thresholds = load_yaml(Path(args.thresholds)).get("defaults", {})
    run_dir = Path(args.run_dir)
    golden_root = Path(args.golden_root)

    results = []
    overall_pass = True

    for feat in matrix.get("features", []):
        fid = feat["id"]
        fdir = run_dir / fid
        stdout_path = fdir / "stdout.txt"
        text = stdout_path.read_text(encoding="utf-8", errors="ignore") if stdout_path.exists() else ""

        missing_required = []
        for pat in feat.get("required_patterns", []):
            if re.search(pat, text, re.MULTILINE) is None:
                missing_required.append(pat)

        hit_forbidden = []
        for pat in feat.get("forbidden_patterns", []):
            if re.search(pat, text, re.MULTILINE) is not None:
                hit_forbidden.append(pat)

        kpi = try_compute_basic_kpis(fdir)
        golden_file = golden_root / fid / "kpi_reference.json"
        golden_cmp = compare_with_golden(kpi, golden_file, thresholds)

        passed = (len(missing_required) == 0 and len(hit_forbidden) == 0 and golden_cmp["pass"])
        overall_pass = overall_pass and passed

        feat_result = {
            "id": fid,
            "name": feat.get("name", ""),
            "scenario": feat.get("scenario", ""),
            "pass": passed,
            "missing_required_patterns": missing_required,
            "hit_forbidden_patterns": hit_forbidden,
            "kpi": kpi,
            "golden": golden_cmp,
            "required_modules_cpp": feat.get("required_modules_cpp", []),
            "required_modules_py": feat.get("required_modules_py", []),
        }

        if args.update_golden:
            target_dir = golden_root / fid
            target_dir.mkdir(parents=True, exist_ok=True)
            with (target_dir / "kpi_reference.json").open("w", encoding="utf-8") as f:
                json.dump(kpi, f, indent=2)

        results.append(feat_result)

    summary = {
        "overall_pass": overall_pass,
        "run_dir": str(run_dir),
        "feature_count": len(results),
        "passed_count": sum(1 for r in results if r["pass"]),
        "results": results,
    }

    out = run_dir / "summary.json"
    out.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(json.dumps({"overall_pass": overall_pass, "summary": str(out)}, indent=2))
    return 0 if overall_pass else 2


if __name__ == "__main__":
    raise SystemExit(main())
