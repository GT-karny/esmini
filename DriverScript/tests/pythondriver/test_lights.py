"""Level 3c: Unit tests for LightState patch management."""

import pytest
from pythondriver.lights import LightState


# L3c-001: Initial state is all "auto"
def test_initial_state_all_auto():
    """Test that all lights default to 'auto'."""
    lights = LightState()

    # No dirty flags initially, so patch should be empty
    assert lights.to_patch_dict() == {}

    # Internal values should all be "auto"
    assert lights._values["low_beam"] == "auto"
    assert lights._values["high_beam"] == "auto"
    assert lights._values["left_indicator"] == "auto"
    assert lights._values["right_indicator"] == "auto"
    assert lights._values["fog"] == "auto"
    assert lights._values["brake"] == "auto"
    assert lights._values["reverse"] == "auto"


# L3c-002: set_low_beam(True) -> "on"
def test_set_low_beam_on():
    """Test setting low beam to on."""
    lights = LightState()
    lights.set_low_beam(True)

    patch = lights.to_patch_dict()
    assert patch == {"low_beam": "on"}


# L3c-003: set_low_beam(False) -> "off"
def test_set_low_beam_off():
    """Test setting low beam to off."""
    lights = LightState()
    lights.set_low_beam(False)

    patch = lights.to_patch_dict()
    assert patch == {"low_beam": "off"}


def test_set_low_beam_auto():
    """Test resetting low beam to auto."""
    lights = LightState()
    lights.set_low_beam(True)
    lights.clear_patch()
    lights.set_low_beam_auto()

    patch = lights.to_patch_dict()
    assert patch == {"low_beam": "auto"}


# L3c-004: set_warning(True) -> both indicators "on"
def test_set_warning_sets_both_indicators():
    """Test that set_warning controls both indicators."""
    lights = LightState()
    lights.set_warning(True)

    patch = lights.to_patch_dict()
    assert patch["left_indicator"] == "on"
    assert patch["right_indicator"] == "on"


def test_set_warning_off():
    """Test that set_warning(False) turns off both indicators."""
    lights = LightState()
    lights.set_warning(False)

    patch = lights.to_patch_dict()
    assert patch["left_indicator"] == "off"
    assert patch["right_indicator"] == "off"


# L3c-005: clear_patch() clears dirty flags
def test_clear_patch():
    """Test that clear_patch clears dirty flags."""
    lights = LightState()
    lights.set_low_beam(True)
    lights.set_brake(True)

    # Before clear
    patch = lights.to_patch_dict()
    assert len(patch) == 2

    # After clear
    lights.clear_patch()
    patch = lights.to_patch_dict()
    assert patch == {}


def test_clear_patch_preserves_values():
    """Test that clear_patch preserves actual values."""
    lights = LightState()
    lights.set_low_beam(True)
    lights.clear_patch()

    # Value should still be "on" even though not dirty
    assert lights._values["low_beam"] == "on"


# Additional: All light setters work
def test_set_high_beam():
    """Test setting high beam."""
    lights = LightState()
    lights.set_high_beam(True)
    assert lights.to_patch_dict() == {"high_beam": "on"}


def test_set_left_indicator():
    """Test setting left indicator."""
    lights = LightState()
    lights.set_left_indicator(True)
    assert lights.to_patch_dict() == {"left_indicator": "on"}


def test_set_right_indicator():
    """Test setting right indicator."""
    lights = LightState()
    lights.set_right_indicator(True)
    assert lights.to_patch_dict() == {"right_indicator": "on"}


def test_set_fog():
    """Test setting fog light."""
    lights = LightState()
    lights.set_fog(True)
    assert lights.to_patch_dict() == {"fog": "on"}


def test_set_brake():
    """Test setting brake light."""
    lights = LightState()
    lights.set_brake(True)
    assert lights.to_patch_dict() == {"brake": "on"}


def test_set_reverse():
    """Test setting reverse light."""
    lights = LightState()
    lights.set_reverse(True)
    assert lights.to_patch_dict() == {"reverse": "on"}


# Additional: Invalid key raises
def test_invalid_key_raises():
    """Test that setting unknown key raises KeyError."""
    lights = LightState()
    with pytest.raises(KeyError):
        lights._set("unknown_light", "on")


# Additional: Invalid value raises
def test_invalid_value_raises():
    """Test that invalid value raises ValueError."""
    lights = LightState()
    with pytest.raises(ValueError):
        lights._set("low_beam", "invalid")


def test_invalid_value_bright_raises():
    """Test that 'bright' is not a valid value."""
    lights = LightState()
    with pytest.raises(ValueError):
        lights._set("low_beam", "bright")


# Additional: Only changed values are in patch
def test_only_changed_values_in_patch():
    """Test that setting same value doesn't mark as dirty."""
    lights = LightState()
    # Initial value is "auto"
    lights._set("low_beam", "auto")

    # Should not be dirty since value didn't change
    assert lights.to_patch_dict() == {}


def test_change_then_change_back():
    """Test changing a value then changing it back."""
    lights = LightState()
    lights.set_low_beam(True)
    lights.set_low_beam(False)

    # Should have "off" in patch (last value)
    assert lights.to_patch_dict() == {"low_beam": "off"}


# Additional: Multiple lights
def test_multiple_lights():
    """Test setting multiple lights."""
    lights = LightState()
    lights.set_low_beam(True)
    lights.set_brake(True)
    lights.set_left_indicator(True)

    patch = lights.to_patch_dict()
    assert len(patch) == 3
    assert patch["low_beam"] == "on"
    assert patch["brake"] == "on"
    assert patch["left_indicator"] == "on"


# Additional: Auto setters
def test_all_auto_setters():
    """Test all auto setters work."""
    lights = LightState()

    # Set all to on first
    lights.set_low_beam(True)
    lights.set_high_beam(True)
    lights.set_left_indicator(True)
    lights.set_right_indicator(True)
    lights.set_fog(True)
    lights.set_brake(True)
    lights.set_reverse(True)
    lights.clear_patch()

    # Now set all to auto
    lights.set_low_beam_auto()
    lights.set_high_beam_auto()
    lights.set_left_indicator_auto()
    lights.set_right_indicator_auto()
    lights.set_fog_auto()
    lights.set_brake_auto()
    lights.set_reverse_auto()

    patch = lights.to_patch_dict()
    assert len(patch) == 7
    for key in patch:
        assert patch[key] == "auto"
