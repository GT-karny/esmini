from realdriver.longitudinal_controller import LongitudinalController
from realdriver.acc_controller import ACCController


def test_longitudinal_update_from_speed_bounds():
    c = LongitudinalController()
    c.set_target_speed(15.0)
    out = c.update_from_speed(5.0, 0.1)
    assert 0.0 <= out.throttle <= 1.0
    assert 0.0 <= out.brake <= 1.0


def test_acc_update_from_speed_no_gt_path():
    c = ACCController()
    c.set_target_speed(20.0)
    out = c.update_from_speed(10.0, 0.1)
    assert 0.0 <= out.throttle <= 1.0
    assert 0.0 <= out.brake <= 1.0
