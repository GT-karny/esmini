from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_trafficsign_pb2 as _osi_trafficsign_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class RoadMarking(_message.Message):
    __slots__ = ("id", "base", "classification", "source_reference", "color_description")
    class Classification(_message.Message):
        __slots__ = ("type", "traffic_main_sign_type", "monochrome_color", "value", "value_text", "assigned_lane_id", "is_out_of_service", "country", "country_revision", "code", "sub_code", "logical_lane_assignment")
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            TYPE_UNKNOWN: _ClassVar[RoadMarking.Classification.Type]
            TYPE_OTHER: _ClassVar[RoadMarking.Classification.Type]
            TYPE_PAINTED_TRAFFIC_SIGN: _ClassVar[RoadMarking.Classification.Type]
            TYPE_SYMBOLIC_TRAFFIC_SIGN: _ClassVar[RoadMarking.Classification.Type]
            TYPE_TEXTUAL_TRAFFIC_SIGN: _ClassVar[RoadMarking.Classification.Type]
            TYPE_GENERIC_SYMBOL: _ClassVar[RoadMarking.Classification.Type]
            TYPE_GENERIC_LINE: _ClassVar[RoadMarking.Classification.Type]
            TYPE_GENERIC_TEXT: _ClassVar[RoadMarking.Classification.Type]
        TYPE_UNKNOWN: RoadMarking.Classification.Type
        TYPE_OTHER: RoadMarking.Classification.Type
        TYPE_PAINTED_TRAFFIC_SIGN: RoadMarking.Classification.Type
        TYPE_SYMBOLIC_TRAFFIC_SIGN: RoadMarking.Classification.Type
        TYPE_TEXTUAL_TRAFFIC_SIGN: RoadMarking.Classification.Type
        TYPE_GENERIC_SYMBOL: RoadMarking.Classification.Type
        TYPE_GENERIC_LINE: RoadMarking.Classification.Type
        TYPE_GENERIC_TEXT: RoadMarking.Classification.Type
        class Color(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            COLOR_UNKNOWN: _ClassVar[RoadMarking.Classification.Color]
            COLOR_OTHER: _ClassVar[RoadMarking.Classification.Color]
            COLOR_WHITE: _ClassVar[RoadMarking.Classification.Color]
            COLOR_YELLOW: _ClassVar[RoadMarking.Classification.Color]
            COLOR_BLUE: _ClassVar[RoadMarking.Classification.Color]
            COLOR_RED: _ClassVar[RoadMarking.Classification.Color]
            COLOR_GREEN: _ClassVar[RoadMarking.Classification.Color]
            COLOR_VIOLET: _ClassVar[RoadMarking.Classification.Color]
            COLOR_ORANGE: _ClassVar[RoadMarking.Classification.Color]
        COLOR_UNKNOWN: RoadMarking.Classification.Color
        COLOR_OTHER: RoadMarking.Classification.Color
        COLOR_WHITE: RoadMarking.Classification.Color
        COLOR_YELLOW: RoadMarking.Classification.Color
        COLOR_BLUE: RoadMarking.Classification.Color
        COLOR_RED: RoadMarking.Classification.Color
        COLOR_GREEN: RoadMarking.Classification.Color
        COLOR_VIOLET: RoadMarking.Classification.Color
        COLOR_ORANGE: RoadMarking.Classification.Color
        TYPE_FIELD_NUMBER: _ClassVar[int]
        TRAFFIC_MAIN_SIGN_TYPE_FIELD_NUMBER: _ClassVar[int]
        MONOCHROME_COLOR_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        VALUE_TEXT_FIELD_NUMBER: _ClassVar[int]
        ASSIGNED_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        IS_OUT_OF_SERVICE_FIELD_NUMBER: _ClassVar[int]
        COUNTRY_FIELD_NUMBER: _ClassVar[int]
        COUNTRY_REVISION_FIELD_NUMBER: _ClassVar[int]
        CODE_FIELD_NUMBER: _ClassVar[int]
        SUB_CODE_FIELD_NUMBER: _ClassVar[int]
        LOGICAL_LANE_ASSIGNMENT_FIELD_NUMBER: _ClassVar[int]
        type: RoadMarking.Classification.Type
        traffic_main_sign_type: _osi_trafficsign_pb2.TrafficSign.MainSign.Classification.Type
        monochrome_color: RoadMarking.Classification.Color
        value: _osi_trafficsign_pb2.TrafficSignValue
        value_text: str
        assigned_lane_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        is_out_of_service: bool
        country: str
        country_revision: str
        code: str
        sub_code: str
        logical_lane_assignment: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.LogicalLaneAssignment]
        def __init__(self, type: _Optional[_Union[RoadMarking.Classification.Type, str]] = ..., traffic_main_sign_type: _Optional[_Union[_osi_trafficsign_pb2.TrafficSign.MainSign.Classification.Type, str]] = ..., monochrome_color: _Optional[_Union[RoadMarking.Classification.Color, str]] = ..., value: _Optional[_Union[_osi_trafficsign_pb2.TrafficSignValue, _Mapping]] = ..., value_text: _Optional[str] = ..., assigned_lane_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., is_out_of_service: bool = ..., country: _Optional[str] = ..., country_revision: _Optional[str] = ..., code: _Optional[str] = ..., sub_code: _Optional[str] = ..., logical_lane_assignment: _Optional[_Iterable[_Union[_osi_common_pb2.LogicalLaneAssignment, _Mapping]]] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    BASE_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    base: _osi_common_pb2.BaseStationary
    classification: RoadMarking.Classification
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    color_description: _osi_common_pb2.ColorDescription
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., classification: _Optional[_Union[RoadMarking.Classification, _Mapping]] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ...) -> None: ...
