/*
 * GT_esminiRMLib - GT Extension for esminiRMLib
 *
 * Implementation of road connection query functions.
 */

#include "gt_esmini/core/GT_esminiRMLib.hpp"
#include "RoadManager.hpp"
#include "LaneIndependentRouter.hpp"
#include "gt_esmini/road/route_lanechange_util.hpp"
#include "gt_esmini/road/OdrSideModel.hpp"
#include "gt_esmini/road/OdrSideExtras.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace roadmanager;

// Helper function to get OpenDrive instance
static OpenDrive* GetODR()
{
    return Position::GetOpenDrive();
}

// --- Route calculation result cache (filled by GT_RM_CalcRoute) ---
static std::vector<GT_RM_RouteWaypoint>     g_routeWaypoints;
static std::vector<gt_esmini::route::LaneChange> g_routeLaneChanges;
static double                               g_routeLength = -1.0;

GT_RM_DLL_API int GT_RM_Init(const char* odrFilename)
{
    if (!odrFilename)
    {
        return -1;
    }

    // Load OpenDRIVE file using Position::LoadOpenDrive
    // This sets the global OpenDrive instance that GetODR() returns
    if (!Position::LoadOpenDrive(odrFilename))
    {
        return -1;
    }

    return 0;
}

GT_RM_DLL_API void GT_RM_Close()
{
    // The OpenDrive instance is managed statically by Position class
    // No explicit cleanup needed, but we could add it if necessary
}
GT_RM_DLL_API int GT_RM_GetRoadSuccessor(uint32_t roadId, GT_RM_RoadLinkInfo* linkInfo)
{
    if (!linkInfo) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1;

    RoadLink* link = road->GetLink(LinkType::SUCCESSOR);
    if (!link) return -2;

    linkInfo->elementId = static_cast<uint32_t>(link->GetElementId());

    switch (link->GetElementType())
    {
        case RoadLink::ElementType::ELEMENT_TYPE_ROAD:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_ROAD;
            break;
        case RoadLink::ElementType::ELEMENT_TYPE_JUNCTION:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_JUNCTION;
            break;
        default:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_UNKNOWN;
            break;
    }

    switch (link->GetContactPointType())
    {
        case ContactPointType::CONTACT_POINT_START:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_START;
            break;
        case ContactPointType::CONTACT_POINT_END:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_END;
            break;
        default:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_UNKNOWN;
            break;
    }

    return 0;
}

GT_RM_DLL_API int GT_RM_GetRoadPredecessor(uint32_t roadId, GT_RM_RoadLinkInfo* linkInfo)
{
    if (!linkInfo) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1;

    RoadLink* link = road->GetLink(LinkType::PREDECESSOR);
    if (!link) return -2;

    linkInfo->elementId = static_cast<uint32_t>(link->GetElementId());

    switch (link->GetElementType())
    {
        case RoadLink::ElementType::ELEMENT_TYPE_ROAD:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_ROAD;
            break;
        case RoadLink::ElementType::ELEMENT_TYPE_JUNCTION:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_JUNCTION;
            break;
        default:
            linkInfo->elementType = GT_RM_ELEMENT_TYPE_UNKNOWN;
            break;
    }

    switch (link->GetContactPointType())
    {
        case ContactPointType::CONTACT_POINT_START:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_START;
            break;
        case ContactPointType::CONTACT_POINT_END:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_END;
            break;
        default:
            linkInfo->contactPoint = GT_RM_CONTACT_POINT_UNKNOWN;
            break;
    }

    return 0;
}

GT_RM_DLL_API int GT_RM_GetJunctionConnectionCount(uint32_t junctionId)
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Junction* junction = odr->GetJunctionById(static_cast<id_t>(junctionId));
    if (!junction) return -1;

    return static_cast<int>(junction->GetConnections().size());
}

