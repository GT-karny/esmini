#!/usr/bin/env python3
import argparse
import html
import json
from pathlib import Path


def esc(text: str) -> str:
    return html.escape(text, quote=True)


def render_list_cell(items: object) -> str:
    values = list(items) if isinstance(items, list) else []
    if not values:
        return "-"
    return "<br/>".join([f"- {esc(str(v))}" for v in values])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True)
    args = parser.parse_args()

    run_dir = Path(args.run_dir)
    summary_path = run_dir / "summary.json"
    if not summary_path.exists():
        raise FileNotFoundError(f"summary.json not found: {summary_path}")

    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    video_cfg = summary.get("video_config", {}) or {}
    video_cfg_text = ""
    if video_cfg.get("enabled"):
        video_cfg_text = (
            f"Video mode: {video_cfg.get('mode', '-')}, "
            f"encoder: {video_cfg.get('encoder_resolved', video_cfg.get('encoder_requested', '-'))}, "
            f"fps: {video_cfg.get('output_fps', '-')}, "
            f"jobs: {video_cfg.get('parallel_jobs', '-')}, "
            f"policy: {video_cfg.get('generate_for', '-')}"
        )

    rows = []
    for r in summary.get("results", []):
        status = "PASS" if r.get("pass") else "FAIL"
        miss = ", ".join(r.get("missing_required_patterns", [])) or "-"
        forb = ", ".join(r.get("hit_forbidden_patterns", [])) or "-"
        kpi_checks = r.get("kpi_checks", {}) or {}
        kpi_check_details = kpi_checks.get("details", []) or []
        kpi_checks_any = r.get("kpi_checks_any", {}) or {}
        kpi_check_any_details = kpi_checks_any.get("details", []) or []
        validation_points = r.get("validation_points", [])
        validation_goal = str(r.get("validation_goal", "") or "")
        failure_reasons = r.get("failure_reasons", [])
        trace_integrity = bool(r.get("trace_integrity", True))
        trace_stats = r.get("trace_stats", {}) or {}
        trace_mismatch = r.get("trace_mismatch_samples", []) or []
        light_mapping_integrity = bool(r.get("light_mapping_integrity", True))
        light_mapping_stats = r.get("light_mapping_stats", {}) or {}
        light_mapping_mismatch = r.get("light_mapping_mismatch_samples", []) or []
        autolight_integrity = bool(r.get("autolight_integrity", True))
        autolight_stats = r.get("autolight_stats", {}) or {}
        autolight_mismatch = r.get("autolight_mismatch_samples", []) or []
        points_text = render_list_cell(validation_points)
        expected_behavior = r.get("expected_behavior_nl") or validation_points
        judgement_criteria = r.get("judgement_criteria_nl", [])
        expected_behavior_text = render_list_cell(expected_behavior)
        judgement_criteria_text = render_list_cell(judgement_criteria)

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
        kpi_any_failed = [d for d in kpi_check_any_details if not d.get("pass", True)]

        kpi_text_parts = []
        if kpi_failed:
            kpi_text_parts.append("AND checks:")
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

        if kpi_check_any_details:
            any_status = "PASS" if kpi_checks_any.get("pass", False) else "FAIL"
            kpi_text_parts.append(f"ANY checks: {any_status}")
            for d in kpi_any_failed:
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

        kpi_text = "<br/>".join(esc(x) for x in kpi_text_parts) if kpi_text_parts else "PASS"
        trace_text = "-"
        if r.get("id") == "F01":
            if trace_integrity:
                trace_text = "PASS"
            else:
                trace_text = "FAIL"
            if trace_stats:
                trace_text += "<br/>" + esc(
                    f"cpp={trace_stats.get('cpp_to_py_count', '-')}, py={trace_stats.get('py_to_cpp_count', '-')}, script={trace_stats.get('python_trace_count', '-')}"
                )
            if trace_mismatch:
                trace_text += "<br/>" + "<br/>".join(esc(str(x)) for x in trace_mismatch[:3])
        light_mapping_text = "-"
        if r.get("id") == "F02":
            light_mapping_text = "PASS" if light_mapping_integrity else "FAIL"
            if light_mapping_stats:
                light_mapping_text += "<br/>" + esc(
                    f"match={light_mapping_stats.get('match_count', '-')}/{light_mapping_stats.get('total_samples', '-')}"
                )
            if light_mapping_mismatch:
                light_mapping_text += "<br/>" + "<br/>".join(esc(str(x)) for x in light_mapping_mismatch[:3])
        autolight_text = "-"
        if r.get("id") == "F03":
            autolight_text = "PASS" if autolight_integrity else "FAIL"
            if autolight_stats:
                autolight_text += "<br/>" + esc(
                    f"lane_changes={autolight_stats.get('lane_change_events', '-')}, reverse_samples={autolight_stats.get('reverse_window_samples', '-')}"
                )
            if autolight_mismatch:
                autolight_text += "<br/>" + "<br/>".join(esc(str(x)) for x in autolight_mismatch[:3])

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
            f"<td>{esc(validation_goal) if validation_goal else '-'}</td>"
            f"<td>{expected_behavior_text}</td>"
            f"<td>{judgement_criteria_text}</td>"
            f"<td>{points_text}</td>"
            f"<td>{road_summary}</td>"
            f"<td>{trace_text}</td>"
            f"<td>{light_mapping_text}</td>"
            f"<td>{autolight_text}</td>"
            f"<td>{kpi_text}</td>"
            f"<td>{render_list_cell(failure_reasons)}</td>"
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
  <p>{esc(video_cfg_text) if video_cfg_text else ''}</p>
  <table>
    <thead>
      <tr>
        <th>ID</th>
        <th>Name</th>
        <th>Status</th>
        <th>Video</th>
        <th>DriverScript</th>
        <th>Validation Goal</th>
        <th>期待挙動（自然言語）</th>
        <th>判定基準（自然言語+数値）</th>
        <th>検証観点</th>
        <th>Road KPI要約</th>
        <th>Trace Integrity</th>
        <th>Light Mapping Integrity</th>
        <th>AutoLight Integrity</th>
        <th>KPI Checks</th>
        <th>Failure Reasons</th>
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
