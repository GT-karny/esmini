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
#include "OSITrafficCommand.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"
#include "gt_esmini/road/OdrSideExtras.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace
{
constexpr const char *kSourceRefTypeOdr = "net.asam.opendrive";

// P4 L3: a catalog-classified sign has a real OSI type (16 GIVE_WAY, 17 STOP, ...).
// A catalog MISS leaves osi_type at the RoadManager "no match" sentinel — the
// protobuf enum's INT_MAX_SENTINEL_DO_NOT_USE_ value, i.e. int32 max (the pb.h in
// this OSI drop does not export it as a named osi3:: constant, so match the raw
// value) — and an explicitly typed-but-mapped-to-unknown sign is 0 (TYPE_UNKNOWN).
// Both mean "the country catalog did not classify this sign", which is the ONLY
// case in which the P4 semantics fallback is allowed to contribute (decision 1).
inline bool OsiTypeIsCatalogUnclassified(int osi_type)
{
    return osi_type == static_cast<int>(osi3::TrafficSign_MainSign_Classification_Type_TYPE_UNKNOWN) ||
           osi_type == static_cast<int>(std::numeric_limits<int32_t>::max());
}

// Map an OpenDRIVE semantics <speed @unit> (m/s|mph|km/h) onto an OSI
// TrafficSignValue_Unit. Anything unexpected -> UNKNOWN (never invents a unit).
inline osi3::TrafficSignValue_Unit SemanticSpeedUnitToOsi(const std::string &unit)
{
    if (unit == "km/h")
        return osi3::TrafficSignValue_Unit_UNIT_KILOMETER_PER_HOUR;
    if (unit == "mph")
        return osi3::TrafficSignValue_Unit_UNIT_MILE_PER_HOUR;
    if (unit == "m/s")
        return osi3::TrafficSignValue_Unit_UNIT_OTHER;  // matches the catalog m/s mapping above
    return osi3::TrafficSignValue_Unit_UNIT_UNKNOWN;
}
}  // namespace