GT_RM_DLL_API int GT_RM_GetJunctionConnection(uint32_t junctionId, int index,
                                               GT_RM_JunctionConnection* connection)
{
    if (!connection) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Junction* junction = odr->GetJunctionById(static_cast<id_t>(junctionId));
    if (!junction) return -1;

    const auto& connections = junction->GetConnections();
    if (index < 0 || index >= static_cast<int>(connections.size())) return -2;

    Connection* conn = connections[static_cast<size_t>(index)];
    if (!conn) return -1;

    Road* incomingRoad = conn->GetIncomingRoad();
    Road* connectingRoad = conn->GetConnectingRoad();

    connection->incomingRoadId = incomingRoad ? static_cast<uint32_t>(incomingRoad->GetId()) : 0xFFFFFFFF;
    connection->connectingRoadId = connectingRoad ? static_cast<uint32_t>(connectingRoad->GetId()) : 0xFFFFFFFF;

    switch (conn->GetContactPoint())
    {
        case ContactPointType::CONTACT_POINT_START:
            connection->contactPoint = GT_RM_CONTACT_POINT_START;
            break;
        case ContactPointType::CONTACT_POINT_END:
            connection->contactPoint = GT_RM_CONTACT_POINT_END;
            break;
        default:
            connection->contactPoint = GT_RM_CONTACT_POINT_UNKNOWN;
            break;
    }

    return 0;
}

GT_RM_DLL_API int GT_RM_GetJunctionConnectionsFromRoad(uint32_t junctionId,
                                                        uint32_t incomingRoadId)
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Junction* junction = odr->GetJunctionById(static_cast<id_t>(junctionId));
    if (!junction) return -1;

    return static_cast<int>(junction->GetNoConnectionsFromRoadId(static_cast<id_t>(incomingRoadId)));
}

GT_RM_DLL_API int GT_RM_GetJunctionConnectionFromRoadByIndex(uint32_t junctionId,
                                                              uint32_t incomingRoadId,
                                                              int index,
                                                              uint32_t* connectingRoadId)
{
    if (!connectingRoadId) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Junction* junction = odr->GetJunctionById(static_cast<id_t>(junctionId));
    if (!junction) return -1;

    id_t roadId = junction->GetConnectingRoadIdFromIncomingRoadId(
        static_cast<id_t>(incomingRoadId),
        static_cast<unsigned int>(index)
    );

    if (roadId == 0) return -1;

    *connectingRoadId = static_cast<uint32_t>(roadId);
    return 0;
}

GT_RM_DLL_API int GT_RM_GetNumRoads()
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    return static_cast<int>(odr->GetNumOfRoads());
}

GT_RM_DLL_API uint32_t GT_RM_GetRoadIdByIndex(int index)
{
    OpenDrive* odr = GetODR();
    if (!odr) return 0xFFFFFFFF;

    if (index < 0 || index >= static_cast<int>(odr->GetNumOfRoads()))
        return 0xFFFFFFFF;

    Road* road = odr->GetRoadByIdx(static_cast<idx_t>(index));
    if (!road) return 0xFFFFFFFF;

    return static_cast<uint32_t>(road->GetId());
}

GT_RM_DLL_API double GT_RM_GetRoadLength(uint32_t roadId)
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1.0;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1.0;

    return road->GetLength();
}

GT_RM_DLL_API int GT_RM_GetRoadSignalCount(uint32_t roadId)
{
    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1;

    return static_cast<int>(road->GetNumberOfSignals());
}

