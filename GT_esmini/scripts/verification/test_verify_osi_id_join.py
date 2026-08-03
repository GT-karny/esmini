"""Both-polarity tests for verify_osi_id_join.py.

A checker that has only ever been run on data it accepts is not a checker. These
pin the RED cases as tightly as the green one: each test that expects a pass has
a sibling that perturbs exactly one thing and must fail.

The perturbation in EntityIdInsteadOfOsiIdIsRejected is the actual bug this whole
change set exists to prevent (shipping Object::GetId() where OSI publishes
Object::g_id_), so that test is the reason the harness exists at all.

Run:
    DriverScript/.venv/Scripts/python.exe -m pytest \
        GT_esmini/scripts/verification/test_verify_osi_id_join.py -q
"""

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from verify_osi_id_join import verify  # noqa: E402

EGO_ID = 10
LEAD_ID = 11
PED_ID = 17
CROSSWALK_OSI_ID = 5
CROSSWALK_ODR_ID = 900000000


def _frame(detail, *, ego_x=0.0, lead_x=38.93, with_stationary=False, overtake_lead=-1):
    """One telemetry frame: ego at ego_x, a 5 m lead at lead_x, both 5 m long."""
    objects = [
        {
            "id": EGO_ID,
            "name": "Ego",
            "x": ego_x,
            "y": 0.0,
            "length": 5.0,
            "is_host": True,
        },
        {
            "id": LEAD_ID,
            "name": "Lead",
            "x": lead_x,
            "y": 0.0,
            "length": 5.0,
            "is_host": False,
        },
        {
            "id": PED_ID,
            "name": "Ped",
            "x": ego_x + 20.0,
            "y": 3.0,
            "length": 0.5,
            "is_host": False,
        },
    ]
    scene = {"objects": objects}
    if with_stationary:
        scene["stationary_objects"] = [
            {
                "id": CROSSWALK_OSI_ID,
                "x": ego_x + 20.0,
                "y": 0.0,
                "odr_object_id": CROSSWALK_ODR_ID,
                "odr_type": "crosswalk",
                "is_crosswalk": True,
            }
        ]
    return {
        "sim_time": 1.0,
        "policy": {"detail": detail},
        "overtake": {"lead_osi_id": overtake_lead},
        "scene": scene,
    }


def _write(tmp_path, frames) -> Path:
    p = tmp_path / "telemetry.jsonl"
    p.write_text(
        "".join(json.dumps(f, separators=(",", ":")) + "\n" for f in frames),
        encoding="utf-8",
    )
    return p


def _run(tmp_path, frames, proximity_m=400.0):
    return verify(_write(tmp_path, frames), proximity_m)


# --- lead vehicle: the id must exist AND sit where the published gap says ----

LEAD_OK = {
    "gt.lead_vehicle.gap_m": "33.93",
    "gt.lead_vehicle.lead_osi_id": str(LEAD_ID),
}


def test_correct_osi_id_passes(tmp_path):
    r = _run(tmp_path, [_frame(LEAD_OK)])
    assert r["findings"] == []
    assert r["keys"]["gt.lead_vehicle.lead_osi_id"]["checked"] == 1


def test_entity_id_instead_of_osi_id_is_rejected(tmp_path):
    # Scenario entity index 1 -- what GetId() would have returned. It resolves to
    # nothing in the GroundTruth, which is the whole point of the id-space fix.
    detail = dict(LEAD_OK, **{"gt.lead_vehicle.lead_osi_id": "1"})
    r = _run(tmp_path, [_frame(detail)])
    assert len(r["findings"]) == 1
    assert "not present" in r["findings"][0]["reason"]


def test_id_of_the_wrong_vehicle_is_rejected_by_geometry(tmp_path):
    # A real OSI id that EXISTS but is not the vehicle the gap describes: the
    # existence check alone would wave this through.
    detail = dict(LEAD_OK, **{"gt.lead_vehicle.lead_osi_id": str(PED_ID)})
    r = _run(tmp_path, [_frame(detail)])
    assert len(r["findings"]) == 1
    assert "disagrees with gt.lead_vehicle.gap_m" in r["findings"][0]["reason"]


