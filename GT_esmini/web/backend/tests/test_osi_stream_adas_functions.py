"""Tests for api/osi_stream.py's HostVehicleData -> JSON ADAS-function projection.

req-vd-ad:REQ-AD-029 (ADAS state / settings / warnings presented to the driver).
This file covers the machine-checkable HALF of that requirement: the SUPPLY.
The pixels themselves are the frontend's business and stay on the eyeball +
screenshot side of verification plan sec6's three-way split; what a test CAN
pin down is that everything the dashboard needs actually crosses the existing
HVD wire, spelled the way face 3 spells it.

Why that split is the honest one here: `HvdGaugePanel` renders what
`_hvd_to_json` hands it. If the projection drops `custom_detail`, or collapses
`driver_override.present` into `active`, or renames a state, the panel goes
quiet or lies -- and no screenshot taken on a good day would catch the
regression later. Those are exactly the failures asserted below.

Step c of the ladder ("no display-only channel is dug") is a review-time
structural constraint, not a runtime quantity, so it is not asserted here --
but note what this file's imports show: the projection under test is a pure
function of the SAME HostVehicleData bytes the OSI bridge already receives on
UDP 48199. There is nothing else to mock.
"""

from __future__ import annotations

from osi3.osi_hostvehicledata_pb2 import HostVehicleData

from GT_esmini.web.backend.api.osi_stream import _hvd_to_json

# Values from design manualdrive_adas_design.md sec8-2 (canonical OSI Name
# enum, NAME_OTHER deliberately unused by the ManualDrive stack).
_NAME_FCW = 3
_NAME_LDW = 4
_NAME_AEB = 7
_NAME_ACC = 10
_NAME_LKA = 11
_NAME_MSL = 25

_STATE_UNAVAILABLE = 3
_STATE_STANDBY = 5
_STATE_ACTIVE = 6

_REASON_BRAKE_PEDAL = 0
_REASON_STEERING_INPUT = 1


def _add(hvd, *, key, name, state, detail=None, override=None, custom_state=""):
    func = hvd.vehicle_automated_driving_function.add()
    func.custom_name = key
    func.name = name
    func.state = state
    if custom_state:
        func.custom_state = custom_state
    for k, v in (detail or {}).items():
        kv = func.custom_detail.add()
        kv.key = k
        kv.value = v
    if override is not None:
        active, reasons = override
        func.driver_override.active = active
        for r in reasons:
            func.driver_override.override_reason.append(r)
    return func


def _manualdrive_frame() -> bytes:
    """A HostVehicleData frame shaped like one ManualDrive ADAS frame."""
    hvd = HostVehicleData()
    hvd.timestamp.seconds = 4
    hvd.timestamp.nanos = 750_000_000
    hvd.vehicle_powertrain.pedal_position_acceleration = 0.3
    hvd.vehicle_brake_system.pedal_position_brake = 0.0

    _add(
        hvd,
        key="gt.aeb",
        name=_NAME_AEB,
        state=_STATE_ACTIVE,
        detail={"gt.aeb.ttc_s": "1.200", "gt.aeb.warning": "true"},
        override=(False, []),
    )
    _add(hvd, key="gt.fcw", name=_NAME_FCW, state=_STATE_ACTIVE)
    _add(
        hvd,
        key="gt.acc",
        name=_NAME_ACC,
        state=_STATE_STANDBY,
        detail={"gt.acc.set_speed_mps": "25.000", "gt.acc.thw_setting_s": "1.600"},
        override=(True, [_REASON_BRAKE_PEDAL]),
    )
    _add(
        hvd,
        key="gt.lka",
        name=_NAME_LKA,
        state=_STATE_ACTIVE,
        detail={"gt.lka.correction": "0.004", "gt.lka.warning": "false"},
        override=(True, [_REASON_STEERING_INPUT]),
    )
    _add(hvd, key="gt.ldw", name=_NAME_LDW, state=_STATE_STANDBY)
    _add(
        hvd,
        key="gt.msl",
        name=_NAME_MSL,
        state=_STATE_UNAVAILABLE,
        detail={"gt.msl.cap_mps": "13.889"},
        custom_state="DRIVER_OVERRIDE_ACCEL",
    )
    return hvd.SerializeToString()


