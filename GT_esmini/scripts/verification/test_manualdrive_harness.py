"""ManualDrive harness additions (§7-1/7-2/7-3/7-5, req-vd-ad:REQ-AD-025..031,
vd-func:FUNC-075) -- the parts that need no DLL/build.

Covers:
  - _write_manualdrive_config: flat-key overrides applied, path override
    absolutized, unknown key raises (test_write_manualdrive_config.py-style
    both-polarity coverage)
  - _prepare_manualdrive_xosc: ConfigFile injected as an absolute path; an
    EXISTING ConfigFile Property is REPLACED, not duplicated; a scenario with
    no ManualDriveController raises
  - _hvd_to_dict: HVD protobuf -> the exact {"inputs": ..., "adas": ...}
    frame shape (state_name derivation off the live enum descriptor,
    driver_override's always-emitted default shape)
  - batch()'s controller routing: a manifest entry with no `controller:` key
    still calls run() with controller="virtualdriver" -- the byte-identity
    guarantee three committed regression baselines depend on
  - _host_ego_from_scene: never fabricates a zero ego

Entirely offline -- no build, no DLL, no GT_Sim required. gt_sim_test.run()
is monkeypatched at the exact call site batch() uses for the routing test,
same pattern as test_batch_output_freshness.py.

Run:
    DriverScript/.venv/Scripts/python.exe -m pytest \
        GT_esmini/scripts/verification/test_manualdrive_harness.py -v
"""

from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

import gt_sim_test as gst
from osi3.osi_hostvehicledata_pb2 import HostVehicleData


@pytest.fixture(autouse=True)
def _no_real_port_checks(monkeypatch):
    """Neutralize _require_gate_ports_free() the same way
    test_batch_output_freshness.py does, so a busy port on the machine
    running these tests cannot fail an otherwise-offline test."""
    monkeypatch.setattr(gst._vd, "check_gate_ports_free", lambda: [])


# ---------------------------------------------------------------------------
# _write_manualdrive_config
# ---------------------------------------------------------------------------

# Mirrors the shape of GT_esmini/config/manual_drive_headless_stub.json: the
# phase-A adas_* keys live nested under "adas", input_scripted_profile_file
# under "input_scripted", input_type at the top level. Kept local (not read
# from the real file) so this test is not coupled to future edits of the
# shipped config's non-ADAS keys.
_BASE_MD_CONFIG_FIXTURE = {
    "input_type": "stub",
    "physics_type": "real_vehicle",
    "ffb_enabled": False,
    "domain": {"lateral": "manual", "longitudinal": "manual"},
    "override": {"enabled": False, "button_takeover": False},
    "adas": {
        "adas_aeb_enabled": False,
        "adas_aeb_kickdown_suppress_enabled": True,
        "adas_aeb_warning_ttc_threshold_s": 3.5,
        "adas_brake_full_decel_mps2": 8.0,
        "adas_brake_kp": 0.05,
        "adas_brake_ki": 0.6,
        "adas_kickdown_threshold": 0.95,
        "adas_kickdown_release_threshold": 0.80,
    },
    "input_scripted": {"input_scripted_profile_file": ""},
}


@pytest.fixture()
def base_md_config(tmp_path, monkeypatch):
    """Point gst.BASE_MD_CONFIG at a throwaway copy of the fixture above."""
    cfg_path = tmp_path / "manual_drive_headless_stub.json"
    cfg_path.write_text(json.dumps(_BASE_MD_CONFIG_FIXTURE), encoding="utf-8")
    monkeypatch.setattr(gst, "BASE_MD_CONFIG", cfg_path)
    return cfg_path


def test_write_manualdrive_config_applies_nested_overrides(base_md_config, tmp_path):
    out = gst._write_manualdrive_config(
        {"adas_aeb_enabled": True, "adas_kickdown_threshold": 0.90},
        tmp_path / "run.json",
    )
    written = json.loads(out.read_text(encoding="utf-8"))
    # The override lands where the base file already nests it (under
    # "adas"), not flattened to the top level -- see _set_flat_key's
    # docstring for why.
    assert written["adas"]["adas_aeb_enabled"] is True
    assert written["adas"]["adas_kickdown_threshold"] == 0.90
    # Untouched sibling keys survive unchanged.
    assert written["adas"]["adas_aeb_kickdown_suppress_enabled"] is True
    assert written["input_type"] == "stub"


