"""Level 3a: Unit tests for EmbeddedControllerBase contract."""

import pytest
from pythondriver.controller_base import EmbeddedControllerBase


class MinimalController(EmbeddedControllerBase):
    """Minimal implementation for testing."""

    def init(self, config):
        self.config = config

    def step(self, frame_data):
        return {
            "throttle": 0.0,
            "brake": 0.0,
            "steering": 0.0,
            "gear": 1,
            "lights": {},
        }


# L3a-001: Abstract methods must be implemented
def test_abstract_methods_enforced():
    """Test that init() and step() must be implemented."""

    class IncompleteController(EmbeddedControllerBase):
        def init(self, config):
            pass

        # Missing step()

    with pytest.raises(TypeError):
        IncompleteController()


def test_abstract_methods_enforced_missing_init():
    """Test that init() must be implemented."""

    class MissingInitController(EmbeddedControllerBase):
        # Missing init()
        def step(self, frame_data):
            return {}

    with pytest.raises(TypeError):
        MissingInitController()


# L3a-002: init() receives config dict
def test_init_receives_config():
    """Test that init() receives config dict."""
    controller = MinimalController()
    test_config = {"xodr_path": "/test.xodr", "dt": 0.01, "ego_id": 42}
    controller.init(test_config)

    assert controller.config == test_config
    assert controller.config["dt"] == 0.01
    assert controller.config["ego_id"] == 42


def test_init_handles_empty_config():
    """Test that init() handles empty config."""
    controller = MinimalController()
    controller.init({})
    assert controller.config == {}


# L3a-003: step() returns required keys
def test_step_returns_required_keys():
    """Test that step() returns dict with required keys."""
    controller = MinimalController()
    controller.init({})
    result = controller.step({})

    assert isinstance(result, dict)
    assert "throttle" in result
    assert "brake" in result
    assert "steering" in result
    assert "gear" in result
    assert "lights" in result


def test_step_returns_correct_types():
    """Test that step() returns correct types."""
    controller = MinimalController()
    controller.init({})
    result = controller.step({})

    assert isinstance(result["throttle"], (int, float))
    assert isinstance(result["brake"], (int, float))
    assert isinstance(result["steering"], (int, float))
    assert isinstance(result["gear"], int)
    assert isinstance(result["lights"], dict)


# L3a-004: close() is optional
def test_close_is_optional():
    """Test that close() is optional and returns None."""
    controller = MinimalController()
    result = controller.close()
    assert result is None


def test_close_can_be_overridden():
    """Test that close() can be overridden."""

    class ControllerWithClose(EmbeddedControllerBase):
        def __init__(self):
            self.closed = False

        def init(self, config):
            pass

        def step(self, frame_data):
            return {"throttle": 0, "brake": 0, "steering": 0, "gear": 1, "lights": {}}

        def close(self):
            self.closed = True

    controller = ControllerWithClose()
    assert not controller.closed
    controller.close()
    assert controller.closed


# Additional: Controller is stateful
def test_controller_maintains_state():
    """Test that controller maintains state across step() calls."""

    class StatefulController(EmbeddedControllerBase):
        def init(self, config):
            self.count = 0

        def step(self, frame_data):
            self.count += 1
            return {
                "throttle": self.count * 0.1,
                "brake": 0.0,
                "steering": 0.0,
                "gear": 1,
                "lights": {},
            }

    controller = StatefulController()
    controller.init({})

    result1 = controller.step({})
    assert result1["throttle"] == pytest.approx(0.1)

    result2 = controller.step({})
    assert result2["throttle"] == pytest.approx(0.2)

    result3 = controller.step({})
    assert result3["throttle"] == pytest.approx(0.3)
