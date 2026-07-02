// OdrCoverageAudit.cpp -- machine-verifiable no-silent-drop coverage audit (plan §3.3) + the
// cluster-21 removed-in-1.6 table + additionalData (userData/dataQuality/include) handling.
//
// Plan: GT_esmini/docs/opendrive_16_19_support_plan.md P1.
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "CommonMini.hpp"
#include "logger.hpp"  // LOG_WARN/LOG_INFO/LOG_ERROR (fmt-style)
#include "odr_side_internal.hpp"
#include "pugixml.hpp"

namespace gt_esmini
{
namespace odr
{
namespace detail
{

namespace
{

// Row type consumed by the generated table.
struct GtOdrWhitelistRow
{
    const char* path;
    const char* attrs_csv;
};

// The generated (path -> attrs_csv) whitelist. Sorted by path (byte order). See
// gen_odr_whitelist.py / parser_coverage.yaml.
#include "OdrCoverageWhitelist.inc"

// ---- cluster 21: elements REMOVED in OpenDRIVE 1.6 ----
//
// Derived by diffing resources/schema/OpenDRIVE_1.5.xsd against resources/schema/OpenDRIVE_1.6/*.xsd
// (verified 2026-07-02, only entries confirmed in the XSDs):
//
//   road/link/neighbor  -- 1.5: t_road_link contains <xs:element name="neighbor"
//                          type="t_road_link_neighbor" minOccurs="0" maxOccurs="2"/> (line 273) with
//                          a full t_road_link_neighbor complexType (side/elementId/direction, 301-306).
//                          1.6: t_road_link (opendrive_16_road.xsd:289) NO LONGER declares a neighbor
//                          element; there is NO t_road_link_neighbor complexType and NO
//                          name="neighbor" element anywhere in the 1.6 XSD set. (The dead keyref
//                          r_road_link_neighbor in opendrive_16_core.xsd:100 and the leftover word
//                          "neighbor" in a doc string are the only mentions -- neither defines the
//                          element.) => <neighbor> was REMOVED in 1.6.
//
// No other element/attribute was found to be *removed* (as opposed to added) between 1.5 and 1.6 in
// these XSDs, so the table has a single honest entry. GT_RoadManager.cpp does not read <neighbor>
// under any version, so on <=1.5 files it is simply a generic unsupported element; on >=1.6 files it
// is reclassified here as a removal.
const char* const kRemovedIn16[] = {
    "road/link/neighbor",
};
const std::size_t kRemovedIn16Count = sizeof(kRemovedIn16) / sizeof(kRemovedIn16[0]);

// Per-parse cap on how many unique unsupported/removed lines we LOG_WARN. Stats stay complete.
constexpr std::size_t kMaxLoggedLines = 200;

bool IsAdditionalDataName(const char* name)
{
    return std::strcmp(name, "userData") == 0 || std::strcmp(name, "dataQuality") == 0 ||
           std::strcmp(name, "include") == 0;
}

// Compose the STORED entry format documented in OdrSideModel.hpp:
//   element:   "<path>|ctx=<ctx>"
//   attribute: "<path>@<attr>|ctx=<ctx>"
//   removed:   "<path>|ctx=<ctx>|removed16"
std::string MakeElementEntry(const std::string& path, const std::string& ctx)
{
    return path + "|ctx=" + ctx;
}
std::string MakeAttrEntry(const std::string& path, const std::string& attr, const std::string& ctx)
{
    return path + "@" + attr + "|ctx=" + ctx;
}
std::string MakeRemovedEntry(const std::string& path, const std::string& ctx)
{
    return path + "|ctx=" + ctx + "|removed16";
}

// State carried through the recursive walk.
struct WalkState
{
    OdrSideModel*         model         = nullptr;
    bool                  found_include = false;
    int                   rev_minor     = -1;
    std::set<std::string> seen;          // dedupe key = the STORED entry string
    std::size_t           logged_lines  = 0;
    std::size_t           suppressed    = 0;

