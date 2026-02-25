from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_roadmarking_pb2 as _osi_roadmarking_pb2
from osi3 import osi_detectedobject_pb2 as _osi_detectedobject_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DetectedRoadMarking(_message.Message):
    __slots__ = ("header", "base", "base_rmse", "candidate", "color_description")
    class CandidateRoadMarking(_message.Message):
        __slots__ = ("probability", "classification")
        PROBABILITY_FIELD_NUMBER: _ClassVar[int]
        CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
        probability: float
        classification: _osi_roadmarking_pb2.RoadMarking.Classification
        def __init__(self, probability: _Optional[float] = ..., classification: _Optional[_Union[_osi_roadmarking_pb2.RoadMarking.Classification, _Mapping]] = ...) -> None: ...
    HEADER_FIELD_NUMBER: _ClassVar[int]
    BASE_FIELD_NUMBER: _ClassVar[int]
    BASE_RMSE_FIELD_NUMBER: _ClassVar[int]
    CANDIDATE_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    header: _osi_detectedobject_pb2.DetectedItemHeader
    base: _osi_common_pb2.BaseStationary
    base_rmse: _osi_common_pb2.BaseStationary
    candidate: _containers.RepeatedCompositeFieldContainer[DetectedRoadMarking.CandidateRoadMarking]
    color_description: _osi_common_pb2.ColorDescription
    def __init__(self, header: _Optional[_Union[_osi_detectedobject_pb2.DetectedItemHeader, _Mapping]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., base_rmse: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., candidate: _Optional[_Iterable[_Union[DetectedRoadMarking.CandidateRoadMarking, _Mapping]]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ...) -> None: ...
