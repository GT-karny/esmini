from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Route(_message.Message):
    __slots__ = ("route_id", "route_segment")
    class LogicalLaneSegment(_message.Message):
        __slots__ = ("logical_lane_id", "start_s", "end_s")
        LOGICAL_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        START_S_FIELD_NUMBER: _ClassVar[int]
        END_S_FIELD_NUMBER: _ClassVar[int]
        logical_lane_id: _osi_common_pb2.Identifier
        start_s: float
        end_s: float
        def __init__(self, logical_lane_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., start_s: _Optional[float] = ..., end_s: _Optional[float] = ...) -> None: ...
    class RouteSegment(_message.Message):
        __slots__ = ("lane_segment",)
        LANE_SEGMENT_FIELD_NUMBER: _ClassVar[int]
        lane_segment: _containers.RepeatedCompositeFieldContainer[Route.LogicalLaneSegment]
        def __init__(self, lane_segment: _Optional[_Iterable[_Union[Route.LogicalLaneSegment, _Mapping]]] = ...) -> None: ...
    ROUTE_ID_FIELD_NUMBER: _ClassVar[int]
    ROUTE_SEGMENT_FIELD_NUMBER: _ClassVar[int]
    route_id: _osi_common_pb2.Identifier
    route_segment: _containers.RepeatedCompositeFieldContainer[Route.RouteSegment]
    def __init__(self, route_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., route_segment: _Optional[_Iterable[_Union[Route.RouteSegment, _Mapping]]] = ...) -> None: ...
