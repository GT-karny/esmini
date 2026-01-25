from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_version_pb2 as _osi_version_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class MotionRequest(_message.Message):
    __slots__ = ("version", "timestamp", "motion_request_type", "desired_state", "desired_trajectory")
    class MotionRequestType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        MOTION_REQUEST_TYPE_DESIRED_STATE: _ClassVar[MotionRequest.MotionRequestType]
        MOTION_REQUEST_TYPE_TRAJECTORY: _ClassVar[MotionRequest.MotionRequestType]
    MOTION_REQUEST_TYPE_DESIRED_STATE: MotionRequest.MotionRequestType
    MOTION_REQUEST_TYPE_TRAJECTORY: MotionRequest.MotionRequestType
    class DesiredState(_message.Message):
        __slots__ = ("timestamp", "position", "orientation", "velocity", "acceleration")
        TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
        POSITION_FIELD_NUMBER: _ClassVar[int]
        ORIENTATION_FIELD_NUMBER: _ClassVar[int]
        VELOCITY_FIELD_NUMBER: _ClassVar[int]
        ACCELERATION_FIELD_NUMBER: _ClassVar[int]
        timestamp: _osi_common_pb2.Timestamp
        position: _osi_common_pb2.Vector3d
        orientation: _osi_common_pb2.Orientation3d
        velocity: _osi_common_pb2.Vector3d
        acceleration: _osi_common_pb2.Vector3d
        def __init__(self, timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ..., velocity: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., acceleration: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ...) -> None: ...
    class DesiredTrajectory(_message.Message):
        __slots__ = ("trajectory_point",)
        TRAJECTORY_POINT_FIELD_NUMBER: _ClassVar[int]
        trajectory_point: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.StatePoint]
        def __init__(self, trajectory_point: _Optional[_Iterable[_Union[_osi_common_pb2.StatePoint, _Mapping]]] = ...) -> None: ...
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    MOTION_REQUEST_TYPE_FIELD_NUMBER: _ClassVar[int]
    DESIRED_STATE_FIELD_NUMBER: _ClassVar[int]
    DESIRED_TRAJECTORY_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    motion_request_type: MotionRequest.MotionRequestType
    desired_state: MotionRequest.DesiredState
    desired_trajectory: MotionRequest.DesiredTrajectory
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., motion_request_type: _Optional[_Union[MotionRequest.MotionRequestType, str]] = ..., desired_state: _Optional[_Union[MotionRequest.DesiredState, _Mapping]] = ..., desired_trajectory: _Optional[_Union[MotionRequest.DesiredTrajectory, _Mapping]] = ...) -> None: ...
