#ifdef __EMSCRIPTEN__

/**
 * Embind wrapper for esmini RoadManager.
 * Exposes road/lane/world coordinate conversion via a static RoadManagerJS class.
 * Does NOT require a running scenario — operates on the OpenDRIVE road network only.
 */

#include "RoadManager.hpp"
#include "CommonMini.hpp"

#include <emscripten/bind.h>
#include <string>

namespace esmini
{
    /**
     * Result struct returned from coordinate conversion calls.
     * All fields are populated; return_code indicates success (0) or error (< 0).
     */
    struct RMPositionResult
    {
        double x;
        double y;
        double z;
        double h;
        double p;
        double r;
        int    road_id;
        int    lane_id;
        double s;
        double t;
        double offset;
        int    return_code;  // 0 = OK, negative = error
    };

    /**
     * Static wrapper around roadmanager::Position for WASM.
     * All methods are static — a temporary Position object is created per call.
     */
    class RoadManagerJS
    {
    public:
        /**
         * Load an OpenDRIVE road network from an XML string.
         * Must be called before any coordinate conversion.
         */
        static bool loadOpenDrive(const std::string& xodrXml)
        {
            return roadmanager::Position::LoadOpenDriveFromXMLString(xodrXml.c_str());
        }

        /**
         * Convert lane position (roadId, laneId, s, offset) to world coordinates.
         */
        static RMPositionResult laneToWorld(int roadId, int laneId, double s, double offset)
        {
            RMPositionResult result = {};
            roadmanager::Position pos;
            auto rc = pos.SetLanePos(static_cast<id_t>(roadId), laneId, s, offset);
            fillResult(result, pos, static_cast<int>(rc));
            return result;
        }

        /**
         * Convert world (x, y) to the nearest road/lane coordinates.
         * Uses 2D lookup (z, h, p, r inferred from the road surface).
         */
        static RMPositionResult worldToLane(double x, double y)
        {
            RMPositionResult result = {};
            roadmanager::Position pos;
            int rc = pos.SetInertiaPos(x, y, 0.0, true);
            fillResult(result, pos, rc);
            return result;
        }

        /**
         * Convert track position (roadId, s, t) to world coordinates.
         */
        static RMPositionResult trackToWorld(int roadId, double s, double t)
        {
            RMPositionResult result = {};
            roadmanager::Position pos;
            auto rc = pos.SetTrackPos(static_cast<id_t>(roadId), s, t);
            fillResult(result, pos, static_cast<int>(rc));
            return result;
        }

        /**
         * Get the length of a road by its numeric ID.
         * Returns -1 if the road is not found.
         */
        static double getRoadLength(int roadId)
        {
            roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
            if (!odr) return -1.0;
            roadmanager::Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
            if (!road) return -1.0;
            return road->GetLength();
        }

        /**
         * Get the width of a lane at a given s coordinate.
         * Returns 0 if not found.
         */
        static double getLaneWidth(int roadId, int laneId, double s)
        {
            roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
            if (!odr) return 0.0;
            roadmanager::Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
            if (!road) return 0.0;
            return road->GetLaneWidthByS(s, laneId);
        }

        /**
         * Get the total number of roads in the loaded OpenDRIVE network.
         */
        static int getNumberOfRoads()
        {
            roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
            if (!odr) return 0;
            return static_cast<int>(odr->GetNumOfRoads());
        }

        /**
         * Get the number of lanes on a road at a given s coordinate.
         */
        static int getNumberOfLanes(int roadId, double s)
        {
            roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
            if (!odr) return 0;
            roadmanager::Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
            if (!road) return 0;
            return static_cast<int>(road->GetNumberOfLanes(s));
        }

    private:
        static void fillResult(RMPositionResult& result, const roadmanager::Position& pos, int rc)
        {
            result.x           = pos.GetX();
            result.y           = pos.GetY();
            result.z           = pos.GetZ();
            result.h           = pos.GetH();
            result.p           = pos.GetP();
            result.r           = pos.GetR();
            result.road_id     = static_cast<int>(pos.GetTrackId());
            result.lane_id     = pos.GetLaneId();
            result.s           = pos.GetS();
            result.t           = pos.GetT();
            result.offset      = pos.GetOffset();
            result.return_code = rc;
        }
    };

    EMSCRIPTEN_BINDINGS(RoadManager)
    {
        emscripten::value_object<RMPositionResult>("RMPositionResult")
            .field("x", &RMPositionResult::x)
            .field("y", &RMPositionResult::y)
            .field("z", &RMPositionResult::z)
            .field("h", &RMPositionResult::h)
            .field("p", &RMPositionResult::p)
            .field("r", &RMPositionResult::r)
            .field("road_id", &RMPositionResult::road_id)
            .field("lane_id", &RMPositionResult::lane_id)
            .field("s", &RMPositionResult::s)
            .field("t", &RMPositionResult::t)
            .field("offset", &RMPositionResult::offset)
            .field("return_code", &RMPositionResult::return_code);

        emscripten::class_<RoadManagerJS>("RoadManagerJS")
            .class_function("loadOpenDrive", &RoadManagerJS::loadOpenDrive)
            .class_function("laneToWorld", &RoadManagerJS::laneToWorld)
            .class_function("worldToLane", &RoadManagerJS::worldToLane)
            .class_function("trackToWorld", &RoadManagerJS::trackToWorld)
            .class_function("getRoadLength", &RoadManagerJS::getRoadLength)
            .class_function("getLaneWidth", &RoadManagerJS::getLaneWidth)
            .class_function("getNumberOfRoads", &RoadManagerJS::getNumberOfRoads)
            .class_function("getNumberOfLanes", &RoadManagerJS::getNumberOfLanes);
    }
}  // namespace esmini

#endif
