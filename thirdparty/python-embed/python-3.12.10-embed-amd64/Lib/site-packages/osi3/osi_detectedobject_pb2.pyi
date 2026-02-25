from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_object_pb2 as _osi_object_pb2
from osi3 import osi_sensorspecific_pb2 as _osi_sensorspecific_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class DetectedItemHeader(_message.Message):
    __slots__ = ("tracking_id", "ground_truth_id", "existence_probability", "age", "measurement_state", "sensor_id")
    class MeasurementState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        MEASUREMENT_STATE_UNKNOWN: _ClassVar[DetectedItemHeader.MeasurementState]
        MEASUREMENT_STATE_OTHER: _ClassVar[DetectedItemHeader.MeasurementState]
        MEASUREMENT_STATE_MEASURED: _ClassVar[DetectedItemHeader.MeasurementState]
        MEASUREMENT_STATE_PREDICTED: _ClassVar[DetectedItemHeader.MeasurementState]
    MEASUREMENT_STATE_UNKNOWN: DetectedItemHeader.MeasurementState
    MEASUREMENT_STATE_OTHER: DetectedItemHeader.MeasurementState
    MEASUREMENT_STATE_MEASURED: DetectedItemHeader.MeasurementState
    MEASUREMENT_STATE_PREDICTED: DetectedItemHeader.MeasurementState
    TRACKING_ID_FIELD_NUMBER: _ClassVar[int]
    GROUND_TRUTH_ID_FIELD_NUMBER: _ClassVar[int]
    EXISTENCE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    AGE_FIELD_NUMBER: _ClassVar[int]
    MEASUREMENT_STATE_FIELD_NUMBER: _ClassVar[int]
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    tracking_id: _osi_common_pb2.Identifier
    ground_truth_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    existence_probability: float
    age: float
    measurement_state: DetectedItemHeader.MeasurementState
    sensor_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    def __init__(self, tracking_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., ground_truth_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., existence_probability: _Optional[float] = ..., age: _Optional[float] = ..., measurement_state: _Optional[_Union[DetectedItemHeader.MeasurementState, str]] = ..., sensor_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ...) -> None: ...