GT_RM_DLL_API int GT_RM_GetRoadSignal(uint32_t roadId, int index, GT_RM_RoadSignalInfo* signalInfo)
{
    if (!signalInfo) return -1;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Road* road = odr->GetRoadById(static_cast<id_t>(roadId));
    if (!road) return -1;

    if (index < 0 || index >= static_cast<int>(road->GetNumberOfSignals())) return -2;

    Signal* signal = road->GetSignal(static_cast<idx_t>(index));
    if (!signal) return -1;

    signalInfo->id = signal->GetId();
    signalInfo->s = signal->GetS();
    signalInfo->t = signal->GetT();

    // Calculate world position
    Position pos;
    // We suppress error checking here since some signals might be slightly off-road or on invalid lanes
    // but SetTrackPos usually initializes basic coords anyway.
    pos.SetTrackPos(static_cast<id_t>(roadId), signalInfo->s, signalInfo->t);

    signalInfo->x = pos.GetX();
    signalInfo->y = pos.GetY();
    signalInfo->z = pos.GetZ() + signal->GetZOffset();

    signalInfo->h = pos.GetH() + signal->GetHOffset();
    signalInfo->p = pos.GetP() + signal->GetPitch();
    signalInfo->r = pos.GetR() + signal->GetRoll();

    strncpy(signalInfo->type, signal->GetType().c_str(), 63);
    signalInfo->type[63] = '\0';

    strncpy(signalInfo->subtype, signal->GetSubType().c_str(), 63);
    signalInfo->subtype[63] = '\0';

    strncpy(signalInfo->country, signal->GetCountry().c_str(), 63);
    signalInfo->country[63] = '\0';

    signalInfo->value = signal->GetValue();

    strncpy(signalInfo->unit, signal->GetUnit().c_str(), 63);
    signalInfo->unit[63] = '\0';

    strncpy(signalInfo->text, signal->GetText().c_str(), 127);
    signalInfo->text[127] = '\0';

    signalInfo->isDynamic = signal->IsDynamic();
    signalInfo->height = signal->GetHeight();
    signalInfo->width = signal->GetWidth();

    return 0;
}

// --------------------------------------------------------------------------
// Lane-change-aware route calculation
// --------------------------------------------------------------------------

static Position::RouteStrategy MapRouteStrategy(int strategy)
{
    switch (strategy)
    {
        case GT_RM_ROUTE_FASTEST:
            return Position::RouteStrategy::FASTEST;
        case GT_RM_ROUTE_MIN_INTERSECTIONS:
            return Position::RouteStrategy::MIN_INTERSECTIONS;
        case GT_RM_ROUTE_SHORTEST:
        default:
            return Position::RouteStrategy::SHORTEST;
    }
}

GT_RM_DLL_API int GT_RM_CalcRoute(uint32_t startRoadId, int startLaneId, double startS,
                                  uint32_t targetRoadId, int targetLaneId, double targetS,
                                  int routeStrategy)
{
    g_routeWaypoints.clear();
    g_routeLaneChanges.clear();
    g_routeLength = -1.0;

    OpenDrive* odr = GetODR();
    if (!odr) return -1;

    Position startPos;
    startPos.SetLanePos(static_cast<id_t>(startRoadId), startLaneId, startS, 0.0);

    Position targetPos;
    targetPos.SetLanePos(static_cast<id_t>(targetRoadId), targetLaneId, targetS, 0.0);
    targetPos.SetRouteStrategy(MapRouteStrategy(routeStrategy));

    LaneIndependentRouter      router(odr);
    std::vector<Node>          path = router.CalculatePath(startPos, targetPos);
    if (path.empty())
    {
        return -2;  // no route found
    }

    std::vector<Position> waypoints = router.GetWaypoints(path, startPos, targetPos);

    g_routeWaypoints.reserve(waypoints.size());
    for (const Position& wp : waypoints)
    {
        GT_RM_RouteWaypoint out;
        out.roadId     = static_cast<uint32_t>(wp.GetTrackId());
        out.junctionId = static_cast<uint32_t>(wp.GetJunctionId());
        out.laneId     = wp.GetLaneId();
        out.s          = wp.GetS();
        out.x          = wp.GetX();
        out.y          = wp.GetY();
        out.z          = wp.GetZ();
        out.h          = wp.GetH();
        g_routeWaypoints.push_back(out);
    }

    g_routeLaneChanges = gt_esmini::route::DeriveLaneChanges(path);
    g_routeLength      = path.back().weight;

    return static_cast<int>(g_routeWaypoints.size());
}

