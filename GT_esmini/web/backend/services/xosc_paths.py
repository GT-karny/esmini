"""Vendored XOSC path-absolutization helper.

Vendored from frozen DriverScript/runtime_api.py (GTExecutionPlanner.absolutize_scenario_paths)
-- audit WEB-6; keep behavior-identical.

Plain stdlib function (os, xml.etree.ElementTree only) so the web server never
needs DriverScript on sys.path just to resolve scenario asset paths.
"""

from __future__ import annotations

import os
import xml.etree.ElementTree as ET


def absolutize_scenario_paths(root: ET.Element, base_dir: str) -> None:
    """Absolutize relative resource paths in a parsed XOSC element tree.

    Mutates *root* in-place.  Handles:
    - RoadNetwork/LogicFile filepath
    - RoadNetwork/SceneGraphFile filepath
    - CatalogLocations/*/Directory path
    - Controller Properties/File filepath (e.g. SumoController .sumocfg)

    Args:
        root:     Root element of the parsed OpenSCENARIO XML tree.
        base_dir: Directory to resolve relative paths against (usually the
                  directory that contains the source .xosc file).
    """
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

    # Controller Properties/File (e.g. SumoController .sumocfg)
    for file_elem in root.iter("File"):
        fp = file_elem.get("filepath", "")
        if fp and not os.path.isabs(fp):
            file_elem.set("filepath", os.path.normpath(os.path.join(base_dir, fp)))
