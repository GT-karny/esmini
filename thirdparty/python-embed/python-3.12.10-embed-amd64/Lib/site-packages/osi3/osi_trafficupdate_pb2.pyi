from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_object_pb2 as _osi_object_pb2
from osi3 import osi_hostvehicledata_pb2 as _osi_hostvehicledata_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TrafficUpdate(_message.Message):
    __slots__ = ("version", "timestamp", "update", "internal_state")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    UPDATE_FIELD_NUMBER: _ClassVar[int]
    INTERNAL_STATE_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    update: _containers.RepeatedCompositeFieldContainer[_osi_object_pb2.MovingObject]
    internal_state: _containers.RepeatedCompositeFieldContainer[_osi_hostvehicledata_pb2.HostVehicleData]
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., update: _Optional[_Iterable[_Union[_osi_object_pb2.MovingObject, _Mapping]]] = ..., internal_state: _Optional[_Iterable[_Union[_osi_hostvehicledata_pb2.HostVehicleData, _Mapping]]] = ...) -> None: ...