GT_RM_DLL_API int GT_RM_GetRouteWaypointCount()
{
    return static_cast<int>(g_routeWaypoints.size());
}

GT_RM_DLL_API int GT_RM_GetRouteWaypoint(int index, GT_RM_RouteWaypoint* wp)
{
    if (!wp) return -1;
    if (index < 0 || index >= static_cast<int>(g_routeWaypoints.size())) return -1;

    *wp = g_routeWaypoints[static_cast<size_t>(index)];
    return 0;
}

GT_RM_DLL_API double GT_RM_GetRouteLength()
{
    return g_routeLength;
}

GT_RM_DLL_API int GT_RM_GetLaneChangeCount()
{
    return static_cast<int>(g_routeLaneChanges.size());
}

GT_RM_DLL_API int GT_RM_GetLaneChange(int index, GT_RM_LaneChange* lc)
{
    if (!lc) return -1;
    if (index < 0 || index >= static_cast<int>(g_routeLaneChanges.size())) return -1;

    const gt_esmini::route::LaneChange& src = g_routeLaneChanges[static_cast<size_t>(index)];
    lc->roadId     = src.roadId;
    lc->s          = src.s;
    lc->fromLaneId = src.fromLaneId;
    lc->toLaneId   = src.toLaneId;
    return 0;
}

// ==========================================================================
// GT ODR side-model metadata JSON accessors (P9a)
//
// A tiny hand-rolled JSON *writer* -- SimpleJson.hpp (gt_esmini/common) is a
// PARSER only, so it cannot serialize here. Everything below stays local to this
// translation unit (anonymous namespace); the public entry points implement the
// uniform two-call buffer protocol documented in GT_esminiRMLib.hpp.
// ==========================================================================
namespace
{
using gt_esmini::odr::OdrSideModel;

// Fetch the side model registered for the currently loaded OpenDrive, or nullptr.
const OdrSideModel* GetSideModelForLoaded()
{
    OpenDrive* odr = GetODR();
    if (!odr) return nullptr;
    return gt_esmini::odr::GetSideModel(static_cast<const void*>(odr));
}

// Minimal JSON builder over a std::string. No pretty-printing; deterministic
// output order (caller-driven). Doubles use snprintf("%.12g") -- compact and
// round-trip-ish for the coordinate/speed magnitudes carried here (NOT a
// bit-exact IEEE754 rendering).
class JsonWriter
{
public:
    std::string& Buffer() { return out_; }

    static void Escape(const std::string& s, std::string& dst)
    {
        dst += '"';
        for (unsigned char c : s)
        {
            switch (c)
            {
                case '"':  dst += "\\\""; break;
                case '\\': dst += "\\\\"; break;
                case '\n': dst += "\\n";  break;
                case '\t': dst += "\\t";  break;
                case '\r': dst += "\\r";  break;
                case '\b': dst += "\\b";  break;
                case '\f': dst += "\\f";  break;
                default:
                    if (c < 0x20)
                    {
                        // Other C0 control chars -> \u00XX (UTF-8 multibyte >= 0x80
                        // passes through verbatim).
                        char esc[8];
                        std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c));
                        dst += esc;
                    }
                    else
                    {
                        dst += static_cast<char>(c);
                    }
                    break;
            }
        }
        dst += '"';
    }

    static std::string Num(double v)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.12g", v);
        return std::string(buf);
    }

    // ---- container framing ----
    void BeginObject() { Sep(); out_ += '{'; PushFresh(); }
    void EndObject()   { out_ += '}'; Pop(); }
    void BeginArray()  { Sep(); out_ += '['; PushFresh(); }
    void EndArray()    { out_ += ']'; Pop(); }

    // ---- object members ----
    void Key(const char* k)
    {
        Sep();
        Escape(k, out_);
        out_ += ':';
        // A key suppresses the leading comma for the value that immediately follows.
        suppress_next_sep_ = true;
    }

    void ValString(const std::string& v) { Sep(); Escape(v, out_); }
    void ValDouble(double v)             { Sep(); out_ += Num(v); }
    void ValInt(long long v)             { Sep(); out_ += std::to_string(v); }
    void ValBool(bool v)                 { Sep(); out_ += (v ? "true" : "false"); }
    void ValNull()                       { Sep(); out_ += "null"; }

    // Convenience: object member = scalar.
    void KV(const char* k, const std::string& v) { Key(k); ValString(v); }
    void KVd(const char* k, double v)            { Key(k); ValDouble(v); }
    void KVi(const char* k, long long v)         { Key(k); ValInt(v); }
    void KVb(const char* k, bool v)              { Key(k); ValBool(v); }

