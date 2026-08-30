#pragma once

#include "CommonMini.hpp"
#include "RoadManager.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

constexpr double kJunctionSharpTurnRateRadPerMeter = 0.04;
constexpr double kJunctionTurnHeadingThresholdRad  = 0.10;

inline bool IsSharpJunctionConnector(const roadmanager::Road* road,
                                     double turn_rate_threshold = kJunctionSharpTurnRateRadPerMeter)
{
    if (!road || road->GetJunction() == ID_UNDEFINED || road->GetLength() <= 0.1)
    {
        return false;
    }

    roadmanager::Position start;
    roadmanager::Position end;
    start.SetTrackPos(road->GetId(), 0.0, 0.0);
    end.SetTrackPos(road->GetId(), road->GetLength(), 0.0);

    const double heading_delta = std::fabs(GetAngleInIntervalMinusPIPlusPI(end.GetH() - start.GetH()));
    return heading_delta / road->GetLength() >= turn_rate_threshold;
}

inline int TurnDirectionFromHeadingDelta(double heading_delta,
                                         double threshold = kJunctionTurnHeadingThresholdRad)
{
    heading_delta = GetAngleInIntervalMinusPIPlusPI(heading_delta);
    if (heading_delta > threshold) return 1;
    if (heading_delta < -threshold) return -1;
    return 0;
}

// Driver-frame direction of a lane change: +1 = left, -1 = right, 0 = none.
//
// OpenDRIVE lane ids are signed relative to the road's s-direction: a HIGHER id is always further
// to the LEFT when looking along +s. That convention is purely geometric, so it is independent of
// the road's LHT/RHT rule -- only the direction of travel matters. A vehicle running against s sees
// road-left as its own right, hence the flip.
//
// This must NOT be derived from the lateral offset of a preview point in the vehicle body frame:
// during a lane change the ego's own yaw contributes more apparent lateral offset at a multi-second
// preview distance than the lane displacement itself (60 m x 0.07 rad ~ 4 m vs. a 3.5 m lane), so
// the sign inverts mid-manoeuvre and the wrong indicator lights up.
inline int LaneChangeIndicatorDir(int current_lane_id, int target_lane_id, bool travelling_along_s)
{
    if (target_lane_id == current_lane_id)
    {
        return 0;
    }
    const int road_frame_dir = (target_lane_id > current_lane_id) ? 1 : -1;
    return travelling_along_s ? road_frame_dir : -road_frame_dir;
}

// Default forward-scan resolution for RouteLookaheadJunctionTurn, shared with
// ControllerVirtualDriver::DetectJunctionTurn's own "trigger + step" upper bound
// (docs/virtualdriver/design/junction_turn_signal.md section 2-2) so the step size
// is not hard-coded in two places.
constexpr double kJunctionTurnLookaheadStepM = 2.0;

// GetDrivingDirectionRelativeRoad() is documented (RoadManager.hpp:4606) to return
// only +1/-1, but is treated defensively here as if 0 ("undetermined") were
// possible: a route that has not yet reversed is s-increasing far more often than
// not, so +1 is the safer default (design doc junction_turn_signal.md section 2-4).
inline int TravDirFromDrivingDirection(int raw_driving_direction)
{
    return raw_driving_direction < 0 ? -1 : 1;
}

// Pure core of ConnectorTurnDirection (below), decoupled from Position/Road so it
// is directly unit-testable without constructing OpenDRIVE geometry. heading_delta
// is the RAW s=0 -> s=length GetH() difference (end - start, NOT yet wrapped into
// [-pi,+pi], same convention IsSharpJunctionConnector uses before its fabs());
// trav_dir is +1 (s-increasing travel) / -1 (s-decreasing).
inline int ConnectorTurnDirectionFromHeadingDelta(double heading_delta, int trav_dir)
{
    return TurnDirectionFromHeadingDelta(GetAngleInIntervalMinusPIPlusPI(heading_delta) * trav_dir);
}

