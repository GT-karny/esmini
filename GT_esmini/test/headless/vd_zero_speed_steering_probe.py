"""feature:F7 — 「速度がゼロになると VD の舵が真っ直ぐに戻る」の再現・切り分けプローブ。

3つの別現象を区別するために作った:
  (1) 実物のホイールが物理的に戻る      -> FFB。ここでは扱わない(VD は input_type=stub)
  (2) 描画される車輪の向きが戻る        -> object->wheel_angle_ = csv の Wheel_Angle
  (3) AD の操舵指令がゼロになる          -> telemetry driver.steer / envelope.steer_out

(2) は (3) の結果なので、両方を同一時刻で並べれば「指令が落ちたのか、指令は生きて
いるのに車輪だけ戻ったのか」が一意に決まる。あわせて preview 点列と lookahead も
出すので、プランナ側が退化しているかもその場で判る。

比較軸:
  * カーブ(road 4, R≈49m) と 直線(road 0) — 直線なら舵ゼロが正しいので、
    カーブで戻るかどうかが仕様/不具合の分かれ目になる
  * 停止のさせ方 — SpeedAction で 0 を指定 / 強めの減速で止める

使い方:
  DriverScript/.venv/Scripts/python.exe GT_esmini/test/headless/vd_zero_speed_steering_probe.py --outdir <dir>
"""

import argparse
import json
import math
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
GT_SIM = REPO_ROOT / "build" / "GT_esmini" / "Release" / "GT_Sim.exe"
RELEASE_DIR = REPO_ROOT / "build" / "GT_esmini" / "Release"
PYTHON_EMBED = REPO_ROOT / "thirdparty" / "python-embed" / "python-3.12.10-embed-amd64"
XODR = (REPO_ROOT / "resources" / "xodr" / "f7_curve_onset.xodr").as_posix()
MODEL = (REPO_ROOT / "resources" / "models" / "car_white.osgb").as_posix()

# VirtualDriver ONLY. ManualDrive は登場しない（この件は VD の話）。
SCENARIO = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
   <FileHeader revMajor="1" revMinor="3" date="2026-07-29T00:00:00"
               description="feature:F7 probe: VD steering at standstill" author="GT_esmini"/>
   <ParameterDeclarations/>
   <CatalogLocations/>
   <RoadNetwork><LogicFile filepath="{xodr}"/></RoadNetwork>
   <Entities>
      <ScenarioObject name="Ego">
         <Vehicle name="car_white" vehicleCategory="car" model3d="{model}">
            <BoundingBox><Center x="1.4" y="0.0" z="0.9"/>
               <Dimensions width="2.0" length="5.0" height="1.8"/></BoundingBox>
            <Performance maxSpeed="69" maxDeceleration="30" maxAcceleration="10"/>
            <Axles>
               <FrontAxle maxSteering="30" wheelDiameter="0.8" trackWidth="1.68" positionX="2.98" positionZ="0.4"/>
               <RearAxle maxSteering="30" wheelDiameter="0.8" trackWidth="1.68" positionX="0" positionZ="0.4"/>
            </Axles>
            <Properties><Property name="model_id" value="0"/>
               <Property name="scaleMode" value="ModelToBB"/></Properties>
         </Vehicle>
         <ObjectController>
            <Controller name="VirtualDriverController">
               <Properties><Property name="esminiController" value="VirtualDriverController"/></Properties>
            </Controller>
         </ObjectController>
      </ScenarioObject>
   </Entities>
   <Storyboard>
      <Init><Actions><Private entityRef="Ego">
         <PrivateAction><TeleportAction><Position>
            <LanePosition roadId="{road}" laneId="-1" offset="0" s="20"/>
         </Position></TeleportAction></PrivateAction>
         <PrivateAction><RoutingAction><AssignRouteAction>
            <Route name="r" closed="false">
               <Waypoint routeStrategy="shortest"><Position>
                  <LanePosition roadId="{road}" laneId="-1" offset="0" s="20"/></Position></Waypoint>
               <Waypoint routeStrategy="shortest"><Position>
                  <LanePosition roadId="{road}" laneId="-1" offset="0" s="175"/></Position></Waypoint>
            </Route>
         </AssignRouteAction></RoutingAction></PrivateAction>
         <PrivateAction><LongitudinalAction><SpeedAction>
            <SpeedActionDynamics dynamicsShape="step" value="0" dynamicsDimension="time"/>
            <SpeedActionTarget><AbsoluteTargetSpeed value="10.0"/></SpeedActionTarget>
         </SpeedAction></LongitudinalAction></PrivateAction>
         <PrivateAction><ControllerAction>
            <ActivateControllerAction objectControllerRef="VirtualDriverController"
                                      lateral="true" longitudinal="true"/>
         </ControllerAction></PrivateAction>
      </Private></Actions></Init>
      <Story name="S"><Act name="A">
         <ManeuverGroup maximumExecutionCount="1" name="G">
            <Actors selectTriggeringEntities="false"><EntityRef entityRef="Ego"/></Actors>
            <Maneuver name="M">
               <Event name="Stop" priority="override">
                  <Action name="StopAction"><PrivateAction><LongitudinalAction><SpeedAction>
                     <SpeedActionDynamics dynamicsShape="{shape}" value="{rate}" dynamicsDimension="rate"/>
                     <SpeedActionTarget><AbsoluteTargetSpeed value="0.0"/></SpeedActionTarget>
                  </SpeedAction></LongitudinalAction></PrivateAction></Action>
                  <StartTrigger><ConditionGroup><Condition name="c" delay="0" conditionEdge="none">
                     <ByValueCondition><SimulationTimeCondition value="5" rule="greaterThan"/></ByValueCondition>
                  </Condition></ConditionGroup></StartTrigger>
               </Event>
               {resume_event}
            </Maneuver>
         </ManeuverGroup>
         <StartTrigger><ConditionGroup><Condition name="a" delay="0" conditionEdge="none">
            <ByValueCondition><SimulationTimeCondition value="0" rule="greaterThan"/></ByValueCondition>
         </Condition></ConditionGroup></StartTrigger>
      </Act></Story>
      <StopTrigger><ConditionGroup><Condition name="q" delay="0" conditionEdge="none">
         <ByValueCondition><SimulationTimeCondition value="{dur}" rule="greaterThan"/></ByValueCondition>
      </Condition></ConditionGroup></StopTrigger>
   </Storyboard>
