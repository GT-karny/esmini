from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TrafficLight(_message.Message):
    __slots__ = ("id", "base", "classification", "model_reference", "source_reference", "color_description")
    class Classification(_message.Message):
        __slots__ = ("color", "icon", "mode", "counter", "assigned_lane_id", "is_out_of_service", "logical_lane_assignment")
        class Color(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            COLOR_UNKNOWN: _ClassVar[TrafficLight.Classification.Color]
            COLOR_OTHER: _ClassVar[TrafficLight.Classification.Color]
            COLOR_RED: _ClassVar[TrafficLight.Classification.Color]
            COLOR_YELLOW: _ClassVar[TrafficLight.Classification.Color]
            COLOR_GREEN: _ClassVar[TrafficLight.Classification.Color]
            COLOR_BLUE: _ClassVar[TrafficLight.Classification.Color]
            COLOR_WHITE: _ClassVar[TrafficLight.Classification.Color]
        COLOR_UNKNOWN: TrafficLight.Classification.Color
        COLOR_OTHER: TrafficLight.Classification.Color
        COLOR_RED: TrafficLight.Classification.Color
        COLOR_YELLOW: TrafficLight.Classification.Color
        COLOR_GREEN: TrafficLight.Classification.Color
        COLOR_BLUE: TrafficLight.Classification.Color
        COLOR_WHITE: TrafficLight.Classification.Color
        class Icon(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            ICON_UNKNOWN: _ClassVar[TrafficLight.Classification.Icon]
            ICON_OTHER: _ClassVar[TrafficLight.Classification.Icon]
            ICON_NONE: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_STRAIGHT_AHEAD: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_LEFT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_DIAG_LEFT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_STRAIGHT_AHEAD_LEFT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_RIGHT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_DIAG_RIGHT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_STRAIGHT_AHEAD_RIGHT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_LEFT_RIGHT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_DOWN: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_DOWN_LEFT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_DOWN_RIGHT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_ARROW_CROSS: _ClassVar[TrafficLight.Classification.Icon]
            ICON_PEDESTRIAN: _ClassVar[TrafficLight.Classification.Icon]
            ICON_WALK: _ClassVar[TrafficLight.Classification.Icon]
            ICON_DONT_WALK: _ClassVar[TrafficLight.Classification.Icon]
            ICON_BICYCLE: _ClassVar[TrafficLight.Classification.Icon]
            ICON_PEDESTRIAN_AND_BICYCLE: _ClassVar[TrafficLight.Classification.Icon]
            ICON_COUNTDOWN_SECONDS: _ClassVar[TrafficLight.Classification.Icon]
            ICON_COUNTDOWN_PERCENT: _ClassVar[TrafficLight.Classification.Icon]
            ICON_TRAM: _ClassVar[TrafficLight.Classification.Icon]
            ICON_BUS: _ClassVar[TrafficLight.Classification.Icon]
            ICON_BUS_AND_TRAM: _ClassVar[TrafficLight.Classification.Icon]
        ICON_UNKNOWN: TrafficLight.Classification.Icon
        ICON_OTHER: TrafficLight.Classification.Icon
        ICON_NONE: TrafficLight.Classification.Icon
        ICON_ARROW_STRAIGHT_AHEAD: TrafficLight.Classification.Icon
        ICON_ARROW_LEFT: TrafficLight.Classification.Icon
        ICON_ARROW_DIAG_LEFT: TrafficLight.Classification.Icon
        ICON_ARROW_STRAIGHT_AHEAD_LEFT: TrafficLight.Classification.Icon
        ICON_ARROW_RIGHT: TrafficLight.Classification.Icon
        ICON_ARROW_DIAG_RIGHT: TrafficLight.Classification.Icon
        ICON_ARROW_STRAIGHT_AHEAD_RIGHT: TrafficLight.Classification.Icon
        ICON_ARROW_LEFT_RIGHT: TrafficLight.Classification.Icon
        ICON_ARROW_DOWN: TrafficLight.Classification.Icon
        ICON_ARROW_DOWN_LEFT: TrafficLight.Classification.Icon
        ICON_ARROW_DOWN_RIGHT: TrafficLight.Classification.Icon
        ICON_ARROW_CROSS: TrafficLight.Classification.Icon
        ICON_PEDESTRIAN: TrafficLight.Classification.Icon
        ICON_WALK: TrafficLight.Classification.Icon
        ICON_DONT_WALK: TrafficLight.Classification.Icon
        ICON_BICYCLE: TrafficLight.Classification.Icon
        ICON_PEDESTRIAN_AND_BICYCLE: TrafficLight.Classification.Icon
        ICON_COUNTDOWN_SECONDS: TrafficLight.Classification.Icon
        ICON_COUNTDOWN_PERCENT: TrafficLight.Classification.Icon
        ICON_TRAM: TrafficLight.Classification.Icon
        ICON_BUS: TrafficLight.Classification.Icon
        ICON_BUS_AND_TRAM: TrafficLight.Classification.Icon
        class Mode(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            MODE_UNKNOWN: _ClassVar[TrafficLight.Classification.Mode]
            MODE_OTHER: _ClassVar[TrafficLight.Classification.Mode]
            MODE_OFF: _ClassVar[TrafficLight.Classification.Mode]
            MODE_CONSTANT: _ClassVar[TrafficLight.Classification.Mode]
            MODE_FLASHING: _ClassVar[TrafficLight.Classification.Mode]
            MODE_COUNTING: _ClassVar[TrafficLight.Classification.Mode]
        MODE_UNKNOWN: TrafficLight.Classification.Mode
        MODE_OTHER: TrafficLight.Classification.Mode
        MODE_OFF: TrafficLight.Classification.Mode
        MODE_CONSTANT: TrafficLight.Classification.Mode
        MODE_FLASHING: TrafficLight.Classification.Mode
        MODE_COUNTING: TrafficLight.Classification.Mode
        COLOR_FIELD_NUMBER: _ClassVar[int]
        ICON_FIELD_NUMBER: _ClassVar[int]
        MODE_FIELD_NUMBER: _ClassVar[int]
        COUNTER_FIELD_NUMBER: _ClassVar[int]
        ASSIGNED_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        IS_OUT_OF_SERVICE_FIELD_NUMBER: _ClassVar[int]
        LOGICAL_LANE_ASSIGNMENT_FIELD_NUMBER: _ClassVar[int]
        color: TrafficLight.Classification.Color
        icon: TrafficLight.Classification.Icon
        mode: TrafficLight.Classification.Mode
        counter: float
        assigned_lane_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        is_out_of_service: bool
        logical_lane_assignment: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.LogicalLaneAssignment]
        def __init__(self, color: _Optional[_Union[TrafficLight.Classification.Color, str]] = ..., icon: _Optional[_Union[TrafficLight.Classification.Icon, str]] = ..., mode: _Optional[_Union[TrafficLight.Classification.Mode, str]] = ..., counter: _Optional[float] = ..., assigned_lane_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., is_out_of_service: bool = ..., logical_lane_assignment: _Optional[_Iterable[_Union[_osi_common_pb2.LogicalLaneAssignment, _Mapping]]] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    BASE_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    MODEL_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    base: _osi_common_pb2.BaseStationary
    classification: TrafficLight.Classification
    model_reference: str
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    color_description: _osi_common_pb2.ColorDescription
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., classification: _Optional[_Union[TrafficLight.Classification, _Mapping]] = ..., model_reference: _Optional[str] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ...) -> None: ...
