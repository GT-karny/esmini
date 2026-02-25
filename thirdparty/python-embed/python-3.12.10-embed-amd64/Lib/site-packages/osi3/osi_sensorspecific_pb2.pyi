from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class RadarSpecificObjectData(_message.Message):
    __slots__ = ("rcs",)
    RCS_FIELD_NUMBER: _ClassVar[int]
    rcs: float
    def __init__(self, rcs: _Optional[float] = ...) -> None: ...

class LidarSpecificObjectData(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class CameraSpecificObjectData(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class UltrasonicSpecificObjectData(_message.Message):
    __slots__ = ("maximum_measurement_distance_sensor", "probability", "trilateration_status", "trend", "signalway")
    class TrilaterationStatus(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        TRILATERATION_STATUS_UNKNOWN: _ClassVar[UltrasonicSpecificObjectData.TrilaterationStatus]
        TRILATERATION_STATUS_OTHER: _ClassVar[UltrasonicSpecificObjectData.TrilaterationStatus]
        TRILATERATION_STATUS_NOT_TRILATERATED: _ClassVar[UltrasonicSpecificObjectData.TrilaterationStatus]
        TRILATERATION_STATUS_TRILATERATED: _ClassVar[UltrasonicSpecificObjectData.TrilaterationStatus]
    TRILATERATION_STATUS_UNKNOWN: UltrasonicSpecificObjectData.TrilaterationStatus
    TRILATERATION_STATUS_OTHER: UltrasonicSpecificObjectData.TrilaterationStatus
    TRILATERATION_STATUS_NOT_TRILATERATED: UltrasonicSpecificObjectData.TrilaterationStatus
    TRILATERATION_STATUS_TRILATERATED: UltrasonicSpecificObjectData.TrilaterationStatus
    class Trend(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        TREND_UNKNOWN: _ClassVar[UltrasonicSpecificObjectData.Trend]
        TREND_OTHER: _ClassVar[UltrasonicSpecificObjectData.Trend]
        TREND_CONSTANT_APPROACHING: _ClassVar[UltrasonicSpecificObjectData.Trend]
        TREND_CONSTANT: _ClassVar[UltrasonicSpecificObjectData.Trend]
        TREND_APPROACHING: _ClassVar[UltrasonicSpecificObjectData.Trend]
        TREND_DEPARTING: _ClassVar[UltrasonicSpecificObjectData.Trend]
    TREND_UNKNOWN: UltrasonicSpecificObjectData.Trend
    TREND_OTHER: UltrasonicSpecificObjectData.Trend
    TREND_CONSTANT_APPROACHING: UltrasonicSpecificObjectData.Trend
    TREND_CONSTANT: UltrasonicSpecificObjectData.Trend
    TREND_APPROACHING: UltrasonicSpecificObjectData.Trend
    TREND_DEPARTING: UltrasonicSpecificObjectData.Trend
    class Signalway(_message.Message):
        __slots__ = ("sender_id", "receiver_id")
        SENDER_ID_FIELD_NUMBER: _ClassVar[int]
        RECEIVER_ID_FIELD_NUMBER: _ClassVar[int]
        sender_id: _osi_common_pb2.Identifier
        receiver_id: _osi_common_pb2.Identifier
        def __init__(self, sender_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., receiver_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ...) -> None: ...
    MAXIMUM_MEASUREMENT_DISTANCE_SENSOR_FIELD_NUMBER: _ClassVar[int]
    PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    TRILATERATION_STATUS_FIELD_NUMBER: _ClassVar[int]
    TREND_FIELD_NUMBER: _ClassVar[int]
    SIGNALWAY_FIELD_NUMBER: _ClassVar[int]
    maximum_measurement_distance_sensor: float
    probability: float
    trilateration_status: UltrasonicSpecificObjectData.TrilaterationStatus
    trend: UltrasonicSpecificObjectData.Trend
    signalway: _containers.RepeatedCompositeFieldContainer[UltrasonicSpecificObjectData.Signalway]
    def __init__(self, maximum_measurement_distance_sensor: _Optional[float] = ..., probability: _Optional[float] = ..., trilateration_status: _Optional[_Union[UltrasonicSpecificObjectData.TrilaterationStatus, str]] = ..., trend: _Optional[_Union[UltrasonicSpecificObjectData.Trend, str]] = ..., signalway: _Optional[_Iterable[_Union[UltrasonicSpecificObjectData.Signalway, _Mapping]]] = ...) -> None: ...
