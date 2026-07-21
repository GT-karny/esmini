from __future__ import annotations

import importlib.util
from pathlib import Path


def _load_validator_module():
    repo_root = Path(__file__).resolve().parents[2]
    script_path = (
        repo_root
        / "archive"
        / "frozen_python_verification"
        / "scripts"
        / "validate_realdriver_feature_results.py"
    )
    spec = importlib.util.spec_from_file_location("validator_mod", script_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)  # type: ignore[attr-defined]
    return module


def _write_xodr(path: Path) -> None:
    path.write_text(
        """<?xml version="1.0" standalone="yes"?>
<OpenDRIVE>
  <road name="R1" length="100.0" id="1" junction="-1">
    <link><successor elementType="road" elementId="2" contactPoint="start"/></link>
    <lanes>
      <laneSection s="0.0">
        <center><lane id="0" type="none" level="false"/></center>
        <right><lane id="-1" type="driving" level="false"/></right>
      </laneSection>
    </lanes>
  </road>
  <road name="R2" length="120.0" id="2" junction="-1">
    <link><predecessor elementType="road" elementId="1" contactPoint="end"/></link>
    <lanes>
      <laneSection s="0.0">
        <center><lane id="0" type="none" level="false"/></center>
        <right><lane id="-1" type="driving" level="false"/></right>
      </laneSection>
    </lanes>
  </road>
</OpenDRIVE>
""",
        encoding="utf-8",
    )


def _write_scenario(path: Path, final_lane: int = -1) -> None:
    path.write_text(
        f"""<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <RoadNetwork>
    <LogicFile filepath="map.xodr"/>
  </RoadNetwork>
  <Storyboard>
    <Story name="S">
      <Act name="A">
        <ManeuverGroup maximumExecutionCount="1" name="MG">
          <Actors selectTriggeringEntities="false"><EntityRef entityRef="Ego"/></Actors>
          <Maneuver name="M">
            <Event name="E" priority="overwrite">
              <Action name="Assign">
                <PrivateAction>
                  <RoutingAction>
                    <AssignRouteAction>
                      <Route name="R" closed="false">
                        <Waypoint routeStrategy="shortest"><Position><LanePosition roadId="1" laneId="-1" s="10.0" offset="0.0"/></Position></Waypoint>
                        <Waypoint routeStrategy="shortest"><Position><LanePosition roadId="2" laneId="{final_lane}" s="90.0" offset="0.0"/></Position></Waypoint>
                      </Route>
                    </AssignRouteAction>
                  </RoutingAction>
                </PrivateAction>
              </Action>
            </Event>
          </Maneuver>
        </ManeuverGroup>
      </Act>
    </Story>
  </Storyboard>
</OpenSCENARIO>
""",
        encoding="utf-8",
    )


def test_assign_route_waypoint_validity_pass(tmp_path: Path) -> None:
    mod = _load_validator_module()
    _write_xodr(tmp_path / "map.xodr")
    scenario = tmp_path / "scenario.xosc"
    _write_scenario(scenario, final_lane=-1)

    result = mod.evaluate_assign_route_waypoint_validity(scenario)
    assert result["pass"] is True
    assert result["kpi"]["assign_route_waypoint_validity_pass"] is True
    assert result["kpi"]["assign_route_waypoint_invalid_reasons"] == []


def test_assign_route_waypoint_validity_fails_for_invalid_lane(tmp_path: Path) -> None:
    mod = _load_validator_module()
    _write_xodr(tmp_path / "map.xodr")
    scenario = tmp_path / "scenario.xosc"
    _write_scenario(scenario, final_lane=-2)

    result = mod.evaluate_assign_route_waypoint_validity(scenario)
    assert result["pass"] is False
    reasons = result["kpi"]["assign_route_waypoint_invalid_reasons"]
    assert any("lane_missing" in reason for reason in reasons)


def test_detect_scenario_stop_reason(tmp_path: Path) -> None:
    mod = _load_validator_module()
    stdout = tmp_path / "stdout.txt"
    stdout.write_text("[24.0] StopOnOffroad: true\n", encoding="utf-8")
    assert mod._detect_scenario_stop_reason(tmp_path) == "offroad"


def test_assign_route_completion_does_not_require_hold_time(tmp_path: Path) -> None:
    mod = _load_validator_module()
    _write_xodr(tmp_path / "map.xodr")
    scenario = tmp_path / "scenario.xosc"
    _write_scenario(scenario, final_lane=-1)

    (tmp_path / "stdout.txt").write_text(
        "[3.0] StopAtFinalWaypoint: true\n", encoding="utf-8"
    )
    (tmp_path / "sim.csv").write_text(
        "Version: 2\n"
        "time,id,roadId,laneId,s\n"
        "0.00,0,1,-1,10.0\n"
        "1.00,0,2,-1,90.0\n",
        encoding="utf-8",
    )
    kpi = {"road_id_end": 2, "lane_id_end": -1, "s_end_m": 90.0}

    result = mod.evaluate_assign_route_completion(
        tmp_path, scenario, kpi, s_tolerance_m=5.0
    )
    assert result["pass"] is True
    assert result["kpi"]["assign_route_final_waypoint_reached"] is True
    assert result["kpi"]["assign_route_waypoint_validity_pass"] is True
    assert result["kpi"]["assign_route_end_hold_time_s"] == 0.0
