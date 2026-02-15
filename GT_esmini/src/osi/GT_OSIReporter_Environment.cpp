/*
 * esmini - Environment Simulator Minimalistic
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) partners of Simulation Scenarios
 * https://sites.google.com/view/simulationscenarios
 */

#include "CommonMini.hpp"
#include "OSIReporter.hpp"
#include "GT_OSIReporter_Internals.hpp"

const char *OSIReporter::GetOSISensorDataRaw()
{
    return reinterpret_cast<const char *>(obj_osi_internal.sd);
}

osi3::SensorView *OSIReporter::GetSensorView()
{
    return obj_osi_external.sv;
}

int OSIReporter::SetOSITimeStampExplicit(unsigned long long nanoseconds)
{
    SE_Env::Inst().SetOSITimeStamp(nanoseconds);
    return 0;
}

void OSIReporter::SetStationaryModelReference(std::string model_reference)
{
    stationary_model_reference = model_reference;
}

void OSIReporter::UpdateEnvironment(const OSCEnvironment &environment)
{
    if (environment.IsEnvironment())
    {
        obj_osi_internal.dynamic_gt->clear_environmental_conditions();
        if (environment.IsWeatherSet())
        {
            UpdateEnvironmentWeather(environment);
        }
        if (environment.IsTimeOfDaySet())
        {
            UpdateEnvironmentTimeOfDay(environment);
        }
    }
}

void OSIReporter::UpdateEnvironmentWeather(const OSCEnvironment &environment)
{
    if (environment.IsAtmosphericPressureSet())
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_atmospheric_pressure(environment.GetAtmosphericPressure());
    }
    if (environment.IsTemperatureSet())
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_temperature(environment.GetTemperature());
    }
    if (environment.IsFractionalCloudStateSet())
    {
        UpdateEnvironmentFractionalCloudState(environment);
    }
    if (environment.IsSunSet())
    {
        UpdateEnvironmentSun(environment);
    }
    if (environment.IsFogSet())
    {
        scenarioengine::Fog fog = environment.GetFog();
        UpdateEnvironmentFog(fog.visibility_range);
    }
    if (environment.IsPrecipitationIntensitySet())
    {
        UpdateEnvironmentPrecipitation(environment.GetPrecipitationIntensity());
    }
    if (environment.IsWindSet())
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_wind()->set_origin_direction(environment.GetWind().direction);
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_wind()->set_speed(environment.GetWind().speed);
    }
}

void OSIReporter::UpdateEnvironmentFractionalCloudState(const OSCEnvironment &environment)
{
    if (environment.GetFractionalCloudState() == "zeroOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_ZERO_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "oneOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_ONE_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "twoOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_TWO_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "threeOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_THREE_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "fourOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_FOUR_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "fiveOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_FIVE_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "sixOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_SIX_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "sevenOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_SEVEN_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "eightOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_EIGHT_OKTAS);
    }
    else if (environment.GetFractionalCloudState() == "nineOktas")
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_SKY_OBSCURED);
    }
    else
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_clouds()->set_fractional_cloud_cover(
            osi3::EnvironmentalConditions_CloudLayer_FractionalCloudCover_FRACTIONAL_CLOUD_COVER_OTHER);
    }
}

void OSIReporter::UpdateEnvironmentSun(const OSCEnvironment &environment)
{
    obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_sun()->set_elevation(environment.GetSun().elevation);
    obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_sun()->set_azimuth(environment.GetSun().azimuth);
    if (environment.IsSunIntensitySet())
    {
        double intensity = environment.GetSunIntensity();
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_sun()->set_intensity(intensity);
        if (intensity > 10000)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL9);
        }
        else if (intensity > 1000)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL8);
        }
        else if (intensity > 400)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL7);
        }
        else if (intensity > 20)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL6);
        }
        else if (intensity > 10)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL5);
        }
        else if (intensity > 3)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL4);
        }
        else if (intensity > 1)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL3);
        }
        else if (intensity > 0.01)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL2);
        }
        else if (intensity > 0)
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_LEVEL1);
        }
        else
        {
            obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_ambient_illumination(
                osi3::EnvironmentalConditions_AmbientIllumination_AMBIENT_ILLUMINATION_OTHER);
        }
    }
}

