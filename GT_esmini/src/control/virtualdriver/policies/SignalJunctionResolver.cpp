#include "gt_esmini/control/virtualdriver/policies/SignalJunctionResolver.hpp"

#include "RoadManager.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace gt_esmini
{

namespace
{
struct SignalJunctionCache
{
    std::string  filename;
    unsigned int num_roads       = 0;
    unsigned int num_controllers = 0;
    unsigned int num_junctions   = 0;

    std::unordered_map<int, std::uint32_t> signal_junction;  // path (a): Signal::GetId() -> junction id

    // for (c)/(b): Signal carries no back-reference to its Road (RoadManager.hpp), so this
    // is built once alongside signal_junction rather than searched per call.
    std::unordered_map<const roadmanager::Signal*, const roadmanager::Road*> signal_road;
};

// roadmanager::Position::GetOpenDrive() (RoadManager.cpp) is a function-local static
// singleton -- `static OpenDrive od; return &od;` -- so the SAME OpenDrive* is reused
// across every xodr load in a process; LoadOpenDriveFile(..., replace=true) clears and
// refills that one instance rather than allocating a new one. Pointer identity alone
// therefore cannot distinguish "still the same file" from "a different file was just
// loaded into this instance": a cache keyed only on the pointer would keep answering
// with the PREVIOUS scenario's controllers/junctions after a reload -- the "stops in
// the wrong scenario's intersection" failure mode. The map below is still keyed by
// OpenDrive* (so two genuinely distinct instances get independent slots), but every
// lookup also re-checks a cheap content fingerprint (filename + the three element
// counts a reload always changes) and rebuilds on mismatch -- self-healing, no caller
// has to remember to invalidate anything.
std::unordered_map<const roadmanager::OpenDrive*, SignalJunctionCache> g_cache;

bool FingerprintMatches(roadmanager::OpenDrive* odr, const SignalJunctionCache& cache)
{
    return cache.num_roads == odr->GetNumOfRoads() && cache.num_controllers == odr->GetNumberOfControllers() &&
           cache.num_junctions == odr->GetNumOfJunctions() && cache.filename == odr->GetOpenDriveFilename();
}

// Walks OpenDrive's junctions and, for each, its OWN referenced-controller list
// (Junction::GetNumberOfControllers()/GetJunctionControllerByIdx() -- NOT
// OpenDrive::GetControllerByIdx(), see header note).
std::vector<JunctionControllerRefs> CollectJunctionControllerRefs(roadmanager::OpenDrive* odr)
{
    std::vector<JunctionControllerRefs> out;
    const unsigned int                  n = odr->GetNumOfJunctions();
    out.reserve(n);
    for (unsigned int j = 0; j < n; ++j)
    {
        roadmanager::Junction* junction = odr->GetJunctionByIdx(j);
        if (!junction) continue;

        JunctionControllerRefs refs;
        refs.junction_id         = junction->GetId();
        const unsigned int ctl_n = junction->GetNumberOfControllers();
        refs.controller_ids.reserve(ctl_n);
        for (unsigned int k = 0; k < ctl_n; ++k)
        {
            roadmanager::JunctionController* jc = junction->GetJunctionControllerByIdx(k);
            if (jc) refs.controller_ids.push_back(jc->id_);
        }
        out.push_back(std::move(refs));
    }
    return out;
}

// Walks OpenDrive's top-level controller list (OpenDrive::GetControllerByIdx(), the
// file-wide list -- NOT any one junction's subset) and each one's authored <control>
// signal ids.
std::vector<ControllerSignals> CollectControllerSignals(roadmanager::OpenDrive* odr)
{
    std::vector<ControllerSignals> out;
    const unsigned int              n = odr->GetNumberOfControllers();
    out.reserve(n);
    for (unsigned int c = 0; c < n; ++c)
    {
        roadmanager::Controller* ctrl = odr->GetControllerByIdx(c);
        if (!ctrl) continue;

        ControllerSignals cs;
        cs.controller_id         = ctrl->GetId();
        const unsigned int ctl_n = ctrl->GetNumberOfControls();
        cs.signal_ids.reserve(ctl_n);
        for (unsigned int k = 0; k < ctl_n; ++k)
        {
            roadmanager::Control* control = ctrl->GetControl(k);
            if (control) cs.signal_ids.push_back(control->signalId_);
        }
        out.push_back(std::move(cs));
    }
    return out;
}

SignalJunctionCache BuildCache(roadmanager::OpenDrive* odr)
{
    SignalJunctionCache cache;
    cache.filename        = odr->GetOpenDriveFilename();
    cache.num_roads       = odr->GetNumOfRoads();
    cache.num_controllers = odr->GetNumberOfControllers();
    cache.num_junctions   = odr->GetNumOfJunctions();

    // The only linear-search-shaped part (controllers x junctions): done once here,
    // never per ScanSignalsAhead frame.
    cache.signal_junction = ResolveControllerChainJunctions(CollectJunctionControllerRefs(odr), CollectControllerSignals(odr));

    for (unsigned int r = 0; r < cache.num_roads; ++r)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(r);
        if (!road) continue;
        const unsigned int sig_n = road->GetNumberOfSignals();
        for (unsigned int s = 0; s < sig_n; ++s)
        {
            roadmanager::Signal* sig = road->GetSignal(s);
            if (sig) cache.signal_road.emplace(sig, road);
        }
    }

    return cache;
}

SignalJunctionCache& GetOrBuildCache(roadmanager::OpenDrive* odr)
{
    const auto it = g_cache.find(odr);
    if (it != g_cache.end() && FingerprintMatches(odr, it->second))
    {
        return it->second;
    }
    SignalJunctionCache& slot = g_cache[odr];
    slot                      = BuildCache(odr);
    return slot;
}

roadmanager::LinkType ToLinkType(SignalAheadEnd end)
{
    return (end == SignalAheadEnd::SUCCESSOR) ? roadmanager::LinkType::SUCCESSOR : roadmanager::LinkType::PREDECESSOR;
}

SignalOrientation ToSignalOrientation(roadmanager::RoadObject::Orientation orientation)
{
    switch (orientation)
    {
        case roadmanager::RoadObject::Orientation::POSITIVE: return SignalOrientation::POSITIVE;
        case roadmanager::RoadObject::Orientation::NEGATIVE: return SignalOrientation::NEGATIVE;
        case roadmanager::RoadObject::Orientation::NONE:     return SignalOrientation::NONE;
        default:                                             return SignalOrientation::NONE;
    }
}
}  // namespace

