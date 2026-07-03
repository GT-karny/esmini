// OdrSignalExtras.cpp -- P3 signal placement / cross-reference support (plan clusters 11/12).
//
//   * ResolveSignalPose            -- <positionRoad>/<positionInertial> physical pose override
//                                     + 1.9 s/t-omission backfill/diagnostics ([GT_ODR:sig-pos]).
//   * MaterializeSignalReferences  -- road-level <signalReference> -> Signal/TrafficLight clone
//                                     ([GT_ODR:sig-ref], called post-parse from the fork hook).
//   * CollectSignalExtras          -- <signal>/<dependency> + <signal>/<reference> L1 storage
//                                     (invoked by BuildSideModel).
//
// Compiled INTO the upstream RoadManager static target (R1 swap-zone exception, see
// EnvironmentSimulator/Modules/RoadManager/CMakeLists.txt marker "# [GT_ODR:cmake]").
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P3.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CommonMini.hpp"
#include "RoadManager.hpp"
#include "logger.hpp"  // LOG_WARN/LOG_INFO/LOG_ERROR (fmt-style)
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{

namespace
{

// Off-road guards for the positionInertial reverse mapping (documented in OdrSideModel.hpp and
// gt_roadmanager_patches.md): XYZ2TrackPos always snaps to the CLOSEST road, so "off-road" is
// detected manually: reconstruction error (projection clamped beyond a road end) or an
// implausible lateral offset.
constexpr double kMaxInertialLateralOffset = 30.0;  // [m] |t| beyond this -> not on this road
constexpr double kMaxReconstructionError   = 0.5;   // [m] planar |given - reconstructed| tolerance

roadmanager::Signal::Orientation ParseOrientation(const pugi::xml_node& node, const char* what)
{
    const char* v = node.attribute("orientation").value();
    if (std::strcmp(v, "+") == 0)
    {
        return roadmanager::Signal::POSITIVE;
    }
    if (std::strcmp(v, "-") == 0)
    {
        return roadmanager::Signal::NEGATIVE;
    }
    if (std::strcmp(v, "none") != 0)
    {
        LOG_WARN("[GT_ODR:sig-ref] {}: unknown orientation '{}', treating as 'none'", what, v);
    }
    return roadmanager::Signal::NONE;
}

// Find the first signal (document/parse order over all roads) whose xodr id matches. Returns the
// signal and its owning road; both null when not found.
roadmanager::Signal* FindSignalByXodrId(roadmanager::OpenDrive* odr, int id)
{
    for (unsigned int ri = 0; ri < odr->GetNumOfRoads(); ++ri)
    {
        roadmanager::Road* road = odr->GetRoadByIdx(ri);
        if (road == nullptr)
        {
            continue;
        }
        for (unsigned int si = 0; si < road->GetNumberOfSignals(); ++si)
        {
            roadmanager::Signal* sig = road->GetSignal(si);
            if (sig != nullptr && sig->GetId() == id)
            {
                return sig;
            }
        }
    }
    return nullptr;
}

// ---- P4 helpers (clusters 10/13/14) ----------------------------------------------------------

// Collect participant children (<vehicle>/<person>/<animal>) of a semantic subtype into `out`.
// vehicle/person carry a <type> child (e_vehicleCategory / e_personCategory); animal carries none.
void CollectParticipants(const pugi::xml_node& parent, std::vector<OdrSemanticParticipant>& out)
{
    for (pugi::xml_node p = parent.first_child(); p; p = p.next_sibling())
    {
        if (p.type() != pugi::node_element)
        {
            continue;
        }
        const char* n = p.name();
        if (std::strcmp(n, "vehicle") != 0 && std::strcmp(n, "person") != 0 && std::strcmp(n, "animal") != 0)
        {
            continue;
        }
        OdrSemanticParticipant part;
        part.kind     = n;
        part.category = p.child("type").text().get();  // "" for animal / when absent
        out.push_back(std::move(part));
    }
}

// Normalize a <speed> semantic. Attribute form (1.8.1/1.9): <speed type value unit/>. Draft
// child-element form fallback: <speed><maximum value unit/></speed> -> type=child tag name. Both
// yield the same OdrSemanticSpeed.
OdrSemanticSpeed NormalizeSpeed(const pugi::xml_node& speed_node)
{
    OdrSemanticSpeed s;
    if (!speed_node.attribute("type").empty() || !speed_node.attribute("value").empty() ||
        !speed_node.attribute("unit").empty())
    {
        // Attribute form (canonical).
        s.type      = speed_node.attribute("type").value();
        s.value_str = speed_node.attribute("value").value();
        s.value     = speed_node.attribute("value").as_double(0.0);
        s.unit      = speed_node.attribute("unit").value();
        return s;
    }
    // Child-element form fallback: first element child names the speed type.
    for (pugi::xml_node c = speed_node.first_child(); c; c = c.next_sibling())
    {
        if (c.type() != pugi::node_element)
        {
            continue;
        }
        s.type      = c.name();
        s.value_str = c.attribute("value").value();
        s.value     = c.attribute("value").as_double(0.0);
        s.unit      = c.attribute("unit").value();
        break;
    }
    return s;
}

}  // namespace

namespace detail
{
void ParseSemantics(const pugi::xml_node& sem, OdrSemantics& out)
{
    for (pugi::xml_node e = sem.first_child(); e; e = e.next_sibling())
    {
        if (e.type() != pugi::node_element)
        {
            continue;
        }
        const char* n = e.name();
        if (std::strcmp(n, "speed") == 0)
        {
            out.speeds.push_back(NormalizeSpeed(e));
        }
        else if (std::strcmp(n, "lane") == 0)
        {
            out.lane_types.emplace_back(e.attribute("type").value());
        }
        else if (std::strcmp(n, "priority") == 0)
        {
            out.priority_types.emplace_back(e.attribute("type").value());
        }
        else if (std::strcmp(n, "prohibited") == 0)
        {
            CollectParticipants(e, out.prohibited);
        }
        else if (std::strcmp(n, "warning") == 0)
        {
            out.warning_count++;
        }
        else if (std::strcmp(n, "routing") == 0)
        {
            out.routing_count++;
        }
        else if (std::strcmp(n, "streetname") == 0)
        {
            out.streetname_count++;
        }
        else if (std::strcmp(n, "parking") == 0)
        {
            out.parking_count++;
        }
        else if (std::strcmp(n, "tourist") == 0)
        {
            out.tourist_count++;
        }
        else if (std::strcmp(n, "supplementaryTime") == 0)
        {
            OdrSemanticTime t;
            t.type      = e.attribute("type").value();
            t.value_str = e.attribute("value").value();
            t.value     = e.attribute("value").as_double(0.0);
            out.supplementary_time.push_back(std::move(t));
        }
        else if (std::strcmp(n, "supplementaryAllows") == 0)
        {
            CollectParticipants(e, out.supplementary_allows);
        }
        else if (std::strcmp(n, "supplementaryProhibits") == 0)
        {
            CollectParticipants(e, out.supplementary_prohibits);
        }
        else if (std::strcmp(n, "supplementaryDistance") == 0)
        {
            OdrSemanticDistance d;
            d.type      = e.attribute("type").value();
            d.value_str = e.attribute("value").value();
            d.value     = e.attribute("value").as_double(0.0);
            d.unit      = e.attribute("unit").value();
            out.supplementary_distance.push_back(std::move(d));
        }
        else if (std::strcmp(n, "supplementaryEnvironment") == 0)
        {
            out.supplementary_environment.emplace_back(e.attribute("type").value());
        }
        else if (std::strcmp(n, "supplementaryExplanatory") == 0)
        {
            out.supplementary_explanatory_count++;
        }
        // additionalData (userData/dataQuality) and the XSD _OpenDriveElement placeholder are ignored.
    }
}
}  // namespace detail

namespace
{
// Parse a <staticBoard> into OdrStaticBoard (shared t_road_signals_board base: @width/@height/@zOffset).
OdrStaticBoard ReadStaticBoard(const pugi::xml_node& b)
{
    OdrStaticBoard s;
    s.width    = b.attribute("width").value();
    s.height   = b.attribute("height").value();
    s.z_offset = b.attribute("zOffset").value();
    return s;
}

// Parse a <vmsBoard> (+ <displayArea> children) into OdrVmsBoard.
OdrVmsBoard ReadVmsBoard(const pugi::xml_node& b)
{
    OdrVmsBoard v;
    v.display_type   = b.attribute("displayType").value();
    v.display_width  = b.attribute("displayWidth").value();
    v.display_height = b.attribute("displayHeight").value();
    v.v              = b.attribute("v").value();
    v.z              = b.attribute("z").value();
    v.width          = b.attribute("width").value();   // t_road_signals_board base
    v.height         = b.attribute("height").value();  // t_road_signals_board base
    for (pugi::xml_node da = b.child("displayArea"); da; da = da.next_sibling("displayArea"))
    {
        OdrDisplayArea a;
        a.index  = da.attribute("index").value();
        a.width  = da.attribute("width").value();
        a.height = da.attribute("height").value();
        a.v      = da.attribute("v").value();
        a.z      = da.attribute("z").value();
        v.display_areas.push_back(std::move(a));
    }
    return v;
}

}  // namespace

SignalPoseResolution ResolveSignalPose(const pugi::xml_node&   signal_node,
                                       roadmanager::OpenDrive* odr,
                                       roadmanager::Road*      logical_road,
                                       double&                 sig_s,
                                       double&                 sig_t,
                                       double&                 z_offset,
                                       double&                 h_offset,
                                       double&                 pitch,
                                       double&                 roll)
{
    SignalPoseResolution res;
    res.road = logical_road;
    res.s    = sig_s;
    res.t    = sig_t;

    const bool s_absent = signal_node.attribute("s").empty();
    const bool t_absent = signal_node.attribute("t").empty();

    const pugi::xml_node pos_road     = signal_node.child("positionRoad");
    const pugi::xml_node pos_inertial = signal_node.child("positionInertial");

    if (!pos_road && !pos_inertial)
    {
        if (s_absent || t_absent)
        {
            // 1.9 allows omitting s/t, but only a <positionRoad>/<positionInertial> child can
            // then define the pose -- absent both is an authoring gap (defaults to 0/0).
            LOG_WARN(
                "[GT_ODR:sig-pos] signal id={} (road={}): @s/@t omitted and no "
                "<positionRoad>/<positionInertial> child; defaulting to s={:.2f} t={:.2f}",
                signal_node.attribute("id").value(),
                logical_road != nullptr ? logical_road->GetIdStr() : "?",
                sig_s,
                sig_t);
        }
        return res;
    }

    if (pos_road && pos_inertial)
    {
        LOG_WARN("[GT_ODR:sig-pos] signal id={}: both <positionRoad> and <positionInertial> given; using <positionRoad>",
                 signal_node.attribute("id").value());
    }

    if (pos_road)
    {
        const std::string  road_id_str = pos_road.attribute("roadId").value();
        roadmanager::Road* ref_road    = odr != nullptr ? odr->GetRoadByIdStr(road_id_str) : nullptr;
        if (ref_road == nullptr)
        {
            // Unknown id, or the referenced road appears LATER in the document (parse-time
            // limitation -- signals are parsed per-road, the referenced road may not exist yet).
            LOG_WARN("[GT_ODR:sig-pos] signal id={}: <positionRoad roadId=\"{}\"> not found (unknown id or a road "
                     "declared later in the document); keeping logical pose",
                     signal_node.attribute("id").value(),
                     road_id_str);
            return res;
        }
        double pr_s = pos_road.attribute("s").as_double(0.0);
        double pr_t = pos_road.attribute("t").as_double(0.0);
        if (pr_s < 0.0 || pr_s > ref_road->GetLength())
        {
            LOG_WARN("[GT_ODR:sig-pos] signal id={}: <positionRoad> s={:.2f} outside road {} [0, {:.2f}]; clamping",
                     signal_node.attribute("id").value(),
                     pr_s,
                     road_id_str,
                     ref_road->GetLength());
            pr_s = CLAMP(pr_s, 0.0, ref_road->GetLength());
        }
        res.road = ref_road;
        res.s    = pr_s;
        res.t    = pr_t;

        z_offset = pos_road.attribute("zOffset").as_double(z_offset);
        h_offset = pos_road.attribute("hOffset").as_double(h_offset);
        if (!pos_road.attribute("pitch").empty())
        {
            pitch = pos_road.attribute("pitch").as_double(pitch);
        }
        if (!pos_road.attribute("roll").empty())
        {
            roll = pos_road.attribute("roll").as_double(roll);
        }
    }
    else  // positionInertial
    {
        const double x = pos_inertial.attribute("x").as_double(0.0);
        const double y = pos_inertial.attribute("y").as_double(0.0);
        const double z = pos_inertial.attribute("z").as_double(0.0);
        (void)z;  // planar mapping; z is informational (road elevation defines the resolved z)

        // Reverse mapping via CENTERLINE evaluation (Position::SetTrackPos/Track2XYZ). NOTE:
        // Position::XYZ2TrackPos cannot be used here -- it evaluates lane OSI points, which are
        // built by SetRoadOSI() AFTER the parse (this code runs from the signal parse loop).
        // Coarse sampling + ternary refinement of |p - centerline(s)|^2 per road; t = signed
        // lateral offset at the optimum.
        roadmanager::Road* best_road = nullptr;
        double             best_s = 0.0, best_t = 0.0, best_d2 = 0.0;
        roadmanager::Position eval;
        auto center_d2 = [&eval, x, y](roadmanager::Road* rd, double sv) {
            eval.SetTrackPos(rd->GetId(), sv, 0.0);
            const double dx = eval.GetX() - x, dy = eval.GetY() - y;
            return dx * dx + dy * dy;
        };
        const unsigned int n_roads = odr != nullptr ? odr->GetNumOfRoads() : 0;
        for (unsigned int ri = 0; ri < n_roads; ++ri)
        {
            roadmanager::Road* rd = odr->GetRoadByIdx(ri);
            if (rd == nullptr || rd->GetLength() < SMALL_NUMBER)
            {
                continue;
            }
            const double L    = rd->GetLength();
            const double step = MAX(0.5, L / 400.0);
            double cs = 0.0, cd = center_d2(rd, 0.0);
            for (double sv = step; sv < L + step * 0.5; sv += step)
            {
                const double sc = MIN(sv, L);
                const double d2 = center_d2(rd, sc);
                if (d2 < cd)
                {
                    cd = d2;
                    cs = sc;
                }
            }
            // ternary refinement around the coarse minimum
            double lo = MAX(0.0, cs - step), hi = MIN(L, cs + step);
            for (int it = 0; it < 60 && (hi - lo) > 1e-9; ++it)
            {
                const double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
                if (center_d2(rd, m1) <= center_d2(rd, m2))
                {
                    hi = m2;
                }
                else
                {
                    lo = m1;
                }
            }
            const double s_star = 0.5 * (lo + hi);
            eval.SetTrackPos(rd->GetId(), s_star, 0.0);
            const double h  = eval.GetHRoad();
            const double tv = -(x - eval.GetX()) * std::sin(h) + (y - eval.GetY()) * std::cos(h);
            const double d2 = center_d2(rd, s_star);
            if (best_road == nullptr || d2 < best_d2)
            {
                best_road = rd;
                best_s    = s_star;
                best_t    = tv;
                best_d2   = d2;
            }
        }

        bool ok = best_road != nullptr;
        if (ok)
        {
            // Off-road guards: implausible lateral offset, or the projection clamped beyond a road
            // end (reconstructed point deviates from the given one).
            roadmanager::Position recon(best_road->GetId(), best_s, best_t);
            const double err = std::hypot(recon.GetX() - x, recon.GetY() - y);
            if (std::fabs(best_t) > kMaxInertialLateralOffset || err > kMaxReconstructionError)
            {
                LOG_WARN(
                    "[GT_ODR:sig-pos] signal id={}: <positionInertial> ({:.2f}, {:.2f}, {:.2f}) does not resolve "
                    "onto any road (closest road {} s={:.2f} t={:.2f}, reconstruction error {:.2f} m); "
                    "skipping the override, keeping logical pose",
                    signal_node.attribute("id").value(),
                    x,
                    y,
                    z,
                    best_road->GetIdStr(),
                    best_s,
                    best_t,
                    err);
                ok = false;
            }
        }
        else
        {
            LOG_WARN("[GT_ODR:sig-pos] signal id={}: <positionInertial> reverse mapping failed (no roads); "
                     "skipping the override, keeping logical pose",
                     signal_node.attribute("id").value());
        }

        if (ok)
        {
            res.road = best_road;
            res.s    = best_s;
            res.t    = best_t;
            if (!pos_inertial.attribute("hdg").empty())
            {
                res.has_world_h = true;
                res.world_h     = pos_inertial.attribute("hdg").as_double(0.0);
            }
            if (!pos_inertial.attribute("pitch").empty())
            {
                pitch = pos_inertial.attribute("pitch").as_double(pitch);
            }
            if (!pos_inertial.attribute("roll").empty())
            {
                roll = pos_inertial.attribute("roll").as_double(roll);
            }
        }
    }

    // 1.9 s/t omission: backfill the LOGICAL s/t from the resolved pose when it lies on the
    // signal's own road (a pose on another road cannot define a logical position here).
    if ((s_absent || t_absent) && res.road == logical_road)
    {
        if (s_absent)
        {
            sig_s = res.s;
        }
        if (t_absent)
        {
            sig_t = res.t;
        }
    }
    else if ((s_absent || t_absent) && res.road != logical_road)
    {
        LOG_WARN("[GT_ODR:sig-pos] signal id={}: @s/@t omitted and the physical pose resolves to ANOTHER road ({}); "
                 "logical position stays s={:.2f} t={:.2f}",
                 signal_node.attribute("id").value(),
                 res.road != nullptr ? res.road->GetIdStr() : "?",
                 sig_s,
                 sig_t);
    }

    return res;
}

std::vector<roadmanager::Signal*> MaterializeSignalReferences(const pugi::xml_document& doc, roadmanager::OpenDrive* odr)
{
    std::vector<roadmanager::Signal*> created;
    if (odr == nullptr)
    {
        return created;
    }

    const pugi::xml_node root = doc.child("OpenDRIVE");
    for (pugi::xml_node road_node = root.child("road"); road_node; road_node = road_node.next_sibling("road"))
    {
        const pugi::xml_node signals = road_node.child("signals");
        if (!signals)
        {
            continue;
        }
        roadmanager::Road* road = nullptr;
        for (pugi::xml_node ref = signals.child("signalReference"); ref; ref = ref.next_sibling("signalReference"))
        {
            if (road == nullptr)
            {
                road = odr->GetRoadByIdStr(road_node.attribute("id").value());
                if (road == nullptr)
                {
                    LOG_WARN("[GT_ODR:sig-ref] road '{}' not found in the parsed network; its <signalReference> "
                             "elements are skipped",
                             road_node.attribute("id").value());
                    break;
                }
            }

            const std::string    target_id_str = ref.attribute("id").value();
            const int            target_id     = atoi(target_id_str.c_str());
            roadmanager::Signal* target        = FindSignalByXodrId(odr, target_id);
            if (target == nullptr)
            {
                LOG_WARN("[GT_ODR:sig-ref] road {}: <signalReference id=\"{}\"> does not match any parsed signal; "
                         "skipped",
                         road_node.attribute("id").value(),
                         target_id_str);
                continue;
            }

            const double s = ref.attribute("s").as_double(0.0);
            const double t = ref.attribute("t").as_double(0.0);

            const roadmanager::Signal::Orientation orientation =
                ParseOrientation(ref, ("road " + std::string(road_node.attribute("id").value()) + " signalReference id=" + target_id_str).c_str());

            roadmanager::Position pos(road->GetId(), s, t);
            const double h = pos.GetHRoad() + (orientation == roadmanager::Signal::NEGATIVE ? M_PI : 0.0);

            roadmanager::Signal* clone = nullptr;
            if (target->IsDynamic())
            {
                // Consistent with [GT_ODR:tl-gate]: dynamic => TrafficLight.
                clone = new roadmanager::TrafficLight(s,
                                                      t,
                                                      target->GetId(),
                                                      target->GetName(),
                                                      target->IsDynamic(),
                                                      orientation,
                                                      target->GetZOffset(),
                                                      target->GetCountry(),
                                                      target->GetOSIType(),
                                                      target->GetType(),
                                                      target->GetSubType(),
                                                      target->GetValueStr(),
                                                      target->GetUnit(),
                                                      target->GetHeight(),
                                                      target->GetWidth(),
                                                      target->GetDepth(),
                                                      target->GetText(),
                                                      target->GetHOffset(),
                                                      target->GetPitch(),
                                                      target->GetRoll(),
                                                      pos.GetX(),
                                                      pos.GetY(),
                                                      pos.GetZ(),
                                                      h);
            }
            else
            {
                clone = new roadmanager::Signal(s,
                                                t,
                                                target->GetId(),
                                                target->GetName(),
                                                target->IsDynamic(),
                                                orientation,
                                                target->GetZOffset(),
                                                target->GetCountry(),
                                                target->GetOSIType(),
                                                target->GetType(),
                                                target->GetSubType(),
                                                target->GetValueStr(),
                                                target->GetUnit(),
                                                target->GetHeight(),
                                                target->GetWidth(),
                                                target->GetDepth(),
                                                target->GetText(),
                                                target->GetHOffset(),
                                                target->GetPitch(),
                                                target->GetRoll(),
                                                pos.GetX(),
                                                pos.GetY(),
                                                pos.GetZ(),
                                                h);
            }

            // Validity comes from the REFERENCE, not the referenced signal.
            for (pugi::xml_node validity_node = ref.child("validity"); validity_node;
                 validity_node                = validity_node.next_sibling("validity"))
            {
                roadmanager::ValidityRecord validity;
                validity.fromLane_ = atoi(validity_node.attribute("fromLane").value());
                validity.toLane_   = atoi(validity_node.attribute("toLane").value());
                clone->validity_.push_back(validity);
            }

            clone->SetGlobalId();
            clone->SetAllValidLanes(clone, road);
            road->AddSignal(clone);
            created.push_back(clone);

            LOG_INFO("[GT_ODR:sig-ref] road {}: materialized signalReference -> clone of signal id={} '{}' at "
                     "s={:.2f} t={:.2f} ({})",
                     road_node.attribute("id").value(),
                     target->GetId(),
                     target->GetName(),
                     s,
                     t,
                     target->IsDynamic() ? "TrafficLight" : "Signal");
        }
    }
    return created;
}

namespace detail
{

void CollectSignalExtras(const pugi::xml_node& root, OdrSideModel& model)
{
    for (pugi::xml_node road_node = root.child("road"); road_node; road_node = road_node.next_sibling("road"))
    {
        const pugi::xml_node signals = road_node.child("signals");
        if (!signals)
        {
            continue;
        }
        for (pugi::xml_node sig = signals.child("signal"); sig; sig = sig.next_sibling("signal"))
        {
            OdrSignalExtras extras;
            for (pugi::xml_node dep = sig.child("dependency"); dep; dep = dep.next_sibling("dependency"))
            {
                OdrSignalDependency d;
                d.id   = dep.attribute("id").value();
                d.type = dep.attribute("type").value();
                extras.dependencies.push_back(std::move(d));
            }
            for (pugi::xml_node ref = sig.child("reference"); ref; ref = ref.next_sibling("reference"))
            {
                OdrSignalReferenceLink r;
                r.element_type = ref.attribute("elementType").value();
                r.element_id   = ref.attribute("elementId").value();
                r.type         = ref.attribute("type").value();
                extras.references.push_back(std::move(r));
            }
            // P4 cluster 10: <semantics> (at most one per XSD; parse the block, even when empty).
            if (pugi::xml_node sem = sig.child("semantics"))
            {
                extras.has_semantics = true;
                ParseSemantics(sem, extras.semantics);
            }
            // P4 cluster 13: <staticBoard> / <vmsBoard> children (1.8 + 1.9 same placement on <signal>).
            for (pugi::xml_node sb = sig.child("staticBoard"); sb; sb = sb.next_sibling("staticBoard"))
            {
                extras.static_boards.push_back(ReadStaticBoard(sb));
            }
            for (pugi::xml_node vb = sig.child("vmsBoard"); vb; vb = vb.next_sibling("vmsBoard"))
            {
                extras.vms_boards.push_back(ReadVmsBoard(vb));
            }
            if (!extras.dependencies.empty() || !extras.references.empty() || extras.HasAnyP4())
            {
                extras.road_id   = road_node.attribute("id").value();
                extras.signal_id = sig.attribute("id").value();
                model.signal_extras.push_back(std::move(extras));
            }
        }
    }
}

void CollectHeaderAndGroupExtras(const pugi::xml_node& root, OdrSideModel& model)
{
    // ---- cluster 13: document-level <vmsGroup> (OpenDRIVE root child, 1.9) ----
    for (pugi::xml_node vg = root.child("vmsGroup"); vg; vg = vg.next_sibling("vmsGroup"))
    {
        OdrVmsGroup group;
        group.id = vg.attribute("id").value();
        for (pugi::xml_node br = vg.child("vmsBoardReference"); br; br = br.next_sibling("vmsBoardReference"))
        {
            OdrVmsBoardReference ref;
            ref.signal_id   = br.attribute("signalId").value();
            ref.vms_index   = br.attribute("vmsIndex").value();
            ref.group_index = br.attribute("groupIndex").value();
            group.board_references.push_back(std::move(ref));
        }
        model.vms_groups.push_back(std::move(group));
    }

    // ---- cluster 14: header/license + header/defaultRegulations ----
    const pugi::xml_node header = root.child("header");
    if (!header)
    {
        return;
    }
    if (pugi::xml_node lic = header.child("license"))
    {
        model.has_license      = true;
        model.license.name     = lic.attribute("name").value();
        model.license.spdxid   = lic.attribute("spdxid").value();
        model.license.text     = lic.attribute("text").value();
        model.license.resource = lic.attribute("resource").value();
    }
    if (pugi::xml_node dr = header.child("defaultRegulations"))
    {
        for (pugi::xml_node reg = dr.first_child(); reg; reg = reg.next_sibling())
        {
            if (reg.type() != pugi::node_element)
            {
                continue;
            }
            const bool is_road   = std::strcmp(reg.name(), "roadRegulations") == 0;
            const bool is_signal = std::strcmp(reg.name(), "signalRegulations") == 0;
            if (!is_road && !is_signal)
            {
                continue;  // _OpenDriveElement placeholder / additionalData
            }
            OdrDefaultRegulation entry;
            entry.is_signal = is_signal;
            entry.type      = reg.attribute("type").value();
            if (is_signal)
            {
                entry.subtype = reg.attribute("subtype").value();
            }
            if (pugi::xml_node sem = reg.child("semantics"))
            {
                entry.has_semantics = true;
                ParseSemantics(sem, entry.semantics);
            }
            model.default_regulations.push_back(std::move(entry));
        }
    }
}

}  // namespace detail

// ---- P4 signal-extras lookup accessors -------------------------------------------------------

const OdrSignalExtras* GetSignalExtras(const void* opendrive_key, const std::string& road_id, const std::string& signal_id)
{
    const OdrSideModel* m = GetSideModel(opendrive_key);
    if (m == nullptr)
    {
        return nullptr;
    }
    const OdrSignalExtras* found = nullptr;
    for (const OdrSignalExtras& e : m->signal_extras)
    {
        if (e.road_id == road_id && e.signal_id == signal_id)
        {
            if (found != nullptr)
            {
                // Should not happen (signal ids are unique per road, XSD key k_road_signals_signalId);
                // first match wins, WARN so a malformed asset is visible.
                LOG_WARN("[GT_ODR:sig-p4] duplicate signal extras for road={} signal={}; using the first",
                         road_id,
                         signal_id);
                break;
            }
            found = &e;
        }
    }
    return found;
}

const OdrSignalExtras* GetSignalExtras(const roadmanager::OpenDrive* od, const roadmanager::Signal* sig)
{
    if (od == nullptr || sig == nullptr)
    {
        return nullptr;
    }
    // Find the road that owns this Signal (pointer identity), then use its authored id + the signal's
    // (numeric) id rendered as a string -- the same key CollectSignalExtras stored.
    for (unsigned int ri = 0; ri < od->GetNumOfRoads(); ++ri)
    {
        roadmanager::Road* road = od->GetRoadByIdx(ri);
        if (road == nullptr)
        {
            continue;
        }
        for (unsigned int si = 0; si < road->GetNumberOfSignals(); ++si)
        {
            if (road->GetSignal(si) == sig)
            {
                return GetSignalExtras(static_cast<const void*>(od), road->GetIdStr(), std::to_string(sig->GetId()));
            }
        }
    }
    return nullptr;
}

}  // namespace odr
}  // namespace gt_esmini
