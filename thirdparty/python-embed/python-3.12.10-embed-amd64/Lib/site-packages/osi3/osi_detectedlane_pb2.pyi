from osi3 import osi_lane_pb2 as _osi_lane_pb2
from osi3 import osi_detectedobject_pb2 as _osi_detectedobject_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DetectedLane(_message.Message):
    __slots__ = ("header", "candidate")
    class CandidateLane(_message.Message):
        __slots__ = ("probability", "classification")
        PROBABILITY_FIELD_NUMBER: _ClassVar[int]
        CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
        probability: float
        classification: _osi_lane_pb2.Lane.Classification
        def __init__(self, probability: _Optional[float] = ..., classification: _Optional[_Union[_osi_lane_pb2.Lane.Classification, _Mapping]] = ...) -> None: ...
    HEADER_FIELD_NUMBER: _ClassVar[int]
    CANDIDATE_FIELD_NUMBER: _ClassVar[int]
    header: _osi_detectedobject_pb2.DetectedItemHeader
    candidate: _containers.RepeatedCompositeFieldContainer[DetectedLane.CandidateLane]
    def __init__(self, header: _Optional[_Union[_osi_detectedobject_pb2.DetectedItemHeader, _Mapping]] = ..., candidate: _Optional[_Iterable[_Union[DetectedLane.CandidateLane, _Mapping]]] = ...) -> None: ...

class DetectedLaneBoundary(_message.Message):
    __slots__ = ("header", "candidate", "boundary_line", "boundary_line_rmse", "boundary_line_confidences", "color_description")
    class CandidateLaneBoundary(_message.Message):
        __slots__ = ("probability", "classification")
        PROBABILITY_FIELD_NUMBER: _ClassVar[int]
        CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
        probability: float
        classification: _osi_lane_pb2.LaneBoundary.Classification
        def __init__(self, probability: _Optional[float] = ..., classification: _Optional[_Union[_osi_lane_pb2.LaneBoundary.Classification, _Mapping]] = ...) -> None: ...
    HEADER_FIELD_NUMBER: _ClassVar[int]
    CANDIDATE_FIELD_NUMBER: _ClassVar[int]
    BOUNDARY_LINE_FIELD_NUMBER: _ClassVar[int]
    BOUNDARY_LINE_RMSE_FIELD_NUMBER: _ClassVar[int]
    BOUNDARY_LINE_CONFIDENCES_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    header: _osi_detectedobject_pb2.DetectedItemHeader
    candidate: _containers.RepeatedCompositeFieldContainer[DetectedLaneBoundary.CandidateLaneBoundary]
    boundary_line: _containers.RepeatedCompositeFieldContainer[_osi_lane_pb2.LaneBoundary.BoundaryPoint]
    boundary_line_rmse: _containers.RepeatedCompositeFieldContainer[_osi_lane_pb2.LaneBoundary.BoundaryPoint]
    boundary_line_confidences: _containers.RepeatedScalarFieldContainer[float]
    color_description: _osi_common_pb2.ColorDescription
    def __init__(self, header: _Optional[_Union[_osi_detectedobject_pb2.DetectedItemHeader, _Mapping]] = ..., candidate: _Optional[_Iterable[_Union[DetectedLaneBoundary.CandidateLaneBoundary, _Mapping]]] = ..., boundary_line: _Optional[_Iterable[_Union[_osi_lane_pb2.LaneBoundary.BoundaryPoint, _Mapping]]] = ..., boundary_line_rmse: _Optional[_Iterable[_Union[_osi_lane_pb2.LaneBoundary.BoundaryPoint, _Mapping]]] = ..., boundary_line_confidences: _Optional[_Iterable[float]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ...) -> None: ...
