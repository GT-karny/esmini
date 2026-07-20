"""Annex F gap-rule checkers — category-group "junctions_direct_geometry".

Covers <junction type="direct"> connection/laneLink structure and the generic
<junction>/<planView> "junction reference line" element count.  Pure
xml.etree.ElementTree, stdlib only.  See CONTRACT.md in this directory for the
shared module contract (signature, corpus, classification taxonomy).

Implemented rules (structural / attribute-only, no road-geometry math):
  - junctions.direct.connecting_road_attribute_usage : @connectingRoad must not
    appear on a <connection> inside a @type="direct" junction (it uses @linkedRoad).
  - junctions.direct.correct_type_linked_road_usage : @linkedRoad must not appear
    on a <connection> inside a junction whose @type is not "direct".
  - junctions.direct.road_connectivity : a direct junction's connections must all
    share one common "hub" road (as @incomingRoad or @linkedRoad) with >=2 distinct
    roads on the other (varying) side -- otherwise the topology is either
    many-to-many with no common hub at all (Annex 12.6.3's "unsolvable case"), or
    the varying side degenerates to a single road id (incl. a lone
    (incomingRoad,linkedRoad) connection, or duplicate connections collapsing to
    the same pair) -- both violate "...connect one road on one side with MULTIPLE
    roads on the other side".
  - junctions.direct.overlap_zone_exclusivity : within a natural overlapping-lane
    sibling group (same incomingRoad+@from, or same linkedRoad+@to, across sibling
    connections), at most 2 <laneLink> elements ("a pair") may carry @overlapZone.

junctions.direct.split_or_merge was originally implemented sharing
road_connectivity's no-common-hub topology signature reframed as "implies
crossing traffic", but that only detects topology (many roots), not actual
geometric crossing -- it over-fired on non-crossing multi-root junctions
(independent parallel splits/merges with zero shared roads) and could never
detect true crossing traffic under a single hub. Adversarial audit reclassified
it to gap_geometry_math (see fixbriefs/fix_report_junctions_direct_geometry.json);
its flagging logic has been removed from this module.

Classified away (see final_status_junctions_direct_geometry.json for the full
per-rule reasoning): flat_exits_entries, linked_lane_smoothness,
overlap_zone_coverage, road_ramp_heading, split_or_merge (all junctions.direct,
genuine road-geometry math: elevation/heading/overlap-length/lane-crossing
evaluation along s) and junctions.geometry.correct_junction_boundry /
ref_line_definition (boundary polygon / perpendicular-reachability geometry math).
"""
from collections import defaultdict

_GEOM_CHILD_TAGS = ("line", "spiral", "arc", "poly3", "paramPoly3")


def _eff_junction_type(j):
    """Junction @type is optional; missing means the common/"default" type."""
    t = j.get("type")
    return t if t else "default"


