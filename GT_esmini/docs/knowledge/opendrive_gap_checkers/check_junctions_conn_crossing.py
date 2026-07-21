"""Gap-rule checkers for category-group junctions_conn_crossing (Annex F, ASAM OpenDRIVE
1.9.0 Checker rules, not covered by qc-opendrive):
  - junctions.connection.no_connecting_road_direct  (1.9.0)
  - junctions.crossing.only_one_high_prio           (1.8.0)
  - junctions.crossing.only_road_sections           (1.8.0)
  - junctions.priority.high_and_low_attr            (1.8.0)
  - junctions.cross_path.correct_junction_id        (1.8.0)
  - junctions.cross_path.lane_linkage               (1.8.0)
  - junctions.cross_path.only_connect_correct_type  (1.8.0)
  - junctions.cross_path.within_junction_area       (1.8.0)

Implemented, PARTIAL_DETERMINISTIC (1), see impl_briefs/junctions_conn_crossing.md:
  - junctions.priority.no_signals (1.7.0) : only the deterministic sub-case is
    checked -- a junction with >=1 <priority> element whose participating
    roads carry a <signal @dynamic="yes"> (a dynamic device, e.g. a traffic
    light). The broader "any signal including static signs" reading is left
    uncovered (ambiguous residual, see impl_briefs).

Not implemented (see structured report for rationale):
  - junctions.connection.lane_change_one_con_road       -> gap_ambiguous
  - junctions.connection.no_lane_change_for_mult_con_roads -> gap_ambiguous
  - junctions.connection.smooth_fit                     -> gap_geometry_math
  - junctions.crossing.s_start_end_coverage              -> gap_geometry_math
  - junctions.cross_path.disregard_cross_road_evelation  -> gap_ambiguous
  - junctions.cross_path.start_end_contained             -> gap_geometry_math

Pure stdlib xml.etree.ElementTree, no re-parsing of the input file (root/roads/etc. are
supplied by the integration layer). Idiom follows scratchpad/gap_rule_check.py.
"""


