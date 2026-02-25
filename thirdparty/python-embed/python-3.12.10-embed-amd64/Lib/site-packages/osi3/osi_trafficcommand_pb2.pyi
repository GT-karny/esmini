from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TrafficCommand(_message.Message):
    __slots__ = ("version", "timestamp", "traffic_participant_id", "action")
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_PARTICIPANT_ID_FIELD_NUMBER: _ClassVar[int]
    ACTION_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    traffic_participant_id: _osi_common_pb2.Identifier
    action: _containers.RepeatedCompositeFieldContainer[TrafficAction]
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., traffic_participant_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., action: _Optional[_Iterable[_Union[TrafficAction, _Mapping]]] = ...) -> None: ...

class TrafficAction(_message.Message):
    __slots__ = ("follow_trajectory_action", "follow_path_action", "acquire_global_position_action", "lane_change_action", "speed_action", "abort_actions_action", "end_actions_action", "custom_action", "longitudinal_distance_action", "lane_offset_action", "lateral_distance_action", "teleport_action")
    class FollowingMode(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        FOLLOWING_MODE_POSITION: _ClassVar[TrafficAction.FollowingMode]
        FOLLOWING_MODE_FOLLOW: _ClassVar[TrafficAction.FollowingMode]
    FOLLOWING_MODE_POSITION: TrafficAction.FollowingMode
    FOLLOWING_MODE_FOLLOW: TrafficAction.FollowingMode
    class DynamicsShape(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        DYNAMICS_SHAPE_UNSPECIFIED: _ClassVar[TrafficAction.DynamicsShape]
        DYNAMICS_SHAPE_LINEAR: _ClassVar[TrafficAction.DynamicsShape]
        DYNAMICS_SHAPE_CUBIC: _ClassVar[TrafficAction.DynamicsShape]
        DYNAMICS_SHAPE_SINUSOIDAL: _ClassVar[TrafficAction.DynamicsShape]
        DYNAMICS_SHAPE_STEP: _ClassVar[TrafficAction.DynamicsShape]
    DYNAMICS_SHAPE_UNSPECIFIED: TrafficAction.DynamicsShape
    DYNAMICS_SHAPE_LINEAR: TrafficAction.DynamicsShape
    DYNAMICS_SHAPE_CUBIC: TrafficAction.DynamicsShape
    DYNAMICS_SHAPE_SINUSOIDAL: TrafficAction.DynamicsShape
    DYNAMICS_SHAPE_STEP: TrafficAction.DynamicsShape
    class ActionHeader(_message.Message):
        __slots__ = ("action_id",)
        ACTION_ID_FIELD_NUMBER: _ClassVar[int]
        action_id: _osi_common_pb2.Identifier
        def __init__(self, action_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ...) -> None: ...
    class DynamicConstraints(_message.Message):
        __slots__ = ("max_acceleration", "max_deceleration", "max_speed")
        MAX_ACCELERATION_FIELD_NUMBER: _ClassVar[int]
        MAX_DECELERATION_FIELD_NUMBER: _ClassVar[int]
        MAX_SPEED_FIELD_NUMBER: _ClassVar[int]
        max_acceleration: float
        max_deceleration: float
        max_speed: float
        def __init__(self, max_acceleration: _Optional[float] = ..., max_deceleration: _Optional[float] = ..., max_speed: _Optional[float] = ...) -> None: ...
    class FollowTrajectoryAction(_message.Message):
        __slots__ = ("action_header", "trajectory_point", "constrain_orientation", "following_mode")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        TRAJECTORY_POINT_FIELD_NUMBER: _ClassVar[int]
        CONSTRAIN_ORIENTATION_FIELD_NUMBER: _ClassVar[int]
        FOLLOWING_MODE_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        trajectory_point: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.StatePoint]
        constrain_orientation: bool
        following_mode: TrafficAction.FollowingMode
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., trajectory_point: _Optional[_Iterable[_Union[_osi_common_pb2.StatePoint, _Mapping]]] = ..., constrain_orientation: bool = ..., following_mode: _Optional[_Union[TrafficAction.FollowingMode, str]] = ...) -> None: ...
    class FollowPathAction(_message.Message):
        __slots__ = ("action_header", "path_point", "constrain_orientation", "following_mode")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        PATH_POINT_FIELD_NUMBER: _ClassVar[int]
        CONSTRAIN_ORIENTATION_FIELD_NUMBER: _ClassVar[int]
        FOLLOWING_MODE_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        path_point: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.StatePoint]
        constrain_orientation: bool
        following_mode: TrafficAction.FollowingMode
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., path_point: _Optional[_Iterable[_Union[_osi_common_pb2.StatePoint, _Mapping]]] = ..., constrain_orientation: bool = ..., following_mode: _Optional[_Union[TrafficAction.FollowingMode, str]] = ...) -> None: ...
    class AcquireGlobalPositionAction(_message.Message):
        __slots__ = ("action_header", "position", "orientation")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        POSITION_FIELD_NUMBER: _ClassVar[int]
        ORIENTATION_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        position: _osi_common_pb2.Vector3d
        orientation: _osi_common_pb2.Orientation3d
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ...) -> None: ...
    class LaneChangeAction(_message.Message):
        __slots__ = ("action_header", "relative_target_lane", "dynamics_shape", "duration", "distance")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        RELATIVE_TARGET_LANE_FIELD_NUMBER: _ClassVar[int]
        DYNAMICS_SHAPE_FIELD_NUMBER: _ClassVar[int]
        DURATION_FIELD_NUMBER: _ClassVar[int]
        DISTANCE_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        relative_target_lane: int
        dynamics_shape: TrafficAction.DynamicsShape
        duration: float
        distance: float
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., relative_target_lane: _Optional[int] = ..., dynamics_shape: _Optional[_Union[TrafficAction.DynamicsShape, str]] = ..., duration: _Optional[float] = ..., distance: _Optional[float] = ...) -> None: ...
    class SpeedAction(_message.Message):
        __slots__ = ("action_header", "absolute_target_speed", "dynamics_shape", "duration", "distance")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        ABSOLUTE_TARGET_SPEED_FIELD_NUMBER: _ClassVar[int]
        DYNAMICS_SHAPE_FIELD_NUMBER: _ClassVar[int]
        DURATION_FIELD_NUMBER: _ClassVar[int]
        DISTANCE_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        absolute_target_speed: float
        dynamics_shape: TrafficAction.DynamicsShape
        duration: float
        distance: float
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., absolute_target_speed: _Optional[float] = ..., dynamics_shape: _Optional[_Union[TrafficAction.DynamicsShape, str]] = ..., duration: _Optional[float] = ..., distance: _Optional[float] = ...) -> None: ...
    class AbortActionsAction(_message.Message):
        __slots__ = ("action_header", "target_action_id")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        TARGET_ACTION_ID_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        target_action_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., target_action_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ...) -> None: ...
    class EndActionsAction(_message.Message):
        __slots__ = ("action_header", "target_action_id")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        TARGET_ACTION_ID_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        target_action_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., target_action_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ...) -> None: ...
    class CustomAction(_message.Message):
        __slots__ = ("action_header", "command", "command_type")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        COMMAND_FIELD_NUMBER: _ClassVar[int]
        COMMAND_TYPE_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        command: str
        command_type: str
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., command: _Optional[str] = ..., command_type: _Optional[str] = ...) -> None: ...
    class LongitudinalDistanceAction(_message.Message):
        __slots__ = ("action_header", "target_traffic_participant_id", "distance", "freespace", "follow", "dynamic_constraints")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        TARGET_TRAFFIC_PARTICIPANT_ID_FIELD_NUMBER: _ClassVar[int]
        DISTANCE_FIELD_NUMBER: _ClassVar[int]
        FREESPACE_FIELD_NUMBER: _ClassVar[int]
        FOLLOW_FIELD_NUMBER: _ClassVar[int]
        DYNAMIC_CONSTRAINTS_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        target_traffic_participant_id: _osi_common_pb2.Identifier
        distance: float
        freespace: bool
        follow: bool
        dynamic_constraints: TrafficAction.DynamicConstraints
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., target_traffic_participant_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., distance: _Optional[float] = ..., freespace: bool = ..., follow: bool = ..., dynamic_constraints: _Optional[_Union[TrafficAction.DynamicConstraints, _Mapping]] = ...) -> None: ...
    class LateralDistanceAction(_message.Message):
        __slots__ = ("action_header", "target_traffic_participant_id", "distance", "freespace", "follow", "dynamic_constraints")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        TARGET_TRAFFIC_PARTICIPANT_ID_FIELD_NUMBER: _ClassVar[int]
        DISTANCE_FIELD_NUMBER: _ClassVar[int]
        FREESPACE_FIELD_NUMBER: _ClassVar[int]
        FOLLOW_FIELD_NUMBER: _ClassVar[int]
        DYNAMIC_CONSTRAINTS_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        target_traffic_participant_id: _osi_common_pb2.Identifier
        distance: float
        freespace: bool
        follow: bool
        dynamic_constraints: TrafficAction.DynamicConstraints
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., target_traffic_participant_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., distance: _Optional[float] = ..., freespace: bool = ..., follow: bool = ..., dynamic_constraints: _Optional[_Union[TrafficAction.DynamicConstraints, _Mapping]] = ...) -> None: ...
    class LaneOffsetAction(_message.Message):
        __slots__ = ("action_header", "target_lane_offset", "dynamics_shape")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        TARGET_LANE_OFFSET_FIELD_NUMBER: _ClassVar[int]
        DYNAMICS_SHAPE_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        target_lane_offset: float
        dynamics_shape: TrafficAction.DynamicsShape
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., target_lane_offset: _Optional[float] = ..., dynamics_shape: _Optional[_Union[TrafficAction.DynamicsShape, str]] = ...) -> None: ...
    class TeleportAction(_message.Message):
        __slots__ = ("action_header", "position", "orientation")
        ACTION_HEADER_FIELD_NUMBER: _ClassVar[int]
        POSITION_FIELD_NUMBER: _ClassVar[int]
        ORIENTATION_FIELD_NUMBER: _ClassVar[int]
        action_header: TrafficAction.ActionHeader
        position: _osi_common_pb2.Vector3d
        orientation: _osi_common_pb2.Orientation3d
        def __init__(self, action_header: _Optional[_Union[TrafficAction.ActionHeader, _Mapping]] = ..., position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ...) -> None: ...
    FOLLOW_TRAJECTORY_ACTION_FIELD_NUMBER: _ClassVar[int]
    FOLLOW_PATH_ACTION_FIELD_NUMBER: _ClassVar[int]
    ACQUIRE_GLOBAL_POSITION_ACTION_FIELD_NUMBER: _ClassVar[int]
    LANE_CHANGE_ACTION_FIELD_NUMBER: _ClassVar[int]
    SPEED_ACTION_FIELD_NUMBER: _ClassVar[int]
    ABORT_ACTIONS_ACTION_FIELD_NUMBER: _ClassVar[int]
    END_ACTIONS_ACTION_FIELD_NUMBER: _ClassVar[int]
    CUSTOM_ACTION_FIELD_NUMBER: _ClassVar[int]
    LONGITUDINAL_DISTANCE_ACTION_FIELD_NUMBER: _ClassVar[int]
    LANE_OFFSET_ACTION_FIELD_NUMBER: _ClassVar[int]
    LATERAL_DISTANCE_ACTION_FIELD_NUMBER: _ClassVar[int]
    TELEPORT_ACTION_FIELD_NUMBER: _ClassVar[int]
    follow_trajectory_action: TrafficAction.FollowTrajectoryAction
    follow_path_action: TrafficAction.FollowPathAction
    acquire_global_position_action: TrafficAction.AcquireGlobalPositionAction
    lane_change_action: TrafficAction.LaneChangeAction
    speed_action: TrafficAction.SpeedAction
    abort_actions_action: TrafficAction.AbortActionsAction
    end_actions_action: TrafficAction.EndActionsAction
    custom_action: TrafficAction.CustomAction
    longitudinal_distance_action: TrafficAction.LongitudinalDistanceAction
    lane_offset_action: TrafficAction.LaneOffsetAction
    lateral_distance_action: TrafficAction.LateralDistanceAction
    teleport_action: TrafficAction.TeleportAction
    def __init__(self, follow_trajectory_action: _Optional[_Union[TrafficAction.FollowTrajectoryAction, _Mapping]] = ..., follow_path_action: _Optional[_Union[TrafficAction.FollowPathAction, _Mapping]] = ..., acquire_global_position_action: _Optional[_Union[TrafficAction.AcquireGlobalPositionAction, _Mapping]] = ..., lane_change_action: _Optional[_Union[TrafficAction.LaneChangeAction, _Mapping]] = ..., speed_action: _Optional[_Union[TrafficAction.SpeedAction, _Mapping]] = ..., abort_actions_action: _Optional[_Union[TrafficAction.AbortActionsAction, _Mapping]] = ..., end_actions_action: _Optional[_Union[TrafficAction.EndActionsAction, _Mapping]] = ..., custom_action: _Optional[_Union[TrafficAction.CustomAction, _Mapping]] = ..., longitudinal_distance_action: _Optional[_Union[TrafficAction.LongitudinalDistanceAction, _Mapping]] = ..., lane_offset_action: _Optional[_Union[TrafficAction.LaneOffsetAction, _Mapping]] = ..., lateral_distance_action: _Optional[_Union[TrafficAction.LateralDistanceAction, _Mapping]] = ..., teleport_action: _Optional[_Union[TrafficAction.TeleportAction, _Mapping]] = ...) -> None: ...
