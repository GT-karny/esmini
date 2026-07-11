"""Tests for services/xosc_paths.absolutize_scenario_paths (vendored, WEB-6)."""

from __future__ import annotations

import os
import xml.etree.ElementTree as ET

from GT_esmini.web.backend.services.xosc_paths import absolutize_scenario_paths

BASE = os.path.abspath(os.path.join(os.sep, "some", "scenario", "dir"))


def _parse(xml: str) -> ET.Element:
    return ET.fromstring(xml)


def test_logic_and_scenegraph_files_absolutized():
    root = _parse(
        """<OpenSCENARIO>
             <RoadNetwork>
               <LogicFile filepath="../xodr/road.xodr"/>
               <SceneGraphFile filepath="../models/road.osgb"/>
             </RoadNetwork>
           </OpenSCENARIO>"""
    )
    absolutize_scenario_paths(root, BASE)
    logic = root.find("RoadNetwork/LogicFile").get("filepath")
    scene = root.find("RoadNetwork/SceneGraphFile").get("filepath")
    assert os.path.isabs(logic)
    assert logic == os.path.normpath(os.path.join(BASE, "../xodr/road.xodr"))
    assert os.path.isabs(scene)
    assert scene == os.path.normpath(os.path.join(BASE, "../models/road.osgb"))


def test_catalog_directories_absolutized():
    root = _parse(
        """<OpenSCENARIO>
             <CatalogLocations>
               <VehicleCatalog><Directory path="../Catalogs/Vehicles"/></VehicleCatalog>
               <ControllerCatalog><Directory path="../Catalogs/Controllers"/></ControllerCatalog>
             </CatalogLocations>
           </OpenSCENARIO>"""
    )
    absolutize_scenario_paths(root, BASE)
    for cat in root.find("CatalogLocations"):
        path = cat.find("Directory").get("path")
        assert os.path.isabs(path), path


def test_controller_properties_file_absolutized():
    root = _parse(
        """<OpenSCENARIO>
             <Entities>
               <Controller><Properties>
                 <File filepath="../sumo/cfg.sumocfg"/>
               </Properties></Controller>
             </Entities>
           </OpenSCENARIO>"""
    )
    absolutize_scenario_paths(root, BASE)
    fp = next(root.iter("File")).get("filepath")
    assert os.path.isabs(fp)
    assert fp == os.path.normpath(os.path.join(BASE, "../sumo/cfg.sumocfg"))


def test_absolute_paths_left_untouched():
    abs_path = os.path.join(BASE, "road.xodr")
    root = _parse(
        f"""<OpenSCENARIO>
              <RoadNetwork><LogicFile filepath="{abs_path}"/></RoadNetwork>
            </OpenSCENARIO>"""
    )
    absolutize_scenario_paths(root, BASE)
    assert root.find("RoadNetwork/LogicFile").get("filepath") == abs_path


def test_empty_and_missing_elements_are_noops():
    # No RoadNetwork / CatalogLocations at all -> must not raise.
    root = _parse("<OpenSCENARIO/>")
    absolutize_scenario_paths(root, BASE)

    # Empty filepath attribute stays empty.
    root = _parse(
        """<OpenSCENARIO>
             <RoadNetwork><LogicFile filepath=""/></RoadNetwork>
           </OpenSCENARIO>"""
    )
    absolutize_scenario_paths(root, BASE)
    assert root.find("RoadNetwork/LogicFile").get("filepath") == ""
