from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Lane(_message.Message):
    __slots__ = ("id", "classification", "source_reference")
    class Classification(_message.Message):
        __slots__ = ("type", "is_host_vehicle_lane", "centerline", "centerline_is_driving_direction", "left_adjacent_lane_id", "right_adjacent_lane_id", "lane_pairing", "right_lane_boundary_id", "left_lane_boundary_id", "free_lane_boundary_id", "road_condition", "subtype")
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            TYPE_UNKNOWN: _ClassVar[Lane.Classification.Type]
            TYPE_OTHER: _ClassVar[Lane.Classification.Type]
            TYPE_DRIVING: _ClassVar[Lane.Classification.Type]
            TYPE_NONDRIVING: _ClassVar[Lane.Classification.Type]
            TYPE_INTERSECTION: _ClassVar[Lane.Classification.Type]
        TYPE_UNKNOWN: Lane.Classification.Type
        TYPE_OTHER: Lane.Classification.Type
        TYPE_DRIVING: Lane.Classification.Type
        TYPE_NONDRIVING: Lane.Classification.Type
        TYPE_INTERSECTION: Lane.Classification.Type
        class Subtype(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            SUBTYPE_UNKNOWN: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_OTHER: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_NORMAL: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_BIKING: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_SIDEWALK: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_PARKING: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_STOP: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_RESTRICTED: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_BORDER: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_SHOULDER: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_EXIT: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_ENTRY: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_ONRAMP: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_OFFRAMP: _ClassVar[Lane.Classification.Subtype]
            SUBTYPE_CONNECTINGRAMP: _ClassVar[Lane.Classification.Subtype]
        SUBTYPE_UNKNOWN: Lane.Classification.Subtype
        SUBTYPE_OTHER: Lane.Classification.Subtype
        SUBTYPE_NORMAL: Lane.Classification.Subtype
        SUBTYPE_BIKING: Lane.Classification.Subtype
        SUBTYPE_SIDEWALK: Lane.Classification.Subtype
        SUBTYPE_PARKING: Lane.Classification.Subtype
        SUBTYPE_STOP: Lane.Classification.Subtype
        SUBTYPE_RESTRICTED: Lane.Classification.Subtype
        SUBTYPE_BORDER: Lane.Classification.Subtype
        SUBTYPE_SHOULDER: Lane.Classification.Subtype
        SUBTYPE_EXIT: Lane.Classification.Subtype
        SUBTYPE_ENTRY: Lane.Classification.Subtype
        SUBTYPE_ONRAMP: Lane.Classification.Subtype
        SUBTYPE_OFFRAMP: Lane.Classification.Subtype
        SUBTYPE_CONNECTINGRAMP: Lane.Classification.Subtype
        class RoadCondition(_message.Message):
            __slots__ = ("surface_temperature", "surface_water_film", "surface_freezing_point", "surface_ice", "surface_roughness", "surface_texture")
            SURFACE_TEMPERATURE_FIELD_NUMBER: _ClassVar[int]
            SURFACE_WATER_FILM_FIELD_NUMBER: _ClassVar[int]
            SURFACE_FREEZING_POINT_FIELD_NUMBER: _ClassVar[int]
            SURFACE_ICE_FIELD_NUMBER: _ClassVar[int]
            SURFACE_ROUGHNESS_FIELD_NUMBER: _ClassVar[int]
            SURFACE_TEXTURE_FIELD_NUMBER: _ClassVar[int]
            surface_temperature: float
            surface_water_film: float
            surface_freezing_point: float
            surface_ice: float
            surface_roughness: float
            surface_texture: float
            def __init__(self, surface_temperature: _Optional[float] = ..., surface_water_film: _Optional[float] = ..., surface_freezing_point: _Optional[float] = ..., surface_ice: _Optional[float] = ..., surface_roughness: _Optional[float] = ..., surface_texture: _Optional[float] = ...) -> None: ...
        class LanePairing(_message.Message):
            __slots__ = ("antecessor_lane_id", "successor_lane_id")
            ANTECESSOR_LANE_ID_FIELD_NUMBER: _ClassVar[int]
            SUCCESSOR_LANE_ID_FIELD_NUMBER: _ClassVar[int]
            antecessor_lane_id: _osi_common_pb2.Identifier
            successor_lane_id: _osi_common_pb2.Identifier
            def __init__(self, antecessor_lane_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., successor_lane_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ...) -> None: ...
        TYPE_FIELD_NUMBER: _ClassVar[int]
        IS_HOST_VEHICLE_LANE_FIELD_NUMBER: _ClassVar[int]
        CENTERLINE_FIELD_NUMBER: _ClassVar[int]
        CENTERLINE_IS_DRIVING_DIRECTION_FIELD_NUMBER: _ClassVar[int]
        LEFT_ADJACENT_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        RIGHT_ADJACENT_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        LANE_PAIRING_FIELD_NUMBER: _ClassVar[int]
        RIGHT_LANE_BOUNDARY_ID_FIELD_NUMBER: _ClassVar[int]
        LEFT_LANE_BOUNDARY_ID_FIELD_NUMBER: _ClassVar[int]
        FREE_LANE_BOUNDARY_ID_FIELD_NUMBER: _ClassVar[int]
        ROAD_CONDITION_FIELD_NUMBER: _ClassVar[int]
        SUBTYPE_FIELD_NUMBER: _ClassVar[int]
        type: Lane.Classification.Type
        is_host_vehicle_lane: bool
        centerline: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Vector3d]
        centerline_is_driving_direction: bool
        left_adjacent_lane_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        right_adjacent_lane_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        lane_pairing: _containers.RepeatedCompositeFieldContainer[Lane.Classification.LanePairing]
        right_lane_boundary_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        left_lane_boundary_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        free_lane_boundary_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        road_condition: Lane.Classification.RoadCondition
        subtype: Lane.Classification.Subtype
        def __init__(self, type: _Optional[_Union[Lane.Classification.Type, str]] = ..., is_host_vehicle_lane: bool = ..., centerline: _Optional[_Iterable[_Union[_osi_common_pb2.Vector3d, _Mapping]]] = ..., centerline_is_driving_direction: bool = ..., left_adjacent_lane_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., right_adjacent_lane_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., lane_pairing: _Optional[_Iterable[_Union[Lane.Classification.LanePairing, _Mapping]]] = ..., right_lane_boundary_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., left_lane_boundary_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., free_lane_boundary_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., road_condition: _Optional[_Union[Lane.Classification.RoadCondition, _Mapping]] = ..., subtype: _Optional[_Union[Lane.Classification.Subtype, str]] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    classification: Lane.Classification
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., classification: _Optional[_Union[Lane.Classification, _Mapping]] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ...) -> None: ...

