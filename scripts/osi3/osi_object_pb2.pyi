from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class StationaryObject(_message.Message):
    __slots__ = ("id", "base", "classification", "model_reference", "source_reference", "color_description")
    class Classification(_message.Message):
        __slots__ = ("type", "material", "density", "color", "emitting_structure_attribute", "assigned_lane_id", "assigned_lane_percentage", "logical_lane_assignment")
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            TYPE_UNKNOWN: _ClassVar[StationaryObject.Classification.Type]
            TYPE_OTHER: _ClassVar[StationaryObject.Classification.Type]
            TYPE_BRIDGE: _ClassVar[StationaryObject.Classification.Type]
            TYPE_BUILDING: _ClassVar[StationaryObject.Classification.Type]
            TYPE_POLE: _ClassVar[StationaryObject.Classification.Type]
            TYPE_PYLON: _ClassVar[StationaryObject.Classification.Type]
            TYPE_DELINEATOR: _ClassVar[StationaryObject.Classification.Type]
            TYPE_TREE: _ClassVar[StationaryObject.Classification.Type]
            TYPE_BARRIER: _ClassVar[StationaryObject.Classification.Type]
            TYPE_VEGETATION: _ClassVar[StationaryObject.Classification.Type]
            TYPE_CURBSTONE: _ClassVar[StationaryObject.Classification.Type]
            TYPE_WALL: _ClassVar[StationaryObject.Classification.Type]
            TYPE_VERTICAL_STRUCTURE: _ClassVar[StationaryObject.Classification.Type]
            TYPE_RECTANGULAR_STRUCTURE: _ClassVar[StationaryObject.Classification.Type]
            TYPE_OVERHEAD_STRUCTURE: _ClassVar[StationaryObject.Classification.Type]
            TYPE_REFLECTIVE_STRUCTURE: _ClassVar[StationaryObject.Classification.Type]
            TYPE_CONSTRUCTION_SITE_ELEMENT: _ClassVar[StationaryObject.Classification.Type]
            TYPE_SPEED_BUMP: _ClassVar[StationaryObject.Classification.Type]
            TYPE_EMITTING_STRUCTURE: _ClassVar[StationaryObject.Classification.Type]
        TYPE_UNKNOWN: StationaryObject.Classification.Type
        TYPE_OTHER: StationaryObject.Classification.Type
        TYPE_BRIDGE: StationaryObject.Classification.Type
        TYPE_BUILDING: StationaryObject.Classification.Type
        TYPE_POLE: StationaryObject.Classification.Type
        TYPE_PYLON: StationaryObject.Classification.Type
        TYPE_DELINEATOR: StationaryObject.Classification.Type
        TYPE_TREE: StationaryObject.Classification.Type
        TYPE_BARRIER: StationaryObject.Classification.Type
        TYPE_VEGETATION: StationaryObject.Classification.Type
        TYPE_CURBSTONE: StationaryObject.Classification.Type
        TYPE_WALL: StationaryObject.Classification.Type
        TYPE_VERTICAL_STRUCTURE: StationaryObject.Classification.Type
        TYPE_RECTANGULAR_STRUCTURE: StationaryObject.Classification.Type
        TYPE_OVERHEAD_STRUCTURE: StationaryObject.Classification.Type
        TYPE_REFLECTIVE_STRUCTURE: StationaryObject.Classification.Type
        TYPE_CONSTRUCTION_SITE_ELEMENT: StationaryObject.Classification.Type
        TYPE_SPEED_BUMP: StationaryObject.Classification.Type
        TYPE_EMITTING_STRUCTURE: StationaryObject.Classification.Type
        class Material(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            MATERIAL_UNKNOWN: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_OTHER: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_WOOD: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_PLASTIC: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_CONCRETE: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_METAL: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_STONE: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_GLASS: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_GLAS: _ClassVar[StationaryObject.Classification.Material]
            MATERIAL_MUD: _ClassVar[StationaryObject.Classification.Material]
        MATERIAL_UNKNOWN: StationaryObject.Classification.Material
        MATERIAL_OTHER: StationaryObject.Classification.Material
        MATERIAL_WOOD: StationaryObject.Classification.Material
        MATERIAL_PLASTIC: StationaryObject.Classification.Material
        MATERIAL_CONCRETE: StationaryObject.Classification.Material
        MATERIAL_METAL: StationaryObject.Classification.Material
        MATERIAL_STONE: StationaryObject.Classification.Material
        MATERIAL_GLASS: StationaryObject.Classification.Material
        MATERIAL_GLAS: StationaryObject.Classification.Material
        MATERIAL_MUD: StationaryObject.Classification.Material
        class Density(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            DENSITY_UNKNOWN: _ClassVar[StationaryObject.Classification.Density]
            DENSITY_OTHER: _ClassVar[StationaryObject.Classification.Density]
            DENSITY_SOLID: _ClassVar[StationaryObject.Classification.Density]
            DENSITY_SMALL_MESH: _ClassVar[StationaryObject.Classification.Density]
            DENSITY_MEDIAN_MESH: _ClassVar[StationaryObject.Classification.Density]
            DENSITY_LARGE_MESH: _ClassVar[StationaryObject.Classification.Density]
            DENSITY_OPEN: _ClassVar[StationaryObject.Classification.Density]
        DENSITY_UNKNOWN: StationaryObject.Classification.Density
        DENSITY_OTHER: StationaryObject.Classification.Density
        DENSITY_SOLID: StationaryObject.Classification.Density
        DENSITY_SMALL_MESH: StationaryObject.Classification.Density
        DENSITY_MEDIAN_MESH: StationaryObject.Classification.Density
        DENSITY_LARGE_MESH: StationaryObject.Classification.Density
        DENSITY_OPEN: StationaryObject.Classification.Density
        class Color(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            COLOR_UNKNOWN: _ClassVar[StationaryObject.Classification.Color]
            COLOR_OTHER: _ClassVar[StationaryObject.Classification.Color]
            COLOR_YELLOW: _ClassVar[StationaryObject.Classification.Color]
            COLOR_GREEN: _ClassVar[StationaryObject.Classification.Color]
            COLOR_BLUE: _ClassVar[StationaryObject.Classification.Color]
            COLOR_VIOLET: _ClassVar[StationaryObject.Classification.Color]
            COLOR_RED: _ClassVar[StationaryObject.Classification.Color]
            COLOR_ORANGE: _ClassVar[StationaryObject.Classification.Color]
            COLOR_BLACK: _ClassVar[StationaryObject.Classification.Color]
            COLOR_GRAY: _ClassVar[StationaryObject.Classification.Color]
            COLOR_GREY: _ClassVar[StationaryObject.Classification.Color]
            COLOR_WHITE: _ClassVar[StationaryObject.Classification.Color]
        COLOR_UNKNOWN: StationaryObject.Classification.Color
        COLOR_OTHER: StationaryObject.Classification.Color
        COLOR_YELLOW: StationaryObject.Classification.Color
        COLOR_GREEN: StationaryObject.Classification.Color
        COLOR_BLUE: StationaryObject.Classification.Color
        COLOR_VIOLET: StationaryObject.Classification.Color
        COLOR_RED: StationaryObject.Classification.Color
        COLOR_ORANGE: StationaryObject.Classification.Color
        COLOR_BLACK: StationaryObject.Classification.Color
        COLOR_GRAY: StationaryObject.Classification.Color
        COLOR_GREY: StationaryObject.Classification.Color
        COLOR_WHITE: StationaryObject.Classification.Color
        class EmittingStructureAttribute(_message.Message):
            __slots__ = ("wavelength_data", "emitted_spatial_signal_strength")
            WAVELENGTH_DATA_FIELD_NUMBER: _ClassVar[int]
            EMITTED_SPATIAL_SIGNAL_STRENGTH_FIELD_NUMBER: _ClassVar[int]
            wavelength_data: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.WavelengthData]
            emitted_spatial_signal_strength: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.SpatialSignalStrength]
            def __init__(self, wavelength_data: _Optional[_Iterable[_Union[_osi_common_pb2.WavelengthData, _Mapping]]] = ..., emitted_spatial_signal_strength: _Optional[_Iterable[_Union[_osi_common_pb2.SpatialSignalStrength, _Mapping]]] = ...) -> None: ...
        TYPE_FIELD_NUMBER: _ClassVar[int]
        MATERIAL_FIELD_NUMBER: _ClassVar[int]
        DENSITY_FIELD_NUMBER: _ClassVar[int]
        COLOR_FIELD_NUMBER: _ClassVar[int]
        EMITTING_STRUCTURE_ATTRIBUTE_FIELD_NUMBER: _ClassVar[int]
        ASSIGNED_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        ASSIGNED_LANE_PERCENTAGE_FIELD_NUMBER: _ClassVar[int]
        LOGICAL_LANE_ASSIGNMENT_FIELD_NUMBER: _ClassVar[int]
        type: StationaryObject.Classification.Type
        material: StationaryObject.Classification.Material
        density: StationaryObject.Classification.Density
        color: StationaryObject.Classification.Color
        emitting_structure_attribute: StationaryObject.Classification.EmittingStructureAttribute
        assigned_lane_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        assigned_lane_percentage: _containers.RepeatedScalarFieldContainer[float]
        logical_lane_assignment: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.LogicalLaneAssignment]
        def __init__(self, type: _Optional[_Union[StationaryObject.Classification.Type, str]] = ..., material: _Optional[_Union[StationaryObject.Classification.Material, str]] = ..., density: _Optional[_Union[StationaryObject.Classification.Density, str]] = ..., color: _Optional[_Union[StationaryObject.Classification.Color, str]] = ..., emitting_structure_attribute: _Optional[_Union[StationaryObject.Classification.EmittingStructureAttribute, _Mapping]] = ..., assigned_lane_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., assigned_lane_percentage: _Optional[_Iterable[float]] = ..., logical_lane_assignment: _Optional[_Iterable[_Union[_osi_common_pb2.LogicalLaneAssignment, _Mapping]]] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    BASE_FIELD_NUMBER: _ClassVar[int]
    CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    MODEL_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    base: _osi_common_pb2.BaseStationary
    classification: StationaryObject.Classification
    model_reference: str
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    color_description: _osi_common_pb2.ColorDescription
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseStationary, _Mapping]] = ..., classification: _Optional[_Union[StationaryObject.Classification, _Mapping]] = ..., model_reference: _Optional[str] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ...) -> None: ...