private:
    // Emit a separating comma before the next element UNLESS this is the first
    // element of the current container or we just wrote an object key.
    void Sep()
    {
        if (suppress_next_sep_)
        {
            suppress_next_sep_ = false;
            return;
        }
        if (fresh_.empty())
        {
            return;  // top-level root value: nothing precedes it
        }
        if (fresh_.back())
        {
            fresh_.back() = false;
            return;  // first element of the current container: no leading comma
        }
        out_ += ',';
    }
    void PushFresh() { fresh_.push_back(true); suppress_next_sep_ = false; }
    void Pop()       { if (!fresh_.empty()) fresh_.pop_back(); }

    std::string       out_;
    std::vector<bool> fresh_;
    bool              suppress_next_sep_ = false;
};

// Copy `src` into the caller buffer per the uniform protocol; returns required
// length (bytes, excl. NUL).
int Emit(const std::string& src, char* buffer, int bufferSize)
{
    const int required = static_cast<int>(src.size());
    if (buffer != nullptr && bufferSize > 0)
    {
        const int copy = (required < bufferSize - 1) ? required : (bufferSize - 1);
        if (copy > 0)
        {
            std::memcpy(buffer, src.data(), static_cast<size_t>(copy));
        }
        buffer[copy] = '\0';
    }
    return required;
}

// Serialize one OdrSemantics block as a JSON object (no leading key -- caller
// supplies the "semantics" key before calling).
void WriteSemantics(JsonWriter& w, const gt_esmini::odr::OdrSemantics& sem)
{
    w.BeginObject();

    w.Key("speeds");
    w.BeginArray();
    for (const auto& sp : sem.speeds)
    {
        w.BeginObject();
        w.KV("type", sp.type);
        w.KVd("value", sp.value);
        w.KV("unit", sp.unit);
        w.EndObject();
    }
    w.EndArray();

    w.Key("lane_types");
    w.BeginArray();
    for (const auto& lt : sem.lane_types) w.ValString(lt);
    w.EndArray();

    w.Key("priority_types");
    w.BeginArray();
    for (const auto& pt : sem.priority_types) w.ValString(pt);
    w.EndArray();

    w.Key("prohibited");
    w.BeginArray();
    for (const auto& p : sem.prohibited)
    {
        w.BeginObject();
        w.KV("kind", p.kind);
        w.KV("category", p.category);
        w.EndObject();
    }
    w.EndArray();

    // warning_count is the primary counter the contract names; the other simple
    // presence counters ride along under their natural snake_case names so a
    // consumer sees the full picture of the sparse "free" subtypes.
    w.KVi("warning_count", sem.warning_count);
    w.KVi("routing_count", sem.routing_count);
    w.KVi("streetname_count", sem.streetname_count);
    w.KVi("parking_count", sem.parking_count);
    w.KVi("tourist_count", sem.tourist_count);
    w.KVi("supplementary_explanatory_count", sem.supplementary_explanatory_count);

    w.EndObject();
}

}  // namespace

