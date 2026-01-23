from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_object_pb2 as _osi_object_pb2
from osi3 import osi_trafficsign_pb2 as _osi_trafficsign_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class LogicalLaneBoundary(_message.Message):
    __slots__ = ("id", "boundary_line", "reference_line_id", "physical_boundary_id", "passing_rule", "source_reference")
    class PassingRule(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        PASSING_RULE_UNKNOWN: _ClassVar[LogicalLaneBoundary.PassingRule]
        PASSING_RULE_OTHER: _ClassVar[LogicalLaneBoundary.PassingRule]
        PASSING_RULE_NONE_ALLOWED: _ClassVar[LogicalLaneBoundary.PassingRule]
        PASSING_RULE_INCREASING_T: _ClassVar[LogicalLaneBoundary.PassingRule]
        PASSING_RULE_DECREASING_T: _ClassVar[LogicalLaneBoundary.PassingRule]
        PASSING_RULE_BOTH_ALLOWED: _ClassVar[LogicalLaneBoundary.PassingRule]
    PASSING_RULE_UNKNOWN: LogicalLaneBoundary.PassingRule
    PASSING_RULE_OTHER: LogicalLaneBoundary.PassingRule
    PASSING_RULE_NONE_ALLOWED: LogicalLaneBoundary.PassingRule
    PASSING_RULE_INCREASING_T: LogicalLaneBoundary.PassingRule
    PASSING_RULE_DECREASING_T: LogicalLaneBoundary.PassingRule
    PASSING_RULE_BOTH_ALLOWED: LogicalLaneBoundary.PassingRule
    class LogicalBoundaryPoint(_message.Message):
        __slots__ = ("position", "s_position", "t_position")
        POSITION_FIELD_NUMBER: _ClassVar[int]
        S_POSITION_FIELD_NUMBER: _ClassVar[int]
        T_POSITION_FIELD_NUMBER: _ClassVar[int]
        position: _osi_common_pb2.Vector3d
        s_position: float
        t_position: float
        def __init__(self, position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., s_position: _Optional[float] = ..., t_position: _Optional[float] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    BOUNDARY_LINE_FIELD_NUMBER: _ClassVar[int]
    REFERENCE_LINE_ID_FIELD_NUMBER: _ClassVar[int]
    PHYSICAL_BOUNDARY_ID_FIELD_NUMBER: _ClassVar[int]
    PASSING_RULE_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    boundary_line: _containers.RepeatedCompositeFieldContainer[LogicalLaneBoundary.LogicalBoundaryPoint]
    reference_line_id: _osi_common_pb2.Identifier
    physical_boundary_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    passing_rule: LogicalLaneBoundary.PassingRule
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., boundary_line: _Optional[_Iterable[_Union[LogicalLaneBoundary.LogicalBoundaryPoint, _Mapping]]] = ..., reference_line_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., physical_boundary_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., passing_rule: _Optional[_Union[LogicalLaneBoundary.PassingRule, str]] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ...) -> None: ...

class LogicalLane(_message.Message):
    __slots__ = ("id", "type", "source_reference", "physical_lane_reference", "reference_line_id", "start_s", "end_s", "move_direction", "right_adjacent_lane", "left_adjacent_lane", "overlapping_lane", "right_boundary_id", "left_boundary_id", "predecessor_lane", "successor_lane", "street_name", "traffic_rule")
    class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        TYPE_UNKNOWN: _ClassVar[LogicalLane.Type]
        TYPE_OTHER: _ClassVar[LogicalLane.Type]
        TYPE_NORMAL: _ClassVar[LogicalLane.Type]
        TYPE_BIKING: _ClassVar[LogicalLane.Type]
        TYPE_SIDEWALK: _ClassVar[LogicalLane.Type]
        TYPE_PARKING: _ClassVar[LogicalLane.Type]
        TYPE_STOP: _ClassVar[LogicalLane.Type]
        TYPE_RESTRICTED: _ClassVar[LogicalLane.Type]
        TYPE_BORDER: _ClassVar[LogicalLane.Type]
        TYPE_SHOULDER: _ClassVar[LogicalLane.Type]
        TYPE_EXIT: _ClassVar[LogicalLane.Type]
        TYPE_ENTRY: _ClassVar[LogicalLane.Type]
        TYPE_ONRAMP: _ClassVar[LogicalLane.Type]
        TYPE_OFFRAMP: _ClassVar[LogicalLane.Type]
        TYPE_CONNECTINGRAMP: _ClassVar[LogicalLane.Type]
        TYPE_MEDIAN: _ClassVar[LogicalLane.Type]
        TYPE_CURB: _ClassVar[LogicalLane.Type]
        TYPE_RAIL: _ClassVar[LogicalLane.Type]
        TYPE_TRAM: _ClassVar[LogicalLane.Type]
    TYPE_UNKNOWN: LogicalLane.Type
    TYPE_OTHER: LogicalLane.Type
    TYPE_NORMAL: LogicalLane.Type
    TYPE_BIKING: LogicalLane.Type
    TYPE_SIDEWALK: LogicalLane.Type
    TYPE_PARKING: LogicalLane.Type
    TYPE_STOP: LogicalLane.Type
    TYPE_RESTRICTED: LogicalLane.Type
    TYPE_BORDER: LogicalLane.Type
    TYPE_SHOULDER: LogicalLane.Type
    TYPE_EXIT: LogicalLane.Type
    TYPE_ENTRY: LogicalLane.Type
    TYPE_ONRAMP: LogicalLane.Type
    TYPE_OFFRAMP: LogicalLane.Type
    TYPE_CONNECTINGRAMP: LogicalLane.Type
    TYPE_MEDIAN: LogicalLane.Type
    TYPE_CURB: LogicalLane.Type
    TYPE_RAIL: LogicalLane.Type
    TYPE_TRAM: LogicalLane.Type
    class MoveDirection(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        MOVE_DIRECTION_UNKNOWN: _ClassVar[LogicalLane.MoveDirection]
        MOVE_DIRECTION_OTHER: _ClassVar[LogicalLane.MoveDirection]
        MOVE_DIRECTION_INCREASING_S: _ClassVar[LogicalLane.MoveDirection]
        MOVE_DIRECTION_DECREASING_S: _ClassVar[LogicalLane.MoveDirection]
        MOVE_DIRECTION_BOTH_ALLOWED: _ClassVar[LogicalLane.MoveDirection]
    MOVE_DIRECTION_UNKNOWN: LogicalLane.MoveDirection
    MOVE_DIRECTION_OTHER: LogicalLane.MoveDirection
    MOVE_DIRECTION_INCREASING_S: LogicalLane.MoveDirection
    MOVE_DIRECTION_DECREASING_S: LogicalLane.MoveDirection
    MOVE_DIRECTION_BOTH_ALLOWED: LogicalLane.MoveDirection
    class PhysicalLaneReference(_message.Message):
        __slots__ = ("physical_lane_id", "start_s", "end_s")
        PHYSICAL_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        START_S_FIELD_NUMBER: _ClassVar[int]
        END_S_FIELD_NUMBER: _ClassVar[int]
        physical_lane_id: _osi_common_pb2.Identifier
        start_s: float
        end_s: float
        def __init__(self, physical_lane_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., start_s: _Optional[float] = ..., end_s: _Optional[float] = ...) -> None: ...
    class LaneConnection(_message.Message):
        __slots__ = ("other_lane_id", "at_begin_of_other_lane")
        OTHER_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        AT_BEGIN_OF_OTHER_LANE_FIELD_NUMBER: _ClassVar[int]
        other_lane_id: _osi_common_pb2.Identifier
        at_begin_of_other_lane: bool
        def __init__(self, other_lane_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., at_begin_of_other_lane: bool = ...) -> None: ...
    class LaneRelation(_message.Message):
        __slots__ = ("other_lane_id", "start_s", "end_s", "start_s_other", "end_s_other")
        OTHER_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        START_S_FIELD_NUMBER: _ClassVar[int]
        END_S_FIELD_NUMBER: _ClassVar[int]
        START_S_OTHER_FIELD_NUMBER: _ClassVar[int]
        END_S_OTHER_FIELD_NUMBER: _ClassVar[int]
        other_lane_id: _osi_common_pb2.Identifier
        start_s: float
        end_s: float
        start_s_other: float
        end_s_other: float
        def __init__(self, other_lane_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., start_s: _Optional[float] = ..., end_s: _Optional[float] = ..., start_s_other: _Optional[float] = ..., end_s_other: _Optional[float] = ...) -> None: ...
    class TrafficRule(_message.Message):
        __slots__ = ("traffic_rule_type", "traffic_rule_validity", "speed_limit")
        class TrafficRuleType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            TRAFFIC_RULE_TYPE_SPEED_LIMIT: _ClassVar[LogicalLane.TrafficRule.TrafficRuleType]
        TRAFFIC_RULE_TYPE_SPEED_LIMIT: LogicalLane.TrafficRule.TrafficRuleType
        class TrafficRuleValidity(_message.Message):
            __slots__ = ("start_s", "end_s", "valid_for_type")
            class TypeValidity(_message.Message):
                __slots__ = ("type", "vehicle_type", "vehicle_role")
                TYPE_FIELD_NUMBER: _ClassVar[int]
                VEHICLE_TYPE_FIELD_NUMBER: _ClassVar[int]
                VEHICLE_ROLE_FIELD_NUMBER: _ClassVar[int]
                type: _osi_object_pb2.MovingObject.Type
                vehicle_type: _osi_object_pb2.MovingObject.VehicleClassification.Type
                vehicle_role: _osi_object_pb2.MovingObject.VehicleClassification.Role
                def __init__(self, type: _Optional[_Union[_osi_object_pb2.MovingObject.Type, str]] = ..., vehicle_type: _Optional[_Union[_osi_object_pb2.MovingObject.VehicleClassification.Type, str]] = ..., vehicle_role: _Optional[_Union[_osi_object_pb2.MovingObject.VehicleClassification.Role, str]] = ...) -> None: ...
            START_S_FIELD_NUMBER: _ClassVar[int]
            END_S_FIELD_NUMBER: _ClassVar[int]
            VALID_FOR_TYPE_FIELD_NUMBER: _ClassVar[int]
            start_s: float
            end_s: float
            valid_for_type: _containers.RepeatedCompositeFieldContainer[LogicalLane.TrafficRule.TrafficRuleValidity.TypeValidity]
            def __init__(self, start_s: _Optional[float] = ..., end_s: _Optional[float] = ..., valid_for_type: _Optional[_Iterable[_Union[LogicalLane.TrafficRule.TrafficRuleValidity.TypeValidity, _Mapping]]] = ...) -> None: ...
        class SpeedLimit(_message.Message):
            __slots__ = ("speed_limit_value",)
            SPEED_LIMIT_VALUE_FIELD_NUMBER: _ClassVar[int]
            speed_limit_value: _osi_trafficsign_pb2.TrafficSignValue
            def __init__(self, speed_limit_value: _Optional[_Union[_osi_trafficsign_pb2.TrafficSignValue, _Mapping]] = ...) -> None: ...
        TRAFFIC_RULE_TYPE_FIELD_NUMBER: _ClassVar[int]
        TRAFFIC_RULE_VALIDITY_FIELD_NUMBER: _ClassVar[int]
        SPEED_LIMIT_FIELD_NUMBER: _ClassVar[int]
        traffic_rule_type: LogicalLane.TrafficRule.TrafficRuleType
        traffic_rule_validity: LogicalLane.TrafficRule.TrafficRuleValidity
        speed_limit: LogicalLane.TrafficRule.SpeedLimit
        def __init__(self, traffic_rule_type: _Optional[_Union[LogicalLane.TrafficRule.TrafficRuleType, str]] = ..., traffic_rule_validity: _Optional[_Union[LogicalLane.TrafficRule.TrafficRuleValidity, _Mapping]] = ..., speed_limit: _Optional[_Union[LogicalLane.TrafficRule.SpeedLimit, _Mapping]] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    TYPE_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    PHYSICAL_LANE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    REFERENCE_LINE_ID_FIELD_NUMBER: _ClassVar[int]
    START_S_FIELD_NUMBER: _ClassVar[int]
    END_S_FIELD_NUMBER: _ClassVar[int]
    MOVE_DIRECTION_FIELD_NUMBER: _ClassVar[int]
    RIGHT_ADJACENT_LANE_FIELD_NUMBER: _ClassVar[int]
    LEFT_ADJACENT_LANE_FIELD_NUMBER: _ClassVar[int]
    OVERLAPPING_LANE_FIELD_NUMBER: _ClassVar[int]
    RIGHT_BOUNDARY_ID_FIELD_NUMBER: _ClassVar[int]
    LEFT_BOUNDARY_ID_FIELD_NUMBER: _ClassVar[int]
    PREDECESSOR_LANE_FIELD_NUMBER: _ClassVar[int]
    SUCCESSOR_LANE_FIELD_NUMBER: _ClassVar[int]
    STREET_NAME_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_RULE_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    type: LogicalLane.Type
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    physical_lane_reference: _containers.RepeatedCompositeFieldContainer[LogicalLane.PhysicalLaneReference]
    reference_line_id: _osi_common_pb2.Identifier
    start_s: float
    end_s: float
    move_direction: LogicalLane.MoveDirection
    right_adjacent_lane: _containers.RepeatedCompositeFieldContainer[LogicalLane.LaneRelation]
    left_adjacent_lane: _containers.RepeatedCompositeFieldContainer[LogicalLane.LaneRelation]
    overlapping_lane: _containers.RepeatedCompositeFieldContainer[LogicalLane.LaneRelation]
    right_boundary_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    left_boundary_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    predecessor_lane: _containers.RepeatedCompositeFieldContainer[LogicalLane.LaneConnection]
    successor_lane: _containers.RepeatedCompositeFieldContainer[LogicalLane.LaneConnection]
    street_name: str
    traffic_rule: _containers.RepeatedCompositeFieldContainer[LogicalLane.TrafficRule]
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., type: _Optional[_Union[LogicalLane.Type, str]] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ..., physical_lane_reference: _Optional[_Iterable[_Union[LogicalLane.PhysicalLaneReference, _Mapping]]] = ..., reference_line_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., start_s: _Optional[float] = ..., end_s: _Optional[float] = ..., move_direction: _Optional[_Union[LogicalLane.MoveDirection, str]] = ..., right_adjacent_lane: _Optional[_Iterable[_Union[LogicalLane.LaneRelation, _Mapping]]] = ..., left_adjacent_lane: _Optional[_Iterable[_Union[LogicalLane.LaneRelation, _Mapping]]] = ..., overlapping_lane: _Optional[_Iterable[_Union[LogicalLane.LaneRelation, _Mapping]]] = ..., right_boundary_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., left_boundary_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., predecessor_lane: _Optional[_Iterable[_Union[LogicalLane.LaneConnection, _Mapping]]] = ..., successor_lane: _Optional[_Iterable[_Union[LogicalLane.LaneConnection, _Mapping]]] = ..., street_name: _Optional[str] = ..., traffic_rule: _Optional[_Iterable[_Union[LogicalLane.TrafficRule, _Mapping]]] = ...) -> None: ...