def _lanes_by_id_in_road(road):
    """Return dict: lane @id (string) -> list of <lane> elements with that id (any
    laneSection), so callers can inspect @type etc. without assuming a single
    laneSection."""
    out = {}
    for ls in road.iter("laneSection"):
        for grp_tag in ("left", "center", "right"):
            grp = ls.find(grp_tag)
            if grp is None:
                continue
            for lane in grp.findall("lane"):
                lid = lane.get("id")
                if lid is None:
                    continue
                out.setdefault(lid, []).append(lane)
    return out


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    # ------------------------------------------------------------------
    # junctions.connection.no_connecting_road_direct (1.9.0)
    # "@connectingRoad shall not be used for junctions with @type='direct'."
    # Direct junctions use @linkedRoad instead (t_junction_connection_direct).
    # ------------------------------------------------------------------
    for jid, j in junctions.items():
        if j.get("type") != "direct":
            continue
        for conn in j.findall("connection"):
            cr = conn.get("connectingRoad")
            if cr is not None:
                flags.append(
                    (
                        "junctions.connection.no_connecting_road_direct",
                        f"junction {jid}(type=direct) connection id={conn.get('id')} が @connectingRoad={cr} を使用（@linkedRoadを使うべき）",
                        f"junction {jid}",
                    )
                )

    # ------------------------------------------------------------------
    # junctions.crossing.only_road_sections (1.8.0)
    # "Junctions with @type='crossing' shall only have <roadSection> elements."
    # Per t_junction_crossing (XSD 1.1 @type-conditional typing, not enforced by
    # common XSD-1.0-only validators), crossing junctions must not carry <connection>
    # or <crossPath> (those belong to t_junction_common / t_junction_virtual).
    # ------------------------------------------------------------------
    for jid, j in junctions.items():
        if j.get("type") != "crossing":
            continue
        bad_children = [c.tag for c in j if c.tag in ("connection", "crossPath")]
        if bad_children:
            flags.append(
                (
                    "junctions.crossing.only_road_sections",
                    f"junction {jid}(type=crossing) に {sorted(set(bad_children))} 要素（roadSection以外）が存在",
                    f"junction {jid}",
                )
            )

    # ------------------------------------------------------------------
    # junctions.crossing.only_one_high_prio (1.8.0)
    # "Only one road defined by @roadId of <roadSection> elements shall have high
    # priority." -> within a crossing junction, the set of distinct @high values
    # across its own <priority> elements must have at most one member.
    # ------------------------------------------------------------------
    for jid, j in junctions.items():
        road_sections = j.findall("roadSection")
        if not road_sections:
            continue
        highs = []
        for pr in j.findall("priority"):
            h = pr.get("high")
            if h is not None:
                highs.append(h)
        distinct_highs = sorted(set(highs))
        if len(distinct_highs) > 1:
            flags.append(
                (
                    "junctions.crossing.only_one_high_prio",
                    f"junction {jid}(crossing) の priority @high が複数の道路 {distinct_highs} にまたがる（1つのみであるべき）",
                    f"junction {jid}",
                )
            )

    # ------------------------------------------------------------------
    # junctions.priority.high_and_low_attr (1.8.0)
    # "<priority> elements shall be defined with a pair of one @high and one @low
    # attribute." Structural presence check (defensive; XSD already requires both,
    # but our corpus is not guaranteed XSD-valid).
    # ------------------------------------------------------------------
    for jid, j in junctions.items():
        for idx, pr in enumerate(j.findall("priority")):
            h, low = pr.get("high"), pr.get("low")
            missing = [
                n for n, v in (("high", h), ("low", low)) if v is None or v == ""
            ]
            if missing:
                flags.append(
                    (
                        "junctions.priority.high_and_low_attr",
                        f"junction {jid} priority[{idx}] に必須属性 {missing} が欠落",
                        f"junction {jid}",
                    )
                )

    # ------------------------------------------------------------------
    # junctions.cross_path.correct_junction_id (1.8.0)
    # "The @junction attribute shall contain the id of the junction to which a road
    # belongs." For any road that declares @junction=J (J != -1), road must actually
    # be referenced as a participant of junction J: connection/@connectingRoad or
    # @linkedRoad, crossPath/@crossingRoad, or boundary/segment/@roadId. Roads that
    # merely appear as an incomingRoad, or as roadSection/@roadId of a crossing
    # junction (which per spec correctly keep @junction=-1), are not checked here.
    # ------------------------------------------------------------------
    def _junction_member_road_ids(j):
        ids = set()
        for conn in j.findall("connection"):
            for attr in ("connectingRoad", "linkedRoad"):
                v = conn.get(attr)
                if v is not None:
                    ids.add(v)
        for cp in j.findall("crossPath"):
            v = cp.get("crossingRoad")
            if v is not None:
                ids.add(v)
        boundary = j.find("boundary")
        if boundary is not None:
            for seg in boundary.findall("segment"):
                v = seg.get("roadId")
                if v is not None:
                    ids.add(v)
        return ids

    for rid, r in roads.items():
        jid = r.get("junction")
        if jid is None or jid == "-1":
            continue
        if jid not in junctions:
            continue  # dangling ref handled by ids.only_ref_defined_ids (not ours)
        member_ids = _junction_member_road_ids(junctions[jid])
        if rid not in member_ids:
            flags.append(
                (
                    "junctions.cross_path.correct_junction_id",
                    f"road {rid} は @junction={jid} を宣言するが、junction {jid} 内で connectingRoad/linkedRoad/crossingRoad/boundary roadId として参照されていない",
                    f"road {rid}",
                )
            )

    # ------------------------------------------------------------------
    # junctions.cross_path.lane_linkage (1.8.0)
    # "Start and end of the crossing road shall reach the linked lanes specified by
    # <startLaneLink> and <endLaneLink>." Structural proxy limited to what is
    # unambiguous from attributes alone: @crossingRoad and @roadAtStart/@roadAtEnd
    # must resolve to defined roads, and @to (lane id "of @crossingRoad" per XSD)
    # must exist as a <lane> in the crossingRoad. @from ("lane id of @roadAtEnd/
    # @roadAtStart") is deliberately NOT checked for lane existence here: when that
    # road is itself a junction-connecting road its lane ids are frequently
    # remapped relative to the road it links to (verified against the official
    # ASAM UC_5Road_Junction.xodr calibration fixture, where @from legitimately
    # does not match the referenced connecting road's own raw lane @id) --
    # resolving that needs lane-linkage-chain / geometry reasoning, not plain
    # attribute reading.
    #
    # junctions.cross_path.only_connect_correct_type (1.8.0)
    # "Cross paths shall only connect lanes with @type='walking' or @type='biking'."
    # Checked only against the crossingRoad's own lane (@to) for the same reason:
    # @from may legitimately reference a non-pedestrian anchor lane (e.g. curb,
    # driving) on the crossed road; type="sidewalk" is accepted as the deprecated
    # (per XSD annotation "Use walking instead") synonym for type="walking".
    # ------------------------------------------------------------------
    PEDESTRIAN_LANE_TYPES = {"walking", "biking", "sidewalk"}
    for jid, j in junctions.items():
        for cp in j.findall("crossPath"):
            cp_id = cp.get("id")
            crossing_road_id = cp.get("crossingRoad")
            crossing_road = roads.get(crossing_road_id) if crossing_road_id else None
            if crossing_road_id is not None and crossing_road_id not in road_ids:
                flags.append(
                    (
                        "junctions.cross_path.lane_linkage",
                        f"junction {jid} crossPath id={cp_id} @crossingRoad={crossing_road_id} が未定義road",
                        f"junction {jid} crossPath id={cp_id}",
                    )
                )

            for tag, road_attr in (
                ("startLaneLink", "roadAtStart"),
                ("endLaneLink", "roadAtEnd"),
            ):
                link = cp.find(tag)
                if link is None:
                    continue
                other_road_id = cp.get(road_attr)
                to_id = link.get("to")

                if other_road_id is not None and other_road_id not in road_ids:
                    flags.append(
                        (
                            "junctions.cross_path.lane_linkage",
                            f"junction {jid} crossPath id={cp_id} {tag} の @{road_attr}={other_road_id} が未定義road",
                            f"junction {jid} crossPath id={cp_id}",
                        )
                    )

                if crossing_road is not None and to_id is not None:
                    to_lanes = _lanes_by_id_in_road(crossing_road)
                    if to_id not in to_lanes:
                        flags.append(
                            (
                                "junctions.cross_path.lane_linkage",
                                f"junction {jid} crossPath id={cp_id} {tag} @to={to_id} が crossingRoad {crossing_road_id} に存在しないlane id",
                                f"junction {jid} crossPath id={cp_id}",
                            )
                        )
                    else:
                        types = {ln.get("type") for ln in to_lanes[to_id]}
                        if types and not (types & PEDESTRIAN_LANE_TYPES):
                            flags.append(
                                (
                                    "junctions.cross_path.only_connect_correct_type",
                                    f"junction {jid} crossPath id={cp_id} {tag} @to={to_id} (crossingRoad {crossing_road_id}) の lane type={sorted(types)} は walking/biking ではない",
                                    f"junction {jid} crossPath id={cp_id}",
                                )
                            )

    # ------------------------------------------------------------------
    # junctions.cross_path.within_junction_area (1.8.0)
    # "Cross paths shall be within the area of a common junction or a virtual
    # junction." Per schema, <crossPath> is only a legal child of t_junction_common
    # (@type default/"default") and t_junction_virtual (@type="virtual") -- not
    # t_junction_crossing or t_junction_direct.
    # ------------------------------------------------------------------
    for jid, j in junctions.items():
        jtype = j.get("type")
        if jtype in ("crossing", "direct") and j.findall("crossPath"):
            flags.append(
                (
                    "junctions.cross_path.within_junction_area",
                    f"junction {jid}(type={jtype}) に crossPath が存在（commonまたはvirtualのみ許容）",
                    f"junction {jid}",
                )
            )

    # ------------------------------------------------------------------
    # junctions.priority.no_signals (PARTIAL_DETERMINISTIC, 1.7.0)
    # "<priority> elements should only be used if there are no signals
    # defined." Only the deterministic dynamic-signal sub-case is checked:
    # for a junction (type "default" or "virtual", both schema-legal parents
    # of <priority>) with >=1 <priority> child, collect its participating
    # road ids (connection/@incomingRoad, connection/@connectingRoad, and for
    # virtual junctions @mainRoad) and flag if any of those roads carries a
    # <signal @dynamic="yes"> (e.g. a traffic light) -- a fixed <priority>
    # ordering co-existing with a dynamically time-varying right-of-way
    # device is a direct logical-consistency conflict. The broader "any
    # signal incl. static signs" reading is deliberately NOT applied (would
    # contradict established GT authoring practice of pairing static
    # yield/priority signs with an explicit <priority>) -- see impl_briefs.
    # ------------------------------------------------------------------
    for jid, j in junctions.items():
        jtype = j.get("type") or "default"
        if jtype not in ("default", "virtual"):
            continue
        if not j.findall("priority"):
            continue
        participant_ids = set()
        for conn in j.findall("connection"):
            for attr in ("incomingRoad", "connectingRoad"):
                v = conn.get(attr)
                if v is not None:
                    participant_ids.add(v)
        if jtype == "virtual":
            mr = j.get("mainRoad")
            if mr is not None:
                participant_ids.add(mr)

        dynamic_hits = []
        for rid in sorted(participant_ids):
            road = roads.get(rid)
            if road is None:
                continue
            signals_el = road.find("signals")
            if signals_el is None:
                continue
            for sig in signals_el.findall("signal"):
                if sig.get("dynamic") == "yes":
                    dynamic_hits.append((rid, sig.get("id")))
        if dynamic_hits:
            roads_hit = sorted({rid for rid, _ in dynamic_hits})
            flags.append(
                (
                    "junctions.priority.no_signals",
                    f"junction {jid}(type={jtype}) に <priority> が定義されているが、参加road "
                    f'{roads_hit} に @dynamic="yes" の signal（動的信号機）が存在'
                    "（動的信号がある場合priorityは使うべきでない）",
                    f"junction {jid}",
                )
            )

    return flags