int OSIReporter::UpdateStaticTrafficSignals()
{
    auto opendrive = roadmanager::Position::GetOpenDrive();

    for (unsigned int i = 0; i < opendrive->GetNumOfRoads(); i++)
    {
        roadmanager::Road *road = opendrive->GetRoadByIdx(i);
        for (unsigned int j = 0; j < road->GetNumberOfSignals(); ++j)
        {
            roadmanager::Signal *signal = road->GetSignal(j);

            if (signal)
            {
                // P8: invalidated (1.9) -> excluded from OSI ground truth (see gt_roadmanager_patches.md P8).
                // A cancelled/crossed-out regulation is not a logical traffic sign or light, so a single
                // check here skips BOTH the TrafficLight and traffic_sign branches below. Signals without
                // stored extras (the overwhelming majority) fall through unchanged (byte-identical output).
                const gt_esmini::odr::OdrSignalExtras *inv_sx =
                    gt_esmini::odr::GetSignalExtras(opendrive, signal);
                if (inv_sx != nullptr && inv_sx->invalidated)
                {
                    continue;
                }

                // A TrafficLight with an unsupported type combo has 0 lamps and would emit NO OSI
                // entity at all (AddTrafficLightToGt is per-lamp). Fall back to the traffic_sign
                // branch so the signal stays visible in the ground truth (pre-P3 behavior for
                // signals the relaxed [GT_ODR:tl-gate] now promotes). P4 (signal semantics) will
                // widen the traffic-light type catalog.
                roadmanager::TrafficLight *gt_tl = dynamic_cast<roadmanager::TrafficLight *>(signal);
                if (signal->IsDynamic() && !signal->GetHasOSCAction() && gt_tl != nullptr && gt_tl->GetNrLamps() > 0)
                {
                    AddTrafficLightToGt(obj_osi_internal.static_gt, signal);
                }
                else
                {
                    osi3::TrafficSign *trafficSign = obj_osi_internal.static_gt->add_traffic_sign();
                    trafficSign->mutable_id()->set_value(static_cast<unsigned int>(signal->GetId()));
                    trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value(signal->GetValue());
                    trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_text(signal->GetText());
                    // GT-fix (bug ①): GetOSIType() may hold the protobuf sentinel
                    // (TrafficSign_MainSign_Classification_Type_INT_MAX_SENTINEL_DO_NOT_USE_, left over
                    // when GT_RoadManager parses an OSI type string it doesn't recognize) or any other
                    // out-of-range int. Passing that straight into set_type() trips the generated enum
                    // IsValid() assertion (fatal under the OSI 3.7.0 debug protobuf runtime). Clamp any
                    // value the schema doesn't define to TYPE_OTHER so the sign still surfaces in the
                    // ground truth instead of aborting the reporter.
                    const int osi_type_raw = signal->GetOSIType();
                    trafficSign->mutable_main_sign()->mutable_classification()->set_type(
                        osi3::TrafficSign_MainSign_Classification_Type_IsValid(osi_type_raw)
                            ? static_cast<osi3::TrafficSign_MainSign_Classification_Type>(osi_type_raw)
                            : osi3::TrafficSign_MainSign_Classification_Type_TYPE_OTHER);
                    trafficSign->mutable_main_sign()->mutable_classification()->set_country(signal->GetCountry());

                    if (std::strcmp(signal->GetUnit().c_str(), "") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_NO_UNIT);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "m") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_METER);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "km") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_KILOMETER);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "ft") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_FEET);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "mile") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_MILE);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "m/s") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_OTHER);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "mph") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_MILE_PER_HOUR);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "km/h") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_KILOMETER_PER_HOUR);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "kg") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_UNKNOWN);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "t") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_METRIC_TON);
                    }
                    else if (std::strcmp(signal->GetUnit().c_str(), "%") == 0)
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_PERCENTAGE);
                    }
                    else
                    {
                        trafficSign->mutable_main_sign()->mutable_classification()->mutable_value()->set_value_unit(
                            osi3::TrafficSignValue_Unit::TrafficSignValue_Unit_UNIT_UNKNOWN);
                    }

                    trafficSign->mutable_main_sign()->mutable_base()->mutable_orientation()->set_pitch(
                        GetAngleInIntervalMinusPIPlusPI(signal->GetPitch()));
                    trafficSign->mutable_main_sign()->mutable_base()->mutable_orientation()->set_roll(
                        GetAngleInIntervalMinusPIPlusPI(signal->GetRoll()));
                    trafficSign->mutable_main_sign()->mutable_base()->mutable_orientation()->set_yaw(GetAngleInIntervalMinusPIPlusPI(
                        signal->GetH() + signal->GetHOffset() + M_PI));
                    trafficSign->mutable_main_sign()->mutable_base()->mutable_dimension()->set_height(signal->GetHeight());
                    trafficSign->mutable_main_sign()->mutable_base()->mutable_dimension()->set_width(signal->GetWidth());

                    trafficSign->mutable_main_sign()->mutable_base()->mutable_position()->set_x(signal->GetX());
                    trafficSign->mutable_main_sign()->mutable_base()->mutable_position()->set_y(signal->GetY());
                    trafficSign->mutable_main_sign()->mutable_base()->mutable_position()->set_z(signal->GetZ() + signal->GetZOffset() +
                                                                                                signal->GetHeight() / 2.0);

                    // ── P4 L3: conservative TrafficSign enrichment from signal <semantics> ──
                    // Catalog-FIRST (decision 1): only signals the country catalog left
                    // UNCLASSIFIED may take a semantics-derived TYPE; the value/unit is only
                    // filled when the sign carries none of its own. Any sign WITHOUT stored
                    // extras (the overwhelming majority) skips this block entirely and
                    // serializes byte-identically to the pre-P4 output.
                    const gt_esmini::odr::OdrSignalExtras *sx =
                        gt_esmini::odr::GetSignalExtras(opendrive, signal);
                    if (sx != nullptr && sx->has_semantics)
                    {
                        auto *cls = trafficSign->mutable_main_sign()->mutable_classification();

                        // (a) STOP / GIVE_WAY from <priority @type>, ONLY on a catalog miss.
                        if (OsiTypeIsCatalogUnclassified(signal->GetOSIType()))
                        {
                            for (const std::string &pt : sx->semantics.priority_types)
                            {
                                if (pt == "stop" || pt == "stopLine")
                                {
                                    cls->set_type(osi3::TrafficSign_MainSign_Classification_Type_TYPE_STOP);
                                    break;
                                }
                                if (pt == "yield")
                                {
                                    cls->set_type(osi3::TrafficSign_MainSign_Classification_Type_TYPE_GIVE_WAY);
                                    break;
                                }
                            }
                        }

                        // (b) semantic speed -> value+unit, ONLY when the sign has no value of
                        // its own (unset @value parses to value_ == 0.0 with an empty @unit).
                        // No TYPE is set: SPEED_LIMIT_BEGIN semantics need zone state (deferred,
                        // decision 3), so a speculative type mapping is avoided.
                        const bool value_unset = std::strcmp(signal->GetUnit().c_str(), "") == 0 &&
                                                 fabs(signal->GetValue()) < SMALL_NUMBER;
                        if (value_unset && !sx->semantics.speeds.empty())
                        {
                            const gt_esmini::odr::OdrSemanticSpeed &sp = sx->semantics.speeds.front();
                            if (!sp.value_str.empty())
                            {
                                cls->mutable_value()->set_value(sp.value);
                                cls->mutable_value()->set_value_unit(SemanticSpeedUnitToOsi(sp.unit));
                            }
                        }
                    }

                    auto source_reference = trafficSign->add_source_reference();
                    source_reference->set_type(kSourceRefTypeOdr);
                    source_reference->add_identifier(fmt::format("traffic_sign_id:{}", signal->GetId()));

                    if (!signal->GetModel3DFullPath().empty())
                    {
                        trafficSign->mutable_main_sign()->set_model_reference(signal->GetModel3DFullPath());
                    }
                }
            }
        }
    }
    return 0;
}

