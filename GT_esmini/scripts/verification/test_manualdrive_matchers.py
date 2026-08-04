"""Both-polarity unit tests for the ManualDrive ADAS matchers (phase A,
req-vd-ad:REQ-AD-025): manual_aeb_fires, no_intervention_in_window,
brake_not_stacked, fcw_leads_intervention, adas_state_matches.

This module IS the red-proof asset for these five matchers. Per
docs/virtualdriver/design/manualdrive_adas_verification_plan.md §4-1, a new
matcher needs a red-proof asset before it can go on a standing gate; an E2E
red is impractical here because the ManualDrive/HVD producing side is not in
the DLL yet (a parallel, in-flight change), so this file is that red proof at
the unit level -- the same allowance the plan documents for
`parking_reverse_gear_matches_segment`.

Every GREEN case has a RED sibling that perturbs exactly one thing, and every
RED assertion checks the returned `detail` string for the specific quantity
that failed (never just `status == "fail"`) -- a checker that has only ever
seen data it accepts is not a checker.

The vacuous-pass guard (manualdrive_adas_verification_plan.md §4-1 discipline,
carried into these five branches in vd_metrics.py) gets its own tests per
matcher: empty window, `function` absent from hvd.adas, and (for the negative
matcher) `function` reported but UNAVAILABLE throughout must all `skip`, never
`pass`. A dedicated test also pins that a MISSING `detail` key is never read
as a fabricated 0.0.

Run:
    DriverScript/.venv/Scripts/python.exe -m pytest \
        GT_esmini/scripts/verification/test_manualdrive_matchers.py -v
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(
    0, str(REPO_ROOT / "GT_esmini" / "web" / "backend" / "services")
)  # vd_metrics, imported flat -- same sys.path convention gt_sim_test.py and
# scripts/score_aeb_c2c_grid.py already use for this config-free module.

from vd_metrics import eval_must  # noqa: E402

# ---------------------------------------------------------------------------
# frame builders
# ---------------------------------------------------------------------------


def _adas_rec(state_name: str, detail: dict | None = None) -> dict:
    """One hvd.adas[function] record, per the contract in vd_metrics.py's
    _hvd_adas_record docstring: state_name plus a detail dict of STRING
    values (the OSI custom_detail KeyValuePair contract)."""
    return {
        "name": 7,
        "state": 6,
        "state_name": state_name,
        "detail": dict(detail) if detail is not None else {},
        "driver_override": {"active": False, "reasons": []},
    }


def _frame(
    t: float, adas: dict[str, dict] | None = None, *, no_hvd: bool = False
) -> dict:
    """One ManualDrive telemetry frame. adas maps custom_name -> _adas_rec(...).
    no_hvd=True omits the "hvd" key entirely (simulates a non-ManualDrive /
    stale-DLL frame) for the "function never reported" vacuous case."""
    fr = {
        "sim_time": t,
        "controller": "ManualDrive",
        "ego": {"x": 0.0, "y": 0.0, "h": 0.0, "speed": 0.0},
        "ego_source": "osi_scene",
    }
    if not no_hvd:
        fr["hvd"] = {
            "inputs": {"throttle": 0.0, "brake": 0.0, "steering": 0.0, "gear": 1},
            "adas": dict(adas) if adas is not None else {},
        }
    return fr


def _aeb_detail(
    *, ttc="1.842", driver_brake="0.000", brake_out="0.000", brake_request="0.000"
):
    return {
        "gt.aeb.ttc_s": ttc,
        "gt.aeb.warning": "true",
        "gt.aeb.driver_brake": driver_brake,
        "gt.aeb.brake_out": brake_out,
        "gt.aeb.brake_request": brake_request,
        "gt.aeb.suppressed": "false",
        "gt.aeb.kickdown": "false",
        "gt.aeb.decel_request_mps2": "5.100",
    }


# ---------------------------------------------------------------------------
# manual_aeb_fires
# ---------------------------------------------------------------------------


def test_manual_aeb_fires_green_when_active_frame_present():
    frames = [
        _frame(0.0, {"gt.aeb": _adas_rec("standby")}),
        _frame(0.5, {"gt.aeb": _adas_rec("active", _aeb_detail())}),
        _frame(1.0, {"gt.aeb": _adas_rec("standby")}),
    ]
    r = eval_must({"event": "manual_aeb_fires"}, frames)
    assert r["status"] == "pass"
    assert "ACTIVE" in r["detail"]


def test_manual_aeb_fires_red_when_reported_but_never_active():
    frames = [
        _frame(0.0, {"gt.aeb": _adas_rec("standby")}),
        _frame(0.5, {"gt.aeb": _adas_rec("standby")}),
    ]
    r = eval_must({"event": "manual_aeb_fires"}, frames)
    assert r["status"] == "fail"
    assert "never ACTIVE" in r["detail"]
    assert "standby" in r["detail"]


def test_manual_aeb_fires_skip_on_empty_window():
    frames = [_frame(0.0, {"gt.aeb": _adas_rec("active", _aeb_detail())})]
    r = eval_must({"event": "manual_aeb_fires", "after": {"sim_time": 100.0}}, frames)
    assert r["status"] == "skip"


def test_manual_aeb_fires_skip_when_function_never_reported():
    frames = [_frame(0.0), _frame(0.5, no_hvd=True)]
    r = eval_must({"event": "manual_aeb_fires"}, frames)
    assert r["status"] == "skip"
    assert "never reported" in r["detail"]


def test_manual_aeb_fires_skip_below_min_frames():
    frames = [_frame(0.0, {"gt.aeb": _adas_rec("active", _aeb_detail())})]
    r = eval_must({"event": "manual_aeb_fires", "min_frames": 5}, frames)
    assert r["status"] == "skip"


def test_manual_aeb_fires_custom_function_param():
    frames = [_frame(0.0, {"gt.custom_aeb": _adas_rec("active", {})})]
    r = eval_must({"event": "manual_aeb_fires", "function": "gt.custom_aeb"}, frames)
    assert r["status"] == "pass"


# ---------------------------------------------------------------------------
# no_intervention_in_window (negative)
# ---------------------------------------------------------------------------


def test_no_intervention_green_when_standby_throughout():
    frames = [
        _frame(0.0, {"gt.aeb": _adas_rec("standby")}),
        _frame(0.5, {"gt.aeb": _adas_rec("standby")}),
    ]
    r = eval_must({"event": "no_intervention_in_window"}, frames)
    assert r["status"] == "pass"
    assert "no misfire" in r["detail"]


def test_no_intervention_red_when_active_frame_present():
    frames = [
        _frame(0.0, {"gt.aeb": _adas_rec("standby")}),
        _frame(0.5, {"gt.aeb": _adas_rec("active", _aeb_detail())}),
    ]
    r = eval_must({"event": "no_intervention_in_window"}, frames)
    assert r["status"] == "fail"
    assert "misfire" in r["detail"]
    assert "ACTIVE" in r["detail"]


def test_no_intervention_skip_on_empty_window():
    frames = [_frame(0.0, {"gt.aeb": _adas_rec("standby")})]
    r = eval_must(
        {"event": "no_intervention_in_window", "after": {"sim_time": 100.0}}, frames
    )
    assert r["status"] == "skip"


def test_no_intervention_skip_when_function_never_reported():
    frames = [_frame(0.0, no_hvd=True)]
    r = eval_must({"event": "no_intervention_in_window"}, frames)
    assert r["status"] == "skip"
    assert "never reported" in r["detail"]


def test_no_intervention_skip_when_unavailable_throughout():
    """The dangerous vacuous case this matcher is specifically guarded
    against: a function switched OFF produces zero ACTIVE frames too, but
    that is not evidence the safety logic correctly declined to intervene
    (REQ-AD-028 STANDBY vs UNAVAILABLE)."""
    frames = [
        _frame(0.0, {"gt.aeb": _adas_rec("unavailable")}),
        _frame(0.5, {"gt.aeb": _adas_rec("unavailable")}),
    ]
    r = eval_must({"event": "no_intervention_in_window"}, frames)
    assert r["status"] == "skip"
    assert "UNAVAILABLE" in r["detail"]


def test_no_intervention_skip_below_min_frames():
    frames = [_frame(0.0, {"gt.aeb": _adas_rec("standby")})]
    r = eval_must({"event": "no_intervention_in_window", "min_frames": 5}, frames)
    assert r["status"] == "skip"


# ---------------------------------------------------------------------------
# brake_not_stacked
# ---------------------------------------------------------------------------


def test_brake_not_stacked_green_when_output_equals_driver_value():
    frames = [
        _frame(
            0.0,
            {
                "gt.aeb": _adas_rec(
                    "active",
                    _aeb_detail(
                        driver_brake="0.600", brake_request="0.420", brake_out="0.600"
                    ),
                )
            },
        ),
    ]
    r = eval_must({"event": "brake_not_stacked"}, frames)
    assert r["status"] == "pass"
    assert "max-composed" in r["detail"]


def test_brake_not_stacked_red_when_output_exceeds_driver_value():
    frames = [
        _frame(
            0.0,
            {
                "gt.aeb": _adas_rec(
                    "active",
                    _aeb_detail(
                        driver_brake="0.600", brake_request="0.420", brake_out="0.900"
                    ),
                )
            },
        ),
    ]
    r = eval_must({"event": "brake_not_stacked"}, frames)
    assert r["status"] == "fail"
    assert "brake_out=0.900" in r["detail"]
    assert "driver_brake=0.600" in r["detail"]
    assert "stacked" in r["detail"]


def test_brake_not_stacked_skip_on_empty_window():
    frames = [
        _frame(
            0.0,
            {
                "gt.aeb": _adas_rec(
                    "active",
                    _aeb_detail(
                        driver_brake="0.600", brake_request="0.420", brake_out="0.600"
                    ),
                )
            },
        )
    ]
    r = eval_must({"event": "brake_not_stacked", "after": {"sim_time": 100.0}}, frames)
    assert r["status"] == "skip"


def test_brake_not_stacked_skip_when_function_never_reported():
    frames = [_frame(0.0, no_hvd=True)]
    r = eval_must({"event": "brake_not_stacked"}, frames)
    assert r["status"] == "skip"
    assert "never reported" in r["detail"]


def test_brake_not_stacked_skip_when_never_active():
    frames = [_frame(0.0, {"gt.aeb": _adas_rec("standby")})]
    r = eval_must({"event": "brake_not_stacked"}, frames)
    assert r["status"] == "skip"
    assert "nothing to check" in r["detail"]


def test_brake_not_stacked_skip_when_human_never_out_brakes_request():
    # ACTIVE, but driver_brake < brake_request the whole time -- the
    # precondition for this claim never holds, so there is nothing to check.
    frames = [
        _frame(
            0.0,
            {
                "gt.aeb": _adas_rec(
                    "active",
                    _aeb_detail(
                        driver_brake="0.100", brake_request="0.420", brake_out="0.420"
                    ),
                )
            },
        )
    ]
    r = eval_must({"event": "brake_not_stacked"}, frames)
    assert r["status"] == "skip"
    assert "nothing to evaluate" in r["detail"]


def test_brake_not_stacked_missing_detail_key_is_not_read_as_zero():
    """The load-bearing regression guard: gt.aeb.driver_brake is ABSENT from
    detail (not "0.000" -- genuinely missing). If the matcher fell back to
    0.0, driver_brake(0.0) >= brake_request(0.420) would be False and this
    would (by accident) also skip/pass for the WRONG reason; assert instead
    that it is excluded for the RIGHT reason (an unparsed/missing key), by
    checking a frame that would otherwise be eligible (brake_request is low
    enough that a fabricated 0.0 could even satisfy driver_brake>=request if
    the guard used >= 0)."""
    detail = _aeb_detail(brake_request="0.000", brake_out="0.500")
    del detail["gt.aeb.driver_brake"]  # simulate a genuinely missing key
    frames = [_frame(0.0, {"gt.aeb": _adas_rec("active", detail)})]
    r = eval_must({"event": "brake_not_stacked"}, frames)
    # Must not silently compute on driver_brake=0.0 (which would make
    # 0.0 >= 0.000 true and then fail/pass on a fabricated comparison);
    # the only correct outcome is "nothing to evaluate".
    assert r["status"] == "skip"
    assert "nothing to evaluate" in r["detail"]


# ---------------------------------------------------------------------------
# fcw_leads_intervention
# ---------------------------------------------------------------------------


def test_fcw_leads_intervention_green_when_lead_meets_threshold():
    frames = [
        _frame(0.0, {"gt.fcw": _adas_rec("standby"), "gt.aeb": _adas_rec("standby")}),
        _frame(1.0, {"gt.fcw": _adas_rec("active"), "gt.aeb": _adas_rec("standby")}),
        _frame(2.0, {"gt.fcw": _adas_rec("active"), "gt.aeb": _adas_rec("active")}),
    ]
    r = eval_must({"event": "fcw_leads_intervention", "min_lead_s": 0.8}, frames)
    assert r["status"] == "pass"
    assert "measured lead = 1.000s" in r["detail"]


def test_fcw_leads_intervention_red_when_lead_below_threshold():
    # Both go active at the SAME frame -- lead == 0, below the 0.8s floor
    # (the design's "warning threshold set equal to intervention threshold"
    # red-proof config from the verification plan §4-2).
    frames = [
        _frame(0.0, {"gt.fcw": _adas_rec("standby"), "gt.aeb": _adas_rec("standby")}),
        _frame(1.0, {"gt.fcw": _adas_rec("active"), "gt.aeb": _adas_rec("active")}),
    ]
    r = eval_must({"event": "fcw_leads_intervention", "min_lead_s": 0.8}, frames)
    assert r["status"] == "fail"
    assert "measured lead = 0.000s" in r["detail"]


def test_fcw_leads_intervention_red_when_warning_never_fires_but_aeb_does():
    """A genuine defect distinct from "lead too short": the intervention
    fired with NO warning at all. Must fail for that reason specifically,
    not be conflated with the lead-too-short case above."""
    frames = [
        _frame(0.0, {"gt.fcw": _adas_rec("standby"), "gt.aeb": _adas_rec("standby")}),
        _frame(1.0, {"gt.fcw": _adas_rec("standby"), "gt.aeb": _adas_rec("active")}),
    ]
    r = eval_must({"event": "fcw_leads_intervention", "min_lead_s": 0.8}, frames)
    assert r["status"] == "fail"
    assert "never went ACTIVE" in r["detail"]
    assert "gt.fcw" in r["detail"]


def test_fcw_leads_intervention_skip_when_intervention_never_fires():
    # Warning-only episode (md-fcw-warning-only-episode): nothing to measure
    # a lead against. This is a legitimate "no incident" case, not a defect.
    frames = [
        _frame(0.0, {"gt.fcw": _adas_rec("standby"), "gt.aeb": _adas_rec("standby")}),
        _frame(1.0, {"gt.fcw": _adas_rec("active"), "gt.aeb": _adas_rec("standby")}),
    ]
    r = eval_must({"event": "fcw_leads_intervention", "min_lead_s": 0.8}, frames)
    assert r["status"] == "skip"
    assert "no intervention" in r["detail"]


def test_fcw_leads_intervention_skip_without_min_lead_s():
    frames = [
        _frame(0.0, {"gt.fcw": _adas_rec("active"), "gt.aeb": _adas_rec("active")})
    ]
    r = eval_must({"event": "fcw_leads_intervention"}, frames)
    assert r["status"] == "skip"
    assert "min_lead_s" in r["detail"]


def test_fcw_leads_intervention_skip_on_empty_window():
    frames = [
        _frame(0.0, {"gt.fcw": _adas_rec("active"), "gt.aeb": _adas_rec("active")})
    ]
    r = eval_must(
        {
            "event": "fcw_leads_intervention",
            "min_lead_s": 0.8,
            "after": {"sim_time": 100.0},
        },
        frames,
    )
    assert r["status"] == "skip"


def test_fcw_leads_intervention_skip_when_function_never_reported():
    frames = [_frame(0.0, no_hvd=True)]
    r = eval_must({"event": "fcw_leads_intervention", "min_lead_s": 0.8}, frames)
    assert r["status"] == "skip"
    assert "never reported" in r["detail"]


# ---------------------------------------------------------------------------
# adas_state_matches
# ---------------------------------------------------------------------------


def test_adas_state_matches_green_mode_all():
    frames = [
        _frame(0.0, {"gt.acc": _adas_rec("active")}),
        _frame(0.5, {"gt.acc": _adas_rec("active")}),
    ]
    r = eval_must(
        {"event": "adas_state_matches", "function": "gt.acc", "expect": "active"},
        frames,
    )
    assert r["status"] == "pass"


def test_adas_state_matches_red_mode_all_when_one_frame_disagrees():
    frames = [
        _frame(0.0, {"gt.acc": _adas_rec("active")}),
        _frame(0.5, {"gt.acc": _adas_rec("standby")}),
    ]
    r = eval_must(
        {"event": "adas_state_matches", "function": "gt.acc", "expect": "active"},
        frames,
    )
    assert r["status"] == "fail"
    assert "'standby'" in r["detail"]
    assert "want 'active'" in r["detail"]


def test_adas_state_matches_green_mode_any():
    frames = [
        _frame(0.0, {"gt.acc": _adas_rec("standby")}),
        _frame(0.5, {"gt.acc": _adas_rec("active")}),
    ]
    r = eval_must(
        {
            "event": "adas_state_matches",
            "function": "gt.acc",
            "expect": "active",
            "mode": "any",
        },
        frames,
    )
    assert r["status"] == "pass"


def test_adas_state_matches_red_mode_any_when_never_observed():
    frames = [
        _frame(0.0, {"gt.acc": _adas_rec("standby")}),
        _frame(0.5, {"gt.acc": _adas_rec("standby")}),
    ]
    r = eval_must(
        {
            "event": "adas_state_matches",
            "function": "gt.acc",
            "expect": "active",
            "mode": "any",
        },
        frames,
    )
    assert r["status"] == "fail"
    assert "'standby'" in r["detail"]


def test_adas_state_matches_skip_without_function_or_expect():
    frames = [_frame(0.0, {"gt.acc": _adas_rec("active")})]
    r = eval_must({"event": "adas_state_matches", "function": "gt.acc"}, frames)
    assert r["status"] == "skip"


def test_adas_state_matches_skip_on_empty_window():
    frames = [_frame(0.0, {"gt.acc": _adas_rec("active")})]
    r = eval_must(
        {
            "event": "adas_state_matches",
            "function": "gt.acc",
            "expect": "active",
            "after": {"sim_time": 100.0},
        },
        frames,
    )
    assert r["status"] == "skip"


def test_adas_state_matches_skip_when_function_never_reported():
    frames = [_frame(0.0, no_hvd=True)]
    r = eval_must(
        {"event": "adas_state_matches", "function": "gt.acc", "expect": "active"},
        frames,
    )
    assert r["status"] == "skip"
    assert "never reported" in r["detail"]


def test_adas_state_matches_skip_on_invalid_expect_token():
    frames = [_frame(0.0, {"gt.acc": _adas_rec("active")})]
    r = eval_must(
        {"event": "adas_state_matches", "function": "gt.acc", "expect": "on"}, frames
    )
    assert r["status"] == "skip"


# config-off red proof (verification plan §4-2, manual_aeb_fires row): the
# same asset run with the function config'd OFF reports UNAVAILABLE instead
# of ACTIVE -- adas_state_matches(expect="active") must catch that as a
# distinct red case from "reported and standby".
def test_adas_state_matches_red_catches_config_off_regression():
    frames = [_frame(0.0, {"gt.aeb": _adas_rec("unavailable")})]
    r = eval_must(
        {"event": "adas_state_matches", "function": "gt.aeb", "expect": "active"},
        frames,
    )
    assert r["status"] == "fail"
    assert "'unavailable'" in r["detail"]