std::unordered_map<int, std::uint32_t> ResolveControllerChainJunctions(
    const std::vector<JunctionControllerRefs>& junction_controllers,
    const std::vector<ControllerSignals>&      controller_signals)
{
    std::unordered_map<std::uint32_t, std::uint32_t> controller_junction;  // controller id -> its one junction so far
    std::unordered_set<std::uint32_t>                 ambiguous;          // controller ids seen under >1 distinct junction

    for (const JunctionControllerRefs& refs : junction_controllers)
    {
        for (std::uint32_t controller_id : refs.controller_ids)
        {
            if (ambiguous.count(controller_id) != 0) continue;

            const auto it = controller_junction.find(controller_id);
            if (it == controller_junction.end())
            {
                controller_junction.emplace(controller_id, refs.junction_id);
            }
            else if (it->second != refs.junction_id)
            {
                controller_junction.erase(it);
                ambiguous.insert(controller_id);
            }
        }
    }

    std::unordered_map<int, std::uint32_t> signal_junction;
    for (const ControllerSignals& cs : controller_signals)
    {
        const auto it = controller_junction.find(cs.controller_id);
        if (it == controller_junction.end()) continue;  // unreferenced or ambiguous -> no entries

        for (int signal_id : cs.signal_ids)
        {
            signal_junction[signal_id] = it->second;
        }
    }

    return signal_junction;
}

SignalAheadEnd ResolveAheadLinkEnd(SignalOrientation orientation, double travel_ds_dir)
{
    switch (orientation)
    {
        case SignalOrientation::POSITIVE: return SignalAheadEnd::SUCCESSOR;
        case SignalOrientation::NEGATIVE: return SignalAheadEnd::PREDECESSOR;
        case SignalOrientation::NONE:     break;
    }
    // NONE: no signal-intrinsic answer -- direction decides.
    return (travel_ds_dir >= 0.0) ? SignalAheadEnd::SUCCESSOR : SignalAheadEnd::PREDECESSOR;
}

std::optional<std::uint32_t> ResolveSignalJunction(roadmanager::OpenDrive*    odr,
                                                    const roadmanager::Signal* signal,
                                                    double                     travel_ds_dir,
                                                    SignalJunctionSource*      source)
{
    if (!odr || !signal) return std::nullopt;

    SignalJunctionCache& cache = GetOrBuildCache(odr);

    // (a) controller chain first -- see header comment for why; existence-check the result before trusting it.
    {
        const auto it = cache.signal_junction.find(signal->GetId());
        if (it != cache.signal_junction.end() && odr->GetJunctionById(it->second) != nullptr)
        {
            if (source) *source = SignalJunctionSource::CONTROLLER_CHAIN;
            return it->second;
        }
    }

    const auto road_it = cache.signal_road.find(signal);
    if (road_it == cache.signal_road.end())
    {
        return std::nullopt;  // signal not found on any road of this OpenDrive
    }
    const roadmanager::Road* road = road_it->second;

    // (c) the road ahead of the signal links directly to a junction.
    {
        const SignalAheadEnd         end  = ResolveAheadLinkEnd(ToSignalOrientation(signal->GetOrientation()), travel_ds_dir);
        const roadmanager::RoadLink* link = road->GetLink(ToLinkType(end));
        if (link && link->GetElementType() == roadmanager::RoadLink::ELEMENT_TYPE_JUNCTION)
        {
            if (source) *source = SignalJunctionSource::ROAD_LINK;
            return link->GetElementId();
        }
    }

    // (b) the signal's own road IS a junction connecting road.
    {
        const id_t junction = road->GetJunction();
        if (junction != ID_UNDEFINED)
        {
            if (source) *source = SignalJunctionSource::CONNECTING_ROAD;
            return junction;
        }
    }

    return std::nullopt;
}

}  // namespace gt_esmini
