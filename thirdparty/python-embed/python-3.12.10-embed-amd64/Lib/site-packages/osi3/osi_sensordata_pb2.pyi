from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_detectedtrafficsign_pb2 as _osi_detectedtrafficsign_pb2
from osi3 import osi_detectedtrafficlight_pb2 as _osi_detectedtrafficlight_pb2
from osi3 import osi_detectedroadmarking_pb2 as _osi_detectedroadmarking_pb2
from osi3 import osi_detectedlane_pb2 as _osi_detectedlane_pb2
from osi3 import osi_detectedobject_pb2 as _osi_detectedobject_pb2
from osi3 import osi_detectedoccupant_pb2 as _osi_detectedoccupant_pb2
from osi3 import osi_sensorview_pb2 as _osi_sensorview_pb2
from osi3 import osi_featuredata_pb2 as _osi_featuredata_pb2
from osi3 import osi_logicaldetectiondata_pb2 as _osi_logicaldetectiondata_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DetectedEntityHeader(_message.Message):
    __slots__ = ("measurement_time", "cycle_counter", "data_qualifier")
    class DataQualifier(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        DATA_QUALIFIER_UNKNOWN: _ClassVar[DetectedEntityHeader.DataQualifier]
        DATA_QUALIFIER_OTHER: _ClassVar[DetectedEntityHeader.DataQualifier]
        DATA_QUALIFIER_AVAILABLE: _ClassVar[DetectedEntityHeader.DataQualifier]
        DATA_QUALIFIER_AVAILABLE_REDUCED: _ClassVar[DetectedEntityHeader.DataQualifier]
        DATA_QUALIFIER_NOT_AVAILABLE: _ClassVar[DetectedEntityHeader.DataQualifier]
        DATA_QUALIFIER_BLINDNESS: _ClassVar[DetectedEntityHeader.DataQualifier]
        DATA_QUALIFIER_TEMPORARY_AVAILABLE: _ClassVar[DetectedEntityHeader.DataQualifier]
    DATA_QUALIFIER_UNKNOWN: DetectedEntityHeader.DataQualifier
    DATA_QUALIFIER_OTHER: DetectedEntityHeader.DataQualifier
    DATA_QUALIFIER_AVAILABLE: DetectedEntityHeader.DataQualifier
    DATA_QUALIFIER_AVAILABLE_REDUCED: DetectedEntityHeader.DataQualifier
    DATA_QUALIFIER_NOT_AVAILABLE: DetectedEntityHeader.DataQualifier
    DATA_QUALIFIER_BLINDNESS: DetectedEntityHeader.DataQualifier
    DATA_QUALIFIER_TEMPORARY_AVAILABLE: DetectedEntityHeader.DataQualifier
    MEASUREMENT_TIME_FIELD_NUMBER: _ClassVar[int]
    CYCLE_COUNTER_FIELD_NUMBER: _ClassVar[int]
    DATA_QUALIFIER_FIELD_NUMBER: _ClassVar[int]
    measurement_time: _osi_common_pb2.Timestamp
    cycle_counter: int
    data_qualifier: DetectedEntityHeader.DataQualifier
    def __init__(self, measurement_time: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., cycle_counter: _Optional[int] = ..., data_qualifier: _Optional[_Union[DetectedEntityHeader.DataQualifier, str]] = ...) -> None: ...

class SensorData(_message.Message):
    __slots__ = ("version", "timestamp", "host_vehicle_location", "host_vehicle_location_rmse", "sensor_id", "mounting_position", "mounting_position_rmse", "sensor_view", "last_measurement_time", "stationary_object_header", "stationary_object", "moving_object_header", "moving_object", "traffic_sign_header", "traffic_sign", "traffic_light_header", "traffic_light", "road_marking_header", "road_marking", "lane_boundary_header", "lane_boundary", "lane_header", "lane", "occupant_header", "occupant", "feature_data", "logical_detection_data", "virtual_detection_area", "system_time")
    class VirtualDetectionArea(_message.Message):
        __slots__ = ("polygon",)
        POLYGON_FIELD_NUMBER: _ClassVar[int]
        polygon: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Polygon3d]
        def __init__(self, polygon: _Optional[_Iterable[_Union[_osi_common_pb2.Polygon3d, _Mapping]]] = ...) -> None: ...
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    HOST_VEHICLE_LOCATION_FIELD_NUMBER: _ClassVar[int]
    HOST_VEHICLE_LOCATION_RMSE_FIELD_NUMBER: _ClassVar[int]
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    SENSOR_VIEW_FIELD_NUMBER: _ClassVar[int]
    LAST_MEASUREMENT_TIME_FIELD_NUMBER: _ClassVar[int]
    STATIONARY_OBJECT_HEADER_FIELD_NUMBER: _ClassVar[int]
    STATIONARY_OBJECT_FIELD_NUMBER: _ClassVar[int]
    MOVING_OBJECT_HEADER_FIELD_NUMBER: _ClassVar[int]
    MOVING_OBJECT_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_SIGN_HEADER_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_SIGN_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_LIGHT_HEADER_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_LIGHT_FIELD_NUMBER: _ClassVar[int]
    ROAD_MARKING_HEADER_FIELD_NUMBER: _ClassVar[int]
    ROAD_MARKING_FIELD_NUMBER: _ClassVar[int]
    LANE_BOUNDARY_HEADER_FIELD_NUMBER: _ClassVar[int]
    LANE_BOUNDARY_FIELD_NUMBER: _ClassVar[int]
    LANE_HEADER_FIELD_NUMBER: _ClassVar[int]
    LANE_FIELD_NUMBER: _ClassVar[int]
    OCCUPANT_HEADER_FIELD_NUMBER: _ClassVar[int]
    OCCUPANT_FIELD_NUMBER: _ClassVar[int]
    FEATURE_DATA_FIELD_NUMBER: _ClassVar[int]
    LOGICAL_DETECTION_DATA_FIELD_NUMBER: _ClassVar[int]
    VIRTUAL_DETECTION_AREA_FIELD_NUMBER: _ClassVar[int]
    SYSTEM_TIME_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    host_vehicle_location: _osi_common_pb2.BaseMoving
    host_vehicle_location_rmse: _osi_common_pb2.BaseMoving
    sensor_id: _osi_common_pb2.Identifier
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    sensor_view: _containers.RepeatedCompositeFieldContainer[_osi_sensorview_pb2.SensorView]
    last_measurement_time: _osi_common_pb2.Timestamp
    stationary_object_header: DetectedEntityHeader
    stationary_object: _containers.RepeatedCompositeFieldContainer[_osi_detectedobject_pb2.DetectedStationaryObject]
    moving_object_header: DetectedEntityHeader
    moving_object: _containers.RepeatedCompositeFieldContainer[_osi_detectedobject_pb2.DetectedMovingObject]
    traffic_sign_header: DetectedEntityHeader
    traffic_sign: _containers.RepeatedCompositeFieldContainer[_osi_detectedtrafficsign_pb2.DetectedTrafficSign]
    traffic_light_header: DetectedEntityHeader
    traffic_light: _containers.RepeatedCompositeFieldContainer[_osi_detectedtrafficlight_pb2.DetectedTrafficLight]
    road_marking_header: DetectedEntityHeader
    road_marking: _containers.RepeatedCompositeFieldContainer[_osi_detectedroadmarking_pb2.DetectedRoadMarking]
    lane_boundary_header: DetectedEntityHeader
    lane_boundary: _containers.RepeatedCompositeFieldContainer[_osi_detectedlane_pb2.DetectedLaneBoundary]
    lane_header: DetectedEntityHeader
    lane: _containers.RepeatedCompositeFieldContainer[_osi_detectedlane_pb2.DetectedLane]
    occupant_header: DetectedEntityHeader
    occupant: _containers.RepeatedCompositeFieldContainer[_osi_detectedoccupant_pb2.DetectedOccupant]
    feature_data: _osi_featuredata_pb2.FeatureData
    logical_detection_data: _osi_logicaldetectiondata_pb2.LogicalDetectionData
    virtual_detection_area: SensorData.VirtualDetectionArea
    system_time: _osi_common_pb2.Timestamp
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., host_vehicle_location: _Optional[_Union[_osi_common_pb2.BaseMoving, _Mapping]] = ..., host_vehicle_location_rmse: _Optional[_Union[_osi_common_pb2.BaseMoving, _Mapping]] = ..., sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., sensor_view: _Optional[_Iterable[_Union[_osi_sensorview_pb2.SensorView, _Mapping]]] = ..., last_measurement_time: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., stationary_object_header: _Optional[_Union[DetectedEntityHeader, _Mapping]] = ..., stationary_object: _Optional[_Iterable[_Union[_osi_detectedobject_pb2.DetectedStationaryObject, _Mapping]]] = ..., moving_object_header: _Optional[_Union[DetectedEntityHeader, _Mapping]] = ..., moving_object: _Optional[_Iterable[_Union[_osi_detectedobject_pb2.DetectedMovingObject, _Mapping]]] = ..., traffic_sign_header: _Optional[_Union[DetectedEntityHeader, _Mapping]] = ..., traffic_sign: _Optional[_Iterable[_Union[_osi_detectedtrafficsign_pb2.DetectedTrafficSign, _Mapping]]] = ..., traffic_light_header: _Optional[_Union[DetectedEntityHeader, _Mapping]] = ..., traffic_light: _Optional[_Iterable[_Union[_osi_detectedtrafficlight_pb2.DetectedTrafficLight, _Mapping]]] = ..., road_marking_header: _Optional[_Union[DetectedEntityHeader, _Mapping]] = ..., road_marking: _Optional[_Iterable[_Union[_osi_detectedroadmarking_pb2.DetectedRoadMarking, _Mapping]]] = ..., lane_boundary_header: _Optional[_Union[DetectedEntityHeader, _Mapping]] = ..., lane_boundary: _Optional[_Iterable[_Union[_osi_detectedlane_pb2.DetectedLaneBoundary, _Mapping]]] = ..., lane_header: _Optional[_Union[DetectedEntityHeader, _Mapping]] = ..., lane: _Optional[_Iterable[_Union[_osi_detectedlane_pb2.DetectedLane, _Mapping]]] = ..., occupant_header: _Optional[_Union[DetectedEntityHeader, _Mapping]] = ..., occupant: _Optional[_Iterable[_Union[_osi_detectedoccupant_pb2.DetectedOccupant, _Mapping]]] = ..., feature_data: _Optional[_Union[_osi_featuredata_pb2.FeatureData, _Mapping]] = ..., logical_detection_data: _Optional[_Union[_osi_logicaldetectiondata_pb2.LogicalDetectionData, _Mapping]] = ..., virtual_detection_area: _Optional[_Union[SensorData.VirtualDetectionArea, _Mapping]] = ..., system_time: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ...) -> None: ...
