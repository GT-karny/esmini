/*
 * GT_esmini - Extended esmini with Light Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#ifdef Object
#undef Object
#endif

#include "gt_esmini/scenario/VehicleLightBridge.hpp"
#include "CommonMini.hpp"  // GetRgbMinMaxColor, CLAMP
#include "OSCPrivateAction.hpp"  // scenarioengine::LightStateAction
#include "Storyboard.hpp"        // StoryBoard / Story / Act / Maneuver / Event

#include <cmath>
#include <cstring>

namespace gt_esmini
{
    using UpType = scenarioengine::Object::VehicleLightType;
    using UpMode = scenarioengine::Object::VehicleLightMode;
    using UpStatus = scenarioengine::Object::VehicleLightStatus;

    namespace lightbridge
    {
        UpType ToUpstream(VehicleLightType gt)
        {
            switch (gt)
            {
                case VehicleLightType::DAYTIME_RUNNING_LIGHTS:     return UpType::DAYTIME_RUNNING_LIGHTS;
                case VehicleLightType::LOW_BEAM:                   return UpType::LOW_BEAM;
                case VehicleLightType::HIGH_BEAM:                  return UpType::HIGH_BEAM;
                case VehicleLightType::FOG_LIGHTS:                 return UpType::FOG_LIGHTS;
                case VehicleLightType::FOG_LIGHTS_FRONT:          return UpType::FOG_LIGHTS_FRONT;
                case VehicleLightType::FOG_LIGHTS_REAR:           return UpType::FOG_LIGHTS_REAR;
                case VehicleLightType::BRAKE_LIGHTS:              return UpType::BRAKE_LIGHTS;
                case VehicleLightType::WARNING_LIGHTS:            return UpType::WARNING_LIGHTS;
                case VehicleLightType::INDICATOR_LEFT:            return UpType::INDICATOR_LEFT;
                case VehicleLightType::INDICATOR_RIGHT:           return UpType::INDICATOR_RIGHT;
                case VehicleLightType::REVERSING_LIGHTS:          return UpType::REVERSING_LIGHTS;
                case VehicleLightType::LICENSE_PLATE_ILLUMINATION: return UpType::LICENSE_PLATE_ILLUMINATION;
                case VehicleLightType::SPECIAL_PURPOSE_LIGHTS:    return UpType::SPECIAL_PURPOSE_LIGHTS;
                // NOTE: upstream UpType::TAIL_LIGHTS has no GT counterpart (intentionally unmapped).
                default:                                          return UpType::UNDEFINED;
            }
        }

        bool ToGt(UpType up, VehicleLightType& out)
        {
            switch (up)
            {
                case UpType::DAYTIME_RUNNING_LIGHTS:     out = VehicleLightType::DAYTIME_RUNNING_LIGHTS;     return true;
                case UpType::LOW_BEAM:                   out = VehicleLightType::LOW_BEAM;                   return true;
                case UpType::HIGH_BEAM:                  out = VehicleLightType::HIGH_BEAM;                  return true;
                case UpType::FOG_LIGHTS:                 out = VehicleLightType::FOG_LIGHTS;                 return true;
                case UpType::FOG_LIGHTS_FRONT:           out = VehicleLightType::FOG_LIGHTS_FRONT;           return true;
                case UpType::FOG_LIGHTS_REAR:            out = VehicleLightType::FOG_LIGHTS_REAR;            return true;
                case UpType::BRAKE_LIGHTS:               out = VehicleLightType::BRAKE_LIGHTS;               return true;
                case UpType::WARNING_LIGHTS:             out = VehicleLightType::WARNING_LIGHTS;             return true;
                case UpType::INDICATOR_LEFT:             out = VehicleLightType::INDICATOR_LEFT;             return true;
                case UpType::INDICATOR_RIGHT:            out = VehicleLightType::INDICATOR_RIGHT;            return true;
                case UpType::REVERSING_LIGHTS:           out = VehicleLightType::REVERSING_LIGHTS;           return true;
                case UpType::LICENSE_PLATE_ILLUMINATION: out = VehicleLightType::LICENSE_PLATE_ILLUMINATION; return true;
                case UpType::SPECIAL_PURPOSE_LIGHTS:     out = VehicleLightType::SPECIAL_PURPOSE_LIGHTS;     return true;
                default:                                 return false;  // TAIL_LIGHTS / UNDEFINED / sentinel
            }
        }

        int MappedGtTypeCount()
        {
            int count = 0;
            for (int i = static_cast<int>(VehicleLightType::DAYTIME_RUNNING_LIGHTS);
                 i <= static_cast<int>(VehicleLightType::SPECIAL_PURPOSE_LIGHTS); ++i)
            {
                UpType up = ToUpstream(static_cast<VehicleLightType>(i));
                if (up != UpType::UNDEFINED && up != UpType::VEHICLE_LIGHT_SIZE)
                {
                    count++;
                }
            }
            return count;
        }
    }  // namespace lightbridge

    namespace
    {
        // Fixed base RGB per light type. Mirrors LightStateAction::SetRgbFromTypeEnum
        // (OSCPrivateAction.cpp). Used only to lazily initialize a vehLghtStsList slot
        // that the native action never touched (GT-only lights, headless or pre-action).
        void BaseRgbForType(UpType type, double* arr)
        {
            switch (type)
            {
                case UpType::DAYTIME_RUNNING_LIGHTS:
                case UpType::LOW_BEAM:
                case UpType::HIGH_BEAM:
                case UpType::REVERSING_LIGHTS:
                case UpType::TAIL_LIGHTS:
                case UpType::LICENSE_PLATE_ILLUMINATION:
                    arr[0] = 0.5; arr[1] = 0.5; arr[2] = 0.5; break;
                case UpType::FOG_LIGHTS_FRONT:
                    arr[0] = 0.8; arr[1] = 0.8; arr[2] = 0.8; break;
                case UpType::FOG_LIGHTS_REAR:
                    arr[0] = 0.6; arr[1] = 0.0; arr[2] = 0.0; break;
                case UpType::BRAKE_LIGHTS:
                    arr[0] = 0.5; arr[1] = 0.0; arr[2] = 0.0; break;
                case UpType::INDICATOR_LEFT:
                case UpType::INDICATOR_RIGHT:
                    arr[0] = 0.5; arr[1] = 0.375; arr[2] = 0.0; break;
                case UpType::SPECIAL_PURPOSE_LIGHTS:
                    arr[0] = 0.3; arr[1] = 0.3; arr[2] = 0.5; break;
                default:
                    arr[0] = 0.5; arr[1] = 0.5; arr[2] = 0.5; break;
            }
        }

        // Lazily initialize a single light slot the way native InitializeLights() does,
        // but only if it has never been initialized (type == UNDEFINED). This makes GT
        // writes show emission in the viewer for models with light geodes; for headless
        // / geode-less models maxRgb stays as computed from the base color, which is still
        // fine (emission is recorded; the viewer simply has nothing to light up).
        void EnsureSlotInitialized(UpStatus& light, UpType type)
        {
            if (light.type != UpType::UNDEFINED)
            {
                return;
            }
            light.type        = type;
            light.mode        = UpMode::UNKNOWN;
            light.emission[0] = 0.0;
            light.emission[1] = 0.0;
            light.emission[2] = 0.0;
            BaseRgbForType(type, light.baseRgb);
            GetRgbMinMaxColor(light.baseRgb, light.rgb, light.maxRgb);
        }

        // Replicates LightStateAction::SetVehicleLightState emission math.
        void ComputeEmission(UpStatus& light, double luminousity)
        {
            luminousity = CLAMP(luminousity, 0.0, lightbridge::kMaxIntensityLum);
            for (int i = 0; i < 3; i++)
            {
                double x          = luminousity / lightbridge::kMaxIntensityLum;
                light.emission[i] = light.maxRgb[i] * std::pow(x, lightbridge::kEmissionExponent);
            }
            light.luminousIntensity = luminousity;
        }

        UpMode ToUpMode(LightState::Mode m)
        {
            switch (m)
            {
                case LightState::Mode::ON:       return UpMode::ON;
                case LightState::Mode::FLASHING: return UpMode::FLASHING;
                case LightState::Mode::OFF:      return UpMode::OFF;
                default:                         return UpMode::OFF;
            }
        }

        // Apply a single concrete (non-aggregate) light slot. Returns true if mode or
        // emission changed.
        bool ApplyConcrete(scenarioengine::Object* obj, UpType upType, const LightState& st)
        {
            UpStatus& light = obj->vehLghtStsList[static_cast<size_t>(upType)];
            EnsureSlotInitialized(light, upType);

            // Snapshot prior effective state for change detection.
            const UpMode prevMode = light.mode;
            double       prevEmission[3];
            std::memcpy(prevEmission, light.emission, sizeof(prevEmission));

            const UpMode newMode = ToUpMode(st.mode);
            light.mode           = newMode;

            if (newMode == UpMode::OFF)
            {
                light.emission[0] = 0.0;
                light.emission[1] = 0.0;
                light.emission[2] = 0.0;
                light.luminousIntensity = 0.0;
            }
            else
            {
                // ON or FLASHING: use scenario intensity or the native default if <= 0.
                double lum = (st.luminousIntensity > 0.0) ? st.luminousIntensity
                                                          : lightbridge::kDefaultLuminousIntensity;
                ComputeEmission(light, lum);
            }

            const bool changed = (prevMode != light.mode) ||
                                 (prevEmission[0] != light.emission[0]) ||
                                 (prevEmission[1] != light.emission[1]) ||
                                 (prevEmission[2] != light.emission[2]);
            return changed;
        }
    }  // namespace

    bool ApplyLight(scenarioengine::Object* obj, VehicleLightType type, const LightState& st)
    {
        if (obj == nullptr)
        {
            return false;
        }

        UpType upType = lightbridge::ToUpstream(type);
        if (upType == UpType::UNDEFINED)
        {
            return false;
        }

        bool changed = false;

        if (upType == UpType::FOG_LIGHTS)
        {
            // Aggregate: record mode on the aggregate slot, expand to front+rear (native
            // SetVehicleLights mirror). Aggregate slot keeps mode for read-back; emission
            // lives on the concrete slots.
            UpStatus& agg = obj->vehLghtStsList[static_cast<size_t>(UpType::FOG_LIGHTS)];
            EnsureSlotInitialized(agg, UpType::FOG_LIGHTS);
            if (agg.mode != ToUpMode(st.mode))
            {
                agg.mode = ToUpMode(st.mode);
                changed  = true;
            }
            changed |= ApplyConcrete(obj, UpType::FOG_LIGHTS_FRONT, st);
            changed |= ApplyConcrete(obj, UpType::FOG_LIGHTS_REAR, st);
        }
        else if (upType == UpType::WARNING_LIGHTS)
        {
            UpStatus& agg = obj->vehLghtStsList[static_cast<size_t>(UpType::WARNING_LIGHTS)];
            EnsureSlotInitialized(agg, UpType::WARNING_LIGHTS);
            if (agg.mode != ToUpMode(st.mode))
            {
                agg.mode = ToUpMode(st.mode);
                changed  = true;
            }
            changed |= ApplyConcrete(obj, UpType::INDICATOR_LEFT, st);
            changed |= ApplyConcrete(obj, UpType::INDICATOR_RIGHT, st);
        }
        else
        {
            changed = ApplyConcrete(obj, upType, st);
        }

        if (changed)
        {
            obj->dirty_.SetBits(static_cast<uint64_t>(scenarioengine::Object::DirtyBit::LIGHT_STATE));
        }
        return changed;
    }

    LightState ReadLight(const scenarioengine::Object* obj, VehicleLightType type)
    {
        LightState st;
        st.mode = LightState::Mode::OFF;
        if (obj == nullptr)
        {
            return st;
        }

        UpType upType = lightbridge::ToUpstream(type);
        if (upType == UpType::UNDEFINED)
        {
            return st;
        }

        const UpStatus& light = obj->vehLghtStsList[static_cast<size_t>(upType)];
        switch (light.mode)
        {
            case UpMode::ON:       st.mode = LightState::Mode::ON; break;
            case UpMode::FLASHING: st.mode = LightState::Mode::FLASHING; break;
            case UpMode::OFF:      st.mode = LightState::Mode::OFF; break;
            case UpMode::UNKNOWN:  st.mode = LightState::Mode::OFF; break;  // UNKNOWN -> OFF
            default:               st.mode = LightState::Mode::OFF; break;
        }
        st.luminousIntensity = light.luminousIntensity;
        st.colorR            = light.rgb[0];
        st.colorG            = light.rgb[1];
        st.colorB            = light.rgb[2];
        return st;
    }

    void LightBlinkTicker::Tick(scenarioengine::Object* obj, double simTime, double dt,
                                double onDuration, double offDuration)
    {
        if (obj == nullptr)
        {
            return;
        }

        // Idempotent per identical timestamp (controllers may Tick more than once).
        if (lastSimTime_ >= 0.0 && simTime == lastSimTime_)
        {
            return;
        }
        lastSimTime_ = simTime;

        if (onDuration <= 0.0)
        {
            onDuration = 0.5;
        }
        if (offDuration <= 0.0)
        {
            offDuration = 0.5;
        }

        timer_ += dt;
        const double limit = phase_ ? onDuration : offDuration;
        bool edge          = false;
        if (timer_ >= limit)
        {
            phase_ = !phase_;
            timer_ = 0.0;
            edge   = true;
        }

        // On a phase edge, toggle emission of every GT-FLASHING light. mode stays FLASHING.
        if (!edge)
        {
            return;
        }

        bool anyToggled = false;
        for (size_t i = 0; i < static_cast<size_t>(UpType::VEHICLE_LIGHT_SIZE); ++i)
        {
            const UpType upType = static_cast<UpType>(i);

            // Aggregate slots carry mode only; emission lives on the concrete slots.
            if (upType == UpType::FOG_LIGHTS || upType == UpType::WARNING_LIGHTS)
            {
                continue;
            }

            UpStatus& light = obj->vehLghtStsList[i];
            if (light.mode != UpMode::FLASHING)
            {
                continue;
            }

            // Never fight a native scenario LightStateAction: the native action animates
            // its own FLASHING lights (toggling luminousIntensity in Step), so the GT
            // ticker must only blink lights whose latest writer is GT. Skip slots that are
            // scenario-controlled — and slots with no GT counterpart (TAIL_LIGHTS), which
            // GT can never have written, so any FLASHING there is scenario-driven too.
            VehicleLightType gtType;
            if (!lightbridge::ToGt(upType, gtType) ||
                ScenarioLightRegistry::Instance().IsScenarioControlled(obj, gtType))
            {
                continue;
            }

            if (phase_)
            {
                // emission-on phase: restore from base color at default intensity.
                ComputeEmission(light, (light.luminousIntensity > 0.0)
                                           ? light.luminousIntensity
                                           : lightbridge::kDefaultLuminousIntensity);
            }
            else
            {
                light.emission[0] = 0.0;
                light.emission[1] = 0.0;
                light.emission[2] = 0.0;
            }
            anyToggled = true;
        }

        if (anyToggled)
        {
            obj->dirty_.SetBits(static_cast<uint64_t>(scenarioengine::Object::DirtyBit::LIGHT_STATE));
        }
    }

    // ========================= ScenarioLightRegistry =========================

    ScenarioLightRegistry& ScenarioLightRegistry::Instance()
    {
        static ScenarioLightRegistry instance;
        return instance;
    }

    namespace
    {
        // Expand a native scenario light type into the concrete GT types it controls
        // (mirrors native aggregate expansion), pushing each into `out`.
        void ExpandScenarioGtTypes(UpType up, std::vector<VehicleLightType>& out)
        {
            if (up == UpType::FOG_LIGHTS)
            {
                out.push_back(VehicleLightType::FOG_LIGHTS);
                out.push_back(VehicleLightType::FOG_LIGHTS_FRONT);
                out.push_back(VehicleLightType::FOG_LIGHTS_REAR);
            }
            else if (up == UpType::WARNING_LIGHTS)
            {
                out.push_back(VehicleLightType::WARNING_LIGHTS);
                out.push_back(VehicleLightType::INDICATOR_LEFT);
                out.push_back(VehicleLightType::INDICATOR_RIGHT);
            }
            else
            {
                VehicleLightType gt;
                if (lightbridge::ToGt(up, gt))
                {
                    out.push_back(gt);
                }
            }
        }

        // Recursively collect native LightStateActions from a storyboard element subtree.
        void CollectLightActions(scenarioengine::StoryBoardElement*               elem,
                                 std::vector<scenarioengine::LightStateAction*>&  out)
        {
            if (elem == nullptr)
            {
                return;
            }
            if (auto* lsa = dynamic_cast<scenarioengine::LightStateAction*>(elem))
            {
                out.push_back(lsa);
            }
            auto* children = elem->GetChildren();
            if (children)
            {
                for (auto* child : *children)
                {
                    CollectLightActions(child, out);
                }
            }
        }
    }  // namespace

    void ScenarioLightRegistry::RegisterFromStoryboard(scenarioengine::StoryBoard& storyBoard)
    {
        // Fresh registration pass: drop any stale entries from a previous scenario so a
        // re-init that bypasses GT_Close (and thus AutoLightManager::Close) is consistent.
        Clear();

        std::vector<scenarioengine::LightStateAction*> actions;

        // Init private actions are not part of GetChildren() — scan them explicitly.
        for (auto* pa : storyBoard.init_.private_action_)
        {
            if (auto* lsa = dynamic_cast<scenarioengine::LightStateAction*>(pa))
            {
                actions.push_back(lsa);
            }
        }
        // Story tree (Story -> Act -> ManeuverGroup -> Maneuver -> Event -> Action).
        CollectLightActions(&storyBoard, actions);

        for (auto* lsa : actions)
        {
            if (lsa->object_ == nullptr)
            {
                continue;
            }
            std::vector<VehicleLightType> gtTypes;
            ExpandScenarioGtTypes(lsa->GetVehicleLightType(), gtTypes);
            for (VehicleLightType gt : gtTypes)
            {
                Key k{lsa->object_, static_cast<int>(gt)};
                owners_[k].push_back(lsa);
            }
        }
    }

    bool ScenarioLightRegistry::IsScenarioControlled(const scenarioengine::Object* obj, VehicleLightType type)
    {
        Key k{obj, static_cast<int>(type)};
        if (latched_.count(k))
        {
            return true;  // permanently latched
        }
        auto it = owners_.find(k);
        if (it == owners_.end())
        {
            return false;  // not scenario-owned
        }
        // Latch once any controlling action has left INIT/STANDBY (i.e. started).
        for (auto* lsa : it->second)
        {
            auto state = lsa->GetCurrentState();
            if (state != scenarioengine::StoryBoardElement::State::INIT &&
                state != scenarioengine::StoryBoardElement::State::STANDBY)
            {
                latched_.insert(k);
                return true;
            }
        }
        return false;
    }

    bool ScenarioLightRegistry::IsRegistered(const scenarioengine::Object* obj, VehicleLightType type) const
    {
        Key k{obj, static_cast<int>(type)};
        return owners_.find(k) != owners_.end();
    }

    void ScenarioLightRegistry::Clear()
    {
        owners_.clear();
        latched_.clear();
    }

}  // namespace gt_esmini
