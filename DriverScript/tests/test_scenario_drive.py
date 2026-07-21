import realdriver.scenario_drive as scenario_drive


class DummyRM:
    def Init(self, _):
        return 0

    def CreatePosition(self):
        return 1

    def Close(self):
        return 0


class DummyRouter:
    def __init__(self, *_):
        pass


def test_scenario_drive_construction_with_mocks(monkeypatch):
    monkeypatch.setattr(scenario_drive, "EsminiRMLib", lambda _: DummyRM())
    monkeypatch.setattr(scenario_drive, "SimplifiedRouter", DummyRouter)

    c = scenario_drive.ScenarioDriveController(
        lib_path="dummy.dll",
        xodr_path="dummy.xodr",
        gt_lib_path=None,
    )
    try:
        c.set_target_speed(10.0)
        assert c.target_speed == 10.0
    finally:
        c.close()
