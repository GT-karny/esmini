from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_environment_pb2 as _osi_environment_pb2
from osi3 import osi_object_pb2 as _osi_object_pb2
from osi3 import osi_trafficsign_pb2 as _osi_trafficsign_pb2
from osi3 import osi_trafficlight_pb2 as _osi_trafficlight_pb2
from osi3 import osi_hostvehicledata_pb2 as _osi_hostvehicledata_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class StreamingUpdate(_message.Message):
    __slots__ = ("version", "timestamp", "stationary_object_update", "moving_object_update", "traffic_sign_update", "traffic_light_update", "environmental_conditions_update", "host_vehicle_data_update", "obsolete_id")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    STATIONARY_OBJECT_UPDATE_FIELD_NUMBER: _ClassVar[int]
    MOVING_OBJECT_UPDATE_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_SIGN_UPDATE_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_LIGHT_UPDATE_FIELD_NUMBER: _ClassVar[int]
    ENVIRONMENTAL_CONDITIONS_UPDATE_FIELD_NUMBER: _ClassVar[int]
    HOST_VEHICLE_DATA_UPDATE_FIELD_NUMBER: _ClassVar[int]
    OBSOLETE_ID_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    stationary_object_update: _containers.RepeatedCompositeFieldContainer[_osi_object_pb2.StationaryObject]
    moving_object_update: _containers.RepeatedCompositeFieldContainer[_osi_object_pb2.MovingObject]
    traffic_sign_update: _containers.RepeatedCompositeFieldContainer[_osi_trafficsign_pb2.TrafficSign]
    traffic_light_update: _containers.RepeatedCompositeFieldContainer[_osi_trafficlight_pb2.TrafficLight]
    environmental_conditions_update: _osi_environment_pb2.EnvironmentalConditions
    host_vehicle_data_update: _containers.RepeatedCompositeFieldContainer[_osi_hostvehicledata_pb2.HostVehicleData]
    obsolete_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., stationary_object_update: _Optional[_Iterable[_Union[_osi_object_pb2.StationaryObject, _Mapping]]] = ..., moving_object_update: _Optional[_Iterable[_Union[_osi_object_pb2.MovingObject, _Mapping]]] = ..., traffic_sign_update: _Optional[_Iterable[_Union[_osi_trafficsign_pb2.TrafficSign, _Mapping]]] = ..., traffic_light_update: _Optional[_Iterable[_Union[_osi_trafficlight_pb2.TrafficLight, _Mapping]]] = ..., environmental_conditions_update: _Optional[_Union[_osi_environment_pb2.EnvironmentalConditions, _Mapping]] = ..., host_vehicle_data_update: _Optional[_Iterable[_Union[_osi_hostvehicledata_pb2.HostVehicleData, _Mapping]]] = ..., obsolete_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ...) -> None: ...
