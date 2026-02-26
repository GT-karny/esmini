from realdriver.client import RealDriverClient, LightMode, IndicatorMode


def test_client_light_mask_and_controls():
    c = RealDriverClient(ip="127.0.0.1", port=53995)
    try:
        c.set_controls(0.4, 0.1, 0.2)
        c.set_headlights(LightMode.LOW)
        c.set_indicators(IndicatorMode.LEFT)
        assert c.hvd.vehicle_powertrain.pedal_position_acceleration == 0.4
        assert c.hvd.vehicle_brake_system.pedal_position_brake == 0.1
        assert (c.light_mask & (1 << 0)) != 0
        assert (c.light_mask & (1 << 2)) != 0
    finally:
        c.close()