class LaneBoundary(_message.Message):
    __slots__ = ("id", "boundary_line", "classification", "source_reference", "color_description")
    class BoundaryPoint(_message.Message):
        __slots__ = ("position", "width", "height", "dash")
        class Dash(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            DASH_UNKNOWN: _ClassVar[LaneBoundary.BoundaryPoint.Dash]
            DASH_OTHER: _ClassVar[LaneBoundary.BoundaryPoint.Dash]
            DASH_START: _ClassVar[LaneBoundary.BoundaryPoint.Dash]
            DASH_CONTINUE: _ClassVar[LaneBoundary.BoundaryPoint.Dash]
            DASH_END: _ClassVar[LaneBoundary.BoundaryPoint.Dash]
            DASH_GAP: _ClassVar[LaneBoundary.BoundaryPoint.Dash]
        DASH_UNKNOWN: LaneBoundary.BoundaryPoint.Dash
        DASH_OTHER: LaneBoundary.BoundaryPoint.Dash
        DASH_START: LaneBoundary.BoundaryPoint.Dash
        DASH_CONTINUE: LaneBoundary.BoundaryPoint.Dash
        DASH_END: LaneBoundary.BoundaryPoint.Dash
        DASH_GAP: LaneBoundary.BoundaryPoint.Dash
        POSITION_FIELD_NUMBER: _ClassVar[int]
        WIDTH_FIELD_NUMBER: _ClassVar[int]
        HEIGHT_FIELD_NUMBER: _ClassVar[int]
        DASH_FIELD_NUMBER: _ClassVar[int]
        position: _osi_common_pb2.Vector3d
        width: float
        height: float
        dash: LaneBoundary.BoundaryPoint.Dash
        def __init__(self, position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., width: _Optional[float] = ..., height: _Optional[float] = ..., dash: _Optional[_Union[LaneBoundary.BoundaryPoint.Dash, str]] = ...) -> None: ...
    class Classification(_message.Message):
        __slots__ = ("type", "color", "limiting_structure_id")
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            TYPE_UNKNOWN: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_OTHER: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_NO_LINE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_SOLID_LINE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_DASHED_LINE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_BOTTS_DOTS: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_ROAD_EDGE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_SNOW_EDGE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_GRASS_EDGE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_GRAVEL_EDGE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_SOIL_EDGE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_GUARD_RAIL: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_CURB: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_STRUCTURE: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_BARRIER: _ClassVar[LaneBoundary.Classification.Type]
            TYPE_SOUND_BARRIER: _ClassVar[LaneBoundary.Classification.Type]
        TYPE_UNKNOWN: LaneBoundary.Classification.Type
        TYPE_OTHER: LaneBoundary.Classification.Type
        TYPE_NO_LINE: LaneBoundary.Classification.Type
        TYPE_SOLID_LINE: LaneBoundary.Classification.Type
        TYPE_DASHED_LINE: LaneBoundary.Classification.Type
        TYPE_BOTTS_DOTS: LaneBoundary.Classification.Type
        TYPE_ROAD_EDGE: LaneBoundary.Classification.Type
        TYPE_SNOW_EDGE: LaneBoundary.Classification.Type
        TYPE_GRASS_EDGE: LaneBoundary.Classification.Type
        TYPE_GRAVEL_EDGE: LaneBoundary.Classification.Type
        TYPE_SOIL_EDGE: LaneBoundary.Classification.Type
        TYPE_GUARD_RAIL: LaneBoundary.Classification.Type
        TYPE_CURB: LaneBoundary.Classification.Type
        TYPE_STRUCTURE: LaneBoundary.Classification.Type
        TYPE_BARRIER: LaneBoundary.Classification.Type
        TYPE_SOUND_BARRIER: LaneBoundary.Classification.Type
        class Color(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            COLOR_UNKNOWN: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_OTHER: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_NONE: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_WHITE: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_YELLOW: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_RED: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_BLUE: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_GREEN: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_VIOLET: _ClassVar[LaneBoundary.Classification.Color]
            COLOR_ORANGE: _ClassVar[LaneBoundary.Classification.Color]
        COLOR_UNKNOWN: LaneBoundary.Classification.Color
        COLOR_OTHER: LaneBoundary.Classification.Color
        COLOR_NONE: LaneBoundary.Classification.Color
        COLOR_WHITE: LaneBoundary.Classification.Color
        COLOR_YELLOW: LaneBoundary.Classification.Color
        COLOR_RED: LaneBoundary.Classification.Color
        COLOR_BLUE: LaneBoundary.Classification.Color
        COLOR_GREEN: LaneBoundary.Classification.Color
        COLOR_VIOLET: LaneBoundary.Classification.Color
        COLOR_ORANGE: LaneBoundary.Classification.Color
        TYPE_FIELD_NUMBER: _ClassVar[int]
        COLOR_FIELD_NUMBER: _ClassVar[int]
        LIMITING_STRUCTURE_ID_FIELD_NUMBER: _ClassVar[int]
        type: LaneBoundary.Classification.Type
        color: LaneBoundary.Classification.Color
        limiting_structure_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        def __init__(self, type: _Optional[_Union[LaneBoundary.Classification.Type, str]] = ..., color: _Optional[_Union[LaneBoundary.Classification.Color, str]] = ..., limiting_structure_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    BOUNDARY_LINE_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    boundary_line: _containers.RepeatedCompositeFieldContainer[LaneBoundary.BoundaryPoint]
    classification: LaneBoundary.Classification
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    color_description: _osi_common_pb2.ColorDescription
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., boundary_line: _Optional[_Iterable[_Union[LaneBoundary.BoundaryPoint, _Mapping]]] = ..., classification: _Optional[_Union[LaneBoundary.Classification, _Mapping]] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ...) -> None: ...
