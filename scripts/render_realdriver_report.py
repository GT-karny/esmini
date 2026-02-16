#!/usr/bin/env python3
import argparse
import html
import json
from pathlib import Path


def esc(text: str) -> str:
    return html.escape(text, quote=True)


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
        kpi_checks = r.get("kpi_checks", {}) or {}
        kpi_check_details = kpi_checks.get("details", []) or []
        validation_points = r.get("validation_points", [])
        points_text = "<br/>".join([f"- {esc(str(p))}" for p in validation_points]) if validation_points else "-"

        road_kpi = r.get("road_kpi", {}) or {}
        road_summary_parts = []
        if road_kpi.get("lane_id_end") is not None:
            road_summary_parts.append(f"lane_end={road_kpi.get('lane_id_end')}")
        if road_kpi.get("lane_change_count") is not None:
            road_summary_parts.append(f"lane_changes={road_kpi.get('lane_change_count')}")
        if road_kpi.get("s_progress_m") is not None:
            road_summary_parts.append(f"s_progress={float(road_kpi.get('s_progress_m')):.2f}m")
        if road_kpi.get("t_abs_max_m") is not None:
            road_summary_parts.append(f"|t|max={float(road_kpi.get('t_abs_max_m')):.2f}m")
        road_summary = "<br/>".join(esc(s) for s in road_summary_parts) if road_summary_parts else "-"
        kpi_failed = [d for d in kpi_check_details if not d.get("pass", True)]
        if kpi_failed:
            kpi_text_parts = []
            for d in kpi_failed:
                metric = d.get("metric", "")
                actual = d.get("actual")
                constraints = []
                if "min" in d:
                    constraints.append(f">={d['min']}")
                if "max" in d:
                    constraints.append(f"<={d['max']}")
                if "equals" in d:
                    constraints.append(f"=={d['equals']}")
                if "target" in d:
                    constraints.append(f"{d['target']}±{d.get('tolerance', 0)}")
                if "in" in d:
                    constraints.append(f"in {d['in']}")
                constraint_text = ", ".join(constraints) if constraints else d.get("reason", "check_failed")
                kpi_text_parts.append(f"{metric}: actual={actual} (expect {constraint_text})")
            kpi_text = "<br/>".join(esc(x) for x in kpi_text_parts)
        else:
            kpi_text = "PASS"

        fid = r.get("id")
        video = r.get("video", {}) or {}
        driver = r.get("driverscript", {}) or {}
        frame_count = int(video.get("frame_count", 0) or 0)
        mp4_available = bool(video.get("mp4_available", False))
        mp4_name = video.get("mp4_path", "result.mp4") or "result.mp4"
        video_error = str(video.get("video_error", "") or "")
        video_error_first = video_error.splitlines()[0] if video_error else ""
        video_rel = Path(str(fid)) / mp4_name

        if mp4_available:
            video_cell = f"<a href=\"{esc(video_rel.as_posix())}\">{esc(mp4_name)}</a><br/>frames={frame_count}"
        elif video_error_first:
            video_cell = f"<span class=\"warn\">N/A</span><br/>frames={frame_count}<br/>{esc(video_error_first)}"
        else:
            video_cell = f"N/A<br/>frames={frame_count}"

        if driver.get("enabled"):
            out_name = str(driver.get("stdout_path", "") or "")
            err_name = str(driver.get("stderr_path", "") or "")
            links = []
            if out_name:
                links.append(f"<a href=\"{esc((Path(str(fid)) / out_name).as_posix())}\">stdout</a>")
            if err_name:
                links.append(f"<a href=\"{esc((Path(str(fid)) / err_name).as_posix())}\">stderr</a>")
            code_text = esc(str(driver.get("exit_code", "")))
            driver_cell = f"exit={code_text}" + (f"<br/>{' | '.join(links)}" if links else "")
        else:
            driver_cell = "N/A"

        rows.append(
            "<tr>"
            f"<td>{esc(str(r.get('id', '')))}</td>"
            f"<td>{esc(str(r.get('name', '')))}</td>"
            f"<td>{status}</td>"
            f"<td>{video_cell}</td>"
            f"<td>{driver_cell}</td>"
            f"<td>{points_text}</td>"
            f"<td>{road_summary}</td>"
            f"<td>{kpi_text}</td>"
            f"<td>{esc(miss)}</td>"
            f"<td>{esc(forb)}</td>"
            "</tr>"
        )

    html = f"""
<!doctype html>
<html>
<head>
  <meta charset=\"utf-8\" />
  <title>RealDriver Feature Report</title>
  <style>
    body {{ font-family: Segoe UI, sans-serif; margin: 20px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ border: 1px solid #ccc; padding: 8px; text-align: left; vertical-align: top; }}
    th {{ background: #f3f3f3; }}
    .warn {{ color: #b94a00; font-weight: 600; }}
  </style>
</head>
<body>
  <h1>RealDriver Feature Report</h1>
  <p>Overall: <b>{'PASS' if summary.get('overall_pass') else 'FAIL'}</b></p>
  <p>Passed: {summary.get('passed_count')}/{summary.get('feature_count')}</p>
  <table>
    <thead>
      <tr>
        <th>ID</th>
        <th>Name</th>
        <th>Status</th>
        <th>Video</th>
        <th>DriverScript</th>
        <th>検証観点</th>
        <th>Road KPI要約</th>
        <th>KPI Checks</th>
        <th>Missing Required</th>
        <th>Forbidden Hits</th>
      </tr>
    </thead>
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
