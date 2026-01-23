from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ReferenceLine(_message.Message):
    __slots__ = ("id", "type", "poly_line")
    class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        TYPE_POLYLINE: _ClassVar[ReferenceLine.Type]
        TYPE_POLYLINE_WITH_T_AXIS: _ClassVar[ReferenceLine.Type]
    TYPE_POLYLINE: ReferenceLine.Type
    TYPE_POLYLINE_WITH_T_AXIS: ReferenceLine.Type
    class ReferenceLinePoint(_message.Message):
        __slots__ = ("world_position", "s_position", "t_axis_yaw")
        WORLD_POSITION_FIELD_NUMBER: _ClassVar[int]
        S_POSITION_FIELD_NUMBER: _ClassVar[int]
        T_AXIS_YAW_FIELD_NUMBER: _ClassVar[int]
        world_position: _osi_common_pb2.Vector3d
        s_position: float
        t_axis_yaw: float
        def __init__(self, world_position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., s_position: _Optional[float] = ..., t_axis_yaw: _Optional[float] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    TYPE_FIELD_NUMBER: _ClassVar[int]
    POLY_LINE_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    type: ReferenceLine.Type
    poly_line: _containers.RepeatedCompositeFieldContainer[ReferenceLine.ReferenceLinePoint]
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., type: _Optional[_Union[ReferenceLine.Type, str]] = ..., poly_line: _Optional[_Iterable[_Union[ReferenceLine.ReferenceLinePoint, _Mapping]]] = ...) -> None: ...