GT_RM_DLL_API int GT_RM_GetOdrAuditJson(char* buffer, int bufferSize)
{
    const OdrSideModel* m = GetSideModelForLoaded();
    if (!m)
    {
        if (buffer != nullptr && bufferSize > 0) buffer[0] = '\0';
        return -1;
    }

    JsonWriter w;
    w.BeginObject();

    w.Key("version");
    w.BeginObject();
    w.KVi("rev_major", m->rev_major);
    w.KVi("rev_minor", m->rev_minor);
    w.EndObject();

    w.KVi("unsupported_elements", static_cast<long long>(m->audit.unsupported_elements));
    w.KVi("unsupported_attributes", static_cast<long long>(m->audit.unsupported_attributes));
    w.KVi("removed16_hits", static_cast<long long>(m->audit.removed16_hits));

    w.Key("entries");
    w.BeginArray();
    for (const auto& e : m->audit.entries) w.ValString(e);
    w.EndArray();

    w.EndObject();
    return Emit(w.Buffer(), buffer, bufferSize);
}

GT_RM_DLL_API int GT_RM_GetUserDataJson(char* buffer, int bufferSize)
{
    const OdrSideModel* m = GetSideModelForLoaded();
    if (!m)
    {
        if (buffer != nullptr && bufferSize > 0) buffer[0] = '\0';
        return -1;
    }

    JsonWriter w;
    w.BeginObject();

    w.Key("user_data");
    w.BeginArray();
    for (const auto& d : m->user_data)
    {
        w.BeginObject();
        w.KV("owner_path", d.owner_path);
        w.KV("context_id", d.context_id);
        w.KV("xml", d.xml);
        w.EndObject();
    }
    w.EndArray();

    w.Key("data_quality");
    w.BeginArray();
    for (const auto& d : m->data_quality)
    {
        w.BeginObject();
        w.KV("owner_path", d.owner_path);
        w.KV("context_id", d.context_id);
        w.KV("xml", d.xml);
        w.EndObject();
    }
    w.EndArray();

    w.EndObject();
    return Emit(w.Buffer(), buffer, bufferSize);
}

GT_RM_DLL_API int GT_RM_GetSignalSemanticsJson(char* buffer, int bufferSize)
{
    const OdrSideModel* m = GetSideModelForLoaded();
    if (!m)
    {
        if (buffer != nullptr && bufferSize > 0) buffer[0] = '\0';
        return -1;
    }

    JsonWriter w;
    w.BeginObject();

    w.Key("signals");
    w.BeginArray();
    for (const auto& s : m->signal_extras)
    {
        w.BeginObject();
        w.KV("road_id", s.road_id);
        w.KV("signal_id", s.signal_id);
        w.KVb("has_semantics", s.has_semantics);

        w.Key("semantics");
        WriteSemantics(w, s.semantics);

        w.Key("dependencies");
        w.BeginArray();
        for (const auto& d : s.dependencies)
        {
            w.BeginObject();
            w.KV("id", d.id);
            w.KV("type", d.type);
            w.EndObject();
        }
        w.EndArray();

        w.Key("references");
        w.BeginArray();
        for (const auto& r : s.references)
        {
            w.BeginObject();
            w.KV("element_type", r.element_type);
            w.KV("element_id", r.element_id);
            w.KV("type", r.type);
            w.EndObject();
        }
        w.EndArray();

        w.KVb("temporary", s.temporary);
        w.KVb("invalidated", s.invalidated);
        w.EndObject();
    }
    w.EndArray();

    w.EndObject();
    return Emit(w.Buffer(), buffer, bufferSize);
}