    // Insert a unique entry; increments the matching stat and (subject to the cap) logs it.
    // `human` is the already-formatted LOG_WARN body (without a trailing newline).
    void Emit(const std::string& stored, std::size_t OdrAuditStats::*counter, const std::string& human)
    {
        if (!seen.insert(stored).second)
        {
            return;  // duplicate (entry, ctx)
        }
        model->audit.entries.push_back(stored);
        (model->audit.*counter)++;
        if (logged_lines < kMaxLoggedLines)
        {
            LOG_WARN("{}", human);
            logged_lines++;
        }
        else
        {
            suppressed++;
        }
    }
};

// Forward decl.
void WalkNode(const pugi::xml_node& node, const std::string& path, const std::string& ctx, WalkState& st);

// Descend into the children of a whitelisted (or root) element.
void WalkChildren(const pugi::xml_node& parent, const std::string& parent_path, const std::string& ctx, WalkState& st)
{
    for (pugi::xml_node child = parent.first_child(); child; child = child.next_sibling())
    {
        // Skip comments, PIs, text/CDATA nodes -- only element nodes are audited.
        if (child.type() != pugi::node_element)
        {
            continue;
        }
        const char* cname = child.name();
        std::string cpath = parent_path.empty() ? std::string(cname) : (parent_path + "/" + cname);
        WalkNode(child, cpath, ctx, st);
    }
}

void WalkNode(const pugi::xml_node& node, const std::string& path, const std::string& ctx, WalkState& st)
{
    const char* name = node.name();

    // --- additionalData family: never unsupported, at ANY position ---
    if (IsAdditionalDataName(name))
    {
        if (std::strcmp(name, "include") == 0)
        {
            // Hard error by design (plan P1). Clear diagnostic; do not descend.
            std::string file = node.attribute("file").value();
            LOG_ERROR(
                "[ODR-INCLUDE] <include> is not supported (hard error by design, plan P1; resolution "
                "decision deferred to P9) file='{}' owner='{}'",
                file,
                path);
            st.found_include = true;
            return;
        }

        // userData / dataQuality: store raw XML + owner path + context; do NOT descend/audit.
        OdrExtraData extra;
        // owner_path is the OWNING (parent) element's path. `path` here is the element's own path
        // (".../userData"); strip the trailing "/userData" or "/dataQuality" to get the owner.
        std::string owner = path;
        const auto  slash = owner.find_last_of('/');
        owner             = (slash == std::string::npos) ? std::string() : owner.substr(0, slash);
        extra.owner_path  = owner;
        extra.context_id  = ctx;
        extra.xml         = NodeToXml(node);

        if (std::strcmp(name, "userData") == 0)
        {
            st.model->user_data.push_back(std::move(extra));
        }
        else
        {
            st.model->data_quality.push_back(std::move(extra));
        }
        return;
    }

    // --- unknown element: emit ONE topmost entry, do not descend, do not audit attrs ---
    if (!IsWhitelistedPath(path))
    {
        if (st.rev_minor >= 6 && IsRemovedIn16(path))
        {
            const std::string stored = MakeRemovedEntry(path, ctx);
            const std::string human  = "[ODR-REMOVED-1.6] " + path + (ctx.empty() ? "" : (" (road=" + ctx + ")")) +
                                      " (removed in OpenDRIVE 1.6)";
            st.Emit(stored, &OdrAuditStats::removed16_hits, human);
        }
        else
        {
            const std::string stored = MakeElementEntry(path, ctx);
            const std::string human  = "[ODR-UNSUPPORTED] " + path + (ctx.empty() ? "" : (" (road=" + ctx + ")"));
            st.Emit(stored, &OdrAuditStats::unsupported_elements, human);
        }
        return;
    }

    // A <road>/<junction> element establishes its OWN id as the context scope -- for its own
    // attributes AND its descendants. Compute that before auditing attributes so that an unsupported
    // attribute ON <road id="7"> is attributed to road 7 (nearest ancestor road/junction, inclusive).
    std::string child_ctx = ctx;
    if (std::strcmp(name, "road") == 0 || std::strcmp(name, "junction") == 0)
    {
        pugi::xml_attribute id = node.attribute("id");
        if (!id.empty())
        {
            child_ctx = id.value();
        }
    }

    // --- known element: audit each attribute, then recurse ---
    for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
    {
        if (!IsWhitelistedAttr(path, attr.name()))
        {
            const std::string aname  = attr.name();
            const std::string stored = MakeAttrEntry(path, aname, child_ctx);
            const std::string human =
                "[ODR-UNSUPPORTED] " + path + "@" + aname + (child_ctx.empty() ? "" : (" (road=" + child_ctx + ")"));
            st.Emit(stored, &OdrAuditStats::unsupported_attributes, human);
        }
    }

    WalkChildren(node, path, child_ctx, st);
}

}  // namespace

bool IsWhitelistedPath(const std::string& path)
{
    // Binary search over the sorted-by-path table.
    const GtOdrWhitelistRow* begin = kGtOdrWhitelist;
    const GtOdrWhitelistRow* end   = kGtOdrWhitelist + kGtOdrWhitelistCount;
    const GtOdrWhitelistRow* it =
        std::lower_bound(begin, end, path, [](const GtOdrWhitelistRow& row, const std::string& key) {
            return std::strcmp(row.path, key.c_str()) < 0;
        });
    return it != end && path == it->path;
}

bool IsWhitelistedAttr(const std::string& path, const std::string& attr)
{
    const GtOdrWhitelistRow* begin = kGtOdrWhitelist;
    const GtOdrWhitelistRow* end   = kGtOdrWhitelist + kGtOdrWhitelistCount;
    const GtOdrWhitelistRow* it =
        std::lower_bound(begin, end, path, [](const GtOdrWhitelistRow& row, const std::string& key) {
            return std::strcmp(row.path, key.c_str()) < 0;
        });
    if (it == end || path != it->path)
    {
        return false;
    }
    // attrs_csv is a comma-separated list; membership test on whole tokens.
    const std::string csv = it->attrs_csv;
    if (csv.empty())
    {
        return false;
    }
    std::size_t start = 0;
    while (start <= csv.size())
    {
        std::size_t comma = csv.find(',', start);
        std::size_t len   = (comma == std::string::npos) ? (csv.size() - start) : (comma - start);
        if (len == attr.size() && csv.compare(start, len, attr) == 0)
        {
            return true;
        }
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return false;
}

bool IsRemovedIn16(const std::string& path)
{
    for (std::size_t i = 0; i < kRemovedIn16Count; ++i)
    {
        if (path == kRemovedIn16[i])
        {
            return true;
        }
    }
    return false;
}

void RunCoverageWalk(const pugi::xml_node& root, OdrSideModel& model, bool& found_include)
{
    WalkState st;
    st.model     = &model;
    st.rev_minor = model.rev_minor;

    // The root <OpenDRIVE> itself is implicitly whitelisted (never audited); walk its children.
    WalkChildren(root, /*parent_path=*/"", /*ctx=*/"", st);

    // Keep the stored entries deterministic: unique (guaranteed by the set) + sorted.
    std::sort(model.audit.entries.begin(), model.audit.entries.end());

    if (st.suppressed > 0)
    {
        LOG_WARN("[ODR-UNSUPPORTED] +{} more unique entries suppressed (see audit stats)", st.suppressed);
    }

    found_include = st.found_include;
}

}  // namespace detail
}  // namespace odr
}  // namespace gt_esmini
