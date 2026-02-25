from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_groundtruth_pb2 as _osi_groundtruth_pb2
from osi3 import osi_sensorviewconfiguration_pb2 as _osi_sensorviewconfiguration_pb2
from osi3 import osi_hostvehicledata_pb2 as _osi_hostvehicledata_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class SensorView(_message.Message):
    __slots__ = ("version", "timestamp", "sensor_id", "mounting_position", "mounting_position_rmse", "host_vehicle_data", "global_ground_truth", "host_vehicle_id", "generic_sensor_view", "radar_sensor_view", "lidar_sensor_view", "camera_sensor_view", "ultrasonic_sensor_view")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    HOST_VEHICLE_DATA_FIELD_NUMBER: _ClassVar[int]
    GLOBAL_GROUND_TRUTH_FIELD_NUMBER: _ClassVar[int]
    HOST_VEHICLE_ID_FIELD_NUMBER: _ClassVar[int]
    GENERIC_SENSOR_VIEW_FIELD_NUMBER: _ClassVar[int]
    RADAR_SENSOR_VIEW_FIELD_NUMBER: _ClassVar[int]
    LIDAR_SENSOR_VIEW_FIELD_NUMBER: _ClassVar[int]
    CAMERA_SENSOR_VIEW_FIELD_NUMBER: _ClassVar[int]
    ULTRASONIC_SENSOR_VIEW_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    sensor_id: _osi_common_pb2.Identifier
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    host_vehicle_data: _osi_hostvehicledata_pb2.HostVehicleData
    global_ground_truth: _osi_groundtruth_pb2.GroundTruth
    host_vehicle_id: _osi_common_pb2.Identifier
    generic_sensor_view: _containers.RepeatedCompositeFieldContainer[GenericSensorView]
    radar_sensor_view: _containers.RepeatedCompositeFieldContainer[RadarSensorView]
    lidar_sensor_view: _containers.RepeatedCompositeFieldContainer[LidarSensorView]
    camera_sensor_view: _containers.RepeatedCompositeFieldContainer[CameraSensorView]
    ultrasonic_sensor_view: _containers.RepeatedCompositeFieldContainer[UltrasonicSensorView]
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., host_vehicle_data: _Optional[_Union[_osi_hostvehicledata_pb2.HostVehicleData, _Mapping]] = ..., global_ground_truth: _Optional[_Union[_osi_groundtruth_pb2.GroundTruth, _Mapping]] = ..., host_vehicle_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., generic_sensor_view: _Optional[_Iterable[_Union[GenericSensorView, _Mapping]]] = ..., radar_sensor_view: _Optional[_Iterable[_Union[RadarSensorView, _Mapping]]] = ..., lidar_sensor_view: _Optional[_Iterable[_Union[LidarSensorView, _Mapping]]] = ..., camera_sensor_view: _Optional[_Iterable[_Union[CameraSensorView, _Mapping]]] = ..., ultrasonic_sensor_view: _Optional[_Iterable[_Union[UltrasonicSensorView, _Mapping]]] = ...) -> None: ...

class GenericSensorView(_message.Message):
    __slots__ = ("view_configuration",)
    VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    view_configuration: _osi_sensorviewconfiguration_pb2.GenericSensorViewConfiguration
    def __init__(self, view_configuration: _Optional[_Union[_osi_sensorviewconfiguration_pb2.GenericSensorViewConfiguration, _Mapping]] = ...) -> None: ...