void OSIReporter::UpdateEnvironmentTimeOfDay(const OSCEnvironment &environment)
{
    if (!environment.GetTimeOfDay().animation)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_time_of_day()->set_seconds_since_midnight(
            GetSecondsSinceMidnight(environment.GetTimeOfDay().datetime));

        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_unix_timestamp(
            GetEpochTimeFromString(environment.GetTimeOfDay().datetime));
    }
    else
    {
        auto dyn_gt_timestamp = obj_osi_internal.dynamic_gt->mutable_timestamp()->seconds();
        if (!environment_timestamp_offset_.has_value() && dyn_gt_timestamp > 0)
        {
            environment_timestamp_offset_ = dyn_gt_timestamp;
        }

        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->mutable_time_of_day()->set_seconds_since_midnight(
            GetSecondsSinceMidnight(environment.GetTimeOfDay().datetime) +
            static_cast<uint32_t>(dyn_gt_timestamp - environment_timestamp_offset_.value_or(0)));

        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_unix_timestamp(
            GetEpochTimeFromString(environment.GetTimeOfDay().datetime) + dyn_gt_timestamp -
            environment_timestamp_offset_.value_or(0));  // plus simulation time, nanosec is wrong
    }
}

void OSIReporter::UpdateEnvironmentFog(const double visibility_range)
{
    if (visibility_range > 40000)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_EXCELLENT_VISIBILITY);
    }
    else if (visibility_range > 10000)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_GOOD_VISIBILITY);
    }
    else if (visibility_range > 4000)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_MODERATE_VISIBILITY);
    }
    else if (visibility_range > 2000)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_POOR_VISIBILITY);
    }
    else if (visibility_range > 1000)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_MIST);
    }
    else if (visibility_range > 200)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_LIGHT);
    }
    else if (visibility_range > 50)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_THICK);
    }
    else if (visibility_range >= 0)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_DENSE);
    }
    else
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_fog(osi3::EnvironmentalConditions_Fog_FOG_OTHER);
    }
}

void OSIReporter::UpdateEnvironmentPrecipitation(const double precipitation_intensity)
{
    if (precipitation_intensity > 149)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_precipitation(
            osi3::EnvironmentalConditions_Precipitation_PRECIPITATION_EXTREME);
    }
    else if (precipitation_intensity > 34)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_precipitation(
            osi3::EnvironmentalConditions_Precipitation_PRECIPITATION_VERY_HEAVY);
    }
    else if (precipitation_intensity > 8.1)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_precipitation(
            osi3::EnvironmentalConditions_Precipitation_PRECIPITATION_HEAVY);
    }
    else if (precipitation_intensity > 1.9)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_precipitation(
            osi3::EnvironmentalConditions_Precipitation_PRECIPITATION_MODERATE);
    }
    else if (precipitation_intensity > 0.5)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_precipitation(
            osi3::EnvironmentalConditions_Precipitation_PRECIPITATION_LIGHT);
    }
    else if (precipitation_intensity > 0.1)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_precipitation(
            osi3::EnvironmentalConditions_Precipitation_PRECIPITATION_VERY_LIGHT);
    }
    else if (precipitation_intensity >= 0)
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_precipitation(
            osi3::EnvironmentalConditions_Precipitation_PRECIPITATION_NONE);
    }
    else
    {
        obj_osi_internal.dynamic_gt->mutable_environmental_conditions()->set_precipitation(
            osi3::EnvironmentalConditions_Precipitation_PRECIPITATION_OTHER);
    }
}

osi3::TrafficLight_Classification_Color OSIReporter::LampColorMap(roadmanager::LampColor c)
{
    switch (c)
    {
        case roadmanager::LampColor::COLOR_UNKNOWN:
            return osi3::TrafficLight_Classification_Color_COLOR_UNKNOWN;
        case roadmanager::LampColor::COLOR_OTHER:
            return osi3::TrafficLight_Classification_Color_COLOR_OTHER;
        case roadmanager::LampColor::COLOR_RED:
            return osi3::TrafficLight_Classification_Color_COLOR_RED;
        case roadmanager::LampColor::COLOR_YELLOW:
            return osi3::TrafficLight_Classification_Color_COLOR_YELLOW;
        case roadmanager::LampColor::COLOR_GREEN:
            return osi3::TrafficLight_Classification_Color_COLOR_GREEN;
        case roadmanager::LampColor::COLOR_BLUE:
            return osi3::TrafficLight_Classification_Color_COLOR_BLUE;
        case roadmanager::LampColor::COLOR_WHITE:
            return osi3::TrafficLight_Classification_Color_COLOR_WHITE;
        default:
            return osi3::TrafficLight_Classification_Color_COLOR_UNKNOWN;
    }
}