GT_RM_DLL_API int GT_RM_GetJunctionPrioritiesJson(char* buffer, int bufferSize)
{
    const OdrSideModel* m = GetSideModelForLoaded();
    if (!m)
    {
        if (buffer != nullptr && bufferSize > 0) buffer[0] = '\0';
        return -1;
    }

    JsonWriter w;
    w.BeginObject();

    w.Key("junctions");
    w.BeginArray();
    for (const auto& j : m->junction_extras)
    {
        if (j.priorities.empty()) continue;  // only junctions WITH priorities
        w.BeginObject();
        w.KV("junction_id", j.junction_id);
        w.KV("type", j.type_str);
        w.Key("priorities");
        w.BeginArray();
        for (const auto& p : j.priorities)
        {
            w.BeginObject();
            w.KV("high", p.high);
            w.KV("low", p.low);
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();

    w.EndObject();
    return Emit(w.Buffer(), buffer, bufferSize);
}

GT_RM_DLL_API int GT_RM_GetCrosswalksJson(char* buffer, int bufferSize)
{
    const OdrSideModel* m = GetSideModelForLoaded();
    if (!m)
    {
        if (buffer != nullptr && bufferSize > 0) buffer[0] = '\0';
        return -1;
    }

    JsonWriter w;
    w.BeginObject();

    w.Key("cross_paths");
    w.BeginArray();
    for (const auto& j : m->junction_extras)
    {
        for (const auto& cp : j.cross_paths)
        {
            w.BeginObject();
            w.KV("junction_id", j.junction_id);
            w.KV("id", cp.id);
            w.KV("crossing_road", cp.crossing_road);
            w.KV("road_at_start", cp.road_at_start);
            w.KV("road_at_end", cp.road_at_end);
            w.KVi("synth_object_id", static_cast<long long>(cp.synth_object_id));
            w.EndObject();
        }
    }
    w.EndArray();

    w.EndObject();
    return Emit(w.Buffer(), buffer, bufferSize);
}

GT_RM_DLL_API int GT_RM_GetRailroadJson(char* buffer, int bufferSize)
{
    const OdrSideModel* m = GetSideModelForLoaded();
    if (!m)
    {
        if (buffer != nullptr && bufferSize > 0) buffer[0] = '\0';
        return -1;
    }

    JsonWriter w;
    w.BeginObject();

    w.Key("switches");
    w.BeginArray();
    for (const auto& sw : m->rail_switches)
    {
        w.BeginObject();
        w.KV("road_id", sw.road_id);
        w.KV("name", sw.name);
        w.KV("id", sw.id);
        w.KV("position", sw.position);

        w.Key("main_track");
        if (sw.has_main_track)
        {
            w.BeginObject();
            w.KV("id", sw.main_track.id);
            w.KVd("s", sw.main_track.s);
            w.KV("dir", sw.main_track.dir);
            w.EndObject();
        }
        else
        {
            w.ValNull();
        }

        w.Key("side_track");
        if (sw.has_side_track)
        {
            w.BeginObject();
            w.KV("id", sw.side_track.id);
            w.KVd("s", sw.side_track.s);
            w.KV("dir", sw.side_track.dir);
            w.EndObject();
        }
        else
        {
            w.ValNull();
        }

        w.Key("partner");
        if (sw.has_partner)
        {
            w.BeginObject();
            w.KV("name", sw.partner_name);
            w.KV("id", sw.partner_id);
            w.EndObject();
        }
        else
        {
            w.ValNull();
        }

        w.EndObject();
    }
    w.EndArray();

    w.Key("stations");
    w.BeginArray();
    for (const auto& st : m->stations)
    {
        w.BeginObject();
        w.KV("id", st.id);
        w.KV("name", st.name);
        w.KV("type", st.type);
        w.Key("platforms");
        w.BeginArray();
        for (const auto& pf : st.platforms)
        {
            w.BeginObject();
            w.KV("id", pf.id);
            w.KV("name", pf.name);
            w.Key("segments");
            w.BeginArray();
            for (const auto& seg : pf.segments)
            {
                w.BeginObject();
                w.KV("road_id", seg.road_id);
                w.KVd("s_start", seg.s_start);
                w.KVd("s_end", seg.s_end);
                w.KV("side", seg.side);
                w.EndObject();
            }
            w.EndArray();
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();

    w.EndObject();
    return Emit(w.Buffer(), buffer, bufferSize);
}
