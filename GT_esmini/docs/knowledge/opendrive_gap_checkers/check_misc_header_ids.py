"""Gap-rule checkers for category-group "misc_header_ids".

Implements (Annex F, ASAM OpenDRIVE 1.9.0, gap = not covered by qc-opendrive):
  - defaultRegulations.only_speed_priority : within <header><defaultRegulations>, the
    <semantics> child of each <roadRegulations>/<signalRegulations> may only carry
    <speed>/<priority> children (t_signals_semantics is shared/generic and also allows
    lane/prohibited/warning/routing/streetname/parking/tourist/supplementary* -- those
    are XSD-valid there but not semantically applicable inside defaultRegulations).
  - header.offset.centered_coords : the dataset's inertial x/y coordinates (planView
    geometry start points + signal/object positionInertial) should stay near (0;0);
    flag when the observed magnitude is implausibly large (float-precision risk / offset
    mis-set) rather than trying to judge "small enough" via geometry math.
  - header.proj.max_one_proj : no more than one projection definition. XSD already caps
    <geoReference> at maxOccurs=1, so this checks (a) that structural invariant defensively
    and (b) the PROJ4 CDATA text itself for accidentally concatenated multiple "+proj="
    definitions (a real authoring mistake XSD cannot see, since it's just character data).
  - ids.id_unique_in_class : id uniqueness per element class (road / junction /
    junctionGroup / controller). object/signal are intentionally excluded -- authors
    legitimately reuse small ids per-road/as placeholders (matches the reference
    gap_rule_check.py, which restricts this rule to road/junction).
  - ids.id_unique_in_lane_section : lane id uniqueness within one <laneSection> (across
    left+center+right combined) -- distinct from id_unique_in_class because lane ids are
    legitimately reused across different lane sections.
  - ids.only_ref_defined_ids : referential integrity for every id-shaped cross-reference
    reachable via plain structural inspection: road link predecessor/successor + @junction,
    junction connection incomingRoad/connectingRoad/linkedRoad (direct junctions),
    junction @mainRoad (virtual junctions), junction crossPath crossingRoad/roadAtStart/
    roadAtEnd, junction priority high/low, junction controller id, junctionGroup
    junctionReference, controller/control signalId, signal/reference elementId
    (signal|object), and the <signalReference> clone element's id.

Module contract: run_checks(file_path, root, roads, road_ids, junctions, junction_ids)
    -> list[(rule_name, detail, location)]
Pure stdlib xml.etree.ElementTree, no re-parsing, no I/O beyond what's passed in.
"""

from collections import Counter

# --------------------------------------------------------------------------- helpers

_META_TAGS = {"_OpenDriveElement", "userData", "include"}


def _fnum(x, default=0.0):
    try:
        return float(x)
    except (TypeError, ValueError):
        return default


def _tag(el):
    """Element.tag, guarded against Comment/ProcessingInstruction (non-str tag)."""
    t = el.tag
    return t if isinstance(t, str) else None