def test_write_manualdrive_config_applies_top_level_override(base_md_config, tmp_path):
    out = gst._write_manualdrive_config(
        {"input_type": "scripted"}, tmp_path / "run.json"
    )
    written = json.loads(out.read_text(encoding="utf-8"))
    assert written["input_type"] == "scripted"


def test_write_manualdrive_config_absolutizes_path_override(base_md_config, tmp_path):
    out = gst._write_manualdrive_config(
        {
            "input_scripted_profile_file": "resources/xosc/verification/09_manualdrive_adas/profiles/unresponsive.json"
        },
        tmp_path / "run.json",
    )
    written = json.loads(out.read_text(encoding="utf-8"))
    value = written["input_scripted"]["input_scripted_profile_file"]
    assert Path(value).is_absolute()
    assert value == str(
        gst.REPO_ROOT
        / "resources/xosc/verification/09_manualdrive_adas/profiles/unresponsive.json"
    )


def test_write_manualdrive_config_leaves_an_already_absolute_path_alone(
    base_md_config, tmp_path
):
    abs_path = str((tmp_path / "already_absolute.json").resolve())
    out = gst._write_manualdrive_config(
        {"input_scripted_profile_file": abs_path}, tmp_path / "run.json"
    )
    written = json.loads(out.read_text(encoding="utf-8"))
    assert written["input_scripted"]["input_scripted_profile_file"] == abs_path


def test_write_manualdrive_config_unknown_key_raises(base_md_config, tmp_path):
    with pytest.raises(ValueError, match="unknown manualdrive_config key"):
        gst._write_manualdrive_config(
            {"adas_aeb_enbaled_typo": True}, tmp_path / "run.json"
        )


# ---------------------------------------------------------------------------
# _prepare_manualdrive_xosc
# ---------------------------------------------------------------------------


def _write_scenario(
    path: Path,
    *,
    controller_name: str | None = "ManualDriveController",
    existing_config_file: str | None = "manual_drive_headless_stub.json",
) -> Path:
    """A minimal-but-structurally-real xosc fragment: one ScenarioObject with
    an ObjectController wrapping a Controller. `controller_name=None` omits
    the ManualDriveController entirely (the missing-controller case);
    `existing_config_file=None` omits the pre-existing ConfigFile Property
    (the fresh-injection case)."""
    props = ['<Property name="esminiController" value="ManualDriveController"/>']
    if existing_config_file is not None:
        props.append(f'<Property name="ConfigFile" value="{existing_config_file}"/>')
    controller_xml = ""
    if controller_name is not None:
        controller_xml = f"""
        <ObjectController>
          <Controller name="{controller_name}">
            <Properties>
              {''.join(props)}
            </Properties>
          </Controller>
        </ObjectController>
        """
    xosc = f"""<?xml version="1.0"?>
<OpenSCENARIO>
  <RoadNetwork>
    <LogicFile filepath="road.xodr"/>
  </RoadNetwork>
  <Entities>
    <ScenarioObject name="Ego">
      {controller_xml}
    </ScenarioObject>
  </Entities>
</OpenSCENARIO>
"""
    path.write_text(xosc, encoding="utf-8")
    return path


def _find_manualdrive_controller(root: ET.Element) -> ET.Element:
    for ctrl in root.iter("Controller"):
        if ctrl.get("name") == "ManualDriveController":
            return ctrl
    raise AssertionError("no ManualDriveController in output xosc")


def test_prepare_manualdrive_xosc_injects_absolute_config_file(tmp_path):
    scen_dir = tmp_path / "scen"
    scen_dir.mkdir()
    scenario = _write_scenario(scen_dir / "md_scenario.xosc", existing_config_file=None)
    run_dir = tmp_path / "run"
    config_path = tmp_path / "manual_drive.run.json"
    config_path.write_text("{}", encoding="utf-8")

    out = gst._prepare_manualdrive_xosc(scenario, run_dir, config_path)

    assert out.is_file()
    root = ET.parse(out).getroot()
    ctrl = _find_manualdrive_controller(root)
    cfg_props = [
        p
        for p in ctrl.find("Properties").findall("Property")
        if p.get("name") == "ConfigFile"
    ]
    assert len(cfg_props) == 1
    assert cfg_props[0].get("value") == str(config_path)
    assert Path(cfg_props[0].get("value")).is_absolute()

    # road/scene paths also absolutized, mirroring _prepare_policy_xosc.
    logic_file = root.find(".//LogicFile")
    assert Path(logic_file.get("filepath")).is_absolute()
    assert logic_file.get("filepath") == str((scen_dir / "road.xodr").resolve())


