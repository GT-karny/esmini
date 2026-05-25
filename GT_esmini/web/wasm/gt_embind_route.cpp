/*
 * GT_esmini - WASM (embind) binding for lane-change-aware route calculation.
 *
 * Exposes roadmanager::LaneIndependentRouter to JavaScript as `GTRouteJS`,
 * mirroring the GT_esminiRMLib C-API (GT_RM_CalcRoute). Lives under GT_esmini
 * (Clean Core: the upstream esminiJS/embind_rm.cpp is not modified); it is
 * pulled into the esminiJS build via the GT_ROUTE_WASM_SOURCES seam.
 *
 * JS usage (after loading the esmini module):
 *   esmini.RoadManagerJS.loadOpenDrive(xmlString);   // shared OpenDRIVE load
 *   const r = esmini.GTRouteJS.calculateRoute(1,-1,0, 5,-1,20, 0);
 *   //  -> { found, length,
 *   //       waypoints:[{x,y,z,h,road_id,lane_id,s}...],
 *   //       laneChanges:[{road_id,s,from_lane,to_lane}...] }
 */

#include <emscripten/bind.h>
#include <vector>

#include "RoadManager.hpp"
#include "LaneIndependentRouter.hpp"
#include "gt_esmini/road/route_lanechange_util.hpp"

namespace gt_esmini
{
    class GTRouteJS
    {
    public:
        // strategy: 0 = SHORTEST, 1 = FASTEST, 2 = MIN_INTERSECTIONS
        static emscripten::val calculateRoute(int    startRoad,
                                              int    startLane,
                                              double startS,
                                              int    endRoad,
                                              int    endLane,
                                              double endS,
                                              int    strategy)
        {
            emscripten::val result      = emscripten::val::object();
            emscripten::val waypointArr = emscripten::val::array();
            emscripten::val laneChgArr  = emscripten::val::array();
            result.set("found", false);
            result.set("length", -1.0);
            result.set("waypoints", waypointArr);
            result.set("laneChanges", laneChgArr);

            roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
            if (!odr)
            {
                return result;
            }

            roadmanager::Position startPos;
            startPos.SetLanePos(static_cast<id_t>(startRoad), startLane, startS, 0.0);

            roadmanager::Position targetPos;
            targetPos.SetLanePos(static_cast<id_t>(endRoad), endLane, endS, 0.0);
            targetPos.SetRouteStrategy(MapStrategy(strategy));

            roadmanager::LaneIndependentRouter router(odr);
            std::vector<roadmanager::Node>     path = router.CalculatePath(startPos, targetPos);
            if (path.empty())
            {
                return result;  // found stays false
            }

            std::vector<roadmanager::Position> waypoints = router.GetWaypoints(path, startPos, targetPos);
            for (const roadmanager::Position& wp : waypoints)
            {
                emscripten::val pt = emscripten::val::object();
                pt.set("x", wp.GetX());
                pt.set("y", wp.GetY());
                pt.set("z", wp.GetZ());
                pt.set("h", wp.GetH());
                pt.set("road_id", static_cast<int>(wp.GetTrackId()));
                pt.set("lane_id", wp.GetLaneId());
                pt.set("s", wp.GetS());
                waypointArr.call<void>("push", pt);
            }

            std::vector<gt_esmini::route::LaneChange> changes = gt_esmini::route::DeriveLaneChanges(path);
            for (const gt_esmini::route::LaneChange& lc : changes)
            {
                emscripten::val c = emscripten::val::object();
                c.set("road_id", static_cast<int>(lc.roadId));
                c.set("s", lc.s);
                c.set("from_lane", lc.fromLaneId);
                c.set("to_lane", lc.toLaneId);
                laneChgArr.call<void>("push", c);
            }

            result.set("found", true);
            result.set("length", path.back().weight);
            return result;
        }

    private:
        static roadmanager::Position::RouteStrategy MapStrategy(int strategy)
        {
            switch (strategy)
            {
                case 1:
                    return roadmanager::Position::RouteStrategy::FASTEST;
                case 2:
                    return roadmanager::Position::RouteStrategy::MIN_INTERSECTIONS;
                case 0:
                default:
                    return roadmanager::Position::RouteStrategy::SHORTEST;
            }
        }
    };

    EMSCRIPTEN_BINDINGS(GTRoute)
    {
        emscripten::class_<GTRouteJS>("GTRouteJS")
            .class_function("calculateRoute", &GTRouteJS::calculateRoute);
    }

}  // namespace gt_esmini
