from __future__ import annotations

import os
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Callable


LogCallback = Callable[[str], None]


def safe_split(arg_line: str) -> list[str]:
    if not arg_line.strip():
        return []
    return shlex.split(arg_line, posix=False)


def has_option(args: list[str], opt: str) -> bool:
    return opt in args


def strip_option(args: list[str], opt: str) -> list[str]:
    out: list[str] = []
    i = 0
    while i < len(args):
        if args[i] == opt:
            i += 1
            if i < len(args) and not args[i].startswith("--"):
                i += 1
            continue
        out.append(args[i])
        i += 1
    return out


@dataclass
class GTSimArgsRequest:
    scenario_path: str
    realdriver_enabled: bool
    entity_name: str
    base_port: int
    osi: str
    hz: float
    window: tuple[int, int, int, int]
    use_threads: bool
    extra_args_line: str = ""


@dataclass
class PythonArgsRequest:
    python_executable: str
    script_path: str
    scenario_path: str
    dynamic_args: list[str] = field(default_factory=list)
    extra_args_line: str = ""
    auto_xodr: bool = True


@dataclass
class ProcessStartRequest:
    program: str
    args: list[str]
    cwd: str
    env: dict[str, str] | None = None


class GTExecutionPlanner:
    def __init__(self, on_log: LogCallback | None = None) -> None:
        self.last_temp_xosc = ""
        self._on_log = on_log or (lambda _msg: None)

    def validate_python_paths(self, python_path: str, script_path: str) -> None:
        if not python_path or not os.path.exists(python_path):
            raise ValueError("Python path is invalid.")
        if not script_path or not os.path.exists(script_path):
            raise ValueError("RealDriver Python script path is invalid.")

    def validate_gtsim_paths(self, gtsim_path: str, scenario_path: str) -> None:
        if not gtsim_path or not os.path.exists(gtsim_path):
            raise ValueError("GT_Sim.exe path is invalid.")
        if not scenario_path or not os.path.exists(scenario_path):
            raise ValueError("Scenario path is invalid.")
        dep_errors = self.check_scenario_dependencies(scenario_path)
        if dep_errors:
            msg = "Scenario dependency check failed:\n- " + "\n- ".join(dep_errors[:8])
            raise ValueError(msg)

    def check_scenario_dependencies(self, scenario_path: str) -> list[str]:
        errors: list[str] = []
        try:
            tree = ET.parse(scenario_path)
            root = tree.getroot()
            scenario_dir = os.path.dirname(os.path.abspath(scenario_path))

            catalog_dirs: list[str] = []
            cat_locations = root.find("CatalogLocations")
            if cat_locations is not None:
                for cat in list(cat_locations):
                    directory = cat.find("Directory")
                    if directory is None:
                        continue
                    path = (directory.get("path") or "").strip()
                    if not path:
                        continue
                    abs_dir = path if os.path.isabs(path) else os.path.normpath(os.path.join(scenario_dir, path))
                    catalog_dirs.append(abs_dir)

            catalog_refs = root.findall(".//CatalogReference")
            if catalog_refs and not catalog_dirs:
                errors.append("CatalogReference exists but CatalogLocations/Directory is missing.")
                return errors

            for ref in catalog_refs:
                catalog_name = (ref.get("catalogName") or "").strip()
                if not catalog_name:
                    continue
                candidates = [catalog_name]
                if not catalog_name.lower().endswith(".xosc"):
                    candidates.append(f"{catalog_name}.xosc")
                found = False
                for base in catalog_dirs:
                    for name in candidates:
                        if os.path.exists(os.path.join(base, name)):
                            found = True
                            break
                    if found:
                        break
                if not found:
                    if catalog_dirs:
                        errors.append(
                            f"Catalog '{catalog_name}' not found in: {', '.join(catalog_dirs)}"
                        )
                    else:
                        errors.append(f"Catalog '{catalog_name}' not found.")
        except Exception as exc:
            errors.append(f"Failed to parse scenario: {exc}")
        return errors

    def get_target_scenario(self, source_path: str, realdriver_enabled: bool, entity_name: str, base_port: int) -> str:
        if not realdriver_enabled:
            return source_path
        return self.generate_temp_realdriver_scenario(
            src_path=source_path,
            entity_name=entity_name,
            base_port=base_port,
        )

    def generate_temp_realdriver_scenario(self, src_path: str, entity_name: str, base_port: int) -> str:
        tree = ET.parse(src_path)
        root = tree.getroot()

        entities = root.find("Entities")
        if entities is None:
            raise RuntimeError("Entities node is missing in scenario.")

        target_name = entity_name.strip() or "Ego"
        target_obj = None
        for obj in entities.findall("ScenarioObject"):
            if obj.get("name") == target_name:
                target_obj = obj
                break

        if target_obj is None:
            for obj in entities.findall("ScenarioObject"):
                if obj.get("name") == "Ego":
                    target_obj = obj
                    target_name = "Ego"
                    break

        if target_obj is None:
            target_obj = entities.find("ScenarioObject")
            if target_obj is None:
                raise RuntimeError("No ScenarioObject found in Entities.")
            target_name = target_obj.get("name", "Ego")

        for existing_obj_ctrl in list(target_obj.findall("ObjectController")):
            target_obj.remove(existing_obj_ctrl)

        obj_ctrl = ET.SubElement(target_obj, "ObjectController")
        controller = ET.SubElement(obj_ctrl, "Controller")
        controller.set("name", "RealDriverController")

        props = controller.find("Properties")
        if props is None:
            props = ET.SubElement(controller, "Properties")

        def _set_prop(name: str, value: str) -> None:
            target = None
            for prop in props.findall("Property"):
                if prop.get("name") == name:
                    target = prop
                    break
            if target is None:
                target = ET.SubElement(props, "Property")
                target.set("name", name)
            target.set("value", value)

        _set_prop("esminiController", "RealDriverController")
        _set_prop("BasePort", str(base_port))
        _set_prop("ClientPort", str(base_port + 1000))
        _set_prop("SendWaypoints", "true")
        _set_prop("WaypointPort", str(base_port + 1001))

        storyboard = root.find("Storyboard")
        if storyboard is None:
            raise RuntimeError("Storyboard node is missing in scenario.")
        init = storyboard.find("Init")
        if init is None:
            init = ET.SubElement(storyboard, "Init")
        actions = init.find("Actions")
        if actions is None:
            actions = ET.SubElement(init, "Actions")

        target_private = None
        for private in actions.findall("Private"):
            if private.get("entityRef") == target_name:
                target_private = private
                break
        if target_private is None:
            target_private = ET.SubElement(actions, "Private")
            target_private.set("entityRef", target_name)

        activate_exists = False
        for private_action in target_private.findall("PrivateAction"):
            activate = private_action.find("ActivateControllerAction")
            if activate is not None:
                activate.set("longitudinal", "true")
                activate.set("lateral", "true")
                activate_exists = True
                break
        if not activate_exists:
            private_action = ET.SubElement(target_private, "PrivateAction")
            activate = ET.SubElement(private_action, "ActivateControllerAction")
            activate.set("longitudinal", "true")
            activate.set("lateral", "true")

        src_dir = os.path.dirname(os.path.abspath(src_path))
        base_name = os.path.splitext(os.path.basename(src_path))[0]
        temp_dir = os.path.join(src_dir, ".gt_sim_temp")
        os.makedirs(temp_dir, exist_ok=True)
        self.absolutize_scenario_paths(root, src_dir)
        out_path = os.path.join(temp_dir, f"{base_name}_realdriver_temp.xosc")

        tree.write(out_path, encoding="utf-8", xml_declaration=True)
        self.last_temp_xosc = out_path
        self._on_log(f"Generated temp scenario: {out_path}")
        return out_path

    @staticmethod
    def absolutize_scenario_paths(root: ET.Element, base_dir: str) -> None:
        logic = root.find("RoadNetwork/LogicFile")
        if logic is not None:
            fp = logic.get("filepath", "")
            if fp and not os.path.isabs(fp):
                logic.set("filepath", os.path.normpath(os.path.join(base_dir, fp)))

        scene = root.find("RoadNetwork/SceneGraphFile")
        if scene is not None:
            fp = scene.get("filepath", "")
            if fp and not os.path.isabs(fp):
                scene.set("filepath", os.path.normpath(os.path.join(base_dir, fp)))

        cat_locs = root.find("CatalogLocations")
        if cat_locs is not None:
            for cat in list(cat_locs):
                directory = cat.find("Directory")
                if directory is None:
                    continue
                path = directory.get("path", "")
                if path and not os.path.isabs(path):
                    directory.set("path", os.path.normpath(os.path.join(base_dir, path)))

    def build_gtsim_args(self, req: GTSimArgsRequest) -> list[str]:
        scenario_path = self.get_target_scenario(
            source_path=req.scenario_path,
            realdriver_enabled=req.realdriver_enabled,
            entity_name=req.entity_name,
            base_port=req.base_port,
        )
        args = ["--osc", scenario_path]

        osi_value = req.osi.strip()
        if osi_value:
            args += ["--osi", osi_value]

        args += ["--hz", f"{req.hz:.1f}"]
        args += ["--window", *(str(v) for v in req.window)]

        if req.use_threads:
            args.append("--threads")

        args += safe_split(req.extra_args_line)
        return args

    @staticmethod
    def resolve_logicfile_xodr(scenario_path: str) -> str:
        if not scenario_path or not os.path.exists(scenario_path):
            return ""
        try:
            tree = ET.parse(scenario_path)
            root = tree.getroot()
            logic = root.find("RoadNetwork/LogicFile")
            if logic is None:
                return ""
            logic_fp = logic.get("filepath", "").strip()
            if not logic_fp:
                return ""
            if os.path.isabs(logic_fp):
                return logic_fp if os.path.exists(logic_fp) else ""
            abs_fp = os.path.normpath(os.path.join(os.path.dirname(scenario_path), logic_fp))
            return abs_fp if os.path.exists(abs_fp) else ""
        except Exception:
            return ""

    @staticmethod
    def resolve_default_lib_path_for_script(script_path: str) -> str:
        if not script_path:
            return ""
        script_dir = os.path.dirname(os.path.abspath(script_path))
        candidate = os.path.normpath(os.path.join(script_dir, "..", "bin", "esminiRMLib.dll"))
        return candidate if os.path.exists(candidate) else ""

    @staticmethod
    def resolve_default_gt_lib_path_for_script(script_path: str) -> str:
        if not script_path:
            return ""
        script_dir = os.path.dirname(os.path.abspath(script_path))
        candidate = os.path.normpath(os.path.join(script_dir, "..", "bin", "GT_esminiLib.dll"))
        return candidate if os.path.exists(candidate) else ""

    def build_python_args(self, req: PythonArgsRequest, on_log: LogCallback | None = None) -> list[str]:
        logger = on_log or self._on_log
        extra_args = list(req.dynamic_args) + safe_split(req.extra_args_line)
        args = []

        if os.path.basename(req.python_executable).lower().startswith("python"):
            args.append("-u")

        supports_xodr = False
        supports_mode = False
        try:
            with open(req.script_path, "r", encoding="utf-8", errors="ignore") as f:
                src = f.read()
            supports_xodr = "--xodr_path" in src
            supports_mode = "--mode" in src
        except Exception:
            pass

        xodr_path = self.resolve_logicfile_xodr(req.scenario_path)

        if req.auto_xodr and supports_xodr and xodr_path and not has_option(extra_args, "--xodr_path"):
            extra_args += ["--xodr_path", xodr_path]
            logger(f"Auto-added --xodr_path from scenario LogicFile: {xodr_path}")
        elif req.auto_xodr and supports_xodr and not has_option(extra_args, "--xodr_path"):
            raise RuntimeError(
                "Failed to resolve --xodr_path from selected Scenario (.xosc). "
                "Select a valid scenario with <RoadNetwork><LogicFile ...>, or set --xodr_path manually."
            )

        if not supports_mode and has_option(extra_args, "--mode"):
            extra_args = strip_option(extra_args, "--mode")
            logger("Removed --mode argument because selected script does not define --mode.")

        script_basename = os.path.basename(req.script_path).lower()
        if script_basename == "scenario_drive_example.py" and not has_option(extra_args, "--mode"):
            logger(
                "Hint: scenario_drive_example.py default mode is 'waypoints'. "
                "Use '--mode udp' if you want route/waypoints from ControllerRealDriver."
            )

        args += [req.script_path] + extra_args
        return args

    @staticmethod
    def resolve_python_workdir(script_path: str) -> str:
        script_dir = os.path.dirname(script_path) or os.getcwd()
        parent_dir = os.path.dirname(script_dir)
        if os.path.exists(os.path.join(parent_dir, "bin", "esminiRMLib.dll")):
            return parent_dir
        return script_dir


class GTExecutionService:
    """GUI外から実行制御できる軽量API。"""

    def __init__(self, planner: GTExecutionPlanner | None = None) -> None:
        self.planner = planner or GTExecutionPlanner()
        self.python_proc: subprocess.Popen | None = None
        self.gtsim_proc: subprocess.Popen | None = None

    def start_process(self, req: ProcessStartRequest) -> subprocess.Popen:
        env = os.environ.copy()
        if req.env:
            env.update(req.env)
        return subprocess.Popen(
            [req.program, *req.args],
            cwd=req.cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def stop_process(self, proc: subprocess.Popen | None, timeout_sec: float = 3.0) -> None:
        if proc is None or proc.poll() is not None:
            return
        proc.terminate()
        try:
            proc.wait(timeout=timeout_sec)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2.0)

    @staticmethod
    def timestamped(message: str) -> str:
        return f"[{time.strftime('%H:%M:%S')}] {message}"