def test_prepare_manualdrive_xosc_replaces_not_duplicates_existing_config_file(
    tmp_path,
):
    """The load-bearing case: a scenario that ALREADY declares a ConfigFile
    (as every 09_manualdrive_adas/ fixture and the 08_handoff split-domain
    fixture do) must end up with exactly ONE ConfigFile Property carrying the
    NEW value -- not two, which ManualDriveConfig::LoadFromFile would resolve
    order-dependently."""
    scen_dir = tmp_path / "scen"
    scen_dir.mkdir()
    scenario = _write_scenario(
        scen_dir / "md_scenario.xosc",
        existing_config_file="manual_drive_headless_stub.json",
    )
    run_dir = tmp_path / "run"
    config_path = tmp_path / "manual_drive.run.json"
    config_path.write_text("{}", encoding="utf-8")

    out = gst._prepare_manualdrive_xosc(scenario, run_dir, config_path)

    root = ET.parse(out).getroot()
    ctrl = _find_manualdrive_controller(root)
    cfg_props = [
        p
        for p in ctrl.find("Properties").findall("Property")
        if p.get("name") == "ConfigFile"
    ]
    assert len(cfg_props) == 1, (
        f"expected exactly one ConfigFile Property, got {len(cfg_props)}: "
        f"{[p.get('value') for p in cfg_props]}"
    )
    assert cfg_props[0].get("value") == str(config_path)
    assert cfg_props[0].get("value") != "manual_drive_headless_stub.json"
    # The non-ConfigFile Property from the original scenario must survive.
    other_props = [
        p
        for p in ctrl.find("Properties").findall("Property")
        if p.get("name") != "ConfigFile"
    ]
    assert any(p.get("name") == "esminiController" for p in other_props)


def test_prepare_manualdrive_xosc_raises_when_no_manualdrive_controller(tmp_path):
    scen_dir = tmp_path / "scen"
    scen_dir.mkdir()
    scenario = _write_scenario(
        scen_dir / "vd_only.xosc", controller_name="VirtualDriverController"
    )
    run_dir = tmp_path / "run"
    config_path = tmp_path / "manual_drive.run.json"
    config_path.write_text("{}", encoding="utf-8")

    with pytest.raises(RuntimeError, match="no ManualDriveController"):
        gst._prepare_manualdrive_xosc(scenario, run_dir, config_path)


# ---------------------------------------------------------------------------
# _hvd_to_dict: HVD protobuf -> frame projection
# ---------------------------------------------------------------------------


def _build_hvd_bytes() -> bytes:
    hvd = HostVehicleData()
    hvd.vehicle_powertrain.pedal_position_acceleration = 0.0
    hvd.vehicle_powertrain.gear_transmission = 1
    hvd.vehicle_brake_system.pedal_position_brake = 0.42
    hvd.vehicle_steering.vehicle_steering_wheel.angle = 0.0

    Fn = HostVehicleData.VehicleAutomatedDrivingFunction

    aeb = hvd.vehicle_automated_driving_function.add()
    aeb.name = Fn.NAME_AUTOMATIC_EMERGENCY_BRAKING  # 7
    aeb.custom_name = "gt.aeb"
    aeb.state = Fn.STATE_ACTIVE  # 6
    kv = aeb.custom_detail.add()
    kv.key = "gt.aeb.ttc_s"
    kv.value = "1.842"
    # driver_override deliberately left unset -- phase A does not populate it.

    fcw = hvd.vehicle_automated_driving_function.add()
    fcw.name = Fn.NAME_FORWARD_COLLISION_WARNING  # 3
    fcw.custom_name = "gt.fcw"
    fcw.state = Fn.STATE_STANDBY  # 5
    # no custom_detail, no driver_override.

    overridden = hvd.vehicle_automated_driving_function.add()
    overridden.name = Fn.NAME_LANE_KEEPING_ASSIST
    overridden.custom_name = "gt.test_override"
    overridden.state = Fn.STATE_UNAVAILABLE  # 3
    overridden.driver_override.active = True
    overridden.driver_override.override_reason.append(
        Fn.DriverOverride.REASON_BRAKE_PEDAL
    )
    overridden.driver_override.override_reason.append(
        Fn.DriverOverride.REASON_STEERING_INPUT
    )

    return hvd.SerializeToString()