def test_clamped_zero_gap_does_not_fail_an_interpenetrating_frame(tmp_path):
    # gap == 0 is the clamp, not a measurement: the bodies overlap and the
    # distance no longer follows the gap. Correct id must still pass.
    detail = {
        "gt.lead_vehicle.gap_m": "0.000",
        "gt.lead_vehicle.lead_osi_id": str(LEAD_ID),
    }
    r = _run(tmp_path, [_frame(detail, lead_x=3.4)])
    assert r["findings"] == []


def test_clamped_zero_gap_still_rejects_a_far_away_partner(tmp_path):
    # ...but "no freespace left" while the named body is 38 m away is impossible.
    detail = {
        "gt.lead_vehicle.gap_m": "0.000",
        "gt.lead_vehicle.lead_osi_id": str(LEAD_ID),
    }
    r = _run(tmp_path, [_frame(detail, lead_x=38.93)])
    assert len(r["findings"]) == 1
    assert "clamped to 0" in r["findings"][0]["reason"]


# --- crosswalk: stationary half + the two-field identity check ---------------

CROSSWALK_OK = {
    "gt.crosswalk.object_id": str(CROSSWALK_ODR_ID),
    "gt.crosswalk.object_osi_id": str(CROSSWALK_OSI_ID),
    "gt.crosswalk.ped_osi_id": str(PED_ID),
}


def test_crosswalk_ids_join_against_the_stationary_half(tmp_path):
    r = _run(tmp_path, [_frame(CROSSWALK_OK, with_stationary=True)])
    assert r["findings"] == []


def test_crosswalk_id_looked_up_in_the_moving_half_would_not_be_found(tmp_path):
    # The crosswalk id lives in the STATIONARY table; a checker that looked it up
    # among moving objects would report a good id as missing. Pinned by using an
    # id that exists only as a moving object.
    detail = dict(CROSSWALK_OK, **{"gt.crosswalk.object_osi_id": str(LEAD_ID)})
    r = _run(tmp_path, [_frame(detail, with_stationary=True)])
    assert len(r["findings"]) == 1
    assert "scene.stationary_objects" in r["findings"][0]["reason"]


def test_stationary_table_accumulates_across_frames(tmp_path):
    # gt_sim_test attaches the static catalogue to ONE frame only. A later frame
    # claiming the crosswalk must still resolve.
    frames = [
        _frame(CROSSWALK_OK, with_stationary=True),
        _frame(CROSSWALK_OK, with_stationary=False),
    ]
    r = _run(tmp_path, frames)
    assert r["findings"] == []
    assert r["keys"]["gt.crosswalk.object_osi_id"]["checked"] == 2


def test_odr_id_disagreement_is_an_identity_failure(tmp_path):
    # Same OSI object, but the policy's own ODR id names a different crosswalk.
    detail = dict(CROSSWALK_OK, **{"gt.crosswalk.object_id": "12345"})
    r = _run(tmp_path, [_frame(detail, with_stationary=True)])
    assert len(r["findings"]) == 1
    assert "odr_object_id" in r["findings"][0]["reason"]


# --- bookkeeping ------------------------------------------------------------


def test_no_partner_is_not_counted_as_a_claim(tmp_path):
    r = _run(tmp_path, [_frame({}, overtake_lead=-1)])
    st = r["keys"]["overtake.lead_osi_id"]
    assert (st["claimed"], st["no_partner"]) == (0, 1)
    assert "overtake.lead_osi_id" in r["unexercised_keys"]


def test_every_known_key_is_reported_even_when_never_emitted(tmp_path):
    # A stats table built only from observed keys cannot tell "verified" from
    # "never happened".
    r = _run(tmp_path, [_frame(LEAD_OK)])
    assert "gt.crosswalk.ped_osi_id" in r["unexercised_keys"]
    assert "gt.conflict_point.other_osi_id" in r["unexercised_keys"]


def test_distant_partner_without_a_published_gap_trips_the_weak_check(tmp_path):
    detail = {"gt.conflict_point.other_osi_id": str(LEAD_ID)}
    r = _run(tmp_path, [_frame(detail, lead_x=5000.0)], proximity_m=400.0)
    assert len(r["findings"]) == 1
    assert "not near the ego" in r["findings"][0]["reason"]


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-q"]))