class MovingObject(_message.Message):
    __slots__ = ("id", "base", "type", "assigned_lane_id", "vehicle_attributes", "vehicle_classification", "model_reference", "future_trajectory", "moving_object_classification", "source_reference", "color_description", "pedestrian_attributes")
    class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        TYPE_UNKNOWN: _ClassVar[MovingObject.Type]
        TYPE_OTHER: _ClassVar[MovingObject.Type]
        TYPE_VEHICLE: _ClassVar[MovingObject.Type]
        TYPE_PEDESTRIAN: _ClassVar[MovingObject.Type]
        TYPE_ANIMAL: _ClassVar[MovingObject.Type]
    TYPE_UNKNOWN: MovingObject.Type
    TYPE_OTHER: MovingObject.Type
    TYPE_VEHICLE: MovingObject.Type
    TYPE_PEDESTRIAN: MovingObject.Type
    TYPE_ANIMAL: MovingObject.Type
    class VehicleAttributes(_message.Message):
        __slots__ = ("driver_id", "radius_wheel", "number_wheels", "bbcenter_to_rear", "bbcenter_to_front", "ground_clearance", "wheel_data", "steering_wheel_angle")
        class WheelData(_message.Message):
            __slots__ = ("axle", "index", "position", "wheel_radius", "rim_radius", "width", "orientation", "rotation_rate", "model_reference", "friction_coefficient")
            AXLE_FIELD_NUMBER: _ClassVar[int]
            INDEX_FIELD_NUMBER: _ClassVar[int]
            POSITION_FIELD_NUMBER: _ClassVar[int]
            WHEEL_RADIUS_FIELD_NUMBER: _ClassVar[int]
            RIM_RADIUS_FIELD_NUMBER: _ClassVar[int]
            WIDTH_FIELD_NUMBER: _ClassVar[int]
            ORIENTATION_FIELD_NUMBER: _ClassVar[int]
            ROTATION_RATE_FIELD_NUMBER: _ClassVar[int]
            MODEL_REFERENCE_FIELD_NUMBER: _ClassVar[int]
            FRICTION_COEFFICIENT_FIELD_NUMBER: _ClassVar[int]
            axle: int
            index: int
            position: _osi_common_pb2.Vector3d
            wheel_radius: float
            rim_radius: float
            width: float
            orientation: _osi_common_pb2.Orientation3d
            rotation_rate: float
            model_reference: str
            friction_coefficient: float
            def __init__(self, axle: _Optional[int] = ..., index: _Optional[int] = ..., position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., wheel_radius: _Optional[float] = ..., rim_radius: _Optional[float] = ..., width: _Optional[float] = ..., orientation: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ..., rotation_rate: _Optional[float] = ..., model_reference: _Optional[str] = ..., friction_coefficient: _Optional[float] = ...) -> None: ...
        DRIVER_ID_FIELD_NUMBER: _ClassVar[int]
        RADIUS_WHEEL_FIELD_NUMBER: _ClassVar[int]
        NUMBER_WHEELS_FIELD_NUMBER: _ClassVar[int]
        BBCENTER_TO_REAR_FIELD_NUMBER: _ClassVar[int]
        BBCENTER_TO_FRONT_FIELD_NUMBER: _ClassVar[int]
        GROUND_CLEARANCE_FIELD_NUMBER: _ClassVar[int]
        WHEEL_DATA_FIELD_NUMBER: _ClassVar[int]
        STEERING_WHEEL_ANGLE_FIELD_NUMBER: _ClassVar[int]
        driver_id: _osi_common_pb2.Identifier
        radius_wheel: float
        number_wheels: int
        bbcenter_to_rear: _osi_common_pb2.Vector3d
        bbcenter_to_front: _osi_common_pb2.Vector3d
        ground_clearance: float
        wheel_data: _containers.RepeatedCompositeFieldContainer[MovingObject.VehicleAttributes.WheelData]
        steering_wheel_angle: float
        def __init__(self, driver_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., radius_wheel: _Optional[float] = ..., number_wheels: _Optional[int] = ..., bbcenter_to_rear: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., bbcenter_to_front: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., ground_clearance: _Optional[float] = ..., wheel_data: _Optional[_Iterable[_Union[MovingObject.VehicleAttributes.WheelData, _Mapping]]] = ..., steering_wheel_angle: _Optional[float] = ...) -> None: ...
    class MovingObjectClassification(_message.Message):
        __slots__ = ("assigned_lane_id", "assigned_lane_percentage", "logical_lane_assignment")
        ASSIGNED_LANE_ID_FIELD_NUMBER: _ClassVar[int]
        ASSIGNED_LANE_PERCENTAGE_FIELD_NUMBER: _ClassVar[int]
        LOGICAL_LANE_ASSIGNMENT_FIELD_NUMBER: _ClassVar[int]
        assigned_lane_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
        assigned_lane_percentage: _containers.RepeatedScalarFieldContainer[float]
        logical_lane_assignment: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.LogicalLaneAssignment]
        def __init__(self, assigned_lane_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., assigned_lane_percentage: _Optional[_Iterable[float]] = ..., logical_lane_assignment: _Optional[_Iterable[_Union[_osi_common_pb2.LogicalLaneAssignment, _Mapping]]] = ...) -> None: ...
    class VehicleClassification(_message.Message):
        __slots__ = ("type", "light_state", "has_trailer", "trailer_id", "role")
        class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            TYPE_UNKNOWN: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_OTHER: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_SMALL_CAR: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_COMPACT_CAR: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_CAR: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_MEDIUM_CAR: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_LUXURY_CAR: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_DELIVERY_VAN: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_HEAVY_TRUCK: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_SEMITRACTOR: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_SEMITRAILER: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_TRAILER: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_MOTORBIKE: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_BICYCLE: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_BUS: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_TRAM: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_TRAIN: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_WHEELCHAIR: _ClassVar[MovingObject.VehicleClassification.Type]
            TYPE_STANDUP_SCOOTER: _ClassVar[MovingObject.VehicleClassification.Type]
        TYPE_UNKNOWN: MovingObject.VehicleClassification.Type
        TYPE_OTHER: MovingObject.VehicleClassification.Type
        TYPE_SMALL_CAR: MovingObject.VehicleClassification.Type
        TYPE_COMPACT_CAR: MovingObject.VehicleClassification.Type
        TYPE_CAR: MovingObject.VehicleClassification.Type
        TYPE_MEDIUM_CAR: MovingObject.VehicleClassification.Type
        TYPE_LUXURY_CAR: MovingObject.VehicleClassification.Type
        TYPE_DELIVERY_VAN: MovingObject.VehicleClassification.Type
        TYPE_HEAVY_TRUCK: MovingObject.VehicleClassification.Type
        TYPE_SEMITRACTOR: MovingObject.VehicleClassification.Type
        TYPE_SEMITRAILER: MovingObject.VehicleClassification.Type
        TYPE_TRAILER: MovingObject.VehicleClassification.Type
        TYPE_MOTORBIKE: MovingObject.VehicleClassification.Type
        TYPE_BICYCLE: MovingObject.VehicleClassification.Type
        TYPE_BUS: MovingObject.VehicleClassification.Type
        TYPE_TRAM: MovingObject.VehicleClassification.Type
        TYPE_TRAIN: MovingObject.VehicleClassification.Type
        TYPE_WHEELCHAIR: MovingObject.VehicleClassification.Type
        TYPE_STANDUP_SCOOTER: MovingObject.VehicleClassification.Type
        class Role(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            ROLE_UNKNOWN: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_OTHER: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_CIVIL: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_AMBULANCE: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_FIRE: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_POLICE: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_PUBLIC_TRANSPORT: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_ROAD_ASSISTANCE: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_GARBAGE_COLLECTION: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_ROAD_CONSTRUCTION: _ClassVar[MovingObject.VehicleClassification.Role]
            ROLE_MILITARY: _ClassVar[MovingObject.VehicleClassification.Role]
        ROLE_UNKNOWN: MovingObject.VehicleClassification.Role
        ROLE_OTHER: MovingObject.VehicleClassification.Role
        ROLE_CIVIL: MovingObject.VehicleClassification.Role
        ROLE_AMBULANCE: MovingObject.VehicleClassification.Role
        ROLE_FIRE: MovingObject.VehicleClassification.Role
        ROLE_POLICE: MovingObject.VehicleClassification.Role
        ROLE_PUBLIC_TRANSPORT: MovingObject.VehicleClassification.Role
        ROLE_ROAD_ASSISTANCE: MovingObject.VehicleClassification.Role
        ROLE_GARBAGE_COLLECTION: MovingObject.VehicleClassification.Role
        ROLE_ROAD_CONSTRUCTION: MovingObject.VehicleClassification.Role
        ROLE_MILITARY: MovingObject.VehicleClassification.Role
        class LightState(_message.Message):
            __slots__ = ("indicator_state", "front_fog_light", "rear_fog_light", "head_light", "high_beam", "reversing_light", "brake_light_state", "license_plate_illumination_rear", "emergency_vehicle_illumination", "service_vehicle_illumination")
            class IndicatorState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
                __slots__ = ()
                INDICATOR_STATE_UNKNOWN: _ClassVar[MovingObject.VehicleClassification.LightState.IndicatorState]
                INDICATOR_STATE_OTHER: _ClassVar[MovingObject.VehicleClassification.LightState.IndicatorState]
                INDICATOR_STATE_OFF: _ClassVar[MovingObject.VehicleClassification.LightState.IndicatorState]
                INDICATOR_STATE_LEFT: _ClassVar[MovingObject.VehicleClassification.LightState.IndicatorState]
                INDICATOR_STATE_RIGHT: _ClassVar[MovingObject.VehicleClassification.LightState.IndicatorState]
                INDICATOR_STATE_WARNING: _ClassVar[MovingObject.VehicleClassification.LightState.IndicatorState]
            INDICATOR_STATE_UNKNOWN: MovingObject.VehicleClassification.LightState.IndicatorState
            INDICATOR_STATE_OTHER: MovingObject.VehicleClassification.LightState.IndicatorState
            INDICATOR_STATE_OFF: MovingObject.VehicleClassification.LightState.IndicatorState
            INDICATOR_STATE_LEFT: MovingObject.VehicleClassification.LightState.IndicatorState
            INDICATOR_STATE_RIGHT: MovingObject.VehicleClassification.LightState.IndicatorState
            INDICATOR_STATE_WARNING: MovingObject.VehicleClassification.LightState.IndicatorState
            class GenericLightState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
                __slots__ = ()
                GENERIC_LIGHT_STATE_UNKNOWN: _ClassVar[MovingObject.VehicleClassification.LightState.GenericLightState]
                GENERIC_LIGHT_STATE_OTHER: _ClassVar[MovingObject.VehicleClassification.LightState.GenericLightState]
                GENERIC_LIGHT_STATE_OFF: _ClassVar[MovingObject.VehicleClassification.LightState.GenericLightState]
                GENERIC_LIGHT_STATE_ON: _ClassVar[MovingObject.VehicleClassification.LightState.GenericLightState]
                GENERIC_LIGHT_STATE_FLASHING_BLUE: _ClassVar[MovingObject.VehicleClassification.LightState.GenericLightState]
                GENERIC_LIGHT_STATE_FLASHING_BLUE_AND_RED: _ClassVar[MovingObject.VehicleClassification.LightState.GenericLightState]
                GENERIC_LIGHT_STATE_FLASHING_AMBER: _ClassVar[MovingObject.VehicleClassification.LightState.GenericLightState]
            GENERIC_LIGHT_STATE_UNKNOWN: MovingObject.VehicleClassification.LightState.GenericLightState
            GENERIC_LIGHT_STATE_OTHER: MovingObject.VehicleClassification.LightState.GenericLightState
            GENERIC_LIGHT_STATE_OFF: MovingObject.VehicleClassification.LightState.GenericLightState
            GENERIC_LIGHT_STATE_ON: MovingObject.VehicleClassification.LightState.GenericLightState
            GENERIC_LIGHT_STATE_FLASHING_BLUE: MovingObject.VehicleClassification.LightState.GenericLightState
            GENERIC_LIGHT_STATE_FLASHING_BLUE_AND_RED: MovingObject.VehicleClassification.LightState.GenericLightState
            GENERIC_LIGHT_STATE_FLASHING_AMBER: MovingObject.VehicleClassification.LightState.GenericLightState
            class BrakeLightState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
                __slots__ = ()
                BRAKE_LIGHT_STATE_UNKNOWN: _ClassVar[MovingObject.VehicleClassification.LightState.BrakeLightState]
                BRAKE_LIGHT_STATE_OTHER: _ClassVar[MovingObject.VehicleClassification.LightState.BrakeLightState]
                BRAKE_LIGHT_STATE_OFF: _ClassVar[MovingObject.VehicleClassification.LightState.BrakeLightState]
                BRAKE_LIGHT_STATE_NORMAL: _ClassVar[MovingObject.VehicleClassification.LightState.BrakeLightState]
                BRAKE_LIGHT_STATE_STRONG: _ClassVar[MovingObject.VehicleClassification.LightState.BrakeLightState]
            BRAKE_LIGHT_STATE_UNKNOWN: MovingObject.VehicleClassification.LightState.BrakeLightState
            BRAKE_LIGHT_STATE_OTHER: MovingObject.VehicleClassification.LightState.BrakeLightState
            BRAKE_LIGHT_STATE_OFF: MovingObject.VehicleClassification.LightState.BrakeLightState
            BRAKE_LIGHT_STATE_NORMAL: MovingObject.VehicleClassification.LightState.BrakeLightState
            BRAKE_LIGHT_STATE_STRONG: MovingObject.VehicleClassification.LightState.BrakeLightState
            INDICATOR_STATE_FIELD_NUMBER: _ClassVar[int]
            FRONT_FOG_LIGHT_FIELD_NUMBER: _ClassVar[int]
            REAR_FOG_LIGHT_FIELD_NUMBER: _ClassVar[int]
            HEAD_LIGHT_FIELD_NUMBER: _ClassVar[int]
            HIGH_BEAM_FIELD_NUMBER: _ClassVar[int]
            REVERSING_LIGHT_FIELD_NUMBER: _ClassVar[int]
            BRAKE_LIGHT_STATE_FIELD_NUMBER: _ClassVar[int]
            LICENSE_PLATE_ILLUMINATION_REAR_FIELD_NUMBER: _ClassVar[int]
            EMERGENCY_VEHICLE_ILLUMINATION_FIELD_NUMBER: _ClassVar[int]
            SERVICE_VEHICLE_ILLUMINATION_FIELD_NUMBER: _ClassVar[int]
            indicator_state: MovingObject.VehicleClassification.LightState.IndicatorState
            front_fog_light: MovingObject.VehicleClassification.LightState.GenericLightState
            rear_fog_light: MovingObject.VehicleClassification.LightState.GenericLightState
            head_light: MovingObject.VehicleClassification.LightState.GenericLightState
            high_beam: MovingObject.VehicleClassification.LightState.GenericLightState
            reversing_light: MovingObject.VehicleClassification.LightState.GenericLightState
            brake_light_state: MovingObject.VehicleClassification.LightState.BrakeLightState
            license_plate_illumination_rear: MovingObject.VehicleClassification.LightState.GenericLightState
            emergency_vehicle_illumination: MovingObject.VehicleClassification.LightState.GenericLightState
            service_vehicle_illumination: MovingObject.VehicleClassification.LightState.GenericLightState
            def __init__(self, indicator_state: _Optional[_Union[MovingObject.VehicleClassification.LightState.IndicatorState, str]] = ..., front_fog_light: _Optional[_Union[MovingObject.VehicleClassification.LightState.GenericLightState, str]] = ..., rear_fog_light: _Optional[_Union[MovingObject.VehicleClassification.LightState.GenericLightState, str]] = ..., head_light: _Optional[_Union[MovingObject.VehicleClassification.LightState.GenericLightState, str]] = ..., high_beam: _Optional[_Union[MovingObject.VehicleClassification.LightState.GenericLightState, str]] = ..., reversing_light: _Optional[_Union[MovingObject.VehicleClassification.LightState.GenericLightState, str]] = ..., brake_light_state: _Optional[_Union[MovingObject.VehicleClassification.LightState.BrakeLightState, str]] = ..., license_plate_illumination_rear: _Optional[_Union[MovingObject.VehicleClassification.LightState.GenericLightState, str]] = ..., emergency_vehicle_illumination: _Optional[_Union[MovingObject.VehicleClassification.LightState.GenericLightState, str]] = ..., service_vehicle_illumination: _Optional[_Union[MovingObject.VehicleClassification.LightState.GenericLightState, str]] = ...) -> None: ...
        TYPE_FIELD_NUMBER: _ClassVar[int]
        LIGHT_STATE_FIELD_NUMBER: _ClassVar[int]
        HAS_TRAILER_FIELD_NUMBER: _ClassVar[int]
        TRAILER_ID_FIELD_NUMBER: _ClassVar[int]
        ROLE_FIELD_NUMBER: _ClassVar[int]
        type: MovingObject.VehicleClassification.Type
        light_state: MovingObject.VehicleClassification.LightState
        has_trailer: bool
        trailer_id: _osi_common_pb2.Identifier
        role: MovingObject.VehicleClassification.Role
        def __init__(self, type: _Optional[_Union[MovingObject.VehicleClassification.Type, str]] = ..., light_state: _Optional[_Union[MovingObject.VehicleClassification.LightState, _Mapping]] = ..., has_trailer: bool = ..., trailer_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., role: _Optional[_Union[MovingObject.VehicleClassification.Role, str]] = ...) -> None: ...
    class PedestrianAttributes(_message.Message):
        __slots__ = ("bbcenter_to_root", "skeleton_bone")
        class Bone(_message.Message):
            __slots__ = ("type", "position", "orientation", "length", "missing", "velocity", "orientation_rate")
            class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
                __slots__ = ()
                TYPE_ROOT: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_HIP: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_LOWER_SPINE: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_UPPER_SPINE: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_NECK: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_HEAD: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_SHOULDER_L: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_SHOULDER_R: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_UPPER_ARM_L: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_UPPER_ARM_R: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_LOWER_ARM_L: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_LOWER_ARM_R: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_FULL_HAND_L: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_FULL_HAND_R: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_UPPER_LEG_L: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_UPPER_LEG_R: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_LOWER_LEG_L: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_LOWER_LEG_R: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_FULL_FOOT_L: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
                TYPE_FULL_FOOT_R: _ClassVar[MovingObject.PedestrianAttributes.Bone.Type]
            TYPE_ROOT: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_HIP: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_LOWER_SPINE: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_UPPER_SPINE: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_NECK: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_HEAD: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_SHOULDER_L: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_SHOULDER_R: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_UPPER_ARM_L: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_UPPER_ARM_R: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_LOWER_ARM_L: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_LOWER_ARM_R: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_FULL_HAND_L: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_FULL_HAND_R: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_UPPER_LEG_L: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_UPPER_LEG_R: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_LOWER_LEG_L: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_LOWER_LEG_R: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_FULL_FOOT_L: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_FULL_FOOT_R: MovingObject.PedestrianAttributes.Bone.Type
            TYPE_FIELD_NUMBER: _ClassVar[int]
            POSITION_FIELD_NUMBER: _ClassVar[int]
            ORIENTATION_FIELD_NUMBER: _ClassVar[int]
            LENGTH_FIELD_NUMBER: _ClassVar[int]
            MISSING_FIELD_NUMBER: _ClassVar[int]
            VELOCITY_FIELD_NUMBER: _ClassVar[int]
            ORIENTATION_RATE_FIELD_NUMBER: _ClassVar[int]
            type: MovingObject.PedestrianAttributes.Bone.Type
            position: _osi_common_pb2.Vector3d
            orientation: _osi_common_pb2.Orientation3d
            length: float
            missing: bool
            velocity: _osi_common_pb2.Vector3d
            orientation_rate: _osi_common_pb2.Orientation3d
            def __init__(self, type: _Optional[_Union[MovingObject.PedestrianAttributes.Bone.Type, str]] = ..., position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ..., length: _Optional[float] = ..., missing: bool = ..., velocity: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., orientation_rate: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ...) -> None: ...
        BBCENTER_TO_ROOT_FIELD_NUMBER: _ClassVar[int]
        SKELETON_BONE_FIELD_NUMBER: _ClassVar[int]
        bbcenter_to_root: _osi_common_pb2.Vector3d
        skeleton_bone: _containers.RepeatedCompositeFieldContainer[MovingObject.PedestrianAttributes.Bone]
        def __init__(self, bbcenter_to_root: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., skeleton_bone: _Optional[_Iterable[_Union[MovingObject.PedestrianAttributes.Bone, _Mapping]]] = ...) -> None: ...
    ID_FIELD_NUMBER: _ClassVar[int]
    BASE_FIELD_NUMBER: _ClassVar[int]
    TYPE_FIELD_NUMBER: _ClassVar[int]
    ASSIGNED_LANE_ID_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_ATTRIBUTES_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    MODEL_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    FUTURE_TRAJECTORY_FIELD_NUMBER: _ClassVar[int]
    MOVING_OBJECT_CLASSIFICATION_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    COLOR_DESCRIPTION_FIELD_NUMBER: _ClassVar[int]
    PEDESTRIAN_ATTRIBUTES_FIELD_NUMBER: _ClassVar[int]
    id: _osi_common_pb2.Identifier
    base: _osi_common_pb2.BaseMoving
    type: MovingObject.Type
    assigned_lane_id: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.Identifier]
    vehicle_attributes: MovingObject.VehicleAttributes
    vehicle_classification: MovingObject.VehicleClassification
    model_reference: str
    future_trajectory: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.StatePoint]
    moving_object_classification: MovingObject.MovingObjectClassification
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    color_description: _osi_common_pb2.ColorDescription
    pedestrian_attributes: MovingObject.PedestrianAttributes
    def __init__(self, id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., base: _Optional[_Union[_osi_common_pb2.BaseMoving, _Mapping]] = ..., type: _Optional[_Union[MovingObject.Type, str]] = ..., assigned_lane_id: _Optional[_Iterable[_Union[_osi_common_pb2.Identifier, _Mapping]]] = ..., vehicle_attributes: _Optional[_Union[MovingObject.VehicleAttributes, _Mapping]] = ..., vehicle_classification: _Optional[_Union[MovingObject.VehicleClassification, _Mapping]] = ..., model_reference: _Optional[str] = ..., future_trajectory: _Optional[_Iterable[_Union[_osi_common_pb2.StatePoint, _Mapping]]] = ..., moving_object_classification: _Optional[_Union[MovingObject.MovingObjectClassification, _Mapping]] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ..., color_description: _Optional[_Union[_osi_common_pb2.ColorDescription, _Mapping]] = ..., pedestrian_attributes: _Optional[_Union[MovingObject.PedestrianAttributes, _Mapping]] = ...) -> None: ...
