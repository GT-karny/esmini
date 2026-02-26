from google.protobuf import descriptor_pb2 as _descriptor_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor
CURRENT_INTERFACE_VERSION_FIELD_NUMBER: _ClassVar[int]
current_interface_version: _descriptor.FieldDescriptor

class InterfaceVersion(_message.Message):
    __slots__ = ("version_major", "version_minor", "version_patch")
    VERSION_MAJOR_FIELD_NUMBER: _ClassVar[int]
    VERSION_MINOR_FIELD_NUMBER: _ClassVar[int]
    VERSION_PATCH_FIELD_NUMBER: _ClassVar[int]
    version_major: int
    version_minor: int
    version_patch: int
    def __init__(self, version_major: _Optional[int] = ..., version_minor: _Optional[int] = ..., version_patch: _Optional[int] = ...) -> None: ...