</OpenSCENARIO>
"""


# 再発進イベント。停止中に舵が全舵角まで振れているなら、動き出した瞬間に
# その舵角で走り出すことになる。実害の有無はここで決まる。
RESUME_EVENT = """<Event name="Resume" priority="override">
                  <Action name="ResumeAction"><PrivateAction><LongitudinalAction><SpeedAction>
                     <SpeedActionDynamics dynamicsShape="linear" value="3.0" dynamicsDimension="rate"/>
                     <SpeedActionTarget><AbsoluteTargetSpeed value="10.0"/></SpeedActionTarget>
                  </SpeedAction></LongitudinalAction></PrivateAction></Action>
                  <StartTrigger><ConditionGroup><Condition name="rc" delay="0" conditionEdge="none">
                     <ByValueCondition><SimulationTimeCondition value="{at}" rule="greaterThan"/></ByValueCondition>
                  </Condition></ConditionGroup></StartTrigger>
               </Event>"""


def build_env(telemetry_path):
    env = os.environ.copy()
    env.pop("ELECTRON_RUN_AS_NODE", None)
    env["PATH"] = (
        f"{PYTHON_EMBED}{os.pathsep}{RELEASE_DIR}{os.pathsep}{env.get('PATH', '')}"
    )
    env["GT_VD_TELEMETRY_JSONL"] = str(telemetry_path)
    return env


def run_case(outdir, name, road, shape="linear", rate=3.0, dur=16, resume_at=0.0):
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    xosc = outdir / f"{name}.xosc"
    xosc.write_text(
        SCENARIO.format(
            xodr=XODR,
            model=MODEL,
            road=road,
            shape=shape,
            rate=rate,
            dur=dur,
            resume_event=RESUME_EVENT.format(at=resume_at) if resume_at > 0 else "",
        ),
        encoding="utf-8",
    )
    csv_path = outdir / f"{name}.csv"
    tel_path = outdir / f"{name}.jsonl"
    for p in (csv_path, tel_path):
        if p.exists():
            p.unlink()

    proc = subprocess.run(
        [
            str(GT_SIM),
            "--osc",
            str(xosc),
            "--headless",
            "--fixed_timestep",
            "0.05",
            "--csv_logger",
            str(csv_path),
        ],
        cwd=str(REPO_ROOT),
        env=build_env(tel_path),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=300,
    )
    (outdir / f"{name}.log").write_text(
        (proc.stdout or "") + "\n==STDERR==\n" + (proc.stderr or ""), encoding="utf-8"
    )
    if proc.returncode != 0:
        raise RuntimeError(f"{name}: GT_Sim exit {proc.returncode}")

    frames = []
    if tel_path.exists():
        for line in tel_path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if line:
                try:
                    frames.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
    return frames


def preview_span(fr):
    """Straight-line distance from the first to the last preview point [m]."""
    pts = (fr.get("preview") or {}).get("points") or []
    if len(pts) < 2:
        return 0.0
    return math.hypot(pts[-1]["x"] - pts[0]["x"], pts[-1]["y"] - pts[0]["y"])


def report(name, frames, every=10):
    print(f"\n===== {name} =====")
    if not frames:
        print("  (no telemetry captured)")
        return
    hdr = (
        f"{'t':>6}{'speed':>8}{'driver.steer':>14}{'env.steer_out':>15}"
        f"{'lookahead':>11}{'prev_n':>8}{'prev_span':>11}{'prev_ok':>9}{'drv_ok':>8}"
    )
    print(hdr)
    for fr in frames[::every]:
        pv = fr.get("preview") or {}
        dv = fr.get("driver") or {}
        env = fr.get("envelope") or {}
        print(
            f"{fr.get('sim_time', 0):>6.2f}{fr['ego']['speed']:>8.3f}"
            f"{dv.get('steer', float('nan')):>14.5f}{env.get('steer_out', float('nan')):>15.5f}"
            f"{dv.get('lookahead', float('nan')):>11.3f}{len(pv.get('points') or []):>8}"
            f"{preview_span(fr):>11.3f}{str(pv.get('valid')):>9}{str(dv.get('valid')):>8}"
        )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    args = ap.parse_args()

    cases = [
        # name,                road, shape,   rate
        ("curve_decel_stop", 4, "linear", 3.0),  # R~49m arc, braked to a stop
        ("straight_decel_stop", 0, "linear", 3.0),  # straight road, same stop
        ("curve_step_stop", 4, "step", 0.0),  # instant SpeedAction 0
    ]
    for name, road, shape, rate in cases:
        frames = run_case(args.outdir, name, road, shape=shape, rate=rate)
        report(name, frames)
    return 0


if __name__ == "__main__":
    sys.exit(main())