def _hub_road(conns):
    """Return a road id shared by every connection (as @incomingRoad OR
    @linkedRoad, since a bidirectional hub road can appear on either side
    across sibling connections -- see real Ex_Slip_Lane junction 5/6), or
    None if no single road id is common to all connections in `conns`.
    `conns` must already be filtered to connections that have both
    @incomingRoad and @linkedRoad present.
    """
    if not conns:
        return None
    candidates = set()
    for attr in ("incomingRoad", "linkedRoad"):
        v = conns[0].get(attr)
        if v is not None:
            candidates.add(v)
    for c in conns[1:]:
        touched = set()
        for attr in ("incomingRoad", "linkedRoad"):
            v = c.get(attr)
            if v is not None:
                touched.add(v)
        candidates &= touched
        if not candidates:
            return None
    return sorted(candidates)[0] if candidates else None


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    for jid, j in junctions.items():
        eff_type = _eff_junction_type(j)
        conns = j.findall("connection")

        # --- junctions.direct.connecting_road_attribute_usage ---
        # --- junctions.direct.correct_type_linked_road_usage ---
        for conn in conns:
            cid = conn.get("id")
            if eff_type == "direct":
                if conn.get("connectingRoad") is not None:
                    flags.append((
                        "junctions.direct.connecting_road_attribute_usage",
                        f"direct junction の connection id={cid} が @connectingRoad="
                        f"{conn.get('connectingRoad')} を持つ（direct junctionは@linkedRoadを使うべき）",
                        f"junction {jid} connection {cid}",
                    ))
            else:
                if conn.get("linkedRoad") is not None:
                    flags.append((
                        "junctions.direct.correct_type_linked_road_usage",
                        f"junction type={eff_type!r} の connection id={cid} が @linkedRoad="
                        f"{conn.get('linkedRoad')} を持つ（@linkedRoadはtype=\"direct\"専用）",
                        f"junction {jid} connection {cid}",
                    ))

        if eff_type != "direct":
            continue

        # --- junctions.direct.road_connectivity ---
        # (junctions.direct.split_or_merge was reclassified to gap_geometry_math --
        #  see module docstring and fixbriefs/fix_report_junctions_direct_geometry.json --
        #  and no longer has flagging logic here.)
        conns_both = [c for c in conns if c.get("incomingRoad") is not None and c.get("linkedRoad") is not None]
        if conns_both:
            pairs = [(c.get("incomingRoad"), c.get("linkedRoad")) for c in conns_both]
            hub = _hub_road(conns_both)
            if hub is None:
                incoming_set = sorted({c.get("incomingRoad") for c in conns_both})
                linked_set = sorted({c.get("linkedRoad") for c in conns_both})
                flags.append((
                    "junctions.direct.road_connectivity",
                    f"direct junction の connection群 {pairs} に全connection共通のhub roadが無い"
                    f"（incomingRoad={incoming_set} / linkedRoad={linked_set} が共に複数）"
                    f"— 片側は単一roadであるべき",
                    f"junction {jid}",
                ))
            else:
                # Hub found on (at least) one side of every connection -- now check
                # the *other* (varying) side actually resolves to >=2 distinct road
                # ids, per "...connect one road on one side with MULTIPLE roads on
                # the other side". A single connection (or duplicate connections
                # collapsing to the same (incomingRoad,linkedRoad) pair) leaves the
                # varying side with just 1 distinct road -- also a violation.
                other_roads = set()
                for c in conns_both:
                    inc = c.get("incomingRoad")
                    link = c.get("linkedRoad")
                    if inc == hub:
                        other_roads.add(link)
                    elif link == hub:
                        other_roads.add(inc)
                if len(other_roads) < 2:
                    flags.append((
                        "junctions.direct.road_connectivity",
                        f"direct junction の connection群 {pairs} はhub road={hub} に対し"
                        f"反対側の road が {sorted(other_roads)} の{len(other_roads)}個しかない"
                        f"（\"1つのroadを反対側の複数roadに接続する\" 規定を満たさない）",
                        f"junction {jid}",
                    ))

        # --- junctions.direct.overlap_zone_exclusivity ---
        exit_groups = defaultdict(list)   # (incomingRoad, from) -> [(conn, laneLink), ...]
        entry_groups = defaultdict(list)  # (linkedRoad, to) -> [(conn, laneLink), ...]
        for conn in conns:
            inc = conn.get("incomingRoad")
            link = conn.get("linkedRoad")
            for ll in conn.findall("laneLink"):
                frm = ll.get("from")
                to = ll.get("to")
                if inc is not None and frm is not None:
                    exit_groups[(inc, frm)].append((conn, ll))
                if link is not None and to is not None:
                    entry_groups[(link, to)].append((conn, ll))

        for label, groups in (("exit(from共有)", exit_groups), ("entry(to共有)", entry_groups)):
            for key, members in groups.items():
                annotated = [m for m in members if m[1].get("overlapZone") is not None]
                if len(annotated) > 2:
                    conn_ids = [c.get("id") for c, _ll in annotated]
                    flags.append((
                        "junctions.direct.overlap_zone_exclusivity",
                        f"{label} キー{key} の重複lane組で @overlapZone を持つ laneLink が"
                        f"{len(annotated)}個（connection {conn_ids}）— 1ペア(2要素)までのはず",
                        f"junction {jid}",
                    ))

    # --- junctions.geometry.only_one_line_element (applies to any junction type
    #     with a <planView> "junction reference line", not just direct) ---
    for jid, j in junctions.items():
        pv = j.find("planView")
        if pv is None:
            continue
        geoms = pv.findall("geometry")
        if len(geoms) != 1:
            flags.append((
                "junctions.geometry.only_one_line_element",
                f"junction planView が <geometry> を{len(geoms)}個持つ（ちょうど1個であるべき）",
                f"junction {jid}",
            ))
            continue
        g = geoms[0]
        children = list(g)
        line_children = [c for c in children if c.tag == "line"]
        other_geom_children = [c for c in children if c.tag in _GEOM_CHILD_TAGS and c.tag != "line"]
        if len(line_children) != 1 or other_geom_children:
            tags = [c.tag for c in children] or ["(空)"]
            flags.append((
                "junctions.geometry.only_one_line_element",
                f"junction planView の <geometry> の子要素が {tags}"
                f"（<line>をちょうど1個だけ持つべき）",
                f"junction {jid}",
            ))

    return flags
