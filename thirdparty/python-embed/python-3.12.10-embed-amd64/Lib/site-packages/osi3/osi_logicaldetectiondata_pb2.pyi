from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class LogicalDetectionClassification(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    LOGICAL_DETECTION_CLASSIFICATION_UNKNOWN: _ClassVar[LogicalDetectionClassification]
    LOGICAL_DETECTION_CLASSIFICATION_OTHER: _ClassVar[LogicalDetectionClassification]
    LOGICAL_DETECTION_CLASSIFICATION_INVALID: _ClassVar[LogicalDetectionClassification]
    LOGICAL_DETECTION_CLASSIFICATION_CLUTTER: _ClassVar[LogicalDetectionClassification]
    LOGICAL_DETECTION_CLASSIFICATION_OVERDRIVABLE: _ClassVar[LogicalDetectionClassification]
    LOGICAL_DETECTION_CLASSIFICATION_UNDERDRIVABLE: _ClassVar[LogicalDetectionClassification]
LOGICAL_DETECTION_CLASSIFICATION_UNKNOWN: LogicalDetectionClassification
LOGICAL_DETECTION_CLASSIFICATION_OTHER: LogicalDetectionClassification
LOGICAL_DETECTION_CLASSIFICATION_INVALID: LogicalDetectionClassification
LOGICAL_DETECTION_CLASSIFICATION_CLUTTER: LogicalDetectionClassification
LOGICAL_DETECTION_CLASSIFICATION_OVERDRIVABLE: LogicalDetectionClassification
LOGICAL_DETECTION_CLASSIFICATION_UNDERDRIVABLE: LogicalDetectionClassification

class LogicalDetectionData(_message.Message):
    __slots__ = ("version", "header", "logical_detection")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    HEADER_FIELD_NUMBER: _ClassVar[int]
    LOGICAL_DETECTION_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    header: LogicalDetectionDataHeader
    logical_detection: _containers.RepeatedCompositeFieldContainer[LogicalDetection]
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., header: _Optional[_Union[LogicalDetectionDataHeader, _Mapping]] = ..., logical_detection: _Optional[_Iterable[_Union[LogicalDetection, _Mapping]]] = ...) -> None: ...

class LogicalDetectionDataHeader(_message.Message):
    __slots__ = ("logical_detection_time", "data_qualifier", "number_of_valid_logical_detections", "sensor_id")
    class DataQualifier(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        DATA_QUALIFIER_UNKNOWN: _ClassVar[LogicalDetectionDataHeader.DataQualifier]
        DATA_QUALIFIER_OTHER: _ClassVar[LogicalDetectionDataHeader.DataQualifier]
        DATA_QUALIFIER_AVAILABLE: _ClassVar[LogicalDetectionDataHeader.DataQualifier]
        DATA_QUALIFIER_AVAILABLE_REDUCED: _ClassVar[LogicalDetectionDataHeader.DataQualifier]
        DATA_QUALIFIER_NOT_AVAILABLE: _ClassVar[LogicalDetectionDataHeader.DataQualifier]
        DATA_QUALIFIER_BLINDNESS: _ClassVar[LogicalDetectionDataHeader.DataQualifier]
        DATA_QUALIFIER_TEMPORARY_AVAILABLE: _ClassVar[LogicalDetectionDataHeader.DataQualifier]
        DATA_QUALIFIER_INVALID: _ClassVar[LogicalDetectionDataHeader.DataQualifier]
    DATA_QUALIFIER_UNKNOWN: LogicalDetectionDataHeader.DataQualifier
    DATA_QUALIFIER_OTHER: LogicalDetectionDataHeader.DataQualifier
    DATA_QUALIFIER_AVAILABLE: LogicalDetectionDataHeader.DataQualifier
    DATA_QUALIFIER_AVAILABLE_REDUCED: LogicalDetectionDataHeader.DataQualifier
    DATA_QUALIFIER_NOT_AVAILABLE: LogicalDetectionDataHeader.DataQualifier
    DATA_QUALIFIER_BLINDNESS: LogicalDetectionDataHeader.DataQualifier
    DATA_QUALIFIER_TEMPORARY_AVAILABLE: LogicalDetectionDataHeader.DataQualifier
    DATA_QUALIFIER_INVALID: LogicalDetectionDataHeader.DataQualifier
    LOGICAL_DETECTION_TIME_FIELD_NUMBER: _ClassVar[int]
    DATA_QUALIFIER_FIELD_NUMBER: _ClassVar[int]
    NUMBER_OF_VALID_LOGICAL_DETECTIONS_FIELD_NUMBER: _ClassVar[int]
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    logical_detection_time: _osi_common_pb2.Timestamp
    data_qualifier: LogicalDetectionDataHeader.DataQualifier
    number_of_valid_logical_detections: int
    sensor_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    def __init__(self, logical_detection_time: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., data_qualifier: _Optional[_Union[LogicalDetectionDataHeader.DataQualifier, str]] = ..., number_of_valid_logical_detections: _Optional[int] = ..., sensor_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ...) -> None: ...

class LogicalDetection(_message.Message):
    __slots__ = ("existence_probability", "object_id", "position", "position_rmse", "velocity", "velocity_rmse", "intensity", "snr", "point_target_probability", "sensor_id", "classification", "echo_pulse_width")
    EXISTENCE_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    OBJECT_ID_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    POSITION_RMSE_FIELD_NUMBER: _ClassVar[int]
    VELOCITY_FIELD_NUMBER: _ClassVar[int]
    VELOCITY_RMSE_FIELD_NUMBER: _ClassVar[int]
    INTENSITY_FIELD_NUMBER: _ClassVar[int]
    SNR_FIELD_NUMBER: _ClassVar[int]
    POINT_TARGET_PROBABILITY_FIELD_NUMBER: _ClassVar[int]
    SENSOR_ID_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    ECHO_PULSE_WIDTH_FIELD_NUMBER: _ClassVar[int]
    existence_probability: float
    object_id: _osi_common_pb2.Identifier
    position: _osi_common_pb2.Vector3d
    position_rmse: _osi_common_pb2.Vector3d
    velocity: _osi_common_pb2.Vector3d
    velocity_rmse: _osi_common_pb2.Vector3d
    intensity: float
    snr: float
    point_target_probability: float
    sensor_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    classification: LogicalDetectionClassification
    echo_pulse_width: float
    def __init__(self, existence_probability: _Optional[float] = ..., object_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., position_rmse: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., velocity: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., velocity_rmse: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., intensity: _Optional[float] = ..., snr: _Optional[float] = ..., point_target_probability: _Optional[float] = ..., sensor_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., classification: _Optional[_Union[LogicalDetectionClassification, str]] = ..., echo_pulse_width: _Optional[float] = ...) -> None: ...
