from osi3 import osi_version_pb2 as _osi_version_pb2
from osi3 import osi_common_pb2 as _osi_common_pb2
from osi3 import osi_route_pb2 as _osi_route_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class HostVehicleData(_message.Message):
    __slots__ = ("version", "timestamp", "host_vehicle_id", "location", "location_rmse", "vehicle_basics", "vehicle_powertrain", "vehicle_brake_system", "vehicle_steering", "vehicle_wheels", "vehicle_localization", "vehicle_automated_driving_function", "vehicle_motion", "route")
    class VehicleBasics(_message.Message):
        __slots__ = ("curb_weight", "operating_state")
        class OperatingState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            OPERATING_STATE_UNKNOWN: _ClassVar[HostVehicleData.VehicleBasics.OperatingState]
            OPERATING_STATE_OTHER: _ClassVar[HostVehicleData.VehicleBasics.OperatingState]
            OPERATING_STATE_SLEEP: _ClassVar[HostVehicleData.VehicleBasics.OperatingState]
            OPERATING_STATE_STANDBY: _ClassVar[HostVehicleData.VehicleBasics.OperatingState]
            OPERATING_STATE_BOARDING: _ClassVar[HostVehicleData.VehicleBasics.OperatingState]
            OPERATING_STATE_ENTERTAINMENT: _ClassVar[HostVehicleData.VehicleBasics.OperatingState]
            OPERATING_STATE_DRIVING: _ClassVar[HostVehicleData.VehicleBasics.OperatingState]
            OPERATING_STATE_DIAGNOSTIC: _ClassVar[HostVehicleData.VehicleBasics.OperatingState]
        OPERATING_STATE_UNKNOWN: HostVehicleData.VehicleBasics.OperatingState
        OPERATING_STATE_OTHER: HostVehicleData.VehicleBasics.OperatingState
        OPERATING_STATE_SLEEP: HostVehicleData.VehicleBasics.OperatingState
        OPERATING_STATE_STANDBY: HostVehicleData.VehicleBasics.OperatingState
        OPERATING_STATE_BOARDING: HostVehicleData.VehicleBasics.OperatingState
        OPERATING_STATE_ENTERTAINMENT: HostVehicleData.VehicleBasics.OperatingState
        OPERATING_STATE_DRIVING: HostVehicleData.VehicleBasics.OperatingState
        OPERATING_STATE_DIAGNOSTIC: HostVehicleData.VehicleBasics.OperatingState
        CURB_WEIGHT_FIELD_NUMBER: _ClassVar[int]
        OPERATING_STATE_FIELD_NUMBER: _ClassVar[int]
        curb_weight: float
        operating_state: HostVehicleData.VehicleBasics.OperatingState
        def __init__(self, curb_weight: _Optional[float] = ..., operating_state: _Optional[_Union[HostVehicleData.VehicleBasics.OperatingState, str]] = ...) -> None: ...
    class VehiclePowertrain(_message.Message):
        __slots__ = ("pedal_position_acceleration", "pedal_position_clutch", "gear_transmission", "motor")
        class Motor(_message.Message):
            __slots__ = ("type", "rpm", "torque")
            class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
                __slots__ = ()
                TYPE_UNKNOWN: _ClassVar[HostVehicleData.VehiclePowertrain.Motor.Type]
                TYPE_OTHER: _ClassVar[HostVehicleData.VehiclePowertrain.Motor.Type]
                TYPE_OTTO: _ClassVar[HostVehicleData.VehiclePowertrain.Motor.Type]
                TYPE_DIESEL: _ClassVar[HostVehicleData.VehiclePowertrain.Motor.Type]
                TYPE_ELECTRIC: _ClassVar[HostVehicleData.VehiclePowertrain.Motor.Type]
            TYPE_UNKNOWN: HostVehicleData.VehiclePowertrain.Motor.Type
            TYPE_OTHER: HostVehicleData.VehiclePowertrain.Motor.Type
            TYPE_OTTO: HostVehicleData.VehiclePowertrain.Motor.Type
            TYPE_DIESEL: HostVehicleData.VehiclePowertrain.Motor.Type
            TYPE_ELECTRIC: HostVehicleData.VehiclePowertrain.Motor.Type
            TYPE_FIELD_NUMBER: _ClassVar[int]
            RPM_FIELD_NUMBER: _ClassVar[int]
            TORQUE_FIELD_NUMBER: _ClassVar[int]
            type: HostVehicleData.VehiclePowertrain.Motor.Type
            rpm: float
            torque: float
            def __init__(self, type: _Optional[_Union[HostVehicleData.VehiclePowertrain.Motor.Type, str]] = ..., rpm: _Optional[float] = ..., torque: _Optional[float] = ...) -> None: ...
        PEDAL_POSITION_ACCELERATION_FIELD_NUMBER: _ClassVar[int]
        PEDAL_POSITION_CLUTCH_FIELD_NUMBER: _ClassVar[int]
        GEAR_TRANSMISSION_FIELD_NUMBER: _ClassVar[int]
        MOTOR_FIELD_NUMBER: _ClassVar[int]
        pedal_position_acceleration: float
        pedal_position_clutch: float
        gear_transmission: int
        motor: _containers.RepeatedCompositeFieldContainer[HostVehicleData.VehiclePowertrain.Motor]
        def __init__(self, pedal_position_acceleration: _Optional[float] = ..., pedal_position_clutch: _Optional[float] = ..., gear_transmission: _Optional[int] = ..., motor: _Optional[_Iterable[_Union[HostVehicleData.VehiclePowertrain.Motor, _Mapping]]] = ...) -> None: ...
    class VehicleBrakeSystem(_message.Message):
        __slots__ = ("pedal_position_brake",)
        PEDAL_POSITION_BRAKE_FIELD_NUMBER: _ClassVar[int]
        pedal_position_brake: float
        def __init__(self, pedal_position_brake: _Optional[float] = ...) -> None: ...
    class VehicleSteering(_message.Message):
        __slots__ = ("vehicle_steering_wheel",)
        VEHICLE_STEERING_WHEEL_FIELD_NUMBER: _ClassVar[int]
        vehicle_steering_wheel: _osi_common_pb2.VehicleSteeringWheel
        def __init__(self, vehicle_steering_wheel: _Optional[_Union[_osi_common_pb2.VehicleSteeringWheel, _Mapping]] = ...) -> None: ...
    class VehicleWheels(_message.Message):
        __slots__ = ("wheel_data",)
        class WheelData(_message.Message):
            __slots__ = ("axle", "index", "rotation_rate", "slip")
            AXLE_FIELD_NUMBER: _ClassVar[int]
            INDEX_FIELD_NUMBER: _ClassVar[int]
            ROTATION_RATE_FIELD_NUMBER: _ClassVar[int]
            SLIP_FIELD_NUMBER: _ClassVar[int]
            axle: int
            index: int
            rotation_rate: float
            slip: float
            def __init__(self, axle: _Optional[int] = ..., index: _Optional[int] = ..., rotation_rate: _Optional[float] = ..., slip: _Optional[float] = ...) -> None: ...
        WHEEL_DATA_FIELD_NUMBER: _ClassVar[int]
        wheel_data: _containers.RepeatedCompositeFieldContainer[HostVehicleData.VehicleWheels.WheelData]
        def __init__(self, wheel_data: _Optional[_Iterable[_Union[HostVehicleData.VehicleWheels.WheelData, _Mapping]]] = ...) -> None: ...
    class VehicleLocalization(_message.Message):
        __slots__ = ("position", "orientation", "geodetic_position")
        POSITION_FIELD_NUMBER: _ClassVar[int]
        ORIENTATION_FIELD_NUMBER: _ClassVar[int]
        GEODETIC_POSITION_FIELD_NUMBER: _ClassVar[int]
        position: _osi_common_pb2.Vector3d
        orientation: _osi_common_pb2.Orientation3d
        geodetic_position: _osi_common_pb2.GeodeticPosition
        def __init__(self, position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ..., geodetic_position: _Optional[_Union[_osi_common_pb2.GeodeticPosition, _Mapping]] = ...) -> None: ...
    class VehicleMotion(_message.Message):
        __slots__ = ("position", "orientation", "velocity", "orientation_rate", "acceleration", "current_curvature")
        POSITION_FIELD_NUMBER: _ClassVar[int]
        ORIENTATION_FIELD_NUMBER: _ClassVar[int]
        VELOCITY_FIELD_NUMBER: _ClassVar[int]
        ORIENTATION_RATE_FIELD_NUMBER: _ClassVar[int]
        ACCELERATION_FIELD_NUMBER: _ClassVar[int]
        CURRENT_CURVATURE_FIELD_NUMBER: _ClassVar[int]
        position: _osi_common_pb2.Vector3d
        orientation: _osi_common_pb2.Orientation3d
        velocity: _osi_common_pb2.Vector3d
        orientation_rate: _osi_common_pb2.Orientation3d
        acceleration: _osi_common_pb2.Vector3d
        current_curvature: float
        def __init__(self, position: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., orientation: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ..., velocity: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., orientation_rate: _Optional[_Union[_osi_common_pb2.Orientation3d, _Mapping]] = ..., acceleration: _Optional[_Union[_osi_common_pb2.Vector3d, _Mapping]] = ..., current_curvature: _Optional[float] = ...) -> None: ...
    class VehicleAutomatedDrivingFunction(_message.Message):
        __slots__ = ("name", "custom_name", "state", "custom_state", "driver_override", "custom_detail")
        class Name(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            NAME_UNKNOWN: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_OTHER: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_BLIND_SPOT_WARNING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_FORWARD_COLLISION_WARNING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_LANE_DEPARTURE_WARNING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_PARKING_COLLISION_WARNING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_REAR_CROSS_TRAFFIC_WARNING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_AUTOMATIC_EMERGENCY_BRAKING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_AUTOMATIC_EMERGENCY_STEERING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_REVERSE_AUTOMATIC_EMERGENCY_BRAKING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_ADAPTIVE_CRUISE_CONTROL: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_LANE_KEEPING_ASSIST: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_ACTIVE_DRIVING_ASSISTANCE: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_BACKUP_CAMERA: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_SURROUND_VIEW_CAMERA: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_ACTIVE_PARKING_ASSISTANCE: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_REMOTE_PARKING_ASSISTANCE: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_TRAILER_ASSISTANCE: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_AUTOMATIC_HIGH_BEAMS: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_DRIVER_MONITORING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_HEAD_UP_DISPLAY: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_NIGHT_VISION: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_URBAN_DRIVING: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_HIGHWAY_AUTOPILOT: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_CRUISE_CONTROL: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
            NAME_SPEED_LIMIT_CONTROL: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.Name]
        NAME_UNKNOWN: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_OTHER: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_BLIND_SPOT_WARNING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_FORWARD_COLLISION_WARNING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_LANE_DEPARTURE_WARNING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_PARKING_COLLISION_WARNING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_REAR_CROSS_TRAFFIC_WARNING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_AUTOMATIC_EMERGENCY_BRAKING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_AUTOMATIC_EMERGENCY_STEERING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_REVERSE_AUTOMATIC_EMERGENCY_BRAKING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_ADAPTIVE_CRUISE_CONTROL: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_LANE_KEEPING_ASSIST: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_ACTIVE_DRIVING_ASSISTANCE: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_BACKUP_CAMERA: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_SURROUND_VIEW_CAMERA: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_ACTIVE_PARKING_ASSISTANCE: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_REMOTE_PARKING_ASSISTANCE: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_TRAILER_ASSISTANCE: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_AUTOMATIC_HIGH_BEAMS: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_DRIVER_MONITORING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_HEAD_UP_DISPLAY: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_NIGHT_VISION: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_URBAN_DRIVING: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_HIGHWAY_AUTOPILOT: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_CRUISE_CONTROL: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        NAME_SPEED_LIMIT_CONTROL: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        class State(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            STATE_UNKNOWN: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.State]
            STATE_OTHER: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.State]
            STATE_ERRORED: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.State]
            STATE_UNAVAILABLE: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.State]
            STATE_AVAILABLE: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.State]
            STATE_STANDBY: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.State]
            STATE_ACTIVE: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.State]
        STATE_UNKNOWN: HostVehicleData.VehicleAutomatedDrivingFunction.State
        STATE_OTHER: HostVehicleData.VehicleAutomatedDrivingFunction.State
        STATE_ERRORED: HostVehicleData.VehicleAutomatedDrivingFunction.State
        STATE_UNAVAILABLE: HostVehicleData.VehicleAutomatedDrivingFunction.State
        STATE_AVAILABLE: HostVehicleData.VehicleAutomatedDrivingFunction.State
        STATE_STANDBY: HostVehicleData.VehicleAutomatedDrivingFunction.State
        STATE_ACTIVE: HostVehicleData.VehicleAutomatedDrivingFunction.State
        class DriverOverride(_message.Message):
            __slots__ = ("active", "override_reason")
            class Reason(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
                __slots__ = ()
                REASON_BRAKE_PEDAL: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.DriverOverride.Reason]
                REASON_STEERING_INPUT: _ClassVar[HostVehicleData.VehicleAutomatedDrivingFunction.DriverOverride.Reason]
            REASON_BRAKE_PEDAL: HostVehicleData.VehicleAutomatedDrivingFunction.DriverOverride.Reason
            REASON_STEERING_INPUT: HostVehicleData.VehicleAutomatedDrivingFunction.DriverOverride.Reason
            ACTIVE_FIELD_NUMBER: _ClassVar[int]
            OVERRIDE_REASON_FIELD_NUMBER: _ClassVar[int]
            active: bool
            override_reason: _containers.RepeatedScalarFieldContainer[HostVehicleData.VehicleAutomatedDrivingFunction.DriverOverride.Reason]
            def __init__(self, active: bool = ..., override_reason: _Optional[_Iterable[_Union[HostVehicleData.VehicleAutomatedDrivingFunction.DriverOverride.Reason, str]]] = ...) -> None: ...
        NAME_FIELD_NUMBER: _ClassVar[int]
        CUSTOM_NAME_FIELD_NUMBER: _ClassVar[int]
        STATE_FIELD_NUMBER: _ClassVar[int]
        CUSTOM_STATE_FIELD_NUMBER: _ClassVar[int]
        DRIVER_OVERRIDE_FIELD_NUMBER: _ClassVar[int]
        CUSTOM_DETAIL_FIELD_NUMBER: _ClassVar[int]
        name: HostVehicleData.VehicleAutomatedDrivingFunction.Name
        custom_name: str
        state: HostVehicleData.VehicleAutomatedDrivingFunction.State
        custom_state: str
        driver_override: HostVehicleData.VehicleAutomatedDrivingFunction.DriverOverride
        custom_detail: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.KeyValuePair]
        def __init__(self, name: _Optional[_Union[HostVehicleData.VehicleAutomatedDrivingFunction.Name, str]] = ..., custom_name: _Optional[str] = ..., state: _Optional[_Union[HostVehicleData.VehicleAutomatedDrivingFunction.State, str]] = ..., custom_state: _Optional[str] = ..., driver_override: _Optional[_Union[HostVehicleData.VehicleAutomatedDrivingFunction.DriverOverride, _Mapping]] = ..., custom_detail: _Optional[_Iterable[_Union[_osi_common_pb2.KeyValuePair, _Mapping]]] = ...) -> None: ...
    VERSION_FIELD_NUMBER: _ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    HOST_VEHICLE_ID_FIELD_NUMBER: _ClassVar[int]
    LOCATION_FIELD_NUMBER: _ClassVar[int]
    LOCATION_RMSE_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_BASICS_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_POWERTRAIN_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_BRAKE_SYSTEM_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_STEERING_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_WHEELS_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_LOCALIZATION_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_AUTOMATED_DRIVING_FUNCTION_FIELD_NUMBER: _ClassVar[int]
    VEHICLE_MOTION_FIELD_NUMBER: _ClassVar[int]
    ROUTE_FIELD_NUMBER: _ClassVar[int]
    version: _osi_version_pb2.InterfaceVersion
    timestamp: _osi_common_pb2.Timestamp
    host_vehicle_id: _osi_common_pb2.Identifier
    location: _osi_common_pb2.BaseMoving
    location_rmse: _osi_common_pb2.BaseMoving
    vehicle_basics: HostVehicleData.VehicleBasics
    vehicle_powertrain: HostVehicleData.VehiclePowertrain
    vehicle_brake_system: HostVehicleData.VehicleBrakeSystem
    vehicle_steering: HostVehicleData.VehicleSteering
    vehicle_wheels: HostVehicleData.VehicleWheels
    vehicle_localization: HostVehicleData.VehicleLocalization
    vehicle_automated_driving_function: _containers.RepeatedCompositeFieldContainer[HostVehicleData.VehicleAutomatedDrivingFunction]
    vehicle_motion: HostVehicleData.VehicleMotion
    route: _osi_route_pb2.Route
    def __init__(self, version: _Optional[_Union[_osi_version_pb2.InterfaceVersion, _Mapping]] = ..., timestamp: _Optional[_Union[_osi_common_pb2.Timestamp, _Mapping]] = ..., host_vehicle_id: _Optional[_Union[_osi_common_pb2.Identifier, _Mapping]] = ..., location: _Optional[_Union[_osi_common_pb2.BaseMoving, _Mapping]] = ..., location_rmse: _Optional[_Union[_osi_common_pb2.BaseMoving, _Mapping]] = ..., vehicle_basics: _Optional[_Union[HostVehicleData.VehicleBasics, _Mapping]] = ..., vehicle_powertrain: _Optional[_Union[HostVehicleData.VehiclePowertrain, _Mapping]] = ..., vehicle_brake_system: _Optional[_Union[HostVehicleData.VehicleBrakeSystem, _Mapping]] = ..., vehicle_steering: _Optional[_Union[HostVehicleData.VehicleSteering, _Mapping]] = ..., vehicle_wheels: _Optional[_Union[HostVehicleData.VehicleWheels, _Mapping]] = ..., vehicle_localization: _Optional[_Union[HostVehicleData.VehicleLocalization, _Mapping]] = ..., vehicle_automated_driving_function: _Optional[_Iterable[_Union[HostVehicleData.VehicleAutomatedDrivingFunction, _Mapping]]] = ..., vehicle_motion: _Optional[_Union[HostVehicleData.VehicleMotion, _Mapping]] = ..., route: _Optional[_Union[_osi_route_pb2.Route, _Mapping]] = ...) -> None: ...
