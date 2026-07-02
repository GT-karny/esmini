"""authoring_common.py — shared helpers for all scenario-authoring generators.

Generators import this module via:
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from authoring_common import repo_root, git_short_hash, write_meta_yaml, ...

For a generator at resources/scenario_authoring/<subdir>/gen_*.py, parents[1]
resolves to resources/scenario_authoring/ (this module's directory), making
`authoring_common` importable from any generator subdirectory.
"""
from __future__ import annotations

import datetime as dt
import re
import subprocess
from pathlib import Path
from typing import Any

import yaml
from scenariogeneration import xosc


# ---------------------------------------------------------------------------
# Repository utilities
# ---------------------------------------------------------------------------

def repo_root() -> Path:
    """Return the repository root as an absolute Path.

    Resolution: this file lives at  resources/scenario_authoring/authoring_common.py
    so parents[0] = scenario_authoring/, parents[1] = resources/, parents[2] = repo root.
    """
    return Path(__file__).resolve().parents[2]


def git_short_hash() -> str:
    """Return the short git commit hash of HEAD, or 'unknown' on failure."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            cwd=str(repo_root()),
            timeout=10,
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except Exception:
        pass
    return "unknown"


# ---------------------------------------------------------------------------
# Deterministic header dates (xodr <header> AND xosc <FileHeader>)
# ---------------------------------------------------------------------------

# scenariogeneration stamps BOTH the OpenDRIVE <header date="..."> and the
# OpenSCENARIO <FileHeader ... date="..."> with datetime.now() unless an explicit
# date is supplied, which makes regeneration non-reproducible. Two complementary
# guards keep committed artifacts byte-stable across regenerations:
#   * xosc generators pass PINNED_XOSC_DATE to xosc.Scenario(creation_date=...),
#     which stamps a fixed ISO date directly (the clean, primary path).
#   * normalize_header_date() is a byte-level safety net that rewrites whichever
#     date attribute is present (xodr <header> OR xosc <FileHeader>) to a fixed
#     value, used by the xodr generators (scenariogeneration's xodr writer has no
#     creation_date hook) and available to xosc generators as a backstop.

# A single fixed datetime for every committed scenario-authoring artifact, so all
# of the layer-1 catalog shares one reproducible stamp.
PINNED_XOSC_DATE = dt.datetime(2026, 6, 13, 0, 0, 0)

# Matches either  <header ... date="...">  (OpenDRIVE)  or
#                 <FileHeader ... date="...">  (OpenSCENARIO).
_HEADER_DATE_RE = re.compile(rb'(<(?:header|FileHeader)\b[^>]*\bdate=")[^"]*(")')


def normalize_header_date(path: Path, date_str: str) -> None:
    """Rewrite the header date attribute of *path* to *date_str*.

    Handles BOTH the OpenDRIVE ``<header date="...">`` and the OpenSCENARIO
    ``<FileHeader ... date="...">`` attributes (whichever the file carries).
    Makes scenariogeneration output reproducible (it otherwise stamps
    datetime.now()). Operates on raw bytes to avoid reformatting the rest of the
    document (so default-path output stays byte-identical to the committed file).
    """
    data = path.read_bytes()
    new = _HEADER_DATE_RE.sub(rb"\g<1>" + date_str.encode("utf-8") + rb"\g<2>", data, count=1)
    if new != data:
        path.write_bytes(new)


# ---------------------------------------------------------------------------
# Metadata I/O
# ---------------------------------------------------------------------------

def write_meta_yaml(path: Path, data: dict[str, Any]) -> None:
    """Write *data* to *path* as YAML (utf-8, insertion-order preserved).

    Sorted keys are intentionally disabled so the caller controls field order
    (makes the file human-readable in the order that matters logically).
    """
    path.write_text(
        yaml.safe_dump(data, allow_unicode=True, sort_keys=False),
        encoding="utf-8",
    )


# ---------------------------------------------------------------------------
# Vehicle catalog helpers
# ---------------------------------------------------------------------------

def make_ego_vehicle() -> xosc.Vehicle:
    """Return the standard GT_esmini Ego vehicle (car_white, model_id=0).

    Dimensions and axle geometry match the hand-authored verification xoscs
    (e.g. resources/xosc/verification/03_traffic_signals/green_no_stop.xosc).
    """
    bb = xosc.BoundingBox(2.0, 5.0, 1.8, 1.4, 0.0, 0.9)
    front_axle = xosc.Axle(0.52, 0.8, 1.68, 2.98, 0.4)
    rear_axle = xosc.Axle(0.0, 0.8, 1.68, 0.0, 0.4)
    # API order: max_speed, max_acceleration, max_deceleration
    # => 69 m/s top speed, 10 m/s² accel, 30 m/s² decel
    veh = xosc.Vehicle(
        "car_white",
        xosc.VehicleCategory.car,
        bb,
        front_axle,
        rear_axle,
        69,   # max_speed
        10,   # max_acceleration
        30,   # max_deceleration
    )
    veh.add_property("model_id", "0")
    return veh


def make_npc_vehicle(model_id: str = "1", name: str = "car_red") -> xosc.Vehicle:
    """Return a standard NPC vehicle with parameterized model_id and name.

    Shares the same bounding-box / axle geometry as the Ego to keep vehicle
    dynamics consistent across all generated scenarios.
    """
    bb = xosc.BoundingBox(2.0, 5.0, 1.8, 1.4, 0.0, 0.9)
    front_axle = xosc.Axle(0.52, 0.8, 1.68, 2.98, 0.4)
    rear_axle = xosc.Axle(0.0, 0.8, 1.68, 0.0, 0.4)
    veh = xosc.Vehicle(
        name,
        xosc.VehicleCategory.car,
        bb,
        front_axle,
        rear_axle,
        69,   # max_speed
        10,   # max_acceleration
        30,   # max_deceleration
    )
    veh.add_property("model_id", model_id)
    return veh


def make_virtual_driver_controller() -> xosc.Controller:
    """Return the VirtualDriverController ObjectController element.

    Matches the exact pattern in
    resources/xosc/verification/03_traffic_signals/green_no_stop.xosc:

        <Controller name="VirtualDriverController">
            <Properties>
                <Property name="esminiController" value="VirtualDriverController"/>
            </Properties>
        </Controller>
    """
    props = xosc.Properties()
    props.add_property("esminiController", "VirtualDriverController")
    return xosc.Controller("VirtualDriverController", props)


# ---------------------------------------------------------------------------
# Scenario assembly helpers (shared by the scene generators 07/08)
# ---------------------------------------------------------------------------

def step_dynamics() -> xosc.TransitionDynamics:
    """A zero-time step transition (instantaneous speed change).

    Used for the constant-speed NPC launches and the Ego initial-speed set, so
    actors snap to their target speed deterministically rather than ramping.
    """
    return xosc.TransitionDynamics(
        xosc.DynamicsShapes.step, xosc.DynamicsDimension.time, 0.0
    )


def lane_pos(road_id: int, lane_id: int, s: float, offset: float = 0.0) -> xosc.LanePosition:
    """Convenience wrapper for xosc.LanePosition with int road/lane ids.

    scenariogeneration takes lane_id / road_id as strings; this keeps the
    generators readable (they reason about ids as integers).
    """
    return xosc.LanePosition(s, offset, str(lane_id), str(road_id))


def make_route(name: str, waypoints: list[xosc.LanePosition]) -> xosc.Route:
    """Build an explicit (deterministic) Route through the given waypoints.

    All waypoints use routeStrategy="shortest" — the same strategy the
    hand-authored junction scenarios use (05_anticipation/*.xosc). The route is
    what makes NPC/ego junction traversal deterministic instead of leaving the
    turn choice to esmini's default junction selection.
    """
    route = xosc.Route(name)
    for wp in waypoints:
        route.add_waypoint(wp, xosc.RouteStrategy.shortest)
    return route


def add_routed_actor_init(
    init: xosc.Init,
    name: str,
    teleport: xosc.LanePosition,
    route: xosc.Route,
    init_speed: float,
) -> None:
    """Add the standard Init actions for a routed actor: teleport + route + speed.

    Order mirrors the hand-authored verification xoscs (teleport, then route,
    then speed). Used for both the ego (caller adds ActivateControllerAction
    afterwards) and the NPCs.
    """
    init.add_init_action(name, xosc.TeleportAction(teleport))
    init.add_init_action(name, xosc.AssignRouteAction(route))
    init.add_init_action(name, xosc.AbsoluteSpeedAction(init_speed, step_dynamics()))


def sim_time_trigger(
    name: str, t: float, edge: xosc.ConditionEdge = xosc.ConditionEdge.none,
    triggeringpoint: str = "start",
) -> xosc.ValueTrigger:
    """A SimulationTime > t trigger (the only trigger kind these scenes need)."""
    return xosc.ValueTrigger(
        name, 0.0, edge,
        xosc.SimulationTimeCondition(t, xosc.Rule.greaterThan),
        triggeringpoint=triggeringpoint,
    )


def make_launch_act(
    act_name: str,
    launches: list[tuple[str, float, float]],
) -> xosc.Act:
    """Build a Story Act that launches each NPC at a per-actor release time.

    *launches* is a list of (actor_name, release_time_s, target_speed) triples.
    Each actor is teleported at rest in Init, then this Act sets it to a constant
    target speed (step dynamics) once SimulationTime exceeds release_time. This is
    how oncoming/cross arrival timing is controlled deterministically without
    ever overflowing the fixed-length approach leg: the actor waits in place and
    launches late rather than being teleported implausibly far back.

    The Act starts at t>0; each launch Event carries its own SimulationTime
    trigger.
    """
    act = xosc.Act(act_name, sim_time_trigger(f"{act_name}_start", 0.0))
    for actor_name, release_t, speed in launches:
        mg = xosc.ManeuverGroup(f"{actor_name}_mg")
        mg.add_actor(actor_name)
        man = xosc.Maneuver(f"{actor_name}_launch")
        ev = xosc.Event(f"{actor_name}_go", xosc.Priority.overwrite)
        ev.add_action(
            f"{actor_name}_speed",
            xosc.AbsoluteSpeedAction(speed, step_dynamics()),
        )
        ev.add_trigger(sim_time_trigger(f"{actor_name}_trig", release_t))
        man.add_event(ev)
        mg.add_maneuver(man)
        act.add_maneuver_group(mg)
    return act


def assemble_scenario(
    name: str,
    description: str,
    roadfile_rel: str,
    entities: xosc.Entities,
    init: xosc.Init,
    stop_time_s: float,
    launch_act: xosc.Act | None = None,
) -> xosc.Scenario:
    """Assemble a complete OpenSCENARIO object with a fixed creation date.

    * roadfile_rel: the xodr path RELATIVE to the scenario file location
      (../../road_catalog/generated/<road>.xodr).
    * stop_time_s: SimulationTime > stop_time_s ends the run.
    * launch_act: optional Story Act (from make_launch_act) for delayed NPC
      launches.

    The creation date is pinned (PINNED_XOSC_DATE) so the serialized
    <FileHeader date="..."> is byte-stable across regenerations.
    """
    stop_trigger = sim_time_trigger(
        "stop", stop_time_s, xosc.ConditionEdge.rising, triggeringpoint="stop"
    )
    sb = xosc.StoryBoard(init, stop_trigger)
    if launch_act is not None:
        story = xosc.Story("LaunchStory")
        story.add_act(launch_act)
        sb.add_story(story)

    rn = xosc.RoadNetwork(roadfile=roadfile_rel)
    return xosc.Scenario(
        name,
        "GT_esmini",
        xosc.ParameterDeclarations(),
        entities,
        sb,
        rn,
        xosc.Catalog(),
        osc_minor_version=1,
        creation_date=PINNED_XOSC_DATE,
    )


def write_scenario(scenario: xosc.Scenario, xosc_path: Path) -> None:
    """Write *scenario* to *xosc_path* and pin its <FileHeader> date.

    creation_date already pins the date inside the Scenario, but
    normalize_header_date() is applied as a belt-and-braces backstop so the
    committed file's date is guaranteed fixed regardless of scenariogeneration
    version drift in how creation_date is serialized.
    """
    scenario.write_xml(str(xosc_path))
    normalize_header_date(xosc_path, PINNED_XOSC_DATE.strftime("%Y-%m-%dT%H:%M:%S"))


# ---------------------------------------------------------------------------
# Pedestrian helpers (scenario set 09 — crosswalk; F2 Phase 3d extension)
# ---------------------------------------------------------------------------

def make_pedestrian(name: str = "pedestrian_adult") -> xosc.Pedestrian:
    """Return an INLINE Pedestrian entity matching the upstream
    PedestrianCatalog pedestrian_adult (mass 80, model EPTa, BB 0.6 long x
    0.5 wide x 1.8 high, center (0.06, 0, 0.923)).

    Inline — not a CatalogReference — so generated scenarios carry no catalog
    directory resolution dependency (they live outside resources/xosc and are
    copied around by the policy-injection harness). The REAL bounding-box dims
    matter: the OSI OBB anti-collision matcher (min_obb_separation_above in
    gt_sim_test) downgrades pass->skip when an object reports no real extents
    (4.0x2.0 fallback), which would gut the crosswalk anti-collision gate.
    Verified: OSI reports length 0.6 / width 0.5 / type 'pedestrian' for this
    entity. model_id 7 = walkman.osgb (resources/model_ids.txt) so the viewer
    shows the standard walking figure.
    """
    bb = xosc.BoundingBox(0.5, 0.6, 1.8, 0.06, 0.0, 0.923)
    ped = xosc.Pedestrian(
        name, 80.0, xosc.PedestrianCategory.pedestrian, bb, model="EPTa"
    )
    ped.add_property("model_id", "7")
    ped.add_property("scaleMode", "BBToModel")
    return ped


def make_crossing_trajectory(
    name: str, road_id: int, s: float, from_lane: int, to_lane: int
) -> xosc.Trajectory:
    """A 2-vertex straight polyline crossing the road laterally at fixed *s*.

    The vertices deliberately carry NO Orientation element. esmini resolves an
    explicit relative vertex heading against the LANE driving direction (left
    lanes point -s), which flips the trajectory's explicit H away from the
    crossing direction; FollowTrajectoryAction::Start then computes
    initialHeadingSign_ = -1, the trajectory s immediately runs below 0 and the
    action ends on the first step (the ped reverts to default lane-follow and
    walks ALONG the road — verified empirically). With no explicit orientation
    esmini auto-aligns the entity heading with the trajectory tangent
    (OSCPrivateAction.cpp Start(), heading-not-specified branch) and the ped
    walks straight across at its current speed.
    """
    v0 = xosc.LanePosition(s, 0.0, str(from_lane), str(road_id))
    v1 = xosc.LanePosition(s, 0.0, str(to_lane), str(road_id))
    traj = xosc.Trajectory(name, closed=False)
    traj.add_shape(xosc.Polyline([], [v0, v1]))
    return traj


def make_ped_crossing_act(
    act_name: str,
    actor: str,
    release_t: float,
    walk_speed: float,
    trajectory: xosc.Trajectory,
) -> xosc.Act:
    """Story Act that releases a pedestrian crossing at *release_t*.

    One Event with two actions (upstream pedestrian.xosc pattern):
      * AbsoluteSpeedAction (step) to *walk_speed*, and
      * FollowTrajectoryAction (followingMode=follow, TimeReference None) along
        *trajectory* (from make_crossing_trajectory).
    The ped is teleported AT REST in Init (speed 0) and starts walking when
    SimulationTime exceeds release_t — the same deterministic release-time
    launch model make_launch_act uses for NPC vehicles. After the trajectory is
    exhausted esmini reverts the ped to default lane-follow on the destination
    sidewalk (it keeps walking along it), which is plausible background motion.
    """
    act = xosc.Act(act_name, sim_time_trigger(f"{act_name}_start", 0.0))
    mg = xosc.ManeuverGroup(f"{actor}_mg")
    mg.add_actor(actor)
    man = xosc.Maneuver(f"{actor}_walk")
    ev = xosc.Event(f"{actor}_go", xosc.Priority.overwrite)
    ev.add_action(
        f"{actor}_speed", xosc.AbsoluteSpeedAction(walk_speed, step_dynamics())
    )
    ev.add_action(
        f"{actor}_traj",
        xosc.FollowTrajectoryAction(trajectory, xosc.FollowingMode.follow),
    )
    ev.add_trigger(sim_time_trigger(f"{actor}_trig", release_t))
    man.add_event(ev)
    mg.add_maneuver(man)
    act.add_maneuver_group(mg)
    return act