osi3::TrafficLight_Classification_Icon OSIReporter::LampIconMap(roadmanager::LampIcon i)
{
    switch (i)
    {
        case roadmanager::LampIcon::ICON_UNKNOWN:
            return osi3::TrafficLight_Classification_Icon_ICON_UNKNOWN;
        case roadmanager::LampIcon::ICON_OTHER:
            return osi3::TrafficLight_Classification_Icon_ICON_OTHER;
        case roadmanager::LampIcon::ICON_NONE:
            return osi3::TrafficLight_Classification_Icon_ICON_NONE;
        case roadmanager::LampIcon::ICON_ARROW_STRAIGHT_AHEAD:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_STRAIGHT_AHEAD;
        case roadmanager::LampIcon::ICON_ARROW_LEFT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_LEFT;
        case roadmanager::LampIcon::ICON_ARROW_DIAG_LEFT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_DIAG_LEFT;
        case roadmanager::LampIcon::ICON_ARROW_STRAIGHT_AHEAD_LEFT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_STRAIGHT_AHEAD_LEFT;
        case roadmanager::LampIcon::ICON_ARROW_RIGHT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_RIGHT;
        case roadmanager::LampIcon::ICON_ARROW_DIAG_RIGHT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_DIAG_RIGHT;
        case roadmanager::LampIcon::ICON_ARROW_STRAIGHT_AHEAD_RIGHT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_STRAIGHT_AHEAD_RIGHT;
        case roadmanager::LampIcon::ICON_ARROW_LEFT_RIGHT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_LEFT_RIGHT;
        case roadmanager::LampIcon::ICON_ARROW_DOWN:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_DOWN;
        case roadmanager::LampIcon::ICON_ARROW_DOWN_LEFT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_DOWN_LEFT;
        case roadmanager::LampIcon::ICON_ARROW_DOWN_RIGHT:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_DOWN_RIGHT;
        case roadmanager::LampIcon::ICON_ARROW_CROSS:
            return osi3::TrafficLight_Classification_Icon_ICON_ARROW_CROSS;
        case roadmanager::LampIcon::ICON_PEDESTRIAN:
            return osi3::TrafficLight_Classification_Icon_ICON_PEDESTRIAN;
        case roadmanager::LampIcon::ICON_WALK:
            return osi3::TrafficLight_Classification_Icon_ICON_WALK;
        case roadmanager::LampIcon::ICON_DONT_WALK:
            return osi3::TrafficLight_Classification_Icon_ICON_DONT_WALK;
        case roadmanager::LampIcon::ICON_BICYCLE:
            return osi3::TrafficLight_Classification_Icon_ICON_BICYCLE;
        case roadmanager::LampIcon::ICON_PEDESTRIAN_AND_BICYCLE:
            return osi3::TrafficLight_Classification_Icon_ICON_PEDESTRIAN_AND_BICYCLE;
        case roadmanager::LampIcon::ICON_COUNTDOWN_SECONDS:
            return osi3::TrafficLight_Classification_Icon_ICON_COUNTDOWN_SECONDS;
        case roadmanager::LampIcon::ICON_COUNTDOWN_PERCENT:
            return osi3::TrafficLight_Classification_Icon_ICON_COUNTDOWN_PERCENT;
        case roadmanager::LampIcon::ICON_TRAM:
            return osi3::TrafficLight_Classification_Icon_ICON_TRAM;
        case roadmanager::LampIcon::ICON_BUS:
            return osi3::TrafficLight_Classification_Icon_ICON_BUS;
        case roadmanager::LampIcon::ICON_BUS_AND_TRAM:
            return osi3::TrafficLight_Classification_Icon_ICON_BUS_AND_TRAM;
        default:
            return osi3::TrafficLight_Classification_Icon_ICON_UNKNOWN;
    }
}

osi3::TrafficLight_Classification_Mode OSIReporter::LampModeMap(roadmanager::Signal::LampMode m)
{
    switch (m)
    {
        case roadmanager::Signal::LampMode::MODE_UNKNOWN:
            return osi3::TrafficLight_Classification_Mode_MODE_UNKNOWN;
        case roadmanager::Signal::LampMode::MODE_OTHER:
            return osi3::TrafficLight_Classification_Mode_MODE_OTHER;
        case roadmanager::Signal::LampMode::MODE_OFF:
            return osi3::TrafficLight_Classification_Mode_MODE_OFF;
        case roadmanager::Signal::LampMode::MODE_CONSTANT:
            return osi3::TrafficLight_Classification_Mode_MODE_CONSTANT;
        case roadmanager::Signal::LampMode::MODE_FLASHING:
            return osi3::TrafficLight_Classification_Mode_MODE_FLASHING;
        case roadmanager::Signal::LampMode::MODE_COUNTING:
            return osi3::TrafficLight_Classification_Mode_MODE_COUNTING;
        default:
            return osi3::TrafficLight_Classification_Mode_MODE_UNKNOWN;
    }
}
