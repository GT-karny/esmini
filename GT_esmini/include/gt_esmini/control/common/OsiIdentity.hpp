#pragma once

#include "CommonMini.hpp"
#include "Entities.hpp"
#include "RoadManager.hpp"

// ============================================================================
// OSI identity of a scene participant.
//
// WHY THIS EXISTS. esmini carries TWO id spaces and they are not the same
// number:
//
//   Object::GetId()        scenario entity index (0, 1, 2, ...). What xosc,
//                          esmini logs and the VD's own internal bookkeeping
//                          use to name an entity.
//   Object::g_id_          the id OSI publishes as MovingObject.id.value
//                          (OSIReporter.cpp UpdateOSIMovingObject). Allocated
//                          from ONE global counter in CommonMini shared by
//                          entities, lanes, road objects and road marks, which
//                          is exactly why it is unique across the moving and
//                          stationary halves of the GroundTruth.
//
// A consumer correlating VD diagnostics with an OSI stream ("which vehicle is
// the ego yielding to?") needs the SECOND one. Every `gt.*.<x>_osi_id` policy
// diagnostic goes through this header so that choice is made in one place
// rather than re-decided per policy.
//
// R1 (Clean Core): read-only access to upstream members. `g_id_` is a public
// member of scenarioengine::Object and RMObject exposes GetGlobalId(), so this
// needs no fork of EnvironmentSimulator.
//
// CAVEAT for anyone joining recorded runs: the global counter is reset by
// OpenDrive::Clear() on road load, so ids are only comparable WITHIN one run.
// Across an in-process batch (gt_sim_test) the numbering restarts.
// ============================================================================

namespace gt_esmini
{

// Emitted in place of an id when the object is absent or was never assigned a
// global id. A real value is always >= 0, so a consumer tests `>= 0` rather
// than having to detect a missing key — the key is emitted unconditionally so
// that "no partner" stays distinguishable from "policy did not run at all"
// (the same negative-diagnosis rule the surrounding gt.* keys follow).
constexpr int kNoOsiId = -1;

// OSI MovingObject.id of a scenario entity (vehicle, pedestrian, ...).
inline int OsiIdOf(const scenarioengine::Object* obj)
{
    if (obj == nullptr || obj->g_id_ == ID_UNDEFINED) return kNoOsiId;
    return static_cast<int>(obj->g_id_);
}

// OSI StationaryObject.id of an OpenDRIVE road object (crosswalk, pole, ...).
//
// Only the FIRST emitted instance keeps this id: a <repeat> road object gets a
// fresh global id per additional instance (OSIReporter.cpp
// UpdateOSIStationaryObjectODR), while the VD only ever holds the RMObject. So
// this identifies the object, not which repeat instance the ego is stopping
// for.
inline int OsiIdOf(const roadmanager::RMObject* obj)
{
    if (obj == nullptr || obj->GetGlobalId() == ID_UNDEFINED) return kNoOsiId;
    return static_cast<int>(obj->GetGlobalId());
}

}  // namespace gt_esmini