// Turn direction of a single junction connector road, from its OWN entry/exit
// heading -- independent of connector length or how far the exit arm is
// (docs/virtualdriver/design/junction_turn_signal.md section 2-1). Same technique
// as IsSharpJunctionConnector (:14-29): SetTrackPos at s=0 and s=length, GetH()
// difference -- but WITHOUT fabs(), since the sign is the whole point here.
// trav_dir must be the caller's OWN GetDrivingDirectionRelativeRoad() (or
// TravDirFromDrivingDirection() applied to it): the difference is always taken in
// the road's native s=0->s=length order, so a route that traverses the connector
// s-decreasing needs its sign flipped explicitly. GetDrivingDirection()'s +-180
// correction does NOT do this -- it adds the same constant to both ends, which
// cancels out in the difference (see the design doc for the full argument).
//
// Returns 0 if road is null, not junction-owned, or too short to read a heading
// delta from (<= 0.1 m, mirroring IsSharpJunctionConnector's own floor).
inline int ConnectorTurnDirection(const roadmanager::Road* road, int trav_dir)
{
    if (!road || road->GetJunction() == ID_UNDEFINED || road->GetLength() <= 0.1)
    {
        return 0;
    }

    roadmanager::Position start;
    roadmanager::Position end;
    start.SetTrackPos(road->GetId(), 0.0, 0.0);
    end.SetTrackPos(road->GetId(), road->GetLength(), 0.0);

    return ConnectorTurnDirectionFromHeadingDelta(end.GetH() - start.GetH(), trav_dir);
}

// Result of RouteLookaheadJunctionTurn (docs/virtualdriver/design/junction_turn_signal.md
// section 3-1). Mirrored verbatim into VirtualDriverTelemetry::junction_turn.
struct JunctionTurnLookahead
{
    int    dir           = 0;      // +1 = left, -1 = right, 0 = none
    double dist_to_entry = -1.0;   // [m]; 0.0 while ego is on the connector itself; -1.0 = not detected
    bool   on_connector  = false;  // true while ego itself is on a junction-owned road
};

