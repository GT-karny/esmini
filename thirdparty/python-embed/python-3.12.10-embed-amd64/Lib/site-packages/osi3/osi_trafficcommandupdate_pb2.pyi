from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TrafficCommandUpdate(_message.Message):
    __slots__ = ("version", "timestamp", "traffic_participant_id", "dismissed_action")
    class DismissedAction(_message.Message):
        __slots__ = ("dismissed_action_id", "failure_reason")
        DISMISSED_ACTION_ID_FIELD_NUMBER: _ClassVar[int]
        FAILURE_REASON_FIELD_NUMBER: _ClassVar[int]
        dismissed_action_id: _osi_common_pb2.Identifier
        failure_reason: str
        def __init__(self, dismissed_action_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., failure_reason: _Optional[str] = ...) -> None: ...
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    TRAFFIC_PARTICIPANT_ID_FIELD_NUMBER: _ClassVar[int]
    DISMISSED_ACTION_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    traffic_participant_id: _osi_common_pb2.Identifier
    dismissed_action: _containers.RepeatedCompositeFieldContainer[TrafficCommandUpdate.DismissedAction]
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., traffic_participant_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., dismissed_action: _Optional[_Iterable[_Union[TrafficCommandUpdate.DismissedAction, _Mapping]]] = ...) -> None: ...
