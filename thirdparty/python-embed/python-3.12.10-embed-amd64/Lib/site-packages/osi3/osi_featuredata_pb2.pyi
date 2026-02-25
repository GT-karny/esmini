from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DetectionClassification(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    DETECTION_CLASSIFICATION_UNKNOWN: _ClassVar[DetectionClassification]
    DETECTION_CLASSIFICATION_OTHER: _ClassVar[DetectionClassification]
    DETECTION_CLASSIFICATION_INVALID: _ClassVar[DetectionClassification]
    DETECTION_CLASSIFICATION_CLUTTER: _ClassVar[DetectionClassification]
    DETECTION_CLASSIFICATION_OVERDRIVABLE: _ClassVar[DetectionClassification]
    DETECTION_CLASSIFICATION_UNDERDRIVABLE: _ClassVar[DetectionClassification]
DETECTION_CLASSIFICATION_UNKNOWN: DetectionClassification
DETECTION_CLASSIFICATION_OTHER: DetectionClassification
DETECTION_CLASSIFICATION_INVALID: DetectionClassification
DETECTION_CLASSIFICATION_CLUTTER: DetectionClassification
DETECTION_CLASSIFICATION_OVERDRIVABLE: DetectionClassification
DETECTION_CLASSIFICATION_UNDERDRIVABLE: DetectionClassification

class FeatureData(_message.Message):
    __slots__ = ("version", "radar_sensor", "lidar_sensor", "ultrasonic_sensor", "camera_sensor")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    RADAR_SENSOR_FIELD_NUMBER: _ClassVar[int]
    LIDAR_SENSOR_FIELD_NUMBER: _ClassVar[int]
    ULTRASONIC_SENSOR_FIELD_NUMBER: _ClassVar[int]
    CAMERA_SENSOR_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    radar_sensor: _containers.RepeatedCompositeFieldContainer[RadarDetectionData]
    lidar_sensor: _containers.RepeatedCompositeFieldContainer[LidarDetectionData]
    ultrasonic_sensor: _containers.RepeatedCompositeFieldContainer[UltrasonicDetectionData]
    camera_sensor: _containers.RepeatedCompositeFieldContainer[CameraDetectionData]
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., radar_sensor: _Optional[_Iterable[_Union[RadarDetectionData, _Mapping]]] = ..., lidar_sensor: _Optional[_Iterable[_Union[LidarDetectionData, _Mapping]]] = ..., ultrasonic_sensor: _Optional[_Iterable[_Union[UltrasonicDetectionData, _Mapping]]] = ..., camera_sensor: _Optional[_Iterable[_Union[CameraDetectionData, _Mapping]]] = ...) -> None: ...