// Look ahead along the route for the next junction turn. Fixes three defects the
// previous RouteLookaheadJunctionTurnDirection had (design doc sections 1-2):
// (1) direction now comes from the connector's OWN geometry (ConnectorTurnDirection
// above), so it no longer depends on reaching the exit arm -- a connector longer
// than `lookahead` is no longer structurally undetectable; (2) `lookahead` is no
// longer eaten by the connector's length for the same reason; (3) ego ALREADY on a
// junction-owned road is detected up front, before the scan loop, so the signal no
// longer drops to 0 for the whole duration of the turn.
inline JunctionTurnLookahead RouteLookaheadJunctionTurn(const roadmanager::Position& start,
                                                        roadmanager::OpenDrive* odr,
                                                        double lookahead,
                                                        double step = kJunctionTurnLookaheadStepM)
{
    if (!odr) return JunctionTurnLookahead{};

    // Defect 3: ego itself is already on a junction-owned connector. Return its
    // direction directly from the connector's own geometry -- a stateless per-frame
    // check (design doc section 2-3), not a latch, so it holds for the turn's whole
    // duration including a stop inside the junction.
    if (roadmanager::Road* start_road = odr->GetRoadById(start.GetTrackId()))
    {
        if (start_road->GetJunction() != ID_UNDEFINED)
        {
            const int trav_dir = TravDirFromDrivingDirection(start.GetDrivingDirectionRelativeRoad());
            return JunctionTurnLookahead{ConnectorTurnDirection(start_road, trav_dir), 0.0, true};
        }
    }

    roadmanager::Position pos;
    pos.Duplicate(start);
    pos.CopyRoute(start);

    const id_t current_track = start.GetTrackId();
    double traveled = 0.0;

    while (traveled < lookahead)
    {
        // [Issue #31] straight-most (0.0), not the randomizing -1.0 convenience overload:
        // an off-route prediction must not re-roll the connecting road (which would flip the
        // detected junction turn direction frame-to-frame). A valid on-route route still steers.
        const int ret = static_cast<int>(pos.MoveAlongS(step, 0.0, 0.0, true,
                                                        roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
        if (ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_GENERIC)) break;
        traveled += step;

        const id_t track = pos.GetTrackId();
        if (track == current_track) continue;

        roadmanager::Road* road = odr->GetRoadById(track);
        if (!road) continue;

        if (road->GetJunction() == ID_UNDEFINED)
        {
            // First road boundary crossed is NOT a junction connector: nothing to
            // detect within this lookahead (mirrors the previous algorithm's
            // "!passed_junction -> return 0", now reached in one step instead of
            // after continuing on through the connector to its exit arm).
            return JunctionTurnLookahead{};
        }

        // Reached the connector (design doc section 2-1): direction from its own
        // geometry, no need to scan on to the exit arm.
        const int trav_dir = TravDirFromDrivingDirection(pos.GetDrivingDirectionRelativeRoad());
        const int dir       = ConnectorTurnDirection(road, trav_dir);

        // Section 2-4: correct the 2 m-quantized `traveled` for however far this
        // step already carried ego INTO the connector, so dist_to_entry reflects
        // the entry point itself, not wherever the fixed-size step landed.
        const double into_connector = (trav_dir >= 0) ? pos.GetS() : (road->GetLength() - pos.GetS());
        const double dist_to_entry  = std::max(0.0, traveled - into_connector);

        return JunctionTurnLookahead{dir, dist_to_entry, false};
    }
    return JunctionTurnLookahead{};
}

// OBSERVATION-ONLY lookahead for the NEXT junction turn on the route, however many plain road
// boundaries lie in between (docs/virtualdriver/design/vd_intent_layer.md section 7).
//
// WHY THIS IS A SECOND FUNCTION AND NOT A BIGGER `lookahead` ARGUMENT (design section 7-1).
// RouteLookaheadJunctionTurn above answers "is the next road boundary I cross a junction
// connector". It works at 30 m only because at that range the ego is already on a road that
// feeds the junction directly. Called with 300 m it does not look 300 m ahead -- it hits the
// first ordinary road boundary and returns "none". The distance was never the limit; the
// contract was.
//
// WHY NOT WIDEN THAT FUNCTION INSTEAD (design section 7-2). It is what the LEGAL indicator gate
// depends on, including the one-frame lookahead that stopped the signal lighting up AFTER the
// statutory 30 m point had already gone past (junction_turn_signal.md section 2-4; measured
// 29.74 m before the fix). Keeping the observation on its own function makes "the signal gate is
// untouched" a structural fact rather than a promise -- there is no shared code path to get
// wrong.
//
// THIS FUNCTION MUST NEVER FEED AN INDICATOR DECISION. Callers pass its result to telemetry and
// nothing else. The distances it reports (hundreds of metres) have no legal meaning; the
// statutory signal distance is RouteLookaheadJunctionTurn's business.
//
// Do NOT coarsen `step` to save time. The loop only notices a road change via GetTrackId(), so a
// step longer than a short connector steps straight over it and reports the ordinary road on the
// far side -- i.e. "no turn" at precisely the junctions that need one (design section 7-4). 2 m
// is the same resolution the signal path uses.
//
// Identical to RouteLookaheadJunctionTurn in every other respect (the already-on-a-connector
// early return, the dist_to_entry correction for however far the last step carried the ego into
// the connector). The ONE difference is marked below.
inline JunctionTurnLookahead RouteLookaheadNextJunctionTurn(const roadmanager::Position& start,
                                                            roadmanager::OpenDrive* odr,
                                                            double lookahead,
                                                            double step = kJunctionTurnLookaheadStepM)
{
    if (!odr || lookahead <= 0.0 || step <= 0.0) return JunctionTurnLookahead{};

    // Already on a connector: same stateless answer as the signal-side function.
    if (roadmanager::Road* start_road = odr->GetRoadById(start.GetTrackId()))
    {
        if (start_road->GetJunction() != ID_UNDEFINED)
        {
            const int trav_dir = TravDirFromDrivingDirection(start.GetDrivingDirectionRelativeRoad());
            return JunctionTurnLookahead{ConnectorTurnDirection(start_road, trav_dir), 0.0, true};
        }
    }

    roadmanager::Position pos;
    pos.Duplicate(start);
    pos.CopyRoute(start);

    id_t   current_track = start.GetTrackId();
    double traveled      = 0.0;

    while (traveled < lookahead)
    {
        // [Issue #31] straight-most (0.0), not the randomizing -1.0 overload -- same reason as
        // the signal-side scan: an off-route prediction must not re-roll the connecting road.
        const int ret = static_cast<int>(pos.MoveAlongS(step, 0.0, 0.0, true,
                                                        roadmanager::Position::MoveDirectionMode::HEADING_DIRECTION, true));
        if (ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_GENERIC)) break;
        traveled += step;

        const id_t track = pos.GetTrackId();
        if (track == current_track) continue;
        current_track = track;

        roadmanager::Road* road = odr->GetRoadById(track);
        if (!road) continue;

        // *** THE ONE DIFFERENCE *** -- RouteLookaheadJunctionTurn returns an empty result here.
        // This one keeps walking, which is the whole reason the function exists.
        if (road->GetJunction() == ID_UNDEFINED) continue;

        const int    trav_dir       = TravDirFromDrivingDirection(pos.GetDrivingDirectionRelativeRoad());
        const int    dir            = ConnectorTurnDirection(road, trav_dir);
        const double into_connector = (trav_dir >= 0) ? pos.GetS() : (road->GetLength() - pos.GetS());
        const double dist_to_entry  = std::max(0.0, traveled - into_connector);

        return JunctionTurnLookahead{dir, dist_to_entry, false};
    }
    return JunctionTurnLookahead{};
}

}  // namespace gt_esmini
