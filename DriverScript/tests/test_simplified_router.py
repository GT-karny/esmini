from dataclasses import dataclass

from realdriver.rm_lib import RM_PositionData
from realdriver.simplified_router import SimplifiedRouter


@dataclass
class DummyGT:
    connections: dict
    lengths: dict

    def get_connected_roads(self, road_id, direction="both"):
        return list(self.connections.get(road_id, []))

    def get_road_length(self, road_id):
        return float(self.lengths.get(road_id, 100.0))


class DummyRM:
    def __init__(self, valid_lanes):
        self.valid_lanes = valid_lanes
        self._last_position = (0, 0, 0.0)

    def CreatePosition(self):
        return 1

    def SetLanePosition(self, handle, road_id, lane_id, lane_offset, s, align=True):
        if lane_id in self.valid_lanes.get(int(road_id), []):
            self._last_position = (int(road_id), int(lane_id), float(s))
            return 0
        return -1

    def SetWorldXYHPosition(self, handle, x, y, h):
        self._last_position = (-1, 0, 0.0)
        return 0

    def GetPositionData(self, handle):
        road_id, lane_id, s = self._last_position
        data = RM_PositionData()
        data.x = float(s)
        data.y = float(road_id)
        data.h = 0.0
        data.roadId = int(road_id)
        data.laneId = int(lane_id)
        data.s = float(s)
        data.laneOffset = 0.0
        data.junctionId = 0xFFFFFFFF
        return 0, data

    def GetRoadNumberOfDrivableLanes(self, road_id, s):
        return len(self.valid_lanes.get(int(road_id), []))

    def GetDrivableLaneIdByIndex(self, road_id, lane_index, s):
        lanes = self.valid_lanes.get(int(road_id), [])
        if 0 <= lane_index < len(lanes):
            return 0, lanes[lane_index]
        return -1, 0


def _make_pos(road_id, lane_id, s, x=0.0, y=0.0):
    p = RM_PositionData()
    p.roadId = int(road_id)
    p.laneId = int(lane_id)
    p.s = float(s)
    p.x = float(x)
    p.y = float(y)
    p.h = 0.0
    p.laneOffset = 0.0
    p.junctionId = 0xFFFFFFFF
    return p


def test_invalid_lane_is_normalized_to_drivable_lane():
    rm = DummyRM({239: [-1], 242: [-1]})
    router = SimplifiedRouter(rm_lib=rm, gt_rm_lib=None)

    resolved = router._resolve_drivable_lane_id(239, 1, 10.0)

    assert resolved == -1


def test_contact_point_end_lane_flip_is_re_normalized():
    rm = DummyRM({230: [-1], 239: [-1], 242: [-1]})
    gt = DummyGT(
        connections={
            230: [(239, "junction_successor", 2)],
            239: [(242, "junction_successor", 1)],
        },
        lengths={230: 60.0, 239: 70.0, 242: 120.0},
    )
    router = SimplifiedRouter(rm_lib=rm, gt_rm_lib=gt)

    start = _make_pos(230, -1, 10.0, x=0.0, y=0.0)
    target = _make_pos(242, -1, 90.0, x=100.0, y=0.0)
    path = router._find_road_path(start, target)

    assert path
    assert any(road_id == 239 and lane_id == -1 for road_id, lane_id, _ in path)


def test_generate_waypoints_along_path_with_invalid_lane_hints_is_not_empty():
    rm = DummyRM({230: [-1], 239: [-1], 242: [-1]})
    gt = DummyGT(connections={}, lengths={230: 60.0, 239: 70.0, 242: 120.0})
    router = SimplifiedRouter(rm_lib=rm, gt_rm_lib=gt)

    road_path = [
        (230, -1, 1),
        (239, 1, 2),
        (242, 1, 1),
    ]
    start = _make_pos(230, -1, 10.0, x=0.0, y=0.0)
    target = _make_pos(242, -1, 90.0, x=100.0, y=0.0)

    waypoints = router._generate_waypoints_along_path(
        road_path, start, target, spacing=20.0
    )

    assert len(waypoints) > 1
    assert any(wp.road_id == 239 for wp in waypoints)
