from osi3 import osi_occupant_pb2 as _osi_occupant_pb2
from osi3 import osi_detectedobject_pb2 as _osi_detectedobject_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DetectedOccupant(_message.Message):
    __slots__ = ("header", "candidate")
    class CandidateOccupant(_message.Message):
        __slots__ = ("probability", "classification")
        PROBABILITY_FIELD_NUMBER: _ClassVar[int]
        CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
        probability: float
        classification: _osi_occupant_pb2.Occupant.Classification
        def __init__(self, probability: _Optional[float] = ..., classification: _Optional[_Union[_osi_occupant_pb2.Occupant.Classification, _Mapping]] = ...) -> None: ...
    HEADER_FIELD_NUMBER: _ClassVar[int]
    CANDIDATE_FIELD_NUMBER: _ClassVar[int]
    header: _osi_detectedobject_pb2.DetectedItemHeader
    candidate: _containers.RepeatedCompositeFieldContainer[DetectedOccupant.CandidateOccupant]
    def __init__(self, header: _Optional[_Union[_osi_detectedobject_pb2.DetectedItemHeader, _Mapping]] = ..., candidate: _Optional[_Iterable[_Union[DetectedOccupant.CandidateOccupant, _Mapping]]] = ...) -> None: ...
