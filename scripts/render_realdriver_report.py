#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True)
    args = parser.parse_args()

    run_dir = Path(args.run_dir)
    summary_path = run_dir / "summary.json"
    if not summary_path.exists():
        raise FileNotFoundError(f"summary.json not found: {summary_path}")

    summary = json.loads(summary_path.read_text(encoding="utf-8"))

    rows = []
    for r in summary.get("results", []):
        status = "PASS" if r.get("pass") else "FAIL"
        miss = ", ".join(r.get("missing_required_patterns", [])) or "-"
        forb = ", ".join(r.get("hit_forbidden_patterns", [])) or "-"
        rows.append(f"<tr><td>{r['id']}</td><td>{r.get('name','')}</td><td>{status}</td><td>{miss}</td><td>{forb}</td></tr>")

    html = f"""
<!doctype html>
<html>
<head>
  <meta charset=\"utf-8\" />
  <title>RealDriver Feature Report</title>
  <style>
    body {{ font-family: Segoe UI, sans-serif; margin: 20px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ border: 1px solid #ccc; padding: 8px; text-align: left; }}
    th {{ background: #f3f3f3; }}
  </style>
</head>
<body>
  <h1>RealDriver Feature Report</h1>
  <p>Overall: <b>{'PASS' if summary.get('overall_pass') else 'FAIL'}</b></p>
  <p>Passed: {summary.get('passed_count')}/{summary.get('feature_count')}</p>
  <table>
    <thead><tr><th>ID</th><th>Name</th><th>Status</th><th>Missing Required</th><th>Forbidden Hits</th></tr></thead>
    <tbody>
      {''.join(rows)}
    </tbody>
  </table>
</body>
</html>
"""

    out = run_dir / "report.html"
    out.write_text(html, encoding="utf-8")
    print(str(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
