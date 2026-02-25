from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_version_pb2 as _osi_version_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class SensorViewConfiguration(_message.Message):
    __slots__ = ("version", "sensor_id", "mounting_position", "mounting_position_rmse", "field_of_view_horizontal", "field_of_view_vertical", "range", "update_cycle_time", "update_cycle_offset", "simulation_start_time", "omit_static_information", "generic_sensor_view_configuration", "radar_sensor_view_configuration", "lidar_sensor_view_configuration", "camera_sensor_view_configuration", "ultrasonic_sensor_view_configuration")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    RANGE_FIELD_NUMBER: _ClassVar[int]
    UPDATE_CYCLE_TIME_FIELD_NUMBER: _ClassVar[int]
    UPDATE_CYCLE_OFFSET_FIELD_NUMBER: _ClassVar[int]
    SIMULATION_START_TIME_FIELD_NUMBER: _ClassVar[int]
    OMIT_STATIC_INFORMATION_FIELD_NUMBER: _ClassVar[int]
    GENERIC_SENSOR_VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    RADAR_SENSOR_VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    LIDAR_SENSOR_VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    CAMERA_SENSOR_VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    ULTRASONIC_SENSOR_VIEW_CONFIGURATION_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    sensor_id: _osi_common_pb2.Identifier
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    field_of_view_horizontal: float
    field_of_view_vertical: float
    range: float
    update_cycle_time: _osi_common_pb2.Timestamp
    update_cycle_offset: _osi_common_pb2.Timestamp
    simulation_start_time: _osi_common_pb2.Timestamp
    omit_static_information: bool
    generic_sensor_view_configuration: _containers.RepeatedCompositeFieldContainer[GenericSensorViewConfiguration]
    radar_sensor_view_configuration: _containers.RepeatedCompositeFieldContainer[RadarSensorViewConfiguration]
    lidar_sensor_view_configuration: _containers.RepeatedCompositeFieldContainer[LidarSensorViewConfiguration]
    camera_sensor_view_configuration: _containers.RepeatedCompositeFieldContainer[CameraSensorViewConfiguration]
    ultrasonic_sensor_view_configuration: _containers.RepeatedCompositeFieldContainer[UltrasonicSensorViewConfiguration]
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., field_of_view_horizontal: _Optional[float] = ..., field_of_view_vertical: _Optional[float] = ..., range: _Optional[float] = ..., update_cycle_time: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., update_cycle_offset: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., simulation_start_time: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., omit_static_information: bool = ..., generic_sensor_view_configuration: _Optional[_Iterable[_Union[GenericSensorViewConfiguration, _Mapping]]] = ..., radar_sensor_view_configuration: _Optional[_Iterable[_Union[RadarSensorViewConfiguration, _Mapping]]] = ..., lidar_sensor_view_configuration: _Optional[_Iterable[_Union[LidarSensorViewConfiguration, _Mapping]]] = ..., camera_sensor_view_configuration: _Optional[_Iterable[_Union[CameraSensorViewConfiguration, _Mapping]]] = ..., ultrasonic_sensor_view_configuration: _Optional[_Iterable[_Union[UltrasonicSensorViewConfiguration, _Mapping]]] = ...) -> None: ...

class GenericSensorViewConfiguration(_message.Message):
    __slots__ = ("sensor_id", "mounting_position", "mounting_position_rmse", "field_of_view_horizontal", "field_of_view_vertical")
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    sensor_id: _osi_common_pb2.Identifier
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    field_of_view_horizontal: float
    field_of_view_vertical: float
    def __init__(self, sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., field_of_view_horizontal: _Optional[float] = ..., field_of_view_vertical: _Optional[float] = ...) -> None: ...