class RadarSensorView(_message.Message):
    __slots__ = ("view_configuration", "reflection")
    class Reflection(_message.Message):
        __slots__ = ("signal_strength", "time_of_flight", "doppler_shift", "source_horizontal_angle", "source_vertical_angle")
        SIGNAL_STRENGTH_FIELD_NUMBER: _ClassVar[int]
        TIME_OF_FLIGHT_FIELD_NUMBER: _ClassVar[int]
        DOPPLER_SHIFT_FIELD_NUMBER: _ClassVar[int]
        SOURCE_HORIZONTAL_ANGLE_FIELD_NUMBER: _ClassVar[int]
        SOURCE_VERTICAL_ANGLE_FIELD_NUMBER: _ClassVar[int]
        signal_strength: float
        time_of_flight: float
        doppler_shift: float
        source_horizontal_angle: float
        source_vertical_angle: float
        def __init__(self, signal_strength: _Optional[float] = ..., time_of_flight: _Optional[float] = ..., doppler_shift: _Optional[float] = ..., source_horizontal_angle: _Optional[float] = ..., source_vertical_angle: _Optional[float] = ...) -> None: ...
    VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    REFLECTION_FIELD_NUMBER: _ClassVar[int]
    view_configuration: _osi_sensorviewconfiguration_pb2.RadarSensorViewConfiguration
    reflection: _containers.RepeatedCompositeFieldContainer[RadarSensorView.Reflection]
    def __init__(self, view_configuration: _Optional[_Union[_osi_sensorviewconfiguration_pb2.RadarSensorViewConfiguration, _Mapping]] = ..., reflection: _Optional[_Iterable[_Union[RadarSensorView.Reflection, _Mapping]]] = ...) -> None: ...

class LidarSensorView(_message.Message):
    __slots__ = ("view_configuration", "reflection")
    class Reflection(_message.Message):
        __slots__ = ("signal_strength", "time_of_flight", "doppler_shift", "normal_to_surface", "object_id")
        SIGNAL_STRENGTH_FIELD_NUMBER: _ClassVar[int]
        TIME_OF_FLIGHT_FIELD_NUMBER: _ClassVar[int]
        DOPPLER_SHIFT_FIELD_NUMBER: _ClassVar[int]
        NORMAL_TO_SURFACE_FIELD_NUMBER: _ClassVar[int]
        OBJECT_ID_FIELD_NUMBER: _ClassVar[int]
        signal_strength: float
        time_of_flight: float
        doppler_shift: float
        normal_to_surface: _osi_common_pb2.Vector3d
        object_id: _osi_common_pb2.Identifier
        def __init__(self, signal_strength: _Optional[float] = ..., time_of_flight: _Optional[float] = ..., doppler_shift: _Optional[float] = ..., normal_to_surface: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., object_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ...) -> None: ...
    VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    REFLECTION_FIELD_NUMBER: _ClassVar[int]
    view_configuration: _osi_sensorviewconfiguration_pb2.LidarSensorViewConfiguration
    reflection: _containers.RepeatedCompositeFieldContainer[LidarSensorView.Reflection]
    def __init__(self, view_configuration: _Optional[_Union[_osi_sensorviewconfiguration_pb2.LidarSensorViewConfiguration, _Mapping]] = ..., reflection: _Optional[_Iterable[_Union[LidarSensorView.Reflection, _Mapping]]] = ...) -> None: ...

class CameraSensorView(_message.Message):
    __slots__ = ("view_configuration", "image_data")
    VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    IMAGE_DATA_FIELD_NUMBER: _ClassVar[int]
    view_configuration: _osi_sensorviewconfiguration_pb2.CameraSensorViewConfiguration
    image_data: bytes
    def __init__(self, view_configuration: _Optional[_Union[_osi_sensorviewconfiguration_pb2.CameraSensorViewConfiguration, _Mapping]] = ..., image_data: _Optional[bytes] = ...) -> None: ...

class UltrasonicSensorView(_message.Message):
    __slots__ = ("view_configuration",)
    VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    view_configuration: _osi_sensorviewconfiguration_pb2.UltrasonicSensorViewConfiguration
    def __init__(self, view_configuration: _Optional[_Union[_osi_sensorviewconfiguration_pb2.UltrasonicSensorViewConfiguration, _Mapping]] = ...) -> None: ...