def test_every_reported_function_reaches_the_frontend_payload():
    msg = _hvd_to_json(_manualdrive_frame())
    assert msg is not None
    rows = msg["adas_functions"]
    # Report ORDER is preserved: the dashboard gets a stable row order without
    # having to invent one, and it is the order the C++ stack writes.
    assert [r["key"] for r in rows] == [
        "gt.aeb",
        "gt.fcw",
        "gt.acc",
        "gt.lka",
        "gt.ldw",
        "gt.msl",
    ]
    assert [r["name"] for r in rows] == [
        _NAME_AEB,
        _NAME_FCW,
        _NAME_ACC,
        _NAME_LKA,
        _NAME_LDW,
        _NAME_MSL,
    ]


def test_three_value_state_discipline_survives_the_projection():
    """UNAVAILABLE / STANDBY / ACTIVE must stay three distinct words.

    "switched off" vs "watching and did not fire" is the whole point of the
    ladder's step a (design sec8-2). If the projection folded either into the
    other, the dashboard would show a quiet ADAS and a disabled ADAS the same.
    """
    rows = {r["key"]: r for r in _hvd_to_json(_manualdrive_frame())["adas_functions"]}
    assert rows["gt.lka"]["state_name"] == "active"
    assert rows["gt.acc"]["state_name"] == "standby"
    assert rows["gt.msl"]["state_name"] == "unavailable"
    assert len({rows[k]["state_name"] for k in ("gt.lka", "gt.acc", "gt.msl")}) == 3


def test_settings_and_warnings_cross_the_wire_as_custom_detail():
    """Step a's settings (ACC set speed / THW stage / MSL cap) and step b's
    warnings (gt.aeb.warning = FCW, gt.lka.warning = LDW) are custom_detail
    key-value pairs -- the projection must not drop or rename them."""
    rows = {r["key"]: r for r in _hvd_to_json(_manualdrive_frame())["adas_functions"]}
    assert rows["gt.acc"]["detail"]["gt.acc.set_speed_mps"] == "25.000"
    assert rows["gt.acc"]["detail"]["gt.acc.thw_setting_s"] == "1.600"
    assert rows["gt.msl"]["detail"]["gt.msl.cap_mps"] == "13.889"
    assert rows["gt.aeb"]["detail"]["gt.aeb.warning"] == "true"
    assert rows["gt.lka"]["detail"]["gt.lka.warning"] == "false"


def test_driver_override_present_is_distinct_from_active():
    """`present` (was the channel written at all) must not collapse into
    `active` (is the driver overriding). design sec8-3: without the
    distinction, a populate path that has been removed entirely looks
    identical to a driver who is simply not overriding anything."""
    rows = {r["key"]: r for r in _hvd_to_json(_manualdrive_frame())["adas_functions"]}

    # written, and the driver IS overriding (ACC cancelled by the brake pedal)
    assert rows["gt.acc"]["driver_override"] == {
        "present": True,
        "active": True,
        "reasons": ["brake_pedal"],
    }
    # written, and the driver is NOT overriding -- an explicit measurement
    assert rows["gt.aeb"]["driver_override"] == {
        "present": True,
        "active": False,
        "reasons": [],
    }
    # never written (no producer for this row): NOT the same as the line above
    assert rows["gt.fcw"]["driver_override"] == {
        "present": False,
        "active": False,
        "reasons": [],
    }
    assert rows["gt.lka"]["driver_override"]["reasons"] == ["steering_input"]


def test_accel_override_is_readable_as_custom_state():
    """OSI's Reason enum has only brake and steering, so the accelerator-borne
    override (kickdown) is carried in custom_state (design sec8-3). Dropping
    the field would make that override invisible on the dashboard while the
    other two stayed visible."""
    rows = {r["key"]: r for r in _hvd_to_json(_manualdrive_frame())["adas_functions"]}
    assert rows["gt.msl"]["custom_state"] == "DRIVER_OVERRIDE_ACCEL"
    assert rows["gt.acc"]["custom_state"] == ""


def test_frame_without_adas_functions_yields_an_empty_list_not_a_missing_key():
    """Controllers that report no ADAS rows must produce [] rather than
    omitting the key: the panel branches on length, and a missing key would
    make it throw instead of rendering nothing."""
    hvd = HostVehicleData()
    hvd.timestamp.seconds = 1
    msg = _hvd_to_json(hvd.SerializeToString())
    assert msg is not None
    assert msg["adas_functions"] == []


def test_existing_gauge_fields_are_untouched_by_the_extension():
    """The pre-existing dashboard fields keep their names and rounding: this
    change is additive to the same message, not a reshape of it."""
    msg = _hvd_to_json(_manualdrive_frame())
    assert msg["type"] == "host_vehicle_data"
    assert msg["sim_time"] == 4.75
    assert msg["throttle"] == 0.3
    assert msg["brake"] == 0.0