class RadarSensorViewConfiguration(_message.Message):
    __slots__ = ("sensor_id", "mounting_position", "mounting_position_rmse", "field_of_view_horizontal", "field_of_view_vertical", "number_of_rays_horizontal", "number_of_rays_vertical", "max_number_of_interactions", "emitter_frequency", "tx_antenna_diagram", "rx_antenna_diagram")
    class AntennaDiagramEntry(_message.Message):
        __slots__ = ("horizontal_angle", "vertical_angle", "response")
        HORIZONTAL_ANGLE_FIELD_NUMBER: _ClassVar[int]
        VERTICAL_ANGLE_FIELD_NUMBER: _ClassVar[int]
        RESPONSE_FIELD_NUMBER: _ClassVar[int]
        horizontal_angle: float
        vertical_angle: float
        response: float
        def __init__(self, horizontal_angle: _Optional[float] = ..., vertical_angle: _Optional[float] = ..., response: _Optional[float] = ...) -> None: ...
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_RAYS_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_RAYS_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    MAX_NUMBER_OF_INTERACTIONS_FIELD_NUMBER: _ClassVar[int]
    EMITTER_FREQUENCY_FIELD_NUMBER: _ClassVar[int]
    TX_ANTENNA_DIAGRAM_FIELD_NUMBER: _ClassVar[int]
    RX_ANTENNA_DIAGRAM_FIELD_NUMBER: _ClassVar[int]
    sensor_id: _osi_common_pb2.Identifier
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    field_of_view_horizontal: float
    field_of_view_vertical: float
    number_of_rays_horizontal: int
    number_of_rays_vertical: int
    max_number_of_interactions: int
    emitter_frequency: float
    tx_antenna_diagram: _containers.RepeatedCompositeFieldContainer[RadarSensorViewConfiguration.AntennaDiagramEntry]
    rx_antenna_diagram: _containers.RepeatedCompositeFieldContainer[RadarSensorViewConfiguration.AntennaDiagramEntry]
    def __init__(self, sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., field_of_view_horizontal: _Optional[float] = ..., field_of_view_vertical: _Optional[float] = ..., number_of_rays_horizontal: _Optional[int] = ..., number_of_rays_vertical: _Optional[int] = ..., max_number_of_interactions: _Optional[int] = ..., emitter_frequency: _Optional[float] = ..., tx_antenna_diagram: _Optional[_Iterable[_Union[RadarSensorViewConfiguration.AntennaDiagramEntry, _Mapping]]] = ..., rx_antenna_diagram: _Optional[_Iterable[_Union[RadarSensorViewConfiguration.AntennaDiagramEntry, _Mapping]]] = ...) -> None: ...

class LidarSensorViewConfiguration(_message.Message):
    __slots__ = ("sensor_id", "mounting_position", "mounting_position_rmse", "field_of_view_horizontal", "field_of_view_vertical", "number_of_rays_horizontal", "number_of_rays_vertical", "max_number_of_interactions", "emitter_frequency", "num_of_pixels", "directions", "timings")
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_RAYS_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_RAYS_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    MAX_NUMBER_OF_INTERACTIONS_FIELD_NUMBER: _ClassVar[int]
    EMITTER_FREQUENCY_FIELD_NUMBER: _ClassVar[int]
    NUM_OF_PIXELS_FIELD_NUMBER: _ClassVar[int]
    DIRECTIONS_FIELD_NUMBER: _ClassVar[int]
    TIMINGS_FIELD_NUMBER: _ClassVar[int]
    sensor_id: _osi_common_pb2.Identifier
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    field_of_view_horizontal: float
    field_of_view_vertical: float
    number_of_rays_horizontal: int
    number_of_rays_vertical: int
    max_number_of_interactions: int
    emitter_frequency: float
    num_of_pixels: int
    directions: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Vector3d]
    timings: _containers.RepeatedScalarFieldContainer[int]
    def __init__(self, sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., field_of_view_horizontal: _Optional[float] = ..., field_of_view_vertical: _Optional[float] = ..., number_of_rays_horizontal: _Optional[int] = ..., number_of_rays_vertical: _Optional[int] = ..., max_number_of_interactions: _Optional[int] = ..., emitter_frequency: _Optional[float] = ..., num_of_pixels: _Optional[int] = ..., directions: _Optional[_Iterable[_Union[_osi_common_pb2.Vector3d, _Mapping]]] = ..., timings: _Optional[_Iterable[int]] = ...) -> None: ...

