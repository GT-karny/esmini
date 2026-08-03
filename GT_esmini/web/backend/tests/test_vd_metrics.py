"""Tests for services/vd_metrics.py — the shared VD verification core (WEB-4).

Covers the pure math (interp, OBB SAT separation), the expectation matchers,
the verdict rollup, and compare() end-to-end against a synthetic OSI baseline."""

from __future__ import annotations

import json
import math
import struct

import pytest

from GT_esmini.web.backend.services import vd_metrics, vd_verify
from GT_esmini.web.backend.services.vd_metrics import (
    assert_expectations,
    compare,
    ego_track_from_osi,
    eval_must,
    interp,
    obb_separation,
    resolve_baseline_osi,
    time_window_ok,
)

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------


def _frame(t, speed, x=0.0, y=0.0, lane=-1, track=1, **ego_extra):
    ego = {"x": x, "y": y, "speed": speed, "lane": lane, "track": track, "h": 0.0}
    ego.update(ego_extra)
    return {"sim_time": t, "ego": ego}


def _rl_frame(t, **route_lane_overrides):
    """A frame carrying a "route_lane" block (RouteLanePlan telemetry -- see
    VirtualDriverTelemetryJson.cpp), defaulted to a clean/on-target state.
    Override individual fields per test, e.g. _rl_frame(0.0, diagnostic="invalid_route").
    """
    fr = _frame(t, 10.0)
    fr["route_lane"] = {
        "valid": True,
        "road_id": 0,
        "ego_lane": -4,
        "ego_lane_raw": -4,
        "target_lanes": [-4],
        "on_target_lane": True,
        "dist_to_connection": 50.0,
        "deviation_count": 0,
        "last_deviation_road_id": -1,
        "rerouted": False,
        "diagnostic": "",
        "reason": "",
    }
    fr["route_lane"].update(route_lane_overrides)
    return fr


