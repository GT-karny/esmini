"""Both-polarity unit tests for the ManualDrive ADAS matchers.

  phase A (req-vd-ad:REQ-AD-025): manual_aeb_fires, no_intervention_in_window,
      brake_not_stacked, fcw_leads_intervention, adas_state_matches
  phase B (req-vd-ad:REQ-AD-028 step b): driver_override_reported

This module IS the red-proof asset for these six matchers. Per
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


def _adas_rec(
    state_name: str,
    detail: dict | None = None,
    *,
    override: dict | None = None,
    custom_state: str = "",
) -> dict:
    """One hvd.adas[function] record, per the contract in vd_metrics.py's
    _hvd_adas_record docstring: state_name plus a detail dict of STRING
    values (the OSI custom_detail KeyValuePair contract).

    `override` defaults to the "channel never written" shape
    (present=False) -- what a phase-A DLL, a switched-off function, or a
    non-ManualDrive controller all produce. Phase-B override tests pass an
    explicit dict; see _override() below."""
    return {
        "name": 7,
        "state": 6,
        "state_name": state_name,
        "detail": dict(detail) if detail is not None else {},
        "driver_override": (
            dict(override)
            if override is not None
            else {"present": False, "active": False, "reasons": []}
        ),
        "custom_state": custom_state,
    }


def _override(present: bool, active: bool = False, reasons: list | None = None) -> dict:
    return {"present": present, "active": active, "reasons": list(reasons or [])}


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


# ---------------------------------------------------------------------------
# driver_override_reported (phase B, req-vd-ad:REQ-AD-028 step b)
# ---------------------------------------------------------------------------
#
# This block IS the unit-level red proof the verification plan's §4-2 row asks
# for ("populate を止めた単体赤実証＋入力プロファイル時刻ずらし"): the first two
# reds below are exactly those two, and the rest pin the vacuous-pass guards
# that keep the negative direction from passing for the wrong reason.


def _kickdown_frames() -> list[dict]:
    """A run where the accelerator override holds from t=2.0 onward: the shape
    the kickdown input profile produces on the AEB row. gt.fcw is present
    throughout with its channel written but never active -- the in-run negative
    control the C++ report builder produces by construction (kickdown
    suppresses the intervention, never the warning)."""
    frames = []
    for i in range(6):
        t = i * 1.0
        active = t >= 2.0
        frames.append(
            _frame(
                t,
                {
                    "gt.aeb": _adas_rec(
                        "standby",
                        override=_override(True, active),
                        custom_state="DRIVER_OVERRIDE_ACCEL" if active else "",
                    ),
                    "gt.fcw": _adas_rec("standby", override=_override(True, False)),
                },
            )
        )
    return frames


def test_driver_override_reported_green_in_the_kickdown_window():
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": True,
            "expect_custom_state": "DRIVER_OVERRIDE_ACCEL",
            "mode": "all",
            "after": {"sim_time": 2.0},
        },
        _kickdown_frames(),
    )
    assert r["status"] == "pass", r["detail"]
    assert "custom_state == 'DRIVER_OVERRIDE_ACCEL'" in r["detail"]


# RED #1 -- "populate を止めた" (verification plan §4-2). The C++ producer is
# removed, so the row still reports (State is unaffected) but the override
# channel is never written. A matcher that skipped here would let the whole
# phase-B mechanism be deleted without one gate turning red; it must FAIL,
# because the row being present is already proof the instrument was live.
def test_driver_override_reported_red_when_populate_is_removed():
    frames = [_frame(t, {"gt.aeb": _adas_rec("standby")}) for t in (2.0, 3.0, 4.0, 5.0)]
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": True,
            "expect_custom_state": "DRIVER_OVERRIDE_ACCEL",
            "after": {"sim_time": 2.0},
        },
        frames,
    )
    assert r["status"] == "fail"
    assert "present=False" in r["detail"]
    assert "active=False" in r["detail"]


# RED #2 -- "入力プロファイル時刻ずらし". The override really happens, but the
# window it is judged in no longer contains the driver input that caused it.
# Judging the WINDOW is the whole point: an override reported at some point
# during a 20 s run says nothing about whether it tracked the driver's input.
def test_driver_override_reported_red_when_the_override_window_is_shifted():
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": True,
            "mode": "all",
            "after": {"sim_time": 0.0},
            "before": {"sim_time": 1.5},  # shifted: now the pre-kickdown window
        },
        _kickdown_frames(),
    )
    assert r["status"] == "fail"
    assert "active=False" in r["detail"]


def test_driver_override_reported_red_on_wrong_custom_state_token():
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": True,
            "expect_custom_state": "DRIVER_OVERRIDE_BRAKE",  # not what the producer emits
            "after": {"sim_time": 2.0},
        },
        _kickdown_frames(),
    )
    assert r["status"] == "fail"
    assert "DRIVER_OVERRIDE_ACCEL" in r["detail"]  # what was actually observed


def test_driver_override_reported_red_on_missing_expected_reason():
    """expect_reason is the channel phases C/D will use (brake -> ACC cancel,
    steering -> LKA interrupt). Phase B has no producer for either, so this
    pins only that asking for a Reason which is absent FAILS -- without it a
    phase-C asset could be written against a producer that was never wired and
    still go green."""
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": True,
            "expect_reason": "REASON_BRAKE_PEDAL",
            "after": {"sim_time": 2.0},
        },
        _kickdown_frames(),
    )
    assert r["status"] == "fail"
    assert "reasons=[]" in r["detail"]


def test_driver_override_reported_green_negative_direction():
    """The negative: an unresponsive driver never overrides. The channel IS
    written (present=True) on every frame, which is what makes this a
    measurement rather than silence."""
    frames = [
        _frame(t, {"gt.aeb": _adas_rec("standby", override=_override(True, False))})
        for t in (0.0, 1.0, 2.0, 3.0)
    ]
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": False,
            "mode": "all",
        },
        frames,
    )
    assert r["status"] == "pass", r["detail"]


def test_driver_override_reported_red_negative_direction_catches_a_stray_override():
    frames = [
        _frame(t, {"gt.aeb": _adas_rec("standby", override=_override(True, False))})
        for t in (0.0, 1.0)
    ]
    frames.append(
        _frame(
            2.0,
            {
                "gt.aeb": _adas_rec(
                    "standby",
                    override=_override(True, True),
                    custom_state="DRIVER_OVERRIDE_ACCEL",
                )
            },
        )
    )
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": False,
            "mode": "all",
        },
        frames,
    )
    assert r["status"] == "fail"
    assert "active=True" in r["detail"]


# The asymmetry between the two directions, pinned. Same frames, no override
# channel written at all: the POSITIVE direction fails (the row is live, so
# absence is a real negative observation) while the NEGATIVE direction skips (a
# channel nobody wrote cannot evidence "the driver did not override"). Getting
# this backwards would hand the phase-B mechanism a green negative in a run
# where it was never even called.
def test_driver_override_reported_negative_direction_skips_when_channel_unwritten():
    frames = [_frame(t, {"gt.aeb": _adas_rec("standby")}) for t in (0.0, 1.0, 2.0)]

    neg = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": False,
            "mode": "all",
        },
        frames,
    )
    assert neg["status"] == "skip"
    assert "never populated" in neg["detail"]

    pos = eval_must({"event": "driver_override_reported", "function": "gt.aeb"}, frames)
    assert pos["status"] == "fail"


def test_driver_override_reported_skip_without_function():
    r = eval_must({"event": "driver_override_reported"}, _kickdown_frames())
    assert r["status"] == "skip"
    assert "no function" in r["detail"]


def test_driver_override_reported_skip_on_empty_window():
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "after": {"sim_time": 100.0},
        },
        _kickdown_frames(),
    )
    assert r["status"] == "skip"
    assert "time window" in r["detail"]


def test_driver_override_reported_skip_when_function_never_reported():
    frames = [_frame(0.0, no_hvd=True)]
    r = eval_must({"event": "driver_override_reported", "function": "gt.aeb"}, frames)
    assert r["status"] == "skip"
    assert "never reported" in r["detail"]


def test_driver_override_reported_skip_below_min_frames():
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "min_frames": 50,
        },
        _kickdown_frames(),
    )
    assert r["status"] == "skip"
    assert "min_frames" in r["detail"]


def test_driver_override_reported_refuses_negative_with_mode_any():
    """expect_active: false + mode: any is satisfied by nearly any run,
    including one where the override fired on every other frame. Refused
    outright rather than reported as a pass nobody should trust."""
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": False,
            "mode": "any",
        },
        _kickdown_frames(),
    )
    assert r["status"] == "skip"
    assert "mode: all" in r["detail"]


def test_driver_override_reported_mode_any_green_on_a_single_matching_frame():
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.aeb",
            "expect_active": True,
            "expect_custom_state": "DRIVER_OVERRIDE_ACCEL",
            "mode": "any",
        },
        _kickdown_frames(),  # frames 0-1 carry no override, 2-5 do
    )
    assert r["status"] == "pass", r["detail"]
    assert "first seen at t=2.00" in r["detail"]


def test_driver_override_reported_fcw_row_is_the_in_run_negative_control():
    """Same frames, same window as the green positive: the FCW row must show
    its override channel written and inactive. Kickdown suppresses the
    INTERVENTION, never the WARNING -- if this ever went active the driver
    would lose the collision cue exactly while accelerating toward a hazard."""
    r = eval_must(
        {
            "event": "driver_override_reported",
            "function": "gt.fcw",
            "expect_active": False,
            "mode": "all",
            "after": {"sim_time": 2.0},
        },
        _kickdown_frames(),
    )
    assert r["status"] == "pass", r["detail"]