class CameraSensorViewConfiguration(_message.Message):
    __slots__ = ("sensor_id", "mounting_position", "mounting_position_rmse", "field_of_view_horizontal", "field_of_view_vertical", "number_of_pixels_horizontal", "number_of_pixels_vertical", "channel_format", "samples_per_pixel", "max_number_of_interactions", "wavelength_data", "pixel_order")
    class PixelOrder(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        PIXEL_ORDER_DEFAULT: _ClassVar[CameraSensorViewConfiguration.PixelOrder]
        PIXEL_ORDER_OTHER: _ClassVar[CameraSensorViewConfiguration.PixelOrder]
        PIXEL_ORDER_RIGHT_LEFT_TOP_BOTTOM: _ClassVar[CameraSensorViewConfiguration.PixelOrder]
        PIXEL_ORDER_LEFT_RIGHT_BOTTOM_TOP: _ClassVar[CameraSensorViewConfiguration.PixelOrder]
    PIXEL_ORDER_DEFAULT: CameraSensorViewConfiguration.PixelOrder
    PIXEL_ORDER_OTHER: CameraSensorViewConfiguration.PixelOrder
    PIXEL_ORDER_RIGHT_LEFT_TOP_BOTTOM: CameraSensorViewConfiguration.PixelOrder
    PIXEL_ORDER_LEFT_RIGHT_BOTTOM_TOP: CameraSensorViewConfiguration.PixelOrder
    class ChannelFormat(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        CHANNEL_FORMAT_UNKNOWN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_OTHER: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_MONO_U8_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_MONO_U16_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_MONO_U32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_MONO_F32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RGB_U8_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RGB_U16_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RGB_U32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RGB_F32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_BAYER_BGGR_U8_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_BAYER_BGGR_U16_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_BAYER_BGGR_U32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_BAYER_BGGR_F32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_BAYER_RGGB_U8_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_BAYER_RGGB_U16_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_BAYER_RGGB_U32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_BAYER_RGGB_F32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RCCC_U8_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RCCC_U16_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RCCC_U32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RCCC_F32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RCCB_U8_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RCCB_U16_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RCCB_U32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
        CHANNEL_FORMAT_RCCB_F32_LIN: _ClassVar[CameraSensorViewConfiguration.ChannelFormat]
    CHANNEL_FORMAT_UNKNOWN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_OTHER: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_MONO_U8_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_MONO_U16_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_MONO_U32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_MONO_F32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RGB_U8_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RGB_U16_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RGB_U32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RGB_F32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_BAYER_BGGR_U8_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_BAYER_BGGR_U16_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_BAYER_BGGR_U32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_BAYER_BGGR_F32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_BAYER_RGGB_U8_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_BAYER_RGGB_U16_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_BAYER_RGGB_U32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_BAYER_RGGB_F32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RCCC_U8_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RCCC_U16_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RCCC_U32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RCCC_F32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RCCB_U8_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RCCB_U16_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RCCB_U32_LIN: CameraSensorViewConfiguration.ChannelFormat
    CHANNEL_FORMAT_RCCB_F32_LIN: CameraSensorViewConfiguration.ChannelFormat
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_PIXELS_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_PIXELS_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_FORMAT_FIELD_NUMBER: _ClassVar[int]
    SAMPLES_PER_PIXEL_FIELD_NUMBER: _ClassVar[int]
    MAX_NUMBER_OF_INTERACTIONS_FIELD_NUMBER: _ClassVar[int]
    WAVELENGTH_DATA_FIELD_NUMBER: _ClassVar[int]
    PIXEL_ORDER_FIELD_NUMBER: _ClassVar[int]
    sensor_id: _osi_common_pb2.Identifier
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    field_of_view_horizontal: float
    field_of_view_vertical: float
    number_of_pixels_horizontal: int
    number_of_pixels_vertical: int
    channel_format: _containers.RepeatedScalarFieldContainer[CameraSensorViewConfiguration.ChannelFormat]
    samples_per_pixel: int
    max_number_of_interactions: int
    wavelength_data: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.WavelengthData]
    pixel_order: CameraSensorViewConfiguration.PixelOrder
    def __init__(self, sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., field_of_view_horizontal: _Optional[float] = ..., field_of_view_vertical: _Optional[float] = ..., number_of_pixels_horizontal: _Optional[int] = ..., number_of_pixels_vertical: _Optional[int] = ..., channel_format: _Optional[_Iterable[_Union[CameraSensorViewConfiguration.ChannelFormat, str]]] = ..., samples_per_pixel: _Optional[int] = ..., max_number_of_interactions: _Optional[int] = ..., wavelength_data: _Optional[_Iterable[_Union[_osi_common_pb2.WavelengthData, _Mapping]]] = ..., pixel_order: _Optional[_Union[CameraSensorViewConfiguration.PixelOrder, str]] = ...) -> None: ...

class UltrasonicSensorViewConfiguration(_message.Message):
    __slots__ = ("sensor_id", "mounting_position", "mounting_position_rmse", "field_of_view_horizontal", "field_of_view_vertical")
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_HORIZONTAL_FIELD_NUMBER: _ClassVar[int]
    FIELD_OF_VIEW_VERTICAL_FIELD_NUMBER: _ClassVar[int]
    sensor_id: _osi_common_pb2.Identifier
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    field_of_view_horizontal: float
    field_of_view_vertical: float
    def __init__(self, sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., field_of_view_horizontal: _Optional[float] = ..., field_of_view_vertical: _Optional[float] = ...) -> None: ...