def test_hvd_to_dict_exact_frame_shape():
    result = gst._hvd_to_dict(_build_hvd_bytes())

    assert result == {
        "inputs": {
            "throttle": 0.0,
            "brake": 0.42,
            "steering": 0.0,
            "gear": 1,
        },
        "adas": {
            "gt.aeb": {
                "name": 7,
                "state": 6,
                "state_name": "active",
                "detail": {"gt.aeb.ttc_s": "1.842"},
                "driver_override": {"active": False, "reasons": []},
            },
            "gt.fcw": {
                "name": 3,
                "state": 5,
                "state_name": "standby",
                "detail": {},
                "driver_override": {"active": False, "reasons": []},
            },
            "gt.test_override": {
                "name": 11,
                "state": 3,
                "state_name": "unavailable",
                "detail": {},
                "driver_override": {
                    "active": True,
                    "reasons": ["brake_pedal", "steering_input"],
                },
            },
        },
    }


def test_hvd_to_dict_returns_none_on_unparseable_bytes():
    assert gst._hvd_to_dict(b"\xff\xff\xff not a valid protobuf message") is None


# ---------------------------------------------------------------------------
# _host_ego_from_scene: never fabricate a zero ego
# ---------------------------------------------------------------------------


def test_host_ego_from_scene_returns_none_when_no_scene():
    assert gst._host_ego_from_scene(None) is None
    assert gst._host_ego_from_scene({}) is None


def test_host_ego_from_scene_returns_none_when_no_host_object():
    scene = {
        "objects": [
            {"id": 1, "x": 1.0, "y": 2.0, "h": 0.0, "speed": 3.0, "is_host": False}
        ]
    }
    assert gst._host_ego_from_scene(scene) is None


def test_host_ego_from_scene_finds_the_host_object():
    scene = {
        "objects": [
            {"id": 1, "x": 1.0, "y": 2.0, "h": 0.1, "speed": 3.0, "is_host": False},
            {"id": 2, "x": 10.0, "y": 20.0, "h": 0.2, "speed": 5.5, "is_host": True},
        ]
    }
    assert gst._host_ego_from_scene(scene) == {
        "x": 10.0,
        "y": 20.0,
        "h": 0.2,
        "speed": 5.5,
    }


# ---------------------------------------------------------------------------
# batch(): a manifest with no `controller:` key routes to virtualdriver
# (byte-identity guarantee three committed regression baselines depend on)
# ---------------------------------------------------------------------------


def test_batch_defaults_to_virtualdriver_controller(tmp_path, monkeypatch):
    manifest = tmp_path / "manifest.yaml"
    manifest.write_text(
        "name: routing_probe\nscenarios:\n  - scenario: does/not/matter.xosc\n",
        encoding="utf-8",
    )
    out_root = tmp_path / "out"

    calls = []

    def _fake_run(*args, **kwargs):
        calls.append((args, kwargs))
        return {
            "scenario": "does/not/matter.xosc",
            "controller": "VirtualDriver",
            "dt": 0.05,
            "frames": 5,
            "sim_duration_s": 1.0,
            "osi": False,
            "commit": "",
        }

    monkeypatch.setattr(gst, "run", _fake_run)

    agg = gst.batch(manifest, out_root)

    assert len(calls) == 1
    call_args, call_kwargs = calls[0]
    # scen_to_run (first positional arg) is the UNMODIFIED resolved scenario
    # path -- no manualdrive config/xosc injection happened.
    assert call_args[0] == gst._resolve_repo("does/not/matter.xosc")
    assert call_kwargs["controller"] == "virtualdriver"
    assert agg["scenarios"][0]["controller"] == "virtualdriver"
    assert agg["scenarios"][0]["error"] is None


def test_batch_manualdrive_entry_with_osi_false_fails_loudly(tmp_path, monkeypatch):
    """The explicit-conflict case the contract calls out: controller:
    manualdrive + osi: false must be a loud per-scenario error, not a
    silent override back to True."""
    manifest = tmp_path / "manifest.yaml"
    manifest.write_text(
        "name: osi_conflict_probe\n"
        "scenarios:\n"
        "  - scenario: does/not/matter.xosc\n"
        "    controller: manualdrive\n"
        "    osi: false\n",
        encoding="utf-8",
    )
    out_root = tmp_path / "out"

    def _fake_run(*args, **kwargs):
        raise AssertionError("run() must not be called when osi:false conflicts")

    monkeypatch.setattr(gst, "run", _fake_run)

    agg = gst.batch(manifest, out_root)

    rec = agg["scenarios"][0]
    assert rec["error"] is not None
    assert "osi: false" in rec["error"]
    assert agg["overall"] == "fail"
