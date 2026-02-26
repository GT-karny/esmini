from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Occupant(_message.Message):
    __slots__ = ("id", "classification", "source_reference")
    class Classification(_message.Message):
        __slots__ = ("is_driver", "seat", "steering_control")
        class Seat(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            SEAT_UNKNOWN: _ClassVar[Occupant.Classification.Seat]
            SEAT_OTHER: _ClassVar[Occupant.Classification.Seat]
            SEAT_FRONT_LEFT: _ClassVar[Occupant.Classification.Seat]
            SEAT_FRONT_RIGHT: _ClassVar[Occupant.Classification.Seat]
            SEAT_FRONT_MIDDLE: _ClassVar[Occupant.Classification.Seat]
            SEAT_BACK_LEFT: _ClassVar[Occupant.Classification.Seat]
            SEAT_BACK_RIGHT: _ClassVar[Occupant.Classification.Seat]
            SEAT_BACK_MIDDLE: _ClassVar[Occupant.Classification.Seat]
            SEAT_THIRD_ROW_LEFT: _ClassVar[Occupant.Classification.Seat]
            SEAT_THIRD_ROW_RIGHT: _ClassVar[Occupant.Classification.Seat]
            SEAT_THIRD_ROW_MIDDLE: _ClassVar[Occupant.Classification.Seat]
        SEAT_UNKNOWN: Occupant.Classification.Seat
        SEAT_OTHER: Occupant.Classification.Seat
        SEAT_FRONT_LEFT: Occupant.Classification.Seat
        SEAT_FRONT_RIGHT: Occupant.Classification.Seat
        SEAT_FRONT_MIDDLE: Occupant.Classification.Seat
        SEAT_BACK_LEFT: Occupant.Classification.Seat
        SEAT_BACK_RIGHT: Occupant.Classification.Seat
        SEAT_BACK_MIDDLE: Occupant.Classification.Seat
        SEAT_THIRD_ROW_LEFT: Occupant.Classification.Seat
        SEAT_THIRD_ROW_RIGHT: Occupant.Classification.Seat
        SEAT_THIRD_ROW_MIDDLE: Occupant.Classification.Seat
        class SteeringControl(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            STEERING_CONTROL_UNKNOWN: _ClassVar[Occupant.Classification.SteeringControl]
            STEERING_CONTROL_OTHER: _ClassVar[Occupant.Classification.SteeringControl]
            STEERING_CONTROL_NO_HAND: _ClassVar[Occupant.Classification.SteeringControl]
            STEERING_CONTROL_ONE_HAND: _ClassVar[Occupant.Classification.SteeringControl]
            STEERING_CONTROL_BOTH_HANDS: _ClassVar[Occupant.Classification.SteeringControl]
            STEERING_CONTROL_LEFT_HAND: _ClassVar[Occupant.Classification.SteeringControl]
            STEERING_CONTROL_RIGHT_HAND: _ClassVar[Occupant.Classification.SteeringControl]
        STEERING_CONTROL_UNKNOWN: Occupant.Classification.SteeringControl
        STEERING_CONTROL_OTHER: Occupant.Classification.SteeringControl
        STEERING_CONTROL_NO_HAND: Occupant.Classification.SteeringControl
        STEERING_CONTROL_ONE_HAND: Occupant.Classification.SteeringControl
        STEERING_CONTROL_BOTH_HANDS: Occupant.Classification.SteeringControl
        STEERING_CONTROL_LEFT_HAND: Occupant.Classification.SteeringControl
        STEERING_CONTROL_RIGHT_HAND: Occupant.Classification.SteeringControl
        IS_DRIVER_FIELD_NUMBER: _ClassVar[int]
        SEAT_FIELD_NUMBER: _ClassVar[int]
        STEERING_CONTROL_FIELD_NUMBER: _ClassVar[int]
        is_driver: bool
        seat: Occupant.Classification.Seat
        steering_control: Occupant.Classification.SteeringControl
        def __init__(self, is_driver: bool = ..., seat: _Optional[_Union[Occupant.Classification.Seat, str]] = ..., steering_control: _Optional[_Union[Occupant.Classification.SteeringControl, str]] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    classification: Occupant.Classification
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., classification: _Optional[_Union[Occupant.Classification, _Mapping]] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ...) -> None: ...