class SensorDetectionHeader(_message.Message):
    __slots__ = ("measurement_time", "cycle_counter", "mounting_position", "mounting_position_rmse", "data_qualifier", "number_of_valid_detections", "sensor_id", "extended_qualifier")
    class DataQualifier(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        DATA_QUALIFIER_UNKNOWN: _ClassVar[SensorDetectionHeader.DataQualifier]
        DATA_QUALIFIER_OTHER: _ClassVar[SensorDetectionHeader.DataQualifier]
        DATA_QUALIFIER_AVAILABLE: _ClassVar[SensorDetectionHeader.DataQualifier]
        DATA_QUALIFIER_AVAILABLE_REDUCED: _ClassVar[SensorDetectionHeader.DataQualifier]
        DATA_QUALIFIER_NOT_AVAILABLE: _ClassVar[SensorDetectionHeader.DataQualifier]
        DATA_QUALIFIER_BLINDNESS: _ClassVar[SensorDetectionHeader.DataQualifier]
        DATA_QUALIFIER_TEMPORARY_AVAILABLE: _ClassVar[SensorDetectionHeader.DataQualifier]
        DATA_QUALIFIER_INVALID: _ClassVar[SensorDetectionHeader.DataQualifier]
    DATA_QUALIFIER_UNKNOWN: SensorDetectionHeader.DataQualifier
    DATA_QUALIFIER_OTHER: SensorDetectionHeader.DataQualifier
    DATA_QUALIFIER_AVAILABLE: SensorDetectionHeader.DataQualifier
    DATA_QUALIFIER_AVAILABLE_REDUCED: SensorDetectionHeader.DataQualifier
    DATA_QUALIFIER_NOT_AVAILABLE: SensorDetectionHeader.DataQualifier
    DATA_QUALIFIER_BLINDNESS: SensorDetectionHeader.DataQualifier
    DATA_QUALIFIER_TEMPORARY_AVAILABLE: SensorDetectionHeader.DataQualifier
    DATA_QUALIFIER_INVALID: SensorDetectionHeader.DataQualifier
    class ExtendedQualifier(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        EXTENDED_QUALIFIER_UNKNOWN: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_OTHER: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_NORMAL_OPERATION_MODE: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_POWER_UP_OR_DOWN: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_SENSOR_NOT_CALIBRATED: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_SENSOR_BLOCKED: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_SENSOR_MISALIGNED: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_BAD_SENSOR_ENVIRONMENTAL_CONDITION: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_REDUCED_FIELD_OF_VIEW: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_INPUT_NOT_AVAILABLE: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_INTERNAL_REASON: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_EXTERNAL_DISTURBANCE: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
        EXTENDED_QUALIFIER_BEGINNING_BLOCKAGE: _ClassVar[SensorDetectionHeader.ExtendedQualifier]
    EXTENDED_QUALIFIER_UNKNOWN: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_OTHER: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_NORMAL_OPERATION_MODE: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_POWER_UP_OR_DOWN: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_SENSOR_NOT_CALIBRATED: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_SENSOR_BLOCKED: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_SENSOR_MISALIGNED: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_BAD_SENSOR_ENVIRONMENTAL_CONDITION: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_REDUCED_FIELD_OF_VIEW: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_INPUT_NOT_AVAILABLE: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_INTERNAL_REASON: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_EXTERNAL_DISTURBANCE: SensorDetectionHeader.ExtendedQualifier
    EXTENDED_QUALIFIER_BEGINNING_BLOCKAGE: SensorDetectionHeader.ExtendedQualifier
    MEASUREMENT_TIME_FIELD_NUMBER: _ClassVar[int]
    CYCLE_COUNTER_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_FIELD_NUMBER: _ClassVar[int]
    MOUNTING_POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    DATA_QUALIFIER_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_VALID_DETECTIONS_FIELD_NUMBER: _ClassVar[int]
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    EXTENDED_QUALIFIER_FIELD_NUMBER: _ClassVar[int]
    measurement_time: _osi_common_pb2.Timestamp
    cycle_counter: int
    mounting_position: _osi_common_pb2.MountingPosition
    mounting_position_rmse: _osi_common_pb2.MountingPosition
    data_qualifier: SensorDetectionHeader.DataQualifier
    number_of_valid_detections: int
    sensor_id: _osi_common_pb2.Identifier
    extended_qualifier: SensorDetectionHeader.ExtendedQualifier
    def __init__(self, measurement_time: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., cycle_counter: _Optional[int] = ..., mounting_position: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., mounting_position_rmse: _Optional[_Union[_osi_common_pb2.MountingPosition, _Mapping]] = ..., data_qualifier: _Optional[_Union[SensorDetectionHeader.DataQualifier, str]] = ..., number_of_valid_detections: _Optional[int] = ..., sensor_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., extended_qualifier: _Optional[_Union[SensorDetectionHeader.ExtendedQualifier, str]] = ...) -> None: ...

class RadarDetectionData(_message.Message):
    __slots__ = ("header", "detection")
    HEADER_FIELD_NUMBER: _ClassVar[int]
    DETECTION_FIELD_NUMBER: _ClassVar[int]
    header: SensorDetectionHeader
    detection: _containers.RepeatedCompositeFieldContainer[RadarDetection]
    def __init__(self, header: _Optional[_Union[SensorDetectionHeader, _Mapping]] = ..., detection: _Optional[_Iterable[_Union[RadarDetection, _Mapping]]] = ...) -> None: ...

class RadarDetection(_message.Message):
    __slots__ = ("existence_probability", "object_id", "position", "position_rmse", "radial_velocity", "radial_velocity_rmse", "rcs", "snr", "point_target_probability", "ambiguity_id", "classification")
    EXISTENCE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    OBJECT_ID_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    RADIAL_VELOCITY_FIELD_NUMBER: _ClassVar[int]
    RADIAL_VELOCITY_RMSE_FIELD_NUMBER: _ClassVar[int]
    RCS_FIELD_NUMBER: _ClassVar[int]
    SNR_FIELD_NUMBER: _ClassVar[int]
    POINT_TARGET_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    AMBIGUITY_ID_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    existence_probability: float
    object_id: _osi_common_pb2.Identifier
    position: _osi_common_pb2.Spherical3d
    position_rmse: _osi_common_pb2.Spherical3d
    radial_velocity: float
    radial_velocity_rmse: float
    rcs: float
    snr: float
    point_target_probability: float
    ambiguity_id: _osi_common_pb2.Identifier
    classification: DetectionClassification
    def __init__(self, existence_probability: _Optional[float] = ..., object_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., position: _Optional[_Union[_osi_common_pb2.Spherical3d, _Mapping]] = ..., position_rmse: _Optional[_Union[_osi_common_pb2.Spherical3d, _Mapping]] = ..., radial_velocity: _Optional[float] = ..., radial_velocity_rmse: _Optional[float] = ..., rcs: _Optional[float] = ..., snr: _Optional[float] = ..., point_target_probability: _Optional[float] = ..., ambiguity_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., classification: _Optional[_Union[DetectionClassification, str]] = ...) -> None: ...

class LidarDetectionData(_message.Message):
    __slots__ = ("header", "detection")
    HEADER_FIELD_NUMBER: _ClassVar[int]
    DETECTION_FIELD_NUMBER: _ClassVar[int]
    header: SensorDetectionHeader
    detection: _containers.RepeatedCompositeFieldContainer[LidarDetection]
    def __init__(self, header: _Optional[_Union[SensorDetectionHeader, _Mapping]] = ..., detection: _Optional[_Iterable[_Union[LidarDetection, _Mapping]]] = ...) -> None: ...

class LidarDetection(_message.Message):
    __slots__ = ("existence_probability", "object_id", "position", "position_rmse", "height", "height_rmse", "intensity", "free_space_probability", "classification", "reflectivity", "echo_pulse_width", "radial_velocity", "beam_id")
    EXISTENCE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    OBJECT_ID_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    HEIGHT_FIELD_NUMBER: _ClassVar[int]
    HEIGHT_RMSE_FIELD_NUMBER: _ClassVar[int]
    INTENSITY_FIELD_NUMBER: _ClassVar[int]
    FREE_SPACE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    REFLECTIVITY_FIELD_NUMBER: _ClassVar[int]
    ECHO_PULSE_WIDTH_FIELD_NUMBER: _ClassVar[int]
    RADIAL_VELOCITY_FIELD_NUMBER: _ClassVar[int]
    BEAM_ID_FIELD_NUMBER: _ClassVar[int]
    existence_probability: float
    object_id: _osi_common_pb2.Identifier
    position: _osi_common_pb2.Spherical3d
    position_rmse: _osi_common_pb2.Spherical3d
    height: float
    height_rmse: float
    intensity: float
    free_space_probability: float
    classification: DetectionClassification
    reflectivity: float
    echo_pulse_width: float
    radial_velocity: float
    beam_id: _osi_common_pb2.Identifier
    def __init__(self, existence_probability: _Optional[float] = ..., object_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., position: _Optional[_Union[_osi_common_pb2.Spherical3d, _Mapping]] = ..., position_rmse: _Optional[_Union[_osi_common_pb2.Spherical3d, _Mapping]] = ..., height: _Optional[float] = ..., height_rmse: _Optional[float] = ..., intensity: _Optional[float] = ..., free_space_probability: _Optional[float] = ..., classification: _Optional[_Union[DetectionClassification, str]] = ..., reflectivity: _Optional[float] = ..., echo_pulse_width: _Optional[float] = ..., radial_velocity: _Optional[float] = ..., beam_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ...) -> None: ...

class UltrasonicDetectionSpecificHeader(_message.Message):
    __slots__ = ("max_range", "number_of_valid_indirect_detections")
    MAX_RANGE_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_VALID_INDIRECT_DETECTIONS_FIELD_NUMBER: _ClassVar[int]
    max_range: float
    number_of_valid_indirect_detections: int
    def __init__(self, max_range: _Optional[float] = ..., number_of_valid_indirect_detections: _Optional[int] = ...) -> None: ...

class UltrasonicDetectionData(_message.Message):
    __slots__ = ("header", "specific_header", "detection", "indirect_detection")
    HEADER_FIELD_NUMBER: _ClassVar[int]
    SPECIFIC_HEADER_FIELD_NUMBER: _ClassVar[int]
    DETECTION_FIELD_NUMBER: _ClassVar[int]
    INDIRECT_DETECTION_FIELD_NUMBER: _ClassVar[int]
    header: SensorDetectionHeader
    specific_header: UltrasonicDetectionSpecificHeader
    detection: _containers.RepeatedCompositeFieldContainer[UltrasonicDetection]
    indirect_detection: _containers.RepeatedCompositeFieldContainer[UltrasonicIndirectDetection]
    def __init__(self, header: _Optional[_Union[SensorDetectionHeader, _Mapping]] = ..., specific_header: _Optional[_Union[UltrasonicDetectionSpecificHeader, _Mapping]] = ..., detection: _Optional[_Iterable[_Union[UltrasonicDetection, _Mapping]]] = ..., indirect_detection: _Optional[_Iterable[_Union[UltrasonicIndirectDetection, _Mapping]]] = ...) -> None: ...

class UltrasonicDetection(_message.Message):
    __slots__ = ("existence_probability", "object_id", "distance")
    EXISTENCE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    OBJECT_ID_FIELD_NUMBER: _ClassVar[int]
    DISTANCE_FIELD_NUMBER: _ClassVar[int]
    existence_probability: float
    object_id: _osi_common_pb2.Identifier
    distance: float
    def __init__(self, existence_probability: _Optional[float] = ..., object_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., distance: _Optional[float] = ...) -> None: ...

class UltrasonicIndirectDetection(_message.Message):
    __slots__ = ("existence_probability", "object_id", "ellipsoid_radial", "ellipsoid_axial", "receiver_id", "receiver_origin")
    EXISTENCE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    OBJECT_ID_FIELD_NUMBER: _ClassVar[int]
    ELLIPSOID_RADIAL_FIELD_NUMBER: _ClassVar[int]
    ELLIPSOID_AXIAL_FIELD_NUMBER: _ClassVar[int]
    RECEIVER_ID_FIELD_NUMBER: _ClassVar[int]
    RECEIVER_ORIGIN_FIELD_NUMBER: _ClassVar[int]
    existence_probability: float
    object_id: _osi_common_pb2.Identifier
    ellipsoid_radial: float
    ellipsoid_axial: float
    receiver_id: _osi_common_pb2.Identifier
    receiver_origin: _osi_common_pb2.Vector3d
    def __init__(self, existence_probability: _Optional[float] = ..., object_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., ellipsoid_radial: _Optional[float] = ..., ellipsoid_axial: _Optional[float] = ..., receiver_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., receiver_origin: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ...) -> None: ...

class CameraDetectionSpecificHeader(_message.Message):
    __slots__ = ("number_of_valid_points",)
    NUMBER_OF_VALID_POINTS_FIELD_NUMBER: _ClassVar[int]
    number_of_valid_points: int
    def __init__(self, number_of_valid_points: _Optional[int] = ...) -> None: ...

class CameraDetectionData(_message.Message):
    __slots__ = ("header", "specific_header", "detection", "point")
    HEADER_FIELD_NUMBER: _ClassVar[int]
    SPECIFIC_HEADER_FIELD_NUMBER: _ClassVar[int]
    DETECTION_FIELD_NUMBER: _ClassVar[int]
    POINT_FIELD_NUMBER: _ClassVar[int]
    header: SensorDetectionHeader
    specific_header: CameraDetectionSpecificHeader
    detection: _containers.RepeatedCompositeFieldContainer[CameraDetection]
    point: _containers.RepeatedCompositeFieldContainer[CameraPoint]
    def __init__(self, header: _Optional[_Union[SensorDetectionHeader, _Mapping]] = ..., specific_header: _Optional[_Union[CameraDetectionSpecificHeader, _Mapping]] = ..., detection: _Optional[_Iterable[_Union[CameraDetection, _Mapping]]] = ..., point: _Optional[_Iterable[_Union[CameraPoint, _Mapping]]] = ...) -> None: ...

class CameraDetection(_message.Message):
    __slots__ = ("existence_probability", "object_id", "time_difference", "image_shape_type", "shape_classification_background", "shape_classification_foreground", "shape_classification_flat", "shape_classification_upright", "shape_classification_ground", "shape_classification_sky", "shape_classification_vegetation", "shape_classification_road", "shape_classification_non_driving_lane", "shape_classification_non_road", "shape_classification_stationary_object", "shape_classification_moving_object", "shape_classification_landmark", "shape_classification_traffic_sign", "shape_classification_traffic_light", "shape_classification_road_marking", "shape_classification_vehicle", "shape_classification_pedestrian", "shape_classification_animal", "shape_classification_pedestrian_front", "shape_classification_pedestrian_side", "shape_classification_pedestrian_rear", "shape_classification_probability", "color", "color_probability", "ambiguity_id", "first_point_index", "number_of_points", "color_description")
    class Color(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        COLOR_UNKNOWN: _ClassVar[CameraDetection.Color]
        COLOR_OTHER: _ClassVar[CameraDetection.Color]
        COLOR_BLACK: _ClassVar[CameraDetection.Color]
        COLOR_GRAY: _ClassVar[CameraDetection.Color]
        COLOR_GREY: _ClassVar[CameraDetection.Color]
        COLOR_WHITE: _ClassVar[CameraDetection.Color]
        COLOR_YELLOW: _ClassVar[CameraDetection.Color]
        COLOR_ORANGE: _ClassVar[CameraDetection.Color]
        COLOR_RED: _ClassVar[CameraDetection.Color]
        COLOR_VIOLET: _ClassVar[CameraDetection.Color]
        COLOR_BLUE: _ClassVar[CameraDetection.Color]
        COLOR_GREEN: _ClassVar[CameraDetection.Color]
        COLOR_REFLECTIVE: _ClassVar[CameraDetection.Color]
    COLOR_UNKNOWN: CameraDetection.Color
    COLOR_OTHER: CameraDetection.Color
    COLOR_BLACK: CameraDetection.Color
    COLOR_GRAY: CameraDetection.Color
    COLOR_GREY: CameraDetection.Color
    COLOR_WHITE: CameraDetection.Color
    COLOR_YELLOW: CameraDetection.Color
    COLOR_ORANGE: CameraDetection.Color
    COLOR_RED: CameraDetection.Color
    COLOR_VIOLET: CameraDetection.Color
    COLOR_BLUE: CameraDetection.Color
    COLOR_GREEN: CameraDetection.Color
    COLOR_REFLECTIVE: CameraDetection.Color
    class ImageShapeType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        IMAGE_SHAPE_TYPE_UNKNOWN: _ClassVar[CameraDetection.ImageShapeType]
        IMAGE_SHAPE_TYPE_OTHER: _ClassVar[CameraDetection.ImageShapeType]
        IMAGE_SHAPE_TYPE_POINT: _ClassVar[CameraDetection.ImageShapeType]
        IMAGE_SHAPE_TYPE_BOX: _ClassVar[CameraDetection.ImageShapeType]
        IMAGE_SHAPE_TYPE_ELLIPSE: _ClassVar[CameraDetection.ImageShapeType]
        IMAGE_SHAPE_TYPE_POLYGON: _ClassVar[CameraDetection.ImageShapeType]
        IMAGE_SHAPE_TYPE_POLYLINE: _ClassVar[CameraDetection.ImageShapeType]
        IMAGE_SHAPE_TYPE_POINT_CLOUD: _ClassVar[CameraDetection.ImageShapeType]
    IMAGE_SHAPE_TYPE_UNKNOWN: CameraDetection.ImageShapeType
    IMAGE_SHAPE_TYPE_OTHER: CameraDetection.ImageShapeType
    IMAGE_SHAPE_TYPE_POINT: CameraDetection.ImageShapeType
    IMAGE_SHAPE_TYPE_BOX: CameraDetection.ImageShapeType
    IMAGE_SHAPE_TYPE_ELLIPSE: CameraDetection.ImageShapeType
    IMAGE_SHAPE_TYPE_POLYGON: CameraDetection.ImageShapeType
    IMAGE_SHAPE_TYPE_POLYLINE: CameraDetection.ImageShapeType
    IMAGE_SHAPE_TYPE_POINT_CLOUD: CameraDetection.ImageShapeType
    EXISTENCE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    OBJECT_ID_FIELD_NUMBER: _ClassVar[int]
    TIME_DIFFERENCE_FIELD_NUMBER: _ClassVar[int]
    IMAGE_SHAPE_TYPE_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_BACKGROUND_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_FOREGROUND_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_FLAT_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_UPRIGHT_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_GROUND_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_SKY_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_VEGETATION_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_ROAD_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_NON_DRIVING_LANE_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_NON_ROAD_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_STATIONARY_OBJECT_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_MOVING_OBJECT_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_LANDMARK_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_TRAFFIC_SIGN_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_TRAFFIC_LIGHT_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_ROAD_MARKING_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_VEHICLE_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_PEDESTRIAN_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_ANIMAL_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_PEDESTRIAN_FRONT_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_PEDESTRIAN_SIDE_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_PEDESTRIAN_REAR_FIELD_NUMBER: _ClassVar[int]
    SHAPE_CLASSIFICATION_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    COLOR_FIELD_NUMBER: _ClassVar[int]
    COLOR_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    AMBIGUITY_ID_FIELD_NUMBER: _ClassVar[int]
    FIRST_POINT_INDEX_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_POINTS_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    existence_probability: float
    object_id: _osi_common_pb2.Identifier
    time_difference: _osi_common_pb2.Timestamp
    image_shape_type: CameraDetection.ImageShapeType
    shape_classification_background: bool
    shape_classification_foreground: bool
    shape_classification_flat: bool
    shape_classification_upright: bool
    shape_classification_ground: bool
    shape_classification_sky: bool
    shape_classification_vegetation: bool
    shape_classification_road: bool
    shape_classification_non_driving_lane: bool
    shape_classification_non_road: bool
    shape_classification_stationary_object: bool
    shape_classification_moving_object: bool
    shape_classification_landmark: bool
    shape_classification_traffic_sign: bool
    shape_classification_traffic_light: bool
    shape_classification_road_marking: bool
    shape_classification_vehicle: bool
    shape_classification_pedestrian: bool
    shape_classification_animal: bool
    shape_classification_pedestrian_front: bool
    shape_classification_pedestrian_side: bool
    shape_classification_pedestrian_rear: bool
    shape_classification_probability: float
    color: CameraDetection.Color
    color_probability: float
    ambiguity_id: _osi_common_pb2.Identifier
    first_point_index: int
    number_of_points: int
    color_description: _osi_common_pb2.ColorDescription
    def __init__(self, existence_probability: _Optional[float] = ..., object_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., time_difference: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., image_shape_type: _Optional[_Union[CameraDetection.ImageShapeType, str]] = ..., shape_classification_background: bool = ..., shape_classification_foreground: bool = ..., shape_classification_flat: bool = ..., shape_classification_upright: bool = ..., shape_classification_ground: bool = ..., shape_classification_sky: bool = ..., shape_classification_vegetation: bool = ..., shape_classification_road: bool = ..., shape_classification_non_driving_lane: bool = ..., shape_classification_non_road: bool = ..., shape_classification_stationary_object: bool = ..., shape_classification_moving_object: bool = ..., shape_classification_landmark: bool = ..., shape_classification_traffic_sign: bool = ..., shape_classification_traffic_light: bool = ..., shape_classification_road_marking: bool = ..., shape_classification_vehicle: bool = ..., shape_classification_pedestrian: bool = ..., shape_classification_animal: bool = ..., shape_classification_pedestrian_front: bool = ..., shape_classification_pedestrian_side: bool = ..., shape_classification_pedestrian_rear: bool = ..., shape_classification_probability: _Optional[float] = ..., color: _Optional[_Union[CameraDetection.Color, str]] = ..., color_probability: _Optional[float] = ..., ambiguity_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., first_point_index: _Optional[int] = ..., number_of_points: _Optional[int] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ...) -> None: ...

class CameraPoint(_message.Message):
    __slots__ = ("existence_probability", "point", "point_rmse")
    EXISTENCE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    POINT_FIELD_NUMBER: _ClassVar[int]
    POINT_RMSE_FIELD_NUMBER: _ClassVar[int]
    existence_probability: float
    point: _osi_common_pb2.Spherical3d
    point_rmse: _osi_common_pb2.Spherical3d
    def __init__(self, existence_probability: _Optional[float] = ..., point: _Optional[_Union[_osi_common_pb2.Spherical3d, _Mapping]] = ..., point_rmse: _Optional[_Union[_osi_common_pb2.Spherical3d, _Mapping]] = ...) -> None: ...
