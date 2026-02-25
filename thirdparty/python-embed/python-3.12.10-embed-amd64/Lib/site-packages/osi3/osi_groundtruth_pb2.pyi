from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_environment_pb2 as _osi_environment_pb2
from osi3 import osi_trafficsign_pb2 as _osi_trafficsign_pb2
from osi3 import osi_trafficlight_pb2 as _osi_trafficlight_pb2
from osi3 import osi_roadmarking_pb2 as _osi_roadmarking_pb2
from osi3 import osi_lane_pb2 as _osi_lane_pb2
from osi3 import osi_logicallane_pb2 as _osi_logicallane_pb2
from osi3 import osi_referenceline_pb2 as _osi_referenceline_pb2
from osi3 import osi_object_pb2 as _osi_object_pb2
from osi3 import osi_occupant_pb2 as _osi_occupant_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class GroundTruth(_message.Message):
    __slots__ = ("version", "timestamp", "host_vehicle_id", "stationary_object", "moving_object", "traffic_sign", "traffic_light", "road_marking", "lane_boundary", "lane", "occupant", "environmental_conditions", "country_code", "proj_string", "map_reference", "model_reference", "reference_line", "logical_lane_boundary", "logical_lane", "proj_frame_offset")
    class ProjFrameOffset(_message.Message):
        __slots__ = ("position", "yaw")
        POSITION_FIELD_NUMBER: _ClassVar[int]
        YAW_FIELD_NUMBER: _ClassVar[int]
        position: _osi_common_pb2.Vector3d
        yaw: float
        def __init__(self, position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., yaw: _Optional[float] = ...) -> None: ...
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    HOST_VEHICLE_ID_FIELD_NUMBER: _ClassVar[int]
    STATIONARY_OBJECT_FIELD_NUMBER: _ClassVar[int]
    MOVING_OBJECT_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_SIGN_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_LIGHT_FIELD_NUMBER: _ClassVar[int]
    ROAD_MARKING_FIELD_NUMBER: _ClassVar[int]
    LANE_BOUNDARY_FIELD_NUMBER: _ClassVar[int]
    LANE_FIELD_NUMBER: _ClassVar[int]
    OCCUPANT_FIELD_NUMBER: _ClassVar[int]
    ENVIRONMENTAL_CONDITIONS_FIELD_NUMBER: _ClassVar[int]
    COUNTRY_CODE_FIELD_NUMBER: _ClassVar[int]
    PROJ_STRING_FIELD_NUMBER: _ClassVar[int]
    MAP_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    MODEL_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    REFERENCE_LINE_FIELD_NUMBER: _ClassVar[int]
    LOGICAL_LANE_BOUNDARY_FIELD_NUMBER: _ClassVar[int]
    LOGICAL_LANE_FIELD_NUMBER: _ClassVar[int]
    PROJ_FRAME_OFFSET_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    host_vehicle_id: _osi_common_pb2.Identifier
    stationary_object: _containers.RepeatedCompositeFieldContainer[_osi_object_pb2.StationaryObject]
    moving_object: _containers.RepeatedCompositeFieldContainer[_osi_object_pb2.MovingObject]
    traffic_sign: _containers.RepeatedCompositeFieldContainer[_osi_trafficsign_pb2.TrafficSign]
    traffic_light: _containers.RepeatedCompositeFieldContainer[_osi_trafficlight_pb2.TrafficLight]
    road_marking: _containers.RepeatedCompositeFieldContainer[_osi_roadmarking_pb2.RoadMarking]
    lane_boundary: _containers.RepeatedCompositeFieldContainer[_osi_lane_pb2.LaneBoundary]
    lane: _containers.RepeatedCompositeFieldContainer[_osi_lane_pb2.Lane]
    occupant: _containers.RepeatedCompositeFieldContainer[_osi_occupant_pb2.Occupant]
    environmental_conditions: _osi_environment_pb2.EnvironmentalConditions
    country_code: int
    proj_string: str
    map_reference: str
    model_reference: str
    reference_line: _containers.RepeatedCompositeFieldContainer[_osi_referenceline_pb2.ReferenceLine]
    logical_lane_boundary: _containers.RepeatedCompositeFieldContainer[_osi_logicallane_pb2.LogicalLaneBoundary]
    logical_lane: _containers.RepeatedCompositeFieldContainer[_osi_logicallane_pb2.LogicalLane]
    proj_frame_offset: GroundTruth.ProjFrameOffset
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., host_vehicle_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., stationary_object: _Optional[_Iterable[_Union[_osi_object_pb2.StationaryObject, _Mapping]]] = ..., moving_object: _Optional[_Iterable[_Union[_osi_object_pb2.MovingObject, _Mapping]]] = ..., traffic_sign: _Optional[_Iterable[_Union[_osi_trafficsign_pb2.TrafficSign, _Mapping]]] = ..., traffic_light: _Optional[_Iterable[_Union[_osi_trafficlight_pb2.TrafficLight, _Mapping]]] = ..., road_marking: _Optional[_Iterable[_Union[_osi_roadmarking_pb2.RoadMarking, _Mapping]]] = ..., lane_boundary: _Optional[_Iterable[_Union[_osi_lane_pb2.LaneBoundary, _Mapping]]] = ..., lane: _Optional[_Iterable[_Union[_osi_lane_pb2.Lane, _Mapping]]] = ..., occupant: _Optional[_Iterable[_Union[_osi_occupant_pb2.Occupant, _Mapping]]] = ..., environmental_conditions: _Optional[_Union[_osi_environment_pb2.EnvironmentalConditions, _Mapping]] = ..., country_code: _Optional[int] = ..., proj_string: _Optional[str] = ..., map_reference: _Optional[str] = ..., model_reference: _Optional[str] = ..., reference_line: _Optional[_Iterable[_Union[_osi_referenceline_pb2.ReferenceLine, _Mapping]]] = ..., logical_lane_boundary: _Optional[_Iterable[_Union[_osi_logicallane_pb2.LogicalLaneBoundary, _Mapping]]] = ..., logical_lane: _Optional[_Iterable[_Union[_osi_logicallane_pb2.LogicalLane, _Mapping]]] = ..., proj_frame_offset: _Optional[_Union[GroundTruth.ProjFrameOffset, _Mapping]] = ...) -> None: ...
