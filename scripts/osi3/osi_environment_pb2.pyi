from osi3 import osi_common_pb2 as _osi_common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class EnvironmentalConditions(_message.Message):
    __slots__ = ("ambient_illumination", "time_of_day", "unix_timestamp", "atmospheric_pressure", "temperature", "relative_humidity", "precipitation", "fog", "source_reference", "clouds", "wind", "sun")
    class Precipitation(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        PRECIPITATION_UNKNOWN: _ClassVar[EnvironmentalConditions.Precipitation]
        PRECIPITATION_OTHER: _ClassVar[EnvironmentalConditions.Precipitation]
        PRECIPITATION_NONE: _ClassVar[EnvironmentalConditions.Precipitation]
        PRECIPITATION_VERY_LIGHT: _ClassVar[EnvironmentalConditions.Precipitation]
        PRECIPITATION_LIGHT: _ClassVar[EnvironmentalConditions.Precipitation]
        PRECIPITATION_MODERATE: _ClassVar[EnvironmentalConditions.Precipitation]
        PRECIPITATION_HEAVY: _ClassVar[EnvironmentalConditions.Precipitation]
        PRECIPITATION_VERY_HEAVY: _ClassVar[EnvironmentalConditions.Precipitation]
        PRECIPITATION_EXTREME: _ClassVar[EnvironmentalConditions.Precipitation]
    PRECIPITATION_UNKNOWN: EnvironmentalConditions.Precipitation
    PRECIPITATION_OTHER: EnvironmentalConditions.Precipitation
    PRECIPITATION_NONE: EnvironmentalConditions.Precipitation
    PRECIPITATION_VERY_LIGHT: EnvironmentalConditions.Precipitation
    PRECIPITATION_LIGHT: EnvironmentalConditions.Precipitation
    PRECIPITATION_MODERATE: EnvironmentalConditions.Precipitation
    PRECIPITATION_HEAVY: EnvironmentalConditions.Precipitation
    PRECIPITATION_VERY_HEAVY: EnvironmentalConditions.Precipitation
    PRECIPITATION_EXTREME: EnvironmentalConditions.Precipitation
    class Fog(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        FOG_UNKNOWN: _ClassVar[EnvironmentalConditions.Fog]
        FOG_OTHER: _ClassVar[EnvironmentalConditions.Fog]
        FOG_EXCELLENT_VISIBILITY: _ClassVar[EnvironmentalConditions.Fog]
        FOG_GOOD_VISIBILITY: _ClassVar[EnvironmentalConditions.Fog]
        FOG_MODERATE_VISIBILITY: _ClassVar[EnvironmentalConditions.Fog]
        FOG_POOR_VISIBILITY: _ClassVar[EnvironmentalConditions.Fog]
        FOG_MIST: _ClassVar[EnvironmentalConditions.Fog]
        FOG_LIGHT: _ClassVar[EnvironmentalConditions.Fog]
        FOG_THICK: _ClassVar[EnvironmentalConditions.Fog]
        FOG_DENSE: _ClassVar[EnvironmentalConditions.Fog]
    FOG_UNKNOWN: EnvironmentalConditions.Fog
    FOG_OTHER: EnvironmentalConditions.Fog
    FOG_EXCELLENT_VISIBILITY: EnvironmentalConditions.Fog
    FOG_GOOD_VISIBILITY: EnvironmentalConditions.Fog
    FOG_MODERATE_VISIBILITY: EnvironmentalConditions.Fog
    FOG_POOR_VISIBILITY: EnvironmentalConditions.Fog
    FOG_MIST: EnvironmentalConditions.Fog
    FOG_LIGHT: EnvironmentalConditions.Fog
    FOG_THICK: EnvironmentalConditions.Fog
    FOG_DENSE: EnvironmentalConditions.Fog
    class AmbientIllumination(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        AMBIENT_ILLUMINATION_UNKNOWN: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_OTHER: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL1: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL2: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL3: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL4: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL5: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL6: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL7: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL8: _ClassVar[EnvironmentalConditions.AmbientIllumination]
        AMBIENT_ILLUMINATION_LEVEL9: _ClassVar[EnvironmentalConditions.AmbientIllumination]
    AMBIENT_ILLUMINATION_UNKNOWN: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_OTHER: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL1: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL2: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL3: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL4: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL5: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL6: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL7: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL8: EnvironmentalConditions.AmbientIllumination
    AMBIENT_ILLUMINATION_LEVEL9: EnvironmentalConditions.AmbientIllumination
    class TimeOfDay(_message.Message):
        __slots__ = ("seconds_since_midnight",)
        SECONDS_SINCE_MIDNIGHT_FIELD_NUMBER: _ClassVar[int]
        seconds_since_midnight: int
        def __init__(self, seconds_since_midnight: _Optional[int] = ...) -> None: ...
    class CloudLayer(_message.Message):
        __slots__ = ("fractional_cloud_cover",)
        class FractionalCloudCover(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
            __slots__ = ()
            FRACTIONAL_CLOUD_COVER_UNKNOWN: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_OTHER: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_ZERO_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_ONE_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_TWO_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_THREE_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_FOUR_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_FIVE_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_SIX_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_SEVEN_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_EIGHT_OKTAS: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
            FRACTIONAL_CLOUD_COVER_SKY_OBSCURED: _ClassVar[EnvironmentalConditions.CloudLayer.FractionalCloudCover]
        FRACTIONAL_CLOUD_COVER_UNKNOWN: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_OTHER: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_ZERO_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_ONE_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_TWO_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_THREE_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_FOUR_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_FIVE_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_SIX_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_SEVEN_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_EIGHT_OKTAS: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_SKY_OBSCURED: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        FRACTIONAL_CLOUD_COVER_FIELD_NUMBER: _ClassVar[int]
        fractional_cloud_cover: EnvironmentalConditions.CloudLayer.FractionalCloudCover
        def __init__(self, fractional_cloud_cover: _Optional[_Union[EnvironmentalConditions.CloudLayer.FractionalCloudCover, str]] = ...) -> None: ...
    class Wind(_message.Message):
        __slots__ = ("origin_direction", "speed")
        ORIGIN_DIRECTION_FIELD_NUMBER: _ClassVar[int]
        SPEED_FIELD_NUMBER: _ClassVar[int]
        origin_direction: float
        speed: float
        def __init__(self, origin_direction: _Optional[float] = ..., speed: _Optional[float] = ...) -> None: ...
    class Sun(_message.Message):
        __slots__ = ("azimuth", "elevation", "intensity")
        AZIMUTH_FIELD_NUMBER: _ClassVar[int]
        ELEVATION_FIELD_NUMBER: _ClassVar[int]
        INTENSITY_FIELD_NUMBER: _ClassVar[int]
        azimuth: float
        elevation: float
        intensity: float
        def __init__(self, azimuth: _Optional[float] = ..., elevation: _Optional[float] = ..., intensity: _Optional[float] = ...) -> None: ...
    AMBIENT_ILLUMINATION_FIELD_NUMBER: _ClassVar[int]
    TIME_OF_DAY_FIELD_NUMBER: _ClassVar[int]
    UNIX_TIMESTAMP_FIELD_NUMBER: _ClassVar[int]
    ATMOSPHERIC_PRESSURE_FIELD_NUMBER: _ClassVar[int]
    TEMPERATURE_FIELD_NUMBER: _ClassVar[int]
    RELATIVE_HUMIDITY_FIELD_NUMBER: _ClassVar[int]
    PRECIPITATION_FIELD_NUMBER: _ClassVar[int]
    FOG_FIELD_NUMBER: _ClassVar[int]
    SOURCE_REFERENCE_FIELD_NUMBER: _ClassVar[int]
    CLOUDS_FIELD_NUMBER: _ClassVar[int]
    WIND_FIELD_NUMBER: _ClassVar[int]
    SUN_FIELD_NUMBER: _ClassVar[int]
    ambient_illumination: EnvironmentalConditions.AmbientIllumination
    time_of_day: EnvironmentalConditions.TimeOfDay
    unix_timestamp: int
    atmospheric_pressure: float
    temperature: float
    relative_humidity: float
    precipitation: EnvironmentalConditions.Precipitation
    fog: EnvironmentalConditions.Fog
    source_reference: _containers.RepeatedCompositeFieldContainer[_osi_common_pb2.ExternalReference]
    clouds: EnvironmentalConditions.CloudLayer
    wind: EnvironmentalConditions.Wind
    sun: EnvironmentalConditions.Sun
    def __init__(self, ambient_illumination: _Optional[_Union[EnvironmentalConditions.AmbientIllumination, str]] = ..., time_of_day: _Optional[_Union[EnvironmentalConditions.TimeOfDay, _Mapping]] = ..., unix_timestamp: _Optional[int] = ..., atmospheric_pressure: _Optional[float] = ..., temperature: _Optional[float] = ..., relative_humidity: _Optional[float] = ..., precipitation: _Optional[_Union[EnvironmentalConditions.Precipitation, str]] = ..., fog: _Optional[_Union[EnvironmentalConditions.Fog, str]] = ..., source_reference: _Optional[_Iterable[_Union[_osi_common_pb2.ExternalReference, _Mapping]]] = ..., clouds: _Optional[_Union[EnvironmentalConditions.CloudLayer, _Mapping]] = ..., wind: _Optional[_Union[EnvironmentalConditions.Wind, _Mapping]] = ..., sun: _Optional[_Union[EnvironmentalConditions.Sun, _Mapping]] = ...) -> None: ...