def _write_telemetry(run_dir, frames):
    run_dir.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps(fr) for fr in frames]
    (run_dir / "telemetry.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_osi(path, osi_frames):
    """osi_frames: list of (t, [(obj_id, x, y, vx, vy)], host_id)."""
    from osi3.osi_groundtruth_pb2 import GroundTruth

    buf = b""
    for t, objs, host_id in osi_frames:
        gt = GroundTruth()
        gt.timestamp.seconds = int(t)
        gt.timestamp.nanos = int(round((t - int(t)) * 1e9))
        if host_id is not None:
            gt.host_vehicle_id.value = host_id
        for oid, x, y, vx, vy in objs:
            mo = gt.moving_object.add()
            mo.id.value = oid
            mo.base.position.x = x
            mo.base.position.y = y
            mo.base.velocity.x = vx
            mo.base.velocity.y = vy
        blob = gt.SerializeToString()
        buf += struct.pack("I", len(blob)) + blob
    path.write_bytes(buf)


# ---------------------------------------------------------------------------
# interp / time_window_ok / OBB
# ---------------------------------------------------------------------------


def test_interp_midpoint_and_clamping():
    track = [(0.0, 0.0, 0.0, 10.0), (2.0, 4.0, 2.0, 20.0)]
    assert interp(track, 1.0) == pytest.approx((2.0, 1.0, 15.0))
    assert interp(track, -5.0) == (0.0, 0.0, 10.0)  # clamp low
    assert interp(track, 99.0) == (4.0, 2.0, 20.0)  # clamp high


def test_time_window_ok_gates():
    spec = {"after": {"sim_time": 1.0}, "before": {"sim_time": 3.0}}
    assert not time_window_ok(0.5, spec)
    assert time_window_ok(1.0, spec)
    assert time_window_ok(3.0, spec)
    assert not time_window_ok(3.5, spec)
    assert time_window_ok(123.0, {})  # no gates -> always ok


def test_obb_separation_overlap_and_gap():
    ego = {"x": 0.0, "y": 0.0, "h": 0.0, "length": 4.0, "width": 2.0}
    overlapping = {"x": 1.0, "y": 0.0, "h": 0.0, "length": 4.0, "width": 2.0}
    assert obb_separation(ego, overlapping) == 0.0

    # Adjacent lane (3.5 m center offset, 2 m wide bodies): 1.5 m clear gap.
    adjacent = {"x": 0.0, "y": 3.5, "h": 0.0, "length": 4.0, "width": 2.0}
    assert obb_separation(ego, adjacent) == pytest.approx(1.5)

    # Rotated 90 deg ahead: half-length + half-width = 3.0 -> gap = 10 - 3 = 7.
    rotated = {"x": 10.0, "y": 0.0, "h": math.pi / 2, "length": 4.0, "width": 2.0}
    assert obb_separation(ego, rotated) == pytest.approx(7.0)


# ---------------------------------------------------------------------------
# matchers
# ---------------------------------------------------------------------------


def test_speed_above_pass_and_fail():
    frames = [_frame(0.0, 5.0), _frame(1.0, 12.0), _frame(2.0, 8.0)]
    assert (
        eval_must({"event": "speed_above", "threshold": 10.0}, frames)["status"]
        == "pass"
    )

    r = eval_must({"event": "speed_above", "threshold": 20.0}, frames)
    assert r["status"] == "fail"
    assert r["idx"] == 1  # closest attempt = max speed frame
    assert r["t"] == 1.0


def test_speed_below_fail_carries_first_offender():
    frames = [_frame(0.0, 5.0), _frame(1.0, 15.0), _frame(2.0, 16.0)]
    r = eval_must({"event": "speed_below", "threshold": 10.0}, frames)
    assert r["status"] == "fail"
    assert r["idx"] == 1


def test_speed_matcher_time_window_skip():
    frames = [_frame(0.0, 5.0)]
    r = eval_must(
        {"event": "speed_above", "threshold": 1.0, "after": {"sim_time": 10.0}}, frames
    )
    assert r["status"] == "skip"


def test_min_speed_above_with_road_filter():
    frames = [
        _frame(0.0, 10.0, track=1),
        _frame(1.0, 2.0, track=2),
        _frame(2.0, 9.0, track=1),
    ]
    # Only road 1 frames considered -> min 9.0 >= 5.0
    r = eval_must({"event": "min_speed_above", "threshold": 5.0, "road_id": 1}, frames)
    assert r["status"] == "pass"
    # All frames -> min 2.0 < 5.0
    r = eval_must({"event": "min_speed_above", "threshold": 5.0}, frames)
    assert r["status"] == "fail" and r["idx"] == 1


def test_lane_keep_and_lane_change_count():
    frames = [
        _frame(0.0, 10.0, lane=-1),
        _frame(1.0, 10.0, lane=-1),
        _frame(2.0, 10.0, lane=-2),
    ]
    r = eval_must({"event": "lane_keep", "road_id": 1, "lane_id": -1}, frames)
    assert r["status"] == "fail" and r["idx"] == 2

    r = eval_must({"event": "lane_change_count", "count": 1}, frames)
    assert r["status"] == "pass"
    r = eval_must({"event": "lane_change_count", "count": 0}, frames)
    assert r["status"] == "fail"


def test_lane_matchers_skip_without_lane_data():
    frames = [{"sim_time": 0.0, "ego": {"x": 0, "y": 0, "speed": 1.0}}]
    assert eval_must({"event": "lane_keep", "lane_id": -1}, frames)["status"] == "skip"
    assert (
        eval_must({"event": "lane_change_count", "count": 0}, frames)["status"]
        == "skip"
    )


def test_no_constraint_kind():
    frames = [
        {**_frame(0.0, 10.0), "midlong": {"constraints": [{"kind": "curve"}]}},
        {**_frame(1.0, 10.0), "midlong": {"constraints": [{"kind": "junction"}]}},
    ]
    r = eval_must({"event": "no_constraint_kind", "kind": "junction"}, frames)
    assert r["status"] == "fail" and r["idx"] == 1
    assert (
        eval_must({"event": "no_constraint_kind", "kind": "stopline"}, frames)["status"]
        == "pass"
    )
    assert eval_must({"event": "no_constraint_kind"}, frames)["status"] == "skip"


def test_min_obb_separation_fallback_dims_inconclusive():
    fr = _frame(0.0, 10.0)
    fr["scene"] = {
        "objects": [
            {
                "id": 0,
                "x": 0.0,
                "y": 0.0,
                "h": 0.0,
                "length": 5.0,
                "width": 2.0,
                "is_host": True,
            },
            {"id": 1, "x": 30.0, "y": 0.0, "h": 0.0},  # no dims -> fallback footprint
        ]
    }
    r = eval_must({"event": "min_obb_separation_above", "threshold": 0.3}, [fr])
    assert r["status"] == "skip"
    assert "inconclusive" in r["detail"]


def test_route_lane_plan_holds_missing_block_is_skip():
    # No "route_lane" key at all -- stale GT_esminiLib.dll (predates RouteLanePlan
    # telemetry) or the feature isn't wired into this run. Must not silently pass.
    r = eval_must(
        {"event": "route_lane_plan_holds", "expect_diagnostic": ""}, [_frame(0.0, 10.0)]
    )
    assert r["status"] == "skip"
    assert "route_lane" in r["detail"]


def test_route_lane_plan_holds_no_keys_named_is_skip():
    # A must entry naming NONE of the expect_*/min_deviations keys checks
    # nothing and must not report pass (reversed-detector guard).
    r = eval_must({"event": "route_lane_plan_holds"}, [_rl_frame(0.0)])
    assert r["status"] == "skip"
    assert "checks nothing" in r["detail"]


def test_route_lane_plan_holds_pass_all_checks():
    frames = [
        _rl_frame(0.0, deviation_count=0),
        _rl_frame(1.0, deviation_count=1),
    ]
    must = {
        "event": "route_lane_plan_holds",
        "expect_diagnostic": "",
        "expect_rerouted": False,
        "expect_target_lanes": [-4],
        "expect_on_target_lane": True,
        "min_deviations": 1,
    }
    r = eval_must(must, frames)
    assert r["status"] == "pass"


def test_route_lane_plan_holds_target_lanes_compares_sorted():
    r = eval_must(
        {"event": "route_lane_plan_holds", "expect_target_lanes": [-1, -4, -2]},
        [_rl_frame(0.0, target_lanes=[-2, -4, -1])],
    )
    assert r["status"] == "pass"


def test_route_lane_plan_holds_window_gates_frames():
    frames = [_rl_frame(0.0, diagnostic="invalid_route"), _rl_frame(5.0, diagnostic="")]
    r = eval_must(
        {
            "event": "route_lane_plan_holds",
            "expect_diagnostic": "",
            "window": [5.0, 5.0],
        },
        frames,
    )
    assert r["status"] == "pass"


# The remaining tests each deliberately construct VIOLATING telemetry -- proof
# the matcher actually fires "fail" rather than merely never having been
# exercised (project discipline: a reversed/no-op detector is worse than none).


def test_route_lane_plan_holds_fail_diagnostic_mismatch():
    r = eval_must(
        {"event": "route_lane_plan_holds", "expect_diagnostic": ""},
        [_rl_frame(0.0, diagnostic="invalid_route")],
    )
    assert r["status"] == "fail"
    assert r["idx"] == 0


def test_route_lane_plan_holds_fail_rerouted_mismatch():
    r = eval_must(
        {"event": "route_lane_plan_holds", "expect_rerouted": False},
        [_rl_frame(0.0, rerouted=True)],
    )
    assert r["status"] == "fail"


def test_route_lane_plan_holds_fail_target_lanes_never_observed():
    r = eval_must(
        {"event": "route_lane_plan_holds", "expect_target_lanes": [-4]},
        [_rl_frame(0.0, target_lanes=[-1, -2])],
    )
    assert r["status"] == "fail"


def test_route_lane_plan_holds_fail_on_target_lane_never_observed():
    r = eval_must(
        {"event": "route_lane_plan_holds", "expect_on_target_lane": False},
        [_rl_frame(0.0, on_target_lane=True)],
    )
    assert r["status"] == "fail"


def test_route_lane_plan_holds_fail_min_deviations_not_met():
    r = eval_must(
        {"event": "route_lane_plan_holds", "min_deviations": 1},
        [_rl_frame(0.0, deviation_count=0)],
    )
    assert r["status"] == "fail"


def test_unknown_event_skips():
    r = eval_must({"event": "does_not_exist"}, [_frame(0.0, 1.0)])
    assert r["status"] == "skip"
    assert r["detail"] == "unknown event type"


# ---------------------------------------------------------------------------
# assert_expectations (verdict rollup + verdict.json)
# ---------------------------------------------------------------------------


def _run_assert(tmp_path, frames, musts):
    run_dir = tmp_path / "run"
    _write_telemetry(run_dir, frames)
    exp = tmp_path / "expectations.yaml"
    import yaml

    exp.write_text(
        yaml.dump({"scenario": "synthetic", "must": musts}), encoding="utf-8"
    )
    return run_dir, assert_expectations(run_dir, exp)


def test_assert_overall_pass_writes_verdict(tmp_path):
    frames = [_frame(0.0, 5.0), _frame(1.0, 12.0)]
    run_dir, verdict = _run_assert(
        tmp_path, frames, [{"event": "speed_above", "threshold": 10.0}]
    )
    assert verdict["overall"] == "pass"
    assert verdict["summary"] == {"pass": 1, "fail": 0, "skip": 0}
    on_disk = json.loads((run_dir / "verdict.json").read_text(encoding="utf-8"))
    assert on_disk["overall"] == "pass"
    assert on_disk["scenario"] == "synthetic"


def test_assert_overall_fail_beats_skip(tmp_path):
    frames = [_frame(0.0, 5.0)]
    _, verdict = _run_assert(
        tmp_path,
        frames,
        [
            {"event": "speed_above", "threshold": 10.0},  # fail (max 5 < 10)
            {"event": "unknown_matcher"},  # skip
        ],
    )
    assert verdict["overall"] == "fail"
    assert verdict["summary"]["fail"] == 1 and verdict["summary"]["skip"] == 1


def test_assert_skip_rolls_up_to_needs_review(tmp_path):
    frames = [_frame(0.0, 15.0)]
    _, verdict = _run_assert(
        tmp_path,
        frames,
        [
            {"event": "speed_above", "threshold": 10.0},  # pass
            {"event": "unknown_matcher"},  # skip
        ],
    )
    assert verdict["overall"] == "needs-review"


# ---------------------------------------------------------------------------
# feature:F7 — a scenario evaluated against NOTHING must not come back green.
#
# The verdict chain used to end in a literal "pass", reached whenever
# n_pass == n_fail == n_skip == 0. Every way of ending up with no matchers hit
# it, and each of them means "this scenario was never checked":
#   * no `must:` key at all
#   * `must:` misspelled, so .get("must") misses it
#   * `must: []`
#
# Same defect class as the 2026-07-27 gate incident (22 scenarios dead on
# WinError 10013, gate still printed PASS) -- fixed there at the batch level,
# still live here per scenario until now.
# ---------------------------------------------------------------------------


def _run_assert_raw(tmp_path, frames, spec: dict):
    """Like _run_assert but writes the expectations mapping verbatim, so a
    MISSING or MISSPELLED `must:` key can be exercised."""
    run_dir = tmp_path / "run"
    _write_telemetry(run_dir, frames)
    exp = tmp_path / "expectations.yaml"
    import yaml

    exp.write_text(yaml.dump(spec), encoding="utf-8")
    return run_dir, assert_expectations(run_dir, exp)


def test_assert_empty_must_list_is_not_a_pass(tmp_path):
    _, verdict = _run_assert(tmp_path, [_frame(0.0, 15.0)], [])
    assert verdict["overall"] == "needs-review", (
        "an expectations file with no matchers checked nothing -- calling that a "
        "pass is the 'zero evaluations = green' bug"
    )
    assert verdict["summary"] == {"pass": 0, "fail": 0, "skip": 0}


def test_assert_missing_must_key_is_not_a_pass(tmp_path):
    _, verdict = _run_assert_raw(
        tmp_path, [_frame(0.0, 15.0)], {"scenario": "synthetic"}
    )
    assert verdict["overall"] == "needs-review"


def test_assert_misspelled_must_key_is_not_a_pass(tmp_path):
    # The dangerous one: the file LOOKS like it asserts something, and a human
    # reviewing it would read the matchers as active. .get("must") does not.
    _, verdict = _run_assert_raw(
        tmp_path,
        [_frame(0.0, 1.0)],
        {
            "scenario": "synthetic",
            "musts": [{"event": "speed_above", "threshold": 10.0}],
        },
    )
    assert verdict["overall"] == "needs-review", (
        "a misspelled `must:` key silently evaluates nothing; it must not be "
        "reported as a pass"
    )


def test_assert_real_matcher_still_passes(tmp_path):
    # Guard the guard: the fix must not turn genuine passes into needs-review.
    _, verdict = _run_assert(
        tmp_path, [_frame(0.0, 15.0)], [{"event": "speed_above", "threshold": 10.0}]
    )
    assert verdict["overall"] == "pass"


# ---------------------------------------------------------------------------
# compare (telemetry vs synthetic OSI baseline)
# ---------------------------------------------------------------------------


def test_compare_identical_tracks_zero_rmse(tmp_path):
    run_dir = tmp_path / "run"
    frames = [_frame(t / 2.0, 10.0, x=5.0 * t, y=0.0) for t in range(11)]  # 0..5 s
    _write_telemetry(run_dir, frames)

    osi = tmp_path / "baseline" / "groundtruth.osi"
    osi.parent.mkdir(parents=True)
    _write_osi(osi, [(t / 2.0, [(7, 5.0 * t, 0.0, 10.0, 0.0)], 7) for t in range(11)])

    result = compare(run_dir, tmp_path / "baseline")
    assert result["xy_rmse_m"] == pytest.approx(0.0, abs=1e-6)
    assert result["speed_rmse_mps"] == pytest.approx(0.0, abs=1e-6)
    assert result["endpoint_dist_m"] == pytest.approx(0.0, abs=1e-6)
    assert result["overlap_s"] == pytest.approx(5.0)
    # artifacts written for the replay UI
    assert (run_dir / "compare.json").is_file()
    ghost = json.loads((run_dir / "baseline_track.json").read_text(encoding="utf-8"))
    assert len(ghost) == len(frames)
    assert ghost[0].keys() == {"t", "x", "y", "speed"}


def test_compare_constant_offset_reports_rmse(tmp_path):
    run_dir = tmp_path / "run"
    frames = [_frame(float(t), 10.0, x=10.0 * t, y=2.0) for t in range(6)]
    _write_telemetry(run_dir, frames)
    osi = tmp_path / "base.osi"
    _write_osi(osi, [(float(t), [(1, 10.0 * t, 0.0, 10.0, 0.0)], 1) for t in range(6)])

    result = compare(run_dir, osi)  # baseline as direct .osi file
    assert result["xy_rmse_m"] == pytest.approx(2.0, abs=1e-3)
    assert result["xy_max_dev_m"] == pytest.approx(2.0, abs=1e-3)
    assert result["endpoint_dist_m"] == pytest.approx(2.0, abs=1e-3)


def test_compare_no_overlap_raises(tmp_path):
    run_dir = tmp_path / "run"
    _write_telemetry(run_dir, [_frame(0.0, 1.0), _frame(1.0, 1.0)])
    osi = tmp_path / "base.osi"
    _write_osi(
        osi,
        [(10.0, [(1, 0.0, 0.0, 0.0, 0.0)], 1), (11.0, [(1, 0.0, 0.0, 0.0, 0.0)], 1)],
    )
    with pytest.raises(RuntimeError, match="no overlapping"):
        compare(run_dir, osi)


def test_ego_track_from_osi_honours_host_vehicle_id(tmp_path):
    osi = tmp_path / "t.osi"
    # Two objects; host is id=9 (second in the list) -> its track must be used.
    _write_osi(osi, [(0.0, [(1, 111.0, 0.0, 0.0, 0.0), (9, 5.0, 6.0, 3.0, 4.0)], 9)])
    track = ego_track_from_osi(osi)
    assert len(track) == 1
    t, x, y, speed = track[0]
    assert (x, y) == (5.0, 6.0)
    assert speed == pytest.approx(5.0)  # |(3,4)|


def test_resolve_baseline_osi_errors(tmp_path):
    with pytest.raises(FileNotFoundError):
        resolve_baseline_osi(tmp_path / "nope")


# ---------------------------------------------------------------------------
# facade stability (api/verification.py contract)
# ---------------------------------------------------------------------------


def test_vd_verify_facade_reexports_shared_core():
    assert vd_verify.compare is vd_metrics.compare
    assert vd_verify.assert_run is vd_metrics.assert_expectations
    assert callable(vd_verify.generate_baseline)
