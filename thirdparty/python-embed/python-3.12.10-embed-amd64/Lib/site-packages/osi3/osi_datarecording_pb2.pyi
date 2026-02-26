from osi3 import osi_sensordata_pb2 as _osi_sensordata_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class SensorDataSeries(_message.Message):
    __slots__ = ("sensor_data",)
    SENSOR_DATA_FIELD_NUMBER: _ClassVar[int]
    sensor_data: _containers.RepeatedCompositeFieldContainer[_osi_sensordata_pb2.SensorData]
    def __init__(self, sensor_data: _Optional[_Iterable[_Union[_osi_sensordata_pb2.SensorData, _Mapping]]] = ...) -> None: ...

class SensorDataSeriesList(_message.Message):
    __slots__ = ("sensor",)
    SENSOR_FIELD_NUMBER: _ClassVar[int]
    sensor: _containers.RepeatedCompositeFieldContainer[SensorDataSeries]
    def __init__(self, sensor: _Optional[_Iterable[_Union[SensorDataSeries, _Mapping]]] = ...) -> None: ...