# --------------------------------------------------------------------------- main entry


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    header = root.find("header")

    # ------------------------------------------------------------------
    # defaultRegulations.only_speed_priority
    # ------------------------------------------------------------------
    if header is not None:
        dr = header.find("defaultRegulations")
        if dr is not None:
            for group_tag in ("roadRegulations", "signalRegulations"):
                for idx, reg in enumerate(dr.findall(group_tag)):
                    sem = reg.find("semantics")
                    if sem is None:
                        continue
                    for child in sem:
                        ctag = _tag(child)
                        if ctag is None or ctag in _META_TAGS:
                            continue
                        if ctag not in ("speed", "priority"):
                            reg_type = reg.get("type")
                            flags.append(
                                (
                                    "defaultRegulations.only_speed_priority",
                                    f"header.defaultRegulations.{group_tag}[{idx}](type={reg_type}).semantics に "
                                    f"<{ctag}> は不可（speed/priorityのみ許容）",
                                    "header defaultRegulations",
                                )
                            )

    # ------------------------------------------------------------------
    # header.offset.centered_coords
    # ------------------------------------------------------------------
    # Threshold: 1e5 m (100km). Well beyond any realistic single road-network extent in
    # this corpus (max observed ~6.2km in the official set) and past where float32
    # rendering precision starts to matter -- a defensible "clearly not centered" bar,
    # not a geometry-math judgment (only reads x/y attributes, no curvature evaluation).
    LARGE_COORD_THRESHOLD = 1.0e5
    max_abs = 0.0
    max_where = None
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is not None:
            for g in pv.findall("geometry"):
                x = _fnum(g.get("x"))
                y = _fnum(g.get("y"))
                m = max(abs(x), abs(y))
                if m > max_abs:
                    max_abs = m
                    max_where = f"road {rid} s={g.get('s')}"
        for pi in r.iter("positionInertial"):
            x = _fnum(pi.get("x"))
            y = _fnum(pi.get("y"))
            m = max(abs(x), abs(y))
            if m > max_abs:
                max_abs = m
                max_where = f"road {rid} positionInertial"
    if max_abs > LARGE_COORD_THRESHOLD:
        flags.append(
            (
                "header.offset.centered_coords",
                f"座標が(0;0)から大きく外れている（|x or y| 最大 {max_abs:.1f}m > "
                f"{LARGE_COORD_THRESHOLD:.0f}m）。header/offset での再centeringを検討",
                max_where or "file",
            )
        )

    # ------------------------------------------------------------------
    # header.proj.max_one_proj
    # ------------------------------------------------------------------
    if header is not None:
        georefs = header.findall("geoReference")
        if len(georefs) > 1:
            flags.append(
                (
                    "header.proj.max_one_proj",
                    f"header内に<geoReference>が{len(georefs)}個定義されている（1個まで）",
                    "header",
                )
            )
        elif len(georefs) == 1:
            text = georefs[0].text or ""
            n_proj = text.count("+proj=")
            # PROJ pipeline CRS ('+proj=pipeline +step +proj=... +step +proj=...') is a
            # SINGLE legal projection definition whose "+step"s each carry their own
            # "+proj=" token -- do not mistake pipeline stages for concatenated separate
            # definitions (real authoring mistakes look like two independent "+proj=..."
            # strings with no "+proj=pipeline"/"+step" framing).
            if "+proj=pipeline" not in text and n_proj > 1:
                flags.append(
                    (
                        "header.proj.max_one_proj",
                        f"<geoReference>のPROJ文字列内に+proj=定義が{n_proj}個含まれている"
                        "（複数の投影定義が連結された疑い、投影定義は1つのみ許容）",
                        "header geoReference",
                    )
                )

    # ------------------------------------------------------------------
    # ids.id_unique_in_class
    # ------------------------------------------------------------------
    # road/junction/junctionGroup are unique-per-file, top-level-only classes.
    #
    # controller is the one trap here: <junction><controller id=".."/> is a *reference*
    # into a synchronization group (t_junction_controller, "Lists the controllers that
    # should be grouped ... limited to that particular junction"), not a new definition
    # -- the same controller id legitimately appears under many junctions. Only the
    # top-level <OpenDRIVE><controller> (t_controller) is the defining class, so use
    # findall (direct children only), not iter, or every multi-junction network with
    # shared signal controllers false-positives here.
    #
    # object/signal are deliberately EXCLUDED from file-global uniqueness here (matches
    # the reference gap_rule_check.py, which restricts this rule to road/junction): in
    # practice authors legitimately reuse small object/signal ids (e.g. id=0/1) per-road
    # as local/placeholder numbering -- this fires heavily on the OFFICIAL ASAM
    # calibration set (e.g. UC_5Road_Junction.xodr reuses object id=0 across 29 objects)
    # and on real GT assets (multi_intersections.xodr signal id=0 x12), which is a false
    # positive per the corpus methodology (official set must stay ~0), not a genuine
    # authoring defect.
    for label, ids in (
        ("road", [e.get("id") for e in root.iter("road") if e.get("id") is not None]),
        (
            "junction",
            [e.get("id") for e in root.iter("junction") if e.get("id") is not None],
        ),
        (
            "junctionGroup",
            [
                e.get("id")
                for e in root.iter("junctionGroup")
                if e.get("id") is not None
            ],
        ),
        (
            "controller",
            [
                e.get("id")
                for e in root.findall("controller")
                if e.get("id") is not None
            ],
        ),
    ):
        for _id, cnt in Counter(ids).items():
            if cnt > 1:
                flags.append(
                    (
                        "ids.id_unique_in_class",
                        f"{label} id={_id} が{cnt}回定義されている（クラス内で一意であるべき）",
                        f"{label} {_id}",
                    )
                )

    # ------------------------------------------------------------------
    # ids.id_unique_in_lane_section
    # ------------------------------------------------------------------
    for rid, r in roads.items():
        for ls in r.iter("laneSection"):
            s = ls.get("s")
            lane_ids = []
            for side in ("left", "center", "right"):
                grp = ls.find(side)
                if grp is None:
                    continue
                for lane in grp.findall("lane"):
                    lid = lane.get("id")
                    if lid is not None:
                        lane_ids.append(lid)
            for lid, cnt in Counter(lane_ids).items():
                if cnt > 1:
                    flags.append(
                        (
                            "ids.id_unique_in_lane_section",
                            f"lane id={lid} がこのlaneSection内で{cnt}回定義されている",
                            f"road {rid} s={s}",
                        )
                    )

    # ------------------------------------------------------------------
    # ids.only_ref_defined_ids (comprehensive referential integrity)
    # ------------------------------------------------------------------
    # only top-level <OpenDRIVE><controller> defines a controller id; <junction><controller>
    # is itself a reference into that set (see id_unique_in_class comment above), so it must
    # not be folded into the "defined ids" universe here.
    controller_ids = {
        c.get("id") for c in root.findall("controller") if c.get("id") is not None
    }
    signal_ids = set()
    object_ids = set()
    for r in roads.values():
        signals_el = r.find("signals")
        if signals_el is not None:
            for s in signals_el.findall("signal"):
                if s.get("id") is not None:
                    signal_ids.add(s.get("id"))
        objects_el = r.find("objects")
        if objects_el is not None:
            for o in objects_el.findall("object"):
                if o.get("id") is not None:
                    object_ids.add(o.get("id"))
    # <junction><objects> shares t_road_objects (same globally-unique-id semantics per the
    # XSD "Unique ID within database" annotation) -- a road-side signal/reference can
    # legitimately point at a junction-owned object (e.g. a trafficIsland), so it must be
    # in the same "defined ids" universe or such refs would false-positive as undefined.
    for j in junctions.values():
        objects_el = j.find("objects")
        if objects_el is not None:
            for o in objects_el.findall("object"):
                if o.get("id") is not None:
                    object_ids.add(o.get("id"))

    # road link predecessor/successor + @junction
    for rid, r in roads.items():
        jid = r.get("junction")
        if jid and jid != "-1" and jid not in junction_ids:
            flags.append(
                (
                    "ids.only_ref_defined_ids",
                    f"road {rid} @junction={jid} が未定義",
                    f"road {rid}",
                )
            )
        link = r.find("link")
        if link is not None:
            for tag in ("predecessor", "successor"):
                e = link.find(tag)
                if e is None:
                    continue
                et, eid = e.get("elementType"), e.get("elementId")
                if eid is None:
                    continue
                if et == "road" and eid not in road_ids:
                    flags.append(
                        (
                            "ids.only_ref_defined_ids",
                            f"road {rid}.link.{tag} -> road {eid} が未定義",
                            f"road {rid}",
                        )
                    )
                elif et == "junction" and eid not in junction_ids:
                    flags.append(
                        (
                            "ids.only_ref_defined_ids",
                            f"road {rid}.link.{tag} -> junction {eid} が未定義",
                            f"road {rid}",
                        )
                    )

    # junction connection incomingRoad/connectingRoad/linkedRoad, priority high/low,
    # controller id, @mainRoad (virtual junction), crossPath crossingRoad/roadAtStart/
    # roadAtEnd (pedestrian-crossing junction element)
    for jid, j in junctions.items():
        main_road = j.get("mainRoad")
        if main_road is not None and main_road not in road_ids:
            flags.append(
                (
                    "ids.only_ref_defined_ids",
                    f"junction {jid} @mainRoad={main_road} が未定義road",
                    f"junction {jid}",
                )
            )
        for conn in j.iter("connection"):
            # linkedRoad (t_junction_connection_direct, OpenDRIVE 1.7+ direct junctions)
            # is a sibling attribute to connectingRoad (t_junction_connection_common) --
            # each connection uses only one of the two depending on junction @type, so
            # checking both against road_ids is safe (the unused one is simply absent).
            for attr in ("incomingRoad", "connectingRoad", "linkedRoad"):
                v = conn.get(attr)
                if v is not None and v not in road_ids:
                    flags.append(
                        (
                            "ids.only_ref_defined_ids",
                            f"junction {jid} connection.{attr}={v} が未定義road",
                            f"junction {jid}",
                        )
                    )
        for cp in j.findall("crossPath"):
            cp_id = cp.get("id")
            for attr in ("crossingRoad", "roadAtStart", "roadAtEnd"):
                v = cp.get(attr)
                if v is not None and v not in road_ids:
                    flags.append(
                        (
                            "ids.only_ref_defined_ids",
                            f"junction {jid} crossPath[id={cp_id}].{attr}={v} が未定義road",
                            f"junction {jid}",
                        )
                    )
        for pr in j.findall("priority"):
            for attr in ("high", "low"):
                v = pr.get(attr)
                if v is not None and v not in road_ids:
                    flags.append(
                        (
                            "ids.only_ref_defined_ids",
                            f"junction {jid} priority.{attr}={v} が未定義road",
                            f"junction {jid}",
                        )
                    )
        for jc in j.findall("controller"):
            v = jc.get("id")
            if v is not None and v not in controller_ids:
                flags.append(
                    (
                        "ids.only_ref_defined_ids",
                        f"junction {jid} controller id={v} が未定義controller",
                        f"junction {jid}",
                    )
                )

    # junctionGroup -> junction
    for jg in root.iter("junctionGroup"):
        gid = jg.get("id")
        for jr in jg.findall("junctionReference"):
            v = jr.get("junction")
            if v is not None and v not in junction_ids:
                flags.append(
                    (
                        "ids.only_ref_defined_ids",
                        f"junctionGroup {gid} junctionReference.junction={v} が未定義junction",
                        f"junctionGroup {gid}",
                    )
                )

    # controller/control -> signal (only top-level <controller> carries <control> children;
    # junction/controller refs have no <control> sub-element, so findall here too for clarity)
    for c in root.findall("controller"):
        cid = c.get("id")
        for ctrl in c.findall("control"):
            v = ctrl.get("signalId")
            if v is not None and v not in signal_ids:
                flags.append(
                    (
                        "ids.only_ref_defined_ids",
                        f"controller {cid} control.signalId={v} が未定義signal",
                        f"controller {cid}",
                    )
                )

    # signal/reference -> signal|object ; top-level signalReference/@id -> signal
    for rid, r in roads.items():
        signals_el = r.find("signals")
        if signals_el is None:
            continue
        for sig in signals_el.findall("signal"):
            sid = sig.get("id")
            for ref in sig.findall("reference"):
                et, eid = ref.get("elementType"), ref.get("elementId")
                if eid is None:
                    continue
                if et == "signal" and eid not in signal_ids:
                    flags.append(
                        (
                            "ids.only_ref_defined_ids",
                            f"road {rid} signal {sid} reference -> signal {eid} が未定義",
                            f"road {rid} signal {sid}",
                        )
                    )
                elif et == "object" and eid not in object_ids:
                    flags.append(
                        (
                            "ids.only_ref_defined_ids",
                            f"road {rid} signal {sid} reference -> object {eid} が未定義",
                            f"road {rid} signal {sid}",
                        )
                    )
        for sref in signals_el.findall("signalReference"):
            v = sref.get("id")
            if v is not None and v not in signal_ids:
                flags.append(
                    (
                        "ids.only_ref_defined_ids",
                        f"road {rid} signalReference id={v} が未定義signal",
                        f"road {rid}",
                    )
                )

    return flags