int OSIReporter::UpdateDynamicTrafficSignals()
{
    obj_osi_internal.dynamic_gt->clear_traffic_light();

    for (auto signal : roadmanager::Position::GetOpenDrive()->GetDynamicSignals())
    {
        if (signal == nullptr || !signal->GetHasOSCAction())
        {
            continue;
        }

        AddTrafficLightToGt(obj_osi_internal.dynamic_gt, signal);
    }

    return 0;
}

void OSIReporter::AddTrafficLightToGt(osi3::GroundTruth *gt, roadmanager::Signal *signal)
{
    roadmanager::TrafficLight *tl = dynamic_cast<roadmanager::TrafficLight *>(signal);
    if (tl == nullptr)
    {
        return;
    }

    for (size_t i = 0; i < tl->GetNrLamps(); i++)
    {
        osi3::TrafficLight *trafficLight = gt->add_traffic_light();
        auto                lamp         = tl->GetLamp(i);

        trafficLight->mutable_id()->set_value(lamp->GetId());

        trafficLight->mutable_base()->mutable_orientation()->set_pitch(GetAngleInIntervalMinusPIPlusPI(signal->GetPitch()));
        trafficLight->mutable_base()->mutable_orientation()->set_roll(GetAngleInIntervalMinusPIPlusPI(signal->GetRoll()));
        trafficLight->mutable_base()->mutable_orientation()->set_yaw(GetAngleInIntervalMinusPIPlusPI(
            signal->GetH() + signal->GetHOffset() + M_PI));

        trafficLight->mutable_base()->mutable_dimension()->set_height(lamp->GetHeight());
        trafficLight->mutable_base()->mutable_dimension()->set_width(lamp->GetWidth());

        trafficLight->mutable_base()->mutable_position()->set_x(lamp->GetX());
        trafficLight->mutable_base()->mutable_position()->set_y(lamp->GetY());
        trafficLight->mutable_base()->mutable_position()->set_z(lamp->GetZ());

        trafficLight->mutable_classification()->set_mode(LampModeMap(lamp->GetMode()));
        trafficLight->mutable_classification()->set_color(LampColorMap(lamp->GetColor()));
        trafficLight->mutable_classification()->set_icon(LampIconMap(lamp->GetIcon()));
        trafficLight->mutable_classification()->set_is_out_of_service(lamp->IsBroken());

        for (const auto &g_lane_id : tl->GetAllValidGlobalLanes())
        {
            trafficLight->mutable_classification()->add_assigned_lane_id()->set_value(g_lane_id);
        }

        auto source_reference = trafficLight->add_source_reference();
        source_reference->set_type(kSourceRefTypeOdr);
        source_reference->add_identifier(fmt::format("traffic_light_id:{}", signal->GetId()));

        if (!signal->GetModel3DFullPath().empty())
        {
            trafficLight->set_model_reference(signal->GetModel3DFullPath());
        }
    }
}

int OSIReporter::UpdateOSITrafficCommand()
{
    obj_osi_external.tc->Clear();

    if (GetUDPClientStatus() == 0 || IsFileOpen())
    {
        obj_osi_external.tc->SerializeToString(&osiTrafficCommand.traffic_command);
        osiTrafficCommand.size = static_cast<unsigned int>(obj_osi_external.tc->ByteSizeLong());
    }

    for (auto state_change : traffic_command_state_changes_)
    {
        if (state_change.transition == StoryBoardElement::Transition::START_TRANSITION && state_change.state == StoryBoardElement::State::RUNNING)
        {
            ReportTrafficCommand(obj_osi_external.tc, state_change.action, scenario_engine_->getSimulationTime());
        }
    }

    traffic_command_state_changes_.clear();
    return 0;
}
