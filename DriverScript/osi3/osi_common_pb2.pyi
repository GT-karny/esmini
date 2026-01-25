from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Vector3d(_message.Message):
    __slots__ = ("x", "y", "z")
    X_FIELD_NUMBER: _ClassVar[int]
    Y_FIELD_NUMBER: _ClassVar[int]
    Z_FIELD_NUMBER: _ClassVar[int]
    x: float
    y: float
    z: float
    def __init__(self, x: _Optional[float] = ..., y: _Optional[float] = ..., z: _Optional[float] = ...) -> None: ...

class Vector2d(_message.Message):
    __slots__ = ("x", "y")
    X_FIELD_NUMBER: _ClassVar[int]
    Y_FIELD_NUMBER: _ClassVar[int]
    x: float
    y: float
    def __init__(self, x: _Optional[float] = ..., y: _Optional[float] = ...) -> None: ...

class Timestamp(_message.Message):
    __slots__ = ("seconds", "nanos")
    SECONDS_FIELD_NUMBER: _ClassVar[int]
    NANOS_FIELD_NUMBER: _ClassVar[int]
    seconds: int
    nanos: int
    def __init__(self, seconds: _Optional[int] = ..., nanos: _Optional[int] = ...) -> None: ...

class Dimension3d(_message.Message):
    __slots__ = ("length", "width", "height")
    LENGTH_FIELD_NUMBER: _ClassVar[int]
    WIDTH_FIELD_NUMBER: _ClassVar[int]
    HEIGHT_FIELD_NUMBER: _ClassVar[int]
    length: float
    width: float
    height: float
    def __init__(self, length: _Optional[float] = ..., width: _Optional[float] = ..., height: _Optional[float] = ...) -> None: ...

class Orientation3d(_message.Message):
    __slots__ = ("roll", "pitch", "yaw")
    ROLL_FIELD_NUMBER: _ClassVar[int]
    PITCH_FIELD_NUMBER: _ClassVar[int]
    YAW_FIELD_NUMBER: _ClassVar[int]
    roll: float
    pitch: float
    yaw: float
    def __init__(self, roll: _Optional[float] = ..., pitch: _Optional[float] = ..., yaw: _Optional[float] = ...) -> None: ...

class Identifier(_message.Message):
    __slots__ = ("value",)
    VALUE_FIELD_NUMBER: _ClassVar[int]
    value: int
    def __init__(self, value: _Optional[int] = ...) -> None: ...

class ExternalReference(_message.Message):
    __slots__ = ("reference", "type", "identifier")
    REFERENCE_FIELD_NUMBER: _ClassVar[int]
    TYPE_FIELD_NUMBER: _ClassVar[int]
    IDENTIFIER_FIELD_NUMBER: _ClassVar[int]
    reference: str
    type: str
    identifier: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, reference: _Optional[str] = ..., type: _Optional[str] = ..., identifier: _Optional[_Iterable[str]] = ...) -> None: ...

