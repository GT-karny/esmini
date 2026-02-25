"""Level 3b: Unit tests for FrameAdapter."""

import pytest
from pythondriver.adapters.frame_adapter import FrameAdapter, EmbeddedFrame


# L3b-001: Parse empty dict
def test_from_dict_empty():
    """Test parsing empty frame data."""
    frame = FrameAdapter.from_dict({})

    assert isinstance(frame, EmbeddedFrame)
    assert frame.frame_id == -1
    assert frame.ground_truth_bytes == b""
    assert frame.waypoints == []
    assert frame.waypoint_index == 0
    assert frame.lon_profile == []
    assert frame.set_speed == 0.0
    assert frame.current_speed == 0.0
    assert frame.dt == 0.0


# L3b-002: Parse dict with waypoints
def test_from_dict_with_waypoints():
    """Test parsing frame data with waypoints."""
    data = {
        "frame_id": 42,
        "waypoints": [
            {"x": 1.0, "y": 2.0, "h": 0.5, "road_id": 1, "s": 10.0, "lane_id": -1, "lane_offset": 0.1},
            {"x": 3.0, "y": 4.0, "h": 0.6, "road_id": 1, "s": 15.0, "lane_id": -1, "lane_offset": 0.0},
        ],
        "waypoint_index": 1,
        "set_speed": 15.0,
        "current_speed": 10.0,
        "dt": 0.01,
    }
    frame = FrameAdapter.from_dict(data)

    assert frame.frame_id == 42
    assert len(frame.waypoints) == 2
    assert frame.waypoints[0].x == pytest.approx(1.0)
    assert frame.waypoints[0].y == pytest.approx(2.0)
    assert frame.waypoints[0].h == pytest.approx(0.5)
    assert frame.waypoints[0].lane_offset == pytest.approx(0.1)
    assert frame.waypoint_index == 1
    assert frame.set_speed == pytest.approx(15.0)
    assert frame.current_speed == pytest.approx(10.0)
    assert frame.dt == pytest.approx(0.01)


def test_from_dict_with_lon_profile():
    """Test parsing frame data with longitudinal profile."""
    data = {
        "lon_profile": [
            {"t_offset": 0.0, "v_target": 10.0, "a_max": 2.0, "j_max": 1.0},
            {"t_offset": 0.15, "v_target": 15.0, "a_max": 2.0, "j_max": 1.0},
        ],
    }
    frame = FrameAdapter.from_dict(data)

    assert len(frame.lon_profile) == 2
    assert frame.lon_profile[0]["v_target"] == pytest.approx(10.0)
    assert frame.lon_profile[1]["t_offset"] == pytest.approx(0.15)


def test_from_dict_with_ground_truth_bytes():
    """Test parsing frame data with ground truth bytes."""
    data = {
        "ground_truth_bytes": b"\x00\x01\x02\x03",
    }
    frame = FrameAdapter.from_dict(data)

    assert frame.ground_truth_bytes == b"\x00\x01\x02\x03"


def test_from_dict_with_none_waypoints():
    """Test parsing frame data with None waypoints."""
    data = {"waypoints": None}
    frame = FrameAdapter.from_dict(data)

    assert frame.waypoints == []


def test_from_dict_with_none_lon_profile():
    """Test parsing frame data with None lon_profile."""
    data = {"lon_profile": None}
    frame = FrameAdapter.from_dict(data)

    assert frame.lon_profile == []


# L3b-003: to_result() minimal output
def test_to_result_minimal():
    """Test creating minimal result dict."""
    result = FrameAdapter.to_result(throttle=0.5, brake=0.1, steering=-0.2)

    assert result["throttle"] == pytest.approx(0.5)
    assert result["brake"] == pytest.approx(0.1)
    assert result["steering"] == pytest.approx(-0.2)
    assert result["gear"] == 1
    assert result["lights"] == {}
    assert result["engine_brake"] == pytest.approx(0.49)
    assert result["adas_states"] == []


# L3b-004: to_result() full output
def test_to_result_full():
    """Test creating result dict with all fields."""
    result = FrameAdapter.to_result(
        throttle=1.0,
        brake=0.0,
        steering=0.5,
        gear=-1,
        lights={"low_beam": "on", "brake": "auto"},
        engine_brake=0.3,
        adas_states=[1, 0, 2],
    )

    assert result["throttle"] == pytest.approx(1.0)
    assert result["steering"] == pytest.approx(0.5)
    assert result["gear"] == -1
    assert result["lights"]["low_beam"] == "on"
    assert result["lights"]["brake"] == "auto"
    assert result["engine_brake"] == pytest.approx(0.3)
    assert result["adas_states"] == [1, 0, 2]


def test_to_result_converts_to_correct_types():
    """Test that to_result() converts values to correct types."""
    result = FrameAdapter.to_result(
        throttle=1,  # int instead of float
        brake="0.5",  # string - should fail or convert?
        steering=0,
    )

    # Should be floats
    assert isinstance(result["throttle"], float)
    assert isinstance(result["steering"], float)


def test_to_result_with_none_lights():
    """Test that None lights becomes empty dict."""
    result = FrameAdapter.to_result(throttle=0, brake=0, steering=0, lights=None)

    assert result["lights"] == {}


def test_to_result_with_none_adas_states():
    """Test that None adas_states becomes empty list."""
    result = FrameAdapter.to_result(throttle=0, brake=0, steering=0, adas_states=None)

    assert result["adas_states"] == []
