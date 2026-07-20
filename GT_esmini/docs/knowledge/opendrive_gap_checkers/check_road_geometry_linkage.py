"""Gap-rule checkers for category-group 'road_geometry_linkage'.

Covers Annex F rule families: road (length/overlap), road.linkage
(predecessor/successor consistency + attribute-usage-by-target-type),
road.geometry (reference-line structure/continuity) and the
arc/paramPoly3/spiral geometry-element sub-rules.

Pure stdlib xml.etree.ElementTree, no new deps. See
scratchpad/checks_gap/CONTRACT.md for the module contract this file
implements (run_checks signature, flag tuple shape, corpus/self-test
methodology).
"""

# --------------------------------------------------------------------------
# small numeric helper (mirrors scratchpad/gap_rule_check.py idiom)
# --------------------------------------------------------------------------
def _fnum(x, d=None):
    try:
        return float(x)
    except (TypeError, ValueError):
        return d


_GEOM_KINDS = ("line", "spiral", "arc", "poly3", "paramPoly3")


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    # ======================================================================
    # road.length_sum_geometries — road @length should equal sum of
    # <geometry>@length inside <planView>.
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            continue
        geoms = pv.findall("geometry")
        if not geoms:
            continue
        ssum = sum(_fnum(g.get("length"), 0.0) for g in geoms)
        rl = _fnum(r.get("length"), None)
        if rl is not None and rl > 0 and abs(ssum - rl) > 1e-2:
            flags.append((
                "road.length_sum_geometries",
                f"road @length={rl:g} != Sigma geometry.length {ssum:g} (diff {ssum - rl:+.4g})",
                f"road {rid}",
            ))

    # ======================================================================
    # road.linkage.both_sides_consistency — road<->road predecessor/successor
    # reciprocity. Connecting roads (junction != -1) link through the
    # junction's <connection> elements, not direct reciprocity -> excluded
    # both as the host road and as the link target (per contract guidance).
    # ======================================================================
    for rid, r in roads.items():
        if r.get("junction", "-1") != "-1":
            continue
        link = r.find("link")
        if link is None:
            continue
        for tag in ("predecessor", "successor"):
            e = link.find(tag)
            if e is None or e.get("elementType") != "road":
                continue
            bid, cp = e.get("elementId"), e.get("contactPoint")
            if e.get("elementS") is not None:
                continue  # virtual-junction mid-road entry (elementS/elementDir alt.
                # encoding): a one-way branch relationship, not a direct road<->road
                # link with simple reciprocity semantics
            if cp not in ("start", "end"):
                continue  # no interpretable contactPoint -> can't determine which
                # side to expect; road_link_attribute_usage already flags this
            if bid not in roads:
                continue  # dangling ref: another checker's job (referential integrity)
            if roads[bid].get("junction", "-1") != "-1":
                continue  # target is a connecting road; resolves via the junction instead
            blink = roads[bid].find("link")
            expect = "predecessor" if cp == "start" else "successor"
            be = blink.find(expect) if blink is not None else None
            ok = be is not None and be.get("elementType") == "road" and be.get("elementId") == rid
            if not ok:
                other = "successor" if expect == "predecessor" else "predecessor"
                bo = blink.find(other) if blink is not None else None
                ok = bo is not None and bo.get("elementType") == "road" and bo.get("elementId") == rid
            if not ok:
                flags.append((
                    "road.linkage.both_sides_consistency",
                    f"road {rid}.{tag}->road {bid}(contactPoint={cp}) is not reciprocated on road {bid}'s side",
                    f"road {rid}",
                ))

    # ======================================================================
    # road.linkage.{junc,road,virtjunc}_link_attribute_usage — the XSD makes
    # elementType/contactPoint/elementS/elementDir all optional on
    # <predecessor>/<successor>, regardless of what kind of element is being
    # linked to; Annex F fills the gap by mandating specific attribute sets
    # per target kind. Scope: only <road><link><predecessor|successor>
    # (the junction-internal t_junction_predecessorSuccessor type used for
    # virtual-junction <connection> children already has elementType/
    # elementS as XSD-required attrs, so it is out of scope here).
    # ======================================================================
    def _link_target_kind(e):
        et = e.get("elementType")
        eid = e.get("elementId")
        if et == "road":
            return "road"
        if et == "junction":
            return "junction"
        # elementType absent/unrecognized -> infer from id namespaces so we
        # can still classify (and flag the missing/invalid elementType itself)
        in_road = eid in road_ids
        in_junc = eid in junction_ids
        if in_road and not in_junc:
            return "road"
        if in_junc and not in_road:
            return "junction"
        return None  # dangling or ambiguous id -> not our concern here

    for rid, r in roads.items():
        link = r.find("link")
        if link is None:
            continue
        for tag in ("predecessor", "successor"):
            e = link.find(tag)
            if e is None:
                continue
            eid = e.get("elementId")
            et = e.get("elementType")

            # elementS is a self-declaring marker: Road.xsd documents it as
            # "Alternative to contactPoint for virtual junctions ... Shall
            # only be used for elementType 'road'". Its mere presence means
            # this predecessor/successor represents a virtual-junction
            # mid-road entry, not a plain road<->road link - route the whole
            # attribute-usage check to virtjunc_link_attribute_usage
            # exclusively (empirically confirmed: every real elementS usage
            # in the 208-file corpus has elementType='road', never
            # elementType='junction' pointing at the junction's own id - the
            # rule's "for a virtual junction" phrasing describes this
            # elementS-marked road link, not a literal elementType='junction'
            # reference to a @type='virtual' junction, which never occurs in
            # valid data since the XSD ties elementS to elementType='road').
            if e.get("elementS") is not None:
                missing = []
                if et != "road":
                    missing.append("elementType='road'")
                if not eid:
                    missing.append("elementId")
                if e.get("elementDir") is None:
                    missing.append("elementDir")
                if missing:
                    flags.append((
                        "road.linkage.virtjunc_link_attribute_usage",
                        f"road {rid}.{tag}->road {eid} (virtual-junction mid-road entry via elementS): missing/invalid {', '.join(missing)}",
                        f"road {rid}",
                    ))
                continue  # elementS-marked links are fully handled here

            kind = _link_target_kind(e)

            if kind == "road":
                missing = []
                if et != "road":
                    missing.append("elementType='road'")
                if not eid:
                    missing.append("elementId")
                if e.get("contactPoint") not in ("start", "end"):
                    missing.append("contactPoint")
                if missing:
                    flags.append((
                        "road.linkage.road_link_attribute_usage",
                        f"road {rid}.{tag}->road {eid}: missing/invalid {', '.join(missing)}",
                        f"road {rid}",
                    ))

            elif kind == "junction":
                jel = junctions.get(eid)
                jtype = jel.get("type") if jel is not None else None
                is_virtual = jtype == "virtual"
                if is_virtual:
                    missing = []
                    if et != "junction":
                        missing.append("elementType='junction'")
                    if not eid:
                        missing.append("elementId")
                    if e.get("elementS") is None:
                        missing.append("elementS")
                    if e.get("elementDir") is None:
                        missing.append("elementDir")
                    if missing:
                        flags.append((
                            "road.linkage.virtjunc_link_attribute_usage",
                            f"road {rid}.{tag}->virtual junction {eid}: missing/invalid {', '.join(missing)}",
                            f"road {rid}",
                        ))
                else:
                    missing = []
                    if et != "junction":
                        missing.append("elementType='junction'")
                    if not eid:
                        missing.append("elementId")
                    if missing:
                        flags.append((
                            "road.linkage.junc_link_attribute_usage",
                            f"road {rid}.{tag}->junction {eid}: missing/invalid {', '.join(missing)}",
                            f"road {rid}",
                        ))
            # kind is None (dangling/ambiguous id): out of scope, other
            # checkers (referential-integrity family) own that.

    # ======================================================================
    # road.geometry.elem_asc_order — <geometry> elements in ascending s order
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            continue
        geoms = pv.findall("geometry")
        s_prev = None
        for g in geoms:
            s = _fnum(g.get("s"), 0.0)
            if s_prev is not None and s < s_prev - 1e-6:
                flags.append((
                    "road.geometry.elem_asc_order",
                    f"geometry s={s:g} < previous geometry s={s_prev:g}",
                    f"road {rid} s={s:g}",
                ))
            s_prev = s

    # ======================================================================
    # road.geometry.one_geom_elem_per_spec — exactly one further-specifying
    # child (line|spiral|arc|poly3|paramPoly3) per <geometry>
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            continue
        for g in pv.findall("geometry"):
            present = [tag for tag in _GEOM_KINDS if g.find(tag) is not None]
            s = _fnum(g.get("s"), 0.0)
            if len(present) == 0:
                flags.append((
                    "road.geometry.one_geom_elem_per_spec",
                    "geometry element has no line/spiral/arc/poly3/paramPoly3 child",
                    f"road {rid} s={s:g}",
                ))
            elif len(present) > 1:
                flags.append((
                    "road.geometry.one_geom_elem_per_spec",
                    f"geometry element has {len(present)} specifying children: {present}",
                    f"road {rid} s={s:g}",
                ))

    # ======================================================================
    # road.geometry.only_one_refline — at most one <planView> per road
    # ======================================================================
    for rid, r in roads.items():
        pvs = r.findall("planView")
        if len(pvs) > 1:
            flags.append((
                "road.geometry.only_one_refline",
                f"road has {len(pvs)} <planView> elements (only one road reference line allowed)",
                f"road {rid}",
            ))

    # ======================================================================
    # road.geometry.refline_exists — every road shall have a reference line
    # (a <planView> with at least one <geometry>)
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            flags.append((
                "road.geometry.refline_exists",
                "road has no <planView> element",
                f"road {rid}",
            ))
            continue
        if not pv.findall("geometry"):
            flags.append((
                "road.geometry.refline_exists",
                "road <planView> has no <geometry> elements",
                f"road {rid}",
            ))

    # ======================================================================
    # road.geometry.refline_no_gaps — consecutive geometry elements must
    # abut (next.s == prev.s + prev.length); first element must start at 0.
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            continue
        geoms = pv.findall("geometry")
        prev_end = None
        for gi, g in enumerate(geoms):
            s = _fnum(g.get("s"), 0.0)
            length = _fnum(g.get("length"), 0.0)
            if gi == 0 and abs(s) > 1e-6:
                flags.append((
                    "road.geometry.refline_no_gaps",
                    f"first geometry s={s:g} != 0",
                    f"road {rid}",
                ))
            elif prev_end is not None and abs(s - prev_end) > 1e-3:
                flags.append((
                    "road.geometry.refline_no_gaps",
                    f"geometry s={s:g} != previous geometry end {prev_end:g} (gap {s - prev_end:+.4g})",
                    f"road {rid} s={s:g}",
                ))
            prev_end = s + length

    # ======================================================================
    # road.geometry.s-value_sum — geometry[i].s shall equal the cumulative
    # sum of all prior geometry lengths (global, not just local-gap check)
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            continue
        geoms = pv.findall("geometry")
        cum = 0.0
        for g in geoms:
            s = _fnum(g.get("s"), 0.0)
            length = _fnum(g.get("length"), 0.0)
            if abs(s - cum) > 1e-3:
                flags.append((
                    "road.geometry.s-value_sum",
                    f"geometry s={s:g} != Sigma(prior lengths)={cum:g} (diff {s - cum:+.4g})",
                    f"road {rid} s={s:g}",
                ))
            cum += length

    # ======================================================================
    # road.geometry.arc.no_zero_curvature
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            continue
        for g in pv.findall("geometry"):
            arc = g.find("arc")
            if arc is None:
                continue
            curvature = _fnum(arc.get("curvature"))
            s = _fnum(g.get("s"), 0.0)
            if curvature is not None and abs(curvature) < 1e-9:
                flags.append((
                    "road.geometry.arc.no_zero_curvature",
                    f"arc @curvature={curvature:g} (should not be zero; use <line> instead)",
                    f"road {rid} s={s:g}",
                ))

    # ======================================================================
    # road.geometry.paramPoly3.valid_parameters — aU=aV=bV=0, bU>0
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            continue
        for g in pv.findall("geometry"):
            pp = g.find("paramPoly3")
            if pp is None:
                continue
            aU = _fnum(pp.get("aU"), 0.0)
            aV = _fnum(pp.get("aV"), 0.0)
            bV = _fnum(pp.get("bV"), 0.0)
            bU = _fnum(pp.get("bU"), 0.0)
            s = _fnum(g.get("s"), 0.0)
            tol = 1e-6
            bad = []
            if abs(aU) > tol:
                bad.append(f"aU={aU:g}(!=0)")
            if abs(aV) > tol:
                bad.append(f"aV={aV:g}(!=0)")
            if abs(bV) > tol:
                bad.append(f"bV={bV:g}(!=0)")
            if bU <= 0.0:
                bad.append(f"bU={bU:g}(should be >0)")
            if bad:
                flags.append((
                    "road.geometry.paramPoly3.valid_parameters",
                    f"paramPoly3 not u/v-aligned with s/t start: {', '.join(bad)} (expect aU=aV=bV=0, bU>0)",
                    f"road {rid} s={s:g}",
                ))

    # ======================================================================
    # road.geometry.spiral.curvature_change — curvStart != curvEnd
    # ======================================================================
    for rid, r in roads.items():
        pv = r.find("planView")
        if pv is None:
            continue
        for g in pv.findall("geometry"):
            sp = g.find("spiral")
            if sp is None:
                continue
            cs = _fnum(sp.get("curvStart"))
            ce = _fnum(sp.get("curvEnd"))
            s = _fnum(g.get("s"), 0.0)
            if cs is not None and ce is not None and abs(cs - ce) < 1e-9:
                flags.append((
                    "road.geometry.spiral.curvature_change",
                    f"spiral curvStart==curvEnd=={cs:g} (no curvature change; use <arc> or <line> instead)",
                    f"road {rid} s={s:g}",
                ))

    # ======================================================================
    # Not implemented via structural inspection alone — classified in the
    # structured report (see StructuredOutput call), not stubbed here:
    #   road.no_overlap_outside_junction / road.no_overlap_self /
    #   road.overlap_inside_junction / road.geometry.contact_point /
    #   road.geometry.refline_no_kinks
    # all require evaluating actual 2D/3D road-reference-line geometry
    # (line/arc/spiral/paramPoly3 position & heading integration) rather
    # than reading attributes/structure.
    # ======================================================================

    return flags