class DetectedStationaryObject(_message.Message):
    __slots__ = ("header", "base", "base_rmse", "candidate", "color_description", "radar_specifics", "lidar_specifics", "camera_specifics", "ultrasonic_specifics")
    class CandidateStationaryObject(_message.Message):
        __slots__ = ("probability", "classification")
        PROBABILITY_FIELD_NUMBER: _ClassVar[int]
        CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
        probability: float
        classification: _osi_object_pb2.StationaryObject.Classification
        def __init__(self, probability: _Optional[float] = ..., classification: _Optional[_Union[_osi_object_pb2.StationaryObject.Classification, _Mapping]] = ...) -> None: ...
    HEADER_FIELD_NUMBER: _ClassVar[int]
    BASE_FIELD_NUMBER: _ClassVar[int]
    BASE_RMSE_FIELD_NUMBER: _ClassVar[int]
    CANDIDATE_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    RADAR_SPECIFICS_FIELD_NUMBER: _ClassVar[int]
    LIDAR_SPECIFICS_FIELD_NUMBER: _ClassVar[int]
    CAMERA_SPECIFICS_FIELD_NUMBER: _ClassVar[int]
    ULTRASONIC_SPECIFICS_FIELD_NUMBER: _ClassVar[int]
    header: DetectedItemHeader
    base: _osi_common_pb2.BaseStationary
    base_rmse: _osi_common_pb2.BaseStationary
    candidate: _containers.RepeatedCompositeFieldContainer[DetectedStationaryObject.CandidateStationaryObject]
    color_description: _osi_common_pb2.ColorDescription
    radar_specifics: _osi_sensorspecific_pb2.RadarSpecificObjectData
    lidar_specifics: _osi_sensorspecific_pb2.LidarSpecificObjectData
    camera_specifics: _osi_sensorspecific_pb2.CameraSpecificObjectData
    ultrasonic_specifics: _osi_sensorspecific_pb2.UltrasonicSpecificObjectData
    def __init__(self, header: _Optional[_Union[DetectedItemHeader, _Mapping]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., base_rmse: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., candidate: _Optional[_Iterable[_Union[DetectedStationaryObject.CandidateStationaryObject, _Mapping]]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ..., radar_specifics: _Optional[_Union[_osi_sensorspecific_pb2.RadarSpecificObjectData, _Mapping]] = ..., lidar_specifics: _Optional[_Union[_osi_sensorspecific_pb2.LidarSpecificObjectData, _Mapping]] = ..., camera_specifics: _Optional[_Union[_osi_sensorspecific_pb2.CameraSpecificObjectData, _Mapping]] = ..., ultrasonic_specifics: _Optional[_Union[_osi_sensorspecific_pb2.UltrasonicSpecificObjectData, _Mapping]] = ...) -> None: ...

class DetectedMovingObject(_message.Message):
    __slots__ = ("header", "base", "base_rmse", "reference_point", "movement_state", "percentage_side_lane_left", "percentage_side_lane_right", "candidate", "color_description", "radar_specifics", "lidar_specifics", "camera_specifics", "ultrasonic_specifics")
    class ReferencePoint(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        REFERENCE_POINT_UNKNOWN: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_OTHER: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_CENTER: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_MIDDLE_LEFT: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_MIDDLE_RIGHT: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_REAR_MIDDLE: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_REAR_LEFT: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_REAR_RIGHT: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_FRONT_MIDDLE: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_FRONT_LEFT: _ClassVar[DetectedMovingObject.ReferencePoint]
        REFERENCE_POINT_FRONT_RIGHT: _ClassVar[DetectedMovingObject.ReferencePoint]
    REFERENCE_POINT_UNKNOWN: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_OTHER: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_CENTER: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_MIDDLE_LEFT: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_MIDDLE_RIGHT: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_REAR_MIDDLE: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_REAR_LEFT: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_REAR_RIGHT: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_FRONT_MIDDLE: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_FRONT_LEFT: DetectedMovingObject.ReferencePoint
    REFERENCE_POINT_FRONT_RIGHT: DetectedMovingObject.ReferencePoint
    class MovementState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        MOVEMENT_STATE_UNKNOWN: _ClassVar[DetectedMovingObject.MovementState]
        MOVEMENT_STATE_OTHER: _ClassVar[DetectedMovingObject.MovementState]
        MOVEMENT_STATE_STATIONARY: _ClassVar[DetectedMovingObject.MovementState]
        MOVEMENT_STATE_MOVING: _ClassVar[DetectedMovingObject.MovementState]
        MOVEMENT_STATE_STOPPED: _ClassVar[DetectedMovingObject.MovementState]
    MOVEMENT_STATE_UNKNOWN: DetectedMovingObject.MovementState
    MOVEMENT_STATE_OTHER: DetectedMovingObject.MovementState
    MOVEMENT_STATE_STATIONARY: DetectedMovingObject.MovementState
    MOVEMENT_STATE_MOVING: DetectedMovingObject.MovementState
    MOVEMENT_STATE_STOPPED: DetectedMovingObject.MovementState
    class CandidateMovingObject(_message.Message):
        __slots__ = ("probability", "type", "vehicle_classification", "head_pose", "upper_body_pose", "moving_object_classification")
        PROBABILITY_FIELD_NUMBER: _ClassVar[int]
        TYPE_FIELD_NUMBER: _ClassVar[int]
        VEHICLE_CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
        HEAD_POSE_FIELD_NUMBER: _ClassVar[int]
        UPPER_BODY_POSE_FIELD_NUMBER: _ClassVar[int]
        MOVING_OBJECT_CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
        probability: float
        type: _osi_object_pb2.MovingObject.Type
        vehicle_classification: _osi_object_pb2.MovingObject.VehicleClassification
        head_pose: _osi_common_pb2.Orientation3d
        upper_body_pose: _osi_common_pb2.Orientation3d
        moving_object_classification: _osi_object_pb2.MovingObject.MovingObjectClassification
        def __init__(self, probability: _Optional[float] = ..., type: _Optional[_Union[_osi_object_pb2.MovingObject.Type, str]] = ..., vehicle_classification: _Optional[_Union[_osi_object_pb2.MovingObject.VehicleClassification, _Mapping]] = ..., head_pose: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ..., upper_body_pose: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ..., moving_object_classification: _Optional[_Union[_osi_object_pb2.MovingObject.MovingObjectClassification, _Mapping]] = ...) -> None: ...
    HEADER_FIELD_NUMBER: _ClassVar[int]
    BASE_FIELD_NUMBER: _ClassVar[int]
    BASE_RMSE_FIELD_NUMBER: _ClassVar[int]
    REFERENCE_POINT_FIELD_NUMBER: _ClassVar[int]
    MOVEMENT_STATE_FIELD_NUMBER: _ClassVar[int]
    PERCENTAGE_SIDE_LANE_LEFT_FIELD_NUMBER: _ClassVar[int]
    PERCENTAGE_SIDE_LANE_RIGHT_FIELD_NUMBER: _ClassVar[int]
    CANDIDATE_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    RADAR_SPECIFICS_FIELD_NUMBER: _ClassVar[int]
    LIDAR_SPECIFICS_FIELD_NUMBER: _ClassVar[int]
    CAMERA_SPECIFICS_FIELD_NUMBER: _ClassVar[int]
    ULTRASONIC_SPECIFICS_FIELD_NUMBER: _ClassVar[int]
    header: DetectedItemHeader
    base: _osi_common_pb2.BaseMoving
    base_rmse: _osi_common_pb2.BaseMoving
    reference_point: DetectedMovingObject.ReferencePoint
    movement_state: DetectedMovingObject.MovementState
    percentage_side_lane_left: float
    percentage_side_lane_right: float
    candidate: _containers.RepeatedCompositeFieldContainer[DetectedMovingObject.CandidateMovingObject]
    color_description: _osi_common_pb2.ColorDescription
    radar_specifics: _osi_sensorspecific_pb2.RadarSpecificObjectData
    lidar_specifics: _osi_sensorspecific_pb2.LidarSpecificObjectData
    camera_specifics: _osi_sensorspecific_pb2.CameraSpecificObjectData
    ultrasonic_specifics: _osi_sensorspecific_pb2.UltrasonicSpecificObjectData
    def __init__(self, header: _Optional[_Union[DetectedItemHeader, _Mapping]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseMoving, _Mapping]] = ..., base_rmse: _Optional[_Union[_osi_common_pb2.BaseMoving, _Mapping]] = ..., reference_point: _Optional[_Union[DetectedMovingObject.ReferencePoint, str]] = ..., movement_state: _Optional[_Union[DetectedMovingObject.MovementState, str]] = ..., percentage_side_lane_left: _Optional[float] = ..., percentage_side_lane_right: _Optional[float] = ..., candidate: _Optional[_Iterable[_Union[DetectedMovingObject.CandidateMovingObject, _Mapping]]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ..., radar_specifics: _Optional[_Union[_osi_sensorspecific_pb2.RadarSpecificObjectData, _Mapping]] = ..., lidar_specifics: _Optional[_Union[_osi_sensorspecific_pb2.LidarSpecificObjectData, _Mapping]] = ..., camera_specifics: _Optional[_Union[_osi_sensorspecific_pb2.CameraSpecificObjectData, _Mapping]] = ..., ultrasonic_specifics: _Optional[_Union[_osi_sensorspecific_pb2.UltrasonicSpecificObjectData, _Mapping]] = ...) -> None: ...
