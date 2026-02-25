from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_trafficsign_pb2 as _osi_trafficsign_pb2
from osi3 import osi_detectedobject_pb2 as _osi_detectedobject_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DetectedTrafficSign(_message.Message):
    __slots__ = ("header", "main_sign", "supplementary_sign")
    class DetectedMainSign(_message.Message):
        __slots__ = ("candidate", "base", "base_rmse", "geometry")
        class Geometry(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            GEOMETRY_UNKNOWN: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_OTHER: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_CIRCLE: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_TRIANGLE_TOP: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_TRIANGLE_DOWN: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_SQUARE: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_POLE: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_RECTANGLE: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_PLATE: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_DIAMOND: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_ARROW_LEFT: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_ARROW_RIGHT: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
            GEOMETRY_OCTAGON: _ClassVar[DetectedTrafficSign.DetectedMainSign.Geometry]
        GEOMETRY_UNKNOWN: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_OTHER: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_CIRCLE: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_TRIANGLE_TOP: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_TRIANGLE_DOWN: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_SQUARE: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_POLE: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_RECTANGLE: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_PLATE: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_DIAMOND: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_ARROW_LEFT: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_ARROW_RIGHT: DetectedTrafficSign.DetectedMainSign.Geometry
        GEOMETRY_OCTAGON: DetectedTrafficSign.DetectedMainSign.Geometry
        class CandidateMainSign(_message.Message):
            __slots__ = ("probability", "classification")
            PROBABILITY_FIELD_NUMBER: _ClassVar[int]
            CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
            probability: float
            classification: _osi_trafficsign_pb2.TrafficSign.MainSign.Classification
            def __init__(self, probability: _Optional[float] = ..., classification: _Optional[_Union[_osi_trafficsign_pb2.TrafficSign.MainSign.Classification, _Mapping]] = ...) -> None: ...
        CANDIDATE_FIELD_NUMBER: _ClassVar[int]
        BASE_FIELD_NUMBER: _ClassVar[int]
        BASE_RMSE_FIELD_NUMBER: _ClassVar[int]
        GEOMETRY_FIELD_NUMBER: _ClassVar[int]
        candidate: _containers.RepeatedCompositeFieldContainer[DetectedTrafficSign.DetectedMainSign.CandidateMainSign]
        base: _osi_common_pb2.BaseStationary
        base_rmse: _osi_common_pb2.BaseStationary
        geometry: DetectedTrafficSign.DetectedMainSign.Geometry
        def __init__(self, candidate: _Optional[_Iterable[_Union[DetectedTrafficSign.DetectedMainSign.CandidateMainSign, _Mapping]]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., base_rmse: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., geometry: _Optional[_Union[DetectedTrafficSign.DetectedMainSign.Geometry, str]] = ...) -> None: ...
    class DetectedSupplementarySign(_message.Message):
        __slots__ = ("candidate", "base", "base_rmse")
        class CandidateSupplementarySign(_message.Message):
            __slots__ = ("probability", "classification")
            PROBABILITY_FIELD_NUMBER: _ClassVar[int]
            CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
            probability: float
            classification: _osi_trafficsign_pb2.TrafficSign.SupplementarySign.Classification
            def __init__(self, probability: _Optional[float] = ..., classification: _Optional[_Union[_osi_trafficsign_pb2.TrafficSign.SupplementarySign.Classification, _Mapping]] = ...) -> None: ...
        CANDIDATE_FIELD_NUMBER: _ClassVar[int]
        BASE_FIELD_NUMBER: _ClassVar[int]
        BASE_RMSE_FIELD_NUMBER: _ClassVar[int]
        candidate: _containers.RepeatedCompositeFieldContainer[DetectedTrafficSign.DetectedSupplementarySign.CandidateSupplementarySign]
        base: _osi_common_pb2.BaseStationary
        base_rmse: _osi_common_pb2.BaseStationary
        def __init__(self, candidate: _Optional[_Iterable[_Union[DetectedTrafficSign.DetectedSupplementarySign.CandidateSupplementarySign, _Mapping]]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., base_rmse: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ...) -> None: ...
    HEADER_FIELD_NUMBER: _ClassVar[int]
    MAIN_SIGN_FIELD_NUMBER: _ClassVar[int]
    SUPPLEMENTARY_SIGN_FIELD_NUMBER: _ClassVar[int]
    header: _osi_detectedobject_pb2.DetectedItemHeader
    main_sign: DetectedTrafficSign.DetectedMainSign
    supplementary_sign: _containers.RepeatedCompositeFieldContainer[DetectedTrafficSign.DetectedSupplementarySign]
    def __init__(self, header: _Optional[_Union[_osi_detectedobject_pb2.DetectedItemHeader, _Mapping]] = ..., main_sign: _Optional[_Union[DetectedTrafficSign.DetectedMainSign, _Mapping]] = ..., supplementary_sign: _Optional[_Iterable[_Union[DetectedTrafficSign.DetectedSupplementarySign, _Mapping]]] = ...) -> None: ...
