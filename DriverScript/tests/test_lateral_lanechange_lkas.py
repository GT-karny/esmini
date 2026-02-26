import pytest

from realdriver.lateral_controller import LateralController
from realdriver.lane_change_controller import LaneChangeController
from realdriver.lkas import LKASController


class DummyState:
    x = 0.0
    y = 0.0
    z = 0.0
    h = 0.0
    speed = 5.0


def test_lateral_controller_without_waypoints_returns_range():
    c = LateralController(rm_lib=None)
    s = c.update_from_state(DummyState(), 0.1)
    assert -1.0 <= s <= 1.0


def test_lane_change_requires_rm_lib():
    with pytest.raises(ValueError):
        LaneChangeController(rm_lib=None)


def test_lkas_missing_files_raise():
    with pytest.raises(FileNotFoundError):
        LKASController(lib_path="missing.dll", xodr_path="missing.xodr")