class MountingPosition(_message.Message):
    __slots__ = ("position", "orientation")
    POSITION_FIELD_NUMBER: _ClassVar[int]
    ORIENTATION_FIELD_NUMBER: _ClassVar[int]
    position: Vector3d
    orientation: Orientation3d
    def __init__(self, position: _Optional[_Union[Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[Orientation3d, _Mapping]] = ...) -> None: ...

class Spherical3d(_message.Message):
    __slots__ = ("distance", "azimuth", "elevation")
    DISTANCE_FIELD_NUMBER: _ClassVar[int]
    AZIMUTH_FIELD_NUMBER: _ClassVar[int]
    ELEVATION_FIELD_NUMBER: _ClassVar[int]
    distance: float
    azimuth: float
    elevation: float
    def __init__(self, distance: _Optional[float] = ..., azimuth: _Optional[float] = ..., elevation: _Optional[float] = ...) -> None: ...

class LogicalLaneAssignment(_message.Message):
    __slots__ = ("assigned_lane_id", "s_position", "t_position", "angle_to_lane")
    ASSIGNED_LANE_ID_FIELD_NUMBER: _ClassVar[int]
    S_POSITION_FIELD_NUMBER: _ClassVar[int]
    T_POSITION_FIELD_NUMBER: _ClassVar[int]
    ANGLE_TO_LANE_FIELD_NUMBER: _ClassVar[int]
    assigned_lane_id: Identifier
    s_position: float
    t_position: float
    angle_to_lane: float
    def __init__(self, assigned_lane_id: _Optional[_Union[Identifier, _Mapping]] = ..., s_position: _Optional[float] = ..., t_position: _Optional[float] = ..., angle_to_lane: _Optional[float] = ...) -> None: ...

class BoundingBox(_message.Message):
    __slots__ = ("dimension", "position", "orientation", "contained_object_type", "model_reference")
    class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        TYPE_UNKNOWN: _ClassVar[BoundingBox.Type]
        TYPE_OTHER: _ClassVar[BoundingBox.Type]
        TYPE_BASE_STRUCTURE: _ClassVar[BoundingBox.Type]
        TYPE_PROTRUDING_STRUCTURE: _ClassVar[BoundingBox.Type]
        TYPE_CARGO: _ClassVar[BoundingBox.Type]
        TYPE_DOOR: _ClassVar[BoundingBox.Type]
        TYPE_SIDE_MIRROR: _ClassVar[BoundingBox.Type]
    TYPE_UNKNOWN: BoundingBox.Type
    TYPE_OTHER: BoundingBox.Type
    TYPE_BASE_STRUCTURE: BoundingBox.Type
    TYPE_PROTRUDING_STRUCTURE: BoundingBox.Type
    TYPE_CARGO: BoundingBox.Type
    TYPE_DOOR: BoundingBox.Type
    TYPE_SIDE_MIRROR: BoundingBox.Type
    DIMENSION_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    ORIENTATION_FIELD_NUMBER: _ClassVar[int]
    CONTAINED_OBJECT_TYPE_FIELD_NUMBER: _ClassVar[int]
    MODEL_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    dimension: Dimension3d
    position: Vector3d
    orientation: Orientation3d
    contained_object_type: BoundingBox.Type
    model_reference: str
    def __init__(self, dimension: _Optional[_Union[Dimension3d, _Mapping]] = ..., position: _Optional[_Union[Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[Orientation3d, _Mapping]] = ..., contained_object_type: _Optional[_Union[BoundingBox.Type, str]] = ..., model_reference: _Optional[str] = ...) -> None: ...

class BaseStationary(_message.Message):
    __slots__ = ("dimension", "position", "orientation", "base_polygon", "bounding_box_section")
    DIMENSION_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    ORIENTATION_FIELD_NUMBER: _ClassVar[int]
    BASE_POLYGON_FIELD_NUMBER: _ClassVar[int]
    BOUNDING_BOX_SECTION_FIELD_NUMBER: _ClassVar[int]
    dimension: Dimension3d
    position: Vector3d
    orientation: Orientation3d
    base_polygon: _containers.RepeatedCompositeFieldContainer[Vector2d]
    bounding_box_section: _containers.RepeatedCompositeFieldContainer[BoundingBox]
    def __init__(self, dimension: _Optional[_Union[Dimension3d, _Mapping]] = ..., position: _Optional[_Union[Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[Orientation3d, _Mapping]] = ..., base_polygon: _Optional[_Iterable[_Union[Vector2d, _Mapping]]] = ..., bounding_box_section: _Optional[_Iterable[_Union[BoundingBox, _Mapping]]] = ...) -> None: ...

class BaseMoving(_message.Message):
    __slots__ = ("dimension", "position", "orientation", "velocity", "acceleration", "orientation_rate", "orientation_acceleration", "base_polygon", "bounding_box_section")
    DIMENSION_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    ORIENTATION_FIELD_NUMBER: _ClassVar[int]
    VELOCITY_FIELD_NUMBER: _ClassVar[int]
    ACCELERATION_FIELD_NUMBER: _ClassVar[int]
    ORIENTATION_RATE_FIELD_NUMBER: _ClassVar[int]
    ORIENTATION_ACCELERATION_FIELD_NUMBER: _ClassVar[int]
    BASE_POLYGON_FIELD_NUMBER: _ClassVar[int]
    BOUNDING_BOX_SECTION_FIELD_NUMBER: _ClassVar[int]
    dimension: Dimension3d
    position: Vector3d
    orientation: Orientation3d
    velocity: Vector3d
    acceleration: Vector3d
    orientation_rate: Orientation3d
    orientation_acceleration: Orientation3d
    base_polygon: _containers.RepeatedCompositeFieldContainer[Vector2d]
    bounding_box_section: _containers.RepeatedCompositeFieldContainer[BoundingBox]
    def __init__(self, dimension: _Optional[_Union[Dimension3d, _Mapping]] = ..., position: _Optional[_Union[Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[Orientation3d, _Mapping]] = ..., velocity: _Optional[_Union[Vector3d, _Mapping]] = ..., acceleration: _Optional[_Union[Vector3d, _Mapping]] = ..., orientation_rate: _Optional[_Union[Orientation3d, _Mapping]] = ..., orientation_acceleration: _Optional[_Union[Orientation3d, _Mapping]] = ..., base_polygon: _Optional[_Iterable[_Union[Vector2d, _Mapping]]] = ..., bounding_box_section: _Optional[_Iterable[_Union[BoundingBox, _Mapping]]] = ...) -> None: ...

class StatePoint(_message.Message):
    __slots__ = ("timestamp", "position", "orientation")
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    ORIENTATION_FIELD_NUMBER: _ClassVar[int]
    timestamp: Timestamp
    position: Vector3d
    orientation: Orientation3d
    def __init__(self, timestamp: _Optional[_Union[Timestamp, _Mapping]] = ..., position: _Optional[_Union[Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[Orientation3d, _Mapping]] = ...) -> None: ...

class WavelengthData(_message.Message):
    __slots__ = ("start", "end", "samples_number")
    START_FIELD_NUMBER: _ClassVar[int]
    END_FIELD_NUMBER: _ClassVar[int]
    SAMPLES_NUMBER_FIELD_NUMBER: _ClassVar[int]
    start: float
    end: float
    samples_number: float
    def __init__(self, start: _Optional[float] = ..., end: _Optional[float] = ..., samples_number: _Optional[float] = ...) -> None: ...

class SpatialSignalStrength(_message.Message):
    __slots__ = ("horizontal_angle", "vertical_angle", "signal_strength")
    HORIZONTAL_ANGLE_FIELD_NUMBER: _ClassVar[int]
    VERTICAL_ANGLE_FIELD_NUMBER: _ClassVar[int]
    SIGNAL_STRENGTH_FIELD_NUMBER: _ClassVar[int]
    horizontal_angle: float
    vertical_angle: float
    signal_strength: float
    def __init__(self, horizontal_angle: _Optional[float] = ..., vertical_angle: _Optional[float] = ..., signal_strength: _Optional[float] = ...) -> None: ...

class ColorDescription(_message.Message):
    __slots__ = ("grey", "rgb", "rgbir", "hsv", "luv", "cmyk")
    GREY_FIELD_NUMBER: _ClassVar[int]
    RGB_FIELD_NUMBER: _ClassVar[int]
    RGBIR_FIELD_NUMBER: _ClassVar[int]
    HSV_FIELD_NUMBER: _ClassVar[int]
    LUV_FIELD_NUMBER: _ClassVar[int]
    CMYK_FIELD_NUMBER: _ClassVar[int]
    grey: ColorGrey
    rgb: ColorRGB
    rgbir: ColorRGBIR
    hsv: ColorHSV
    luv: ColorLUV
    cmyk: ColorCMYK
    def __init__(self, grey: _Optional[_Union[ColorGrey, _Mapping]] = ..., rgb: _Optional[_Union[ColorRGB, _Mapping]] = ..., rgbir: _Optional[_Union[ColorRGBIR, _Mapping]] = ..., hsv: _Optional[_Union[ColorHSV, _Mapping]] = ..., luv: _Optional[_Union[ColorLUV, _Mapping]] = ..., cmyk: _Optional[_Union[ColorCMYK, _Mapping]] = ...) -> None: ...

class ColorGrey(_message.Message):
    __slots__ = ("grey",)
    GREY_FIELD_NUMBER: _ClassVar[int]
    grey: float
    def __init__(self, grey: _Optional[float] = ...) -> None: ...

class ColorRGB(_message.Message):
    __slots__ = ("red", "green", "blue")
    RED_FIELD_NUMBER: _ClassVar[int]
    GREEN_FIELD_NUMBER: _ClassVar[int]
    BLUE_FIELD_NUMBER: _ClassVar[int]
    red: float
    green: float
    blue: float
    def __init__(self, red: _Optional[float] = ..., green: _Optional[float] = ..., blue: _Optional[float] = ...) -> None: ...

class ColorRGBIR(_message.Message):
    __slots__ = ("red", "green", "blue", "infrared")
    RED_FIELD_NUMBER: _ClassVar[int]
    GREEN_FIELD_NUMBER: _ClassVar[int]
    BLUE_FIELD_NUMBER: _ClassVar[int]
    INFRARED_FIELD_NUMBER: _ClassVar[int]
    red: float
    green: float
    blue: float
    infrared: float
    def __init__(self, red: _Optional[float] = ..., green: _Optional[float] = ..., blue: _Optional[float] = ..., infrared: _Optional[float] = ...) -> None: ...

class ColorHSV(_message.Message):
    __slots__ = ("hue", "saturation", "value")
    HUE_FIELD_NUMBER: _ClassVar[int]
    SATURATION_FIELD_NUMBER: _ClassVar[int]
    VALUE_FIELD_NUMBER: _ClassVar[int]
    hue: float
    saturation: float
    value: float
    def __init__(self, hue: _Optional[float] = ..., saturation: _Optional[float] = ..., value: _Optional[float] = ...) -> None: ...

class ColorLUV(_message.Message):
    __slots__ = ("luminance", "u", "v")
    LUMINANCE_FIELD_NUMBER: _ClassVar[int]
    U_FIELD_NUMBER: _ClassVar[int]
    V_FIELD_NUMBER: _ClassVar[int]
    luminance: float
    u: float
    v: float
    def __init__(self, luminance: _Optional[float] = ..., u: _Optional[float] = ..., v: _Optional[float] = ...) -> None: ...

class ColorCMYK(_message.Message):
    __slots__ = ("cyan", "magenta", "yellow", "key")
    CYAN_FIELD_NUMBER: _ClassVar[int]
    MAGENTA_FIELD_NUMBER: _ClassVar[int]
    YELLOW_FIELD_NUMBER: _ClassVar[int]
    KEY_FIELD_NUMBER: _ClassVar[int]
    cyan: float
    magenta: float
    yellow: float
    key: float
    def __init__(self, cyan: _Optional[float] = ..., magenta: _Optional[float] = ..., yellow: _Optional[float] = ..., key: _Optional[float] = ...) -> None: ...

class Pedalry(_message.Message):
    __slots__ = ("pedal_position_acceleration", "pedal_position_brake", "pedal_position_clutch")
    PEDAL_POSITION_ACCELERATION_FIELD_NUMBER: _ClassVar[int]
    PEDAL_POSITION_BRAKE_FIELD_NUMBER: _ClassVar[int]
    PEDAL_POSITION_CLUTCH_FIELD_NUMBER: _ClassVar[int]
    pedal_position_acceleration: float
    pedal_position_brake: float
    pedal_position_clutch: float
    def __init__(self, pedal_position_acceleration: _Optional[float] = ..., pedal_position_brake: _Optional[float] = ..., pedal_position_clutch: _Optional[float] = ...) -> None: ...

class VehicleSteeringWheel(_message.Message):
    __slots__ = ("angle", "angular_speed", "torque")
    ANGLE_FIELD_NUMBER: _ClassVar[int]
    ANGULAR_SPEED_FIELD_NUMBER: _ClassVar[int]
    TORQUE_FIELD_NUMBER: _ClassVar[int]
    angle: float
    angular_speed: float
    torque: float
    def __init__(self, angle: _Optional[float] = ..., angular_speed: _Optional[float] = ..., torque: _Optional[float] = ...) -> None: ...

class GeodeticPosition(_message.Message):
    __slots__ = ("longitude", "latitude", "altitude")
    LONGITUDE_FIELD_NUMBER: _ClassVar[int]
    LATITUDE_FIELD_NUMBER: _ClassVar[int]
    ALTITUDE_FIELD_NUMBER: _ClassVar[int]
    longitude: float
    latitude: float
    altitude: float
    def __init__(self, longitude: _Optional[float] = ..., latitude: _Optional[float] = ..., altitude: _Optional[float] = ...) -> None: ...

class KeyValuePair(_message.Message):
    __slots__ = ("key", "value")
    KEY_FIELD_NUMBER: _ClassVar[int]
    VALUE_FIELD_NUMBER: _ClassVar[int]
    key: str
    value: str
    def __init__(self, key: _Optional[str] = ..., value: _Optional[str] = ...) -> None: ...

class Polygon3d(_message.Message):
    __slots__ = ("vertex",)
    VERTEX_FIELD_NUMBER: _ClassVar[int]
    vertex: _containers.RepeatedCompositeFieldContainer[Vector3d]
    def __init__(self, vertex: _Optional[_Iterable[_Union[Vector3d, _Mapping]]] = ...) -> None: ...
