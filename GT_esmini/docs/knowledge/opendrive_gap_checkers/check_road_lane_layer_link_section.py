"""Gap-rule checkers for category-group "road_lane_layer_link_section".

Covers: road.lane.layer.* (permanent/temporary lane-layer rules, OpenDRIVE 1.9),
         road.lane.link.* (lane predecessor/successor linkage rules),
         road.lane_section.* (laneSection structural rules),
         road.lanes.lane_offset.* (laneOffset rules).

Pure stdlib xml.etree.ElementTree, no re-parsing of files (root is passed in).
Style/idiom matches scratchpad/gap_rule_check.py (roads dict, road_ids, tuple flags).

Key domain fact (ASAM OpenDRIVE 1.9.0 Section 11.2 "Lane layers" / 11.6 "Lane linkage"):
  - A <road> may contain up to 2 <lanes> elements, distinguished by @layer
    ("permanent" | "temporary"); omitting @layer defaults to "permanent" (spec-stated
    default, applies both to <lanes>@layer and to <predecessor>/<successor>@layer).
  - Each <lanes> element owns its own independent <laneSection> and <laneOffset> lists.
  - <laneSection>@length is legal only on the temporary layer (permanent layer must
    cover the road implicitly/contiguously, cannot use @length -- XSD encodes this via
    an xs:assert that most XSD validators/tools do not enforce -> a genuine gap).
"""

import xml.etree.ElementTree as ET  # noqa: F401  (kept for parity with sibling modules; root is pre-parsed)

_EPS = 1e-6
_STOL = 1e-3  # s-coordinate comparison tolerance (meters)

# Lane @type values considered "driving-relevant" for road.lane_section.lane_long_zero_width
# -- matches esmini RoadManager's own Lane::LaneType::LANE_TYPE_ANY_DRIVING bitmask
# (RoadManager.hpp), i.e. lanes a vehicle actually routes/drives through. Structural /
# decorative types (border, shoulder, curb, sidewalk, median, restricted, stop, parking,
# special1-3, biking, tram, rail, roadWorks, none, ...) legitimately sit at width=0 for
# long stretches -- e.g. a <border> lane simply has no border along most of the road --
# and are NOT what the rule's "using lanes with a width of 0 ... should be avoided"
# guidance is warning about.
_DRIVING_LANE_TYPES = {
    "driving",
    "entry",
    "exit",
    "offRamp",
    "onRamp",
    "connectingRamp",
    "bidirectional",
}


# --------------------------------------------------------------------------------------
# small numeric / structural helpers
# --------------------------------------------------------------------------------------


def _fnum(x, default=0.0):
    try:
        return float(x)
    except (TypeError, ValueError):
        return default


def _poly(a, b, c, d, ds):
    return a + b * ds + c * ds * ds + d * ds * ds * ds


def _layer_of(lanes_elem):
    """Effective @layer of a <lanes> element ("" is not valid XSD but treat any
    falsy/missing value as the spec-stated default: permanent)."""
    return lanes_elem.get("layer") or "permanent"


def _lane_id(lane_elem):
    return lane_elem.get("id")


def _lane_sections_sorted(lanes_elem):
    """laneSection children sorted by s (float); tolerant of missing/bad @s."""
    secs = lanes_elem.findall("laneSection")
    return sorted(secs, key=lambda e: _fnum(e.get("s")))


def _section_length(road, lanes_elem, sec, idx, secs_sorted):
    """Effective length of a laneSection: explicit @length, else distance to the next
    section's s (same layer), else remaining road length. Returns None if unknown
    (missing road @length on an open-ended last section) -- callers must guard."""
    L = sec.get("length")
    if L is not None:
        return _fnum(L)
    s = _fnum(sec.get("s"))
    if idx + 1 < len(secs_sorted):
        return _fnum(secs_sorted[idx + 1].get("s")) - s
    rl = road.get("length")
    if rl is None:
        return None
    return _fnum(rl) - s


def _lane_width_at(lane_elem, ds):
    """Evaluate a left/right <lane>'s width polynomial at ds (offset from the start of
    its own laneSection). Returns None if the lane uses <border> (unsupported -- would
    need road-reference-line t-computation, out of scope) or defines no <width> at all.
    """
    if lane_elem.find("border") is not None:
        return None
    widths = lane_elem.findall("width")
    if not widths:
        return None
    recs = []
    for w in widths:
        recs.append(
            (
                _fnum(w.get("sOffset")),
                _fnum(w.get("a")),
                _fnum(w.get("b")),
                _fnum(w.get("c")),
                _fnum(w.get("d")),
            )
        )
    recs.sort(key=lambda r: r[0])
    applicable = recs[0]
    for r in recs:
        if r[0] <= ds + 1e-9:
            applicable = r
        else:
            break
    so, a, b, c, d = applicable
    local_ds = max(0.0, ds - so)
    return _poly(a, b, c, d, local_ds)


def _laneoffset_at(lanes_elem, s):
    """Evaluate the <laneOffset> polynomial active at absolute road s-coordinate s
    (0.0 if none defined -- no shift)."""
    recs = []
    for lo in lanes_elem.findall("laneOffset"):
        recs.append(
            (
                _fnum(lo.get("s")),
                _fnum(lo.get("a")),
                _fnum(lo.get("b")),
                _fnum(lo.get("c")),
                _fnum(lo.get("d")),
            )
        )
    if not recs:
        return 0.0
    recs.sort(key=lambda r: r[0])
    applicable = None
    for r in recs:
        if r[0] <= s + 1e-9:
            applicable = r
        else:
            break
    if applicable is None:
        return 0.0
    so, a, b, c, d = applicable
    return _poly(a, b, c, d, s - so)


def _perm_temp_layers(road):
    """Return (permanent_lanes_elem_or_None, temporary_lanes_elem_or_None)."""
    perm = None
    temp = None
    for c in road.findall("lanes"):
        layer = _layer_of(c)
        if layer == "permanent" and perm is None:
            perm = c
        elif layer == "temporary" and temp is None:
            temp = c
    return perm, temp


def _lane_group_width_sum(sec, side, ds_for_width, exclude_types=None):
    """Sum lane widths for a single lane group (@side: "left" or "right" -- per
    ASAM OpenDRIVE 1.9.0 Section 11.3 "Lane groups", <left>/<right> are the two
    lane-group elements of a <laneSection>; <center> carries no width) at offset
    ds_for_width within the section. `exclude_types`, if given, drops lanes whose
    @type is in that set from the sum (used by lane_group_width_temporary to drop
    lane type="border" specifically -- per ASAM's own definition a border lane is "a
    soft border at the edge of the road ... allowing the interpretation of a road as
    it is with no hard curbs", i.e. not a rigid physical constraint, so it routinely
    grows/shrinks during roadworks without representing a real drivable-envelope
    change. Confirmed against two official ASAM examples that both need this excluded
    to avoid a false positive for a different reason each: Ex_Lane_MultiLaneLayer.xodr
    (a border lane alone widens 0.75->2.3 while the driving lanes shrink slightly,
    inflating the raw per-side sum) and Ex_Motorway_roadworks_temporary_layer_lane_offset
    .xodr (a large lane-offset diversion; the permanent side's border width must NOT
    count as usable envelope headroom there either, or the arithmetic still balances
    by coincidence for the wrong reason). Restricting all the way down to
    _DRIVING_LANE_TYPES was tried and rejected: it fixes Ex_Lane_MultiLaneLayer but
    then *creates* a new false positive on Ex_Motorway (dropping its legitimate
    shoulder/stop width headroom on the permanent side). Returns (total_or_None, ok,
    n_lanes_counted) -- ok=False if any counted lane's width could not be evaluated
    (e.g. <border>-element-defined), in which case callers should skip the comparison
    rather than risk a false positive from an incomplete sum; n_lanes_counted lets
    callers detect an empty group (a side legitimately absent from one or both
    layers) and skip it too."""
    total = 0.0
    ok = True
    n = 0
    grp = sec.find(side)
    if grp is None:
        return 0.0, True, 0
    for lane in grp.findall("lane"):
        if exclude_types is not None and lane.get("type") in exclude_types:
            continue
        n += 1
        w = _lane_width_at(lane, ds_for_width)
        if w is None:
            ok = False
            continue
        total += w
    return total, ok, n


# --------------------------------------------------------------------------------------
# run_checks
# --------------------------------------------------------------------------------------


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    for rid, road in roads.items():
        lanes_elems = road.findall("lanes")
        perm, temp = _perm_temp_layers(road)
        road_len = road.get("length")
        road_len_v = _fnum(road_len) if road_len is not None else None

        # ---- road.lane_section.lane_sect_req -------------------------------------
        if not list(road.iter("laneSection")):
            flags.append(
                (
                    "road.lane_section.lane_sect_req",
                    f"road {rid} に laneSection が1つも定義されていない",
                    f"road {rid}",
                )
            )
            # nothing further to check structurally for this road's lanes
            continue

        # ---- road.lane.layer.layer_limits -----------------------------------------
        perm_count = sum(1 for c in lanes_elems if _layer_of(c) == "permanent")
        temp_count = sum(1 for c in lanes_elems if _layer_of(c) == "temporary")
        if perm_count != 1:
            flags.append(
                (
                    "road.lane.layer.layer_limits",
                    f"road {rid} の permanent lane層数={perm_count}（要求=ちょうど1）",
                    f"road {rid}",
                )
            )
        if temp_count > 1:
            flags.append(
                (
                    "road.lane.layer.layer_limits",
                    f"road {rid} の temporary lane層数={temp_count}（上限1）",
                    f"road {rid}",
                )
            )

        # ---- road.lane.layer.layer_mandatory_permanent -----------------------------
        if perm is None:
            flags.append(
                (
                    "road.lane.layer.layer_mandatory_permanent",
                    f"road {rid} に permanent lane層が存在しない（全s区間でlaneが欠落）",
                    f"road {rid}",
                )
            )
        else:
            perm_secs = _lane_sections_sorted(perm)
            if not perm_secs:
                flags.append(
                    (
                        "road.lane.layer.layer_mandatory_permanent",
                        f"road {rid} permanent層に laneSection が無い",
                        f"road {rid}",
                    )
                )
            else:
                first_s = _fnum(perm_secs[0].get("s"))
                if abs(first_s) > _STOL:
                    flags.append(
                        (
                            "road.lane.layer.layer_mandatory_permanent",
                            f"road {rid} permanent層の最初のlaneSection s={first_s:g} != 0"
                            f"（s=0〜{first_s:g}区間でpermanent laneが存在しない）",
                            f"road {rid} s=0",
                        )
                    )

        # ---- per-<lanes> (per layer) checks ----------------------------------------
        for lanes_elem in lanes_elems:
            layer = _layer_of(lanes_elem)
            secs_doc_order = lanes_elem.findall("laneSection")
            secs_sorted = _lane_sections_sorted(lanes_elem)

            # road.lane_section.elem_asc_order (document order must be non-decreasing s)
            prev_s = None
            for sec in secs_doc_order:
                s = _fnum(sec.get("s"))
                if prev_s is not None and s < prev_s - _EPS:
                    flags.append(
                        (
                            "road.lane_section.elem_asc_order",
                            f"road {rid} ({layer}層) laneSection s={s:g} が直前 s={prev_s:g} より小さい",
                            f"road {rid} s={s:g}",
                        )
                    )
                prev_s = s

            # road.lanes.lane_offset.elem_asc_order
            offs = lanes_elem.findall("laneOffset")
            prev_os = None
            for lo in offs:
                s = _fnum(lo.get("s"))
                if prev_os is not None and s < prev_os - _EPS:
                    flags.append(
                        (
                            "road.lanes.lane_offset.elem_asc_order",
                            f"road {rid} ({layer}層) laneOffset s={s:g} が直前 s={prev_os:g} より小さい",
                            f"road {rid} s={s:g}",
                        )
                    )
                prev_os = s

            # road.lanes.lane_offset.no_offset_if_border_defined
            # NOTE: must not write `any(lanes_elem.iter("border"))` -- ElementTree's
            # Element.__bool__ is defined by child *count*, not identity, so a
            # self-closed <border/> (no children, the common case) is falsy and would
            # silently vanish from an `any()` over the iterator. Count explicitly.
            if offs and next(lanes_elem.iter("border"), None) is not None:
                flags.append(
                    (
                        "road.lanes.lane_offset.no_offset_if_border_defined",
                        f"road {rid} ({layer}層) laneOffset とborder定義（幅記述）が併存",
                        f"road {rid}",
                    )
                )

            # road.lane.layer.center_lane_permanent
            if layer == "permanent":
                for sec in secs_doc_order:
                    s = sec.get("s")
                    center = sec.find("center")
                    clane = center.find("lane") if center is not None else None
                    if clane is None or clane.get("id") != "0":
                        flags.append(
                            (
                                "road.lane.layer.center_lane_permanent",
                                f"road {rid} permanent層 laneSection s={s} に center lane(id=0)が無い",
                                f"road {rid} s={s}",
                            )
                        )

            # road.lane.layer.length_only_temporary (permanent layer must not use @length)
            if layer == "permanent":
                for sec in secs_doc_order:
                    if sec.get("length") is not None:
                        flags.append(
                            (
                                "road.lane.layer.length_only_temporary",
                                f"road {rid} permanent層 laneSection s={sec.get('s')} が@lengthを使用"
                                "（temporary層限定の属性）",
                                f"road {rid} s={sec.get('s')}",
                            )
                        )

            # road.lane.layer.lane_phys_attr_temporary (temporary lanes: no height/material)
            if layer == "temporary":
                for lane in lanes_elem.iter("lane"):
                    lid = _lane_id(lane)
                    if lane.find("height") is not None:
                        flags.append(
                            (
                                "road.lane.layer.lane_phys_attr_temporary",
                                f"road {rid} temporary層 lane {lid} に <height> が存在",
                                f"road {rid} lane {lid}",
                            )
                        )
                    if lane.find("material") is not None:
                        flags.append(
                            (
                                "road.lane.layer.lane_phys_attr_temporary",
                                f"road {rid} temporary層 lane {lid} に <material> が存在",
                                f"road {rid} lane {lid}",
                            )
                        )

            # road.lane_section.valid_length (> 0, strictly -- XSD only enforces >= 0)
            for idx, sec in enumerate(secs_sorted):
                s = _fnum(sec.get("s"))
                length = _section_length(road, lanes_elem, sec, idx, secs_sorted)
                if length is not None and length <= _EPS:
                    flags.append(
                        (
                            "road.lane_section.valid_length",
                            f"road {rid} ({layer}層) laneSection s={s:g} の長さ={length:g} <= 0",
                            f"road {rid} s={s:g}",
                        )
                    )

            # road.lane_section.lanesec_length_limit_road (@length shall not exceed road end)
            if road_len_v is not None:
                for sec in secs_doc_order:
                    Lattr = sec.get("length")
                    if Lattr is None:
                        continue
                    s = _fnum(sec.get("s"))
                    Lval = _fnum(Lattr)
                    if s + Lval > road_len_v + _STOL:
                        flags.append(
                            (
                                "road.lane_section.lanesec_length_limit_road",
                                f"road {rid} laneSection s={s:g} +@length={Lval:g} = {s+Lval:g} "
                                f"> road長 {road_len_v:g}",
                                f"road {rid} s={s:g}",
                            )
                        )

            # road.lane_section.lane_long_zero_width (heuristic: an entire laneSection's
            # width polynomial is identically zero -- all coefficients zero, not just a
            # momentary sample -- and that section's span exceeds a documented threshold)
            ZERO_SPAN_THRESHOLD_M = 50.0
            for idx, sec in enumerate(secs_sorted):
                length = _section_length(road, lanes_elem, sec, idx, secs_sorted)
                if length is None or length <= ZERO_SPAN_THRESHOLD_M:
                    continue
                s = _fnum(sec.get("s"))
                for side in ("left", "right"):
                    grp = sec.find(side)
                    if grp is None:
                        continue
                    for lane in grp.findall("lane"):
                        if lane.get("type") not in _DRIVING_LANE_TYPES:
                            continue  # structural/decorative lane type -- 0-width is normal
                        widths = lane.findall("width")
                        if not widths or lane.find("border") is not None:
                            continue
                        if all(
                            abs(_fnum(w.get("a"))) < _EPS
                            and abs(_fnum(w.get("b"))) < _EPS
                            and abs(_fnum(w.get("c"))) < _EPS
                            and abs(_fnum(w.get("d"))) < _EPS
                            for w in widths
                        ):
                            flags.append(
                                (
                                    "road.lane_section.lane_long_zero_width",
                                    f"road {rid} lane {_lane_id(lane)} が s={s:g}〜{s+length:g}"
                                    f"（{length:g}m）にわたり幅0",
                                    f"road {rid} s={s:g} lane {_lane_id(lane)}",
                                )
                            )

            # NOTE: an earlier revision also flagged lanes with a present-but-empty
            # <link/> (no predecessor/successor children) under the "has no link" half
            # of this rule's prose. Self-test against the official ASAM corpus showed
            # that pattern is pervasive, idiomatic authoring (many tools always emit an
            # empty <link/> placeholder) -- 907 flags across the calibration set, i.e.
            # not a real violation signal. Dropped; only the junction-boundary clause
            # below (which fires 0 times on the official set) is implemented.

            # road.lane.link.multiple_connections (2nd normative sentence only: non-zero
            # width required at any declared connection point; the 1st sentence -- multi-
            # link required for abrupt splits/merges -- needs topological/geometric split
            # detection, out of structural-only scope, see rule status notes)
            #
            # Restricted to drivable lane types (_DRIVING_LANE_TYPES) and to non-junction
            # roads: border/shoulder lanes routinely sit at width 0 with tool-emitted
            # links (T4_multiconn_FP_border_zero.xodr, resources/xodr/parking_demo.xodr
            # road 2 lane 2), and a junction connecting road's lane <link> resolves
            # through the junction's <connection>/<laneLink> topology, not this rule's
            # direct-neighbor semantics (E1_connroad_multiconn.xodr). A lane that tapers
            # smoothly to/from 0 exactly at a section boundary is also a legitimate
            # lane begin/end (lane-drop / lane-add) -- confirmed against a real smooth
            # lane-drop (DriverScript/resources/xodr/soderleden.xodr road 0 lane -3) and
            # the official Ex_Motorway_roadworks_temporary_layer_lane_offset.xodr (road 8
            # lane 2 opens from 0). Only flag when the lane's width polynomial is
            # identically zero across the WHOLE section (never actually has width) yet
            # still declares a link -- a taper has at least one non-zero coefficient.
            if road.get("junction", "-1") == "-1":
                for idx, sec in enumerate(secs_sorted):
                    s = _fnum(sec.get("s"))
                    length = _section_length(road, lanes_elem, sec, idx, secs_sorted)
                    for side in ("left", "right"):
                        grp = sec.find(side)
                        if grp is None:
                            continue
                        for lane in grp.findall("lane"):
                            if lane.get("type") not in _DRIVING_LANE_TYPES:
                                continue
                            link = lane.find("link")
                            if link is None:
                                continue
                            if lane.find("border") is not None:
                                continue  # width via <border> -- cannot evaluate safely
                            widths = lane.findall("width")
                            if not widths:
                                continue
                            const_zero = all(
                                abs(_fnum(w.get("a"))) < _EPS
                                and abs(_fnum(w.get("b"))) < _EPS
                                and abs(_fnum(w.get("c"))) < _EPS
                                and abs(_fnum(w.get("d"))) < _EPS
                                for w in widths
                            )
                            if not const_zero:
                                continue  # tapers to/from non-zero -- legitimate begin/end
                            lid = _lane_id(lane)
                            if link.findall("predecessor"):
                                flags.append(
                                    (
                                        "road.lane.link.multiple_connections",
                                        f"road {rid} lane {lid} は predecessorリンクを持つが"
                                        f"区間全体(s={s:g}〜)で幅が恒常的に0",
                                        f"road {rid} s={s:g} lane {lid}",
                                    )
                                )
                            if link.findall("successor"):
                                end_s = s + length if length is not None else s
                                flags.append(
                                    (
                                        "road.lane.link.multiple_connections",
                                        f"road {rid} lane {lid} は successorリンクを持つが"
                                        f"区間全体(s={s:g}〜{end_s:g})で幅が恒常的に0",
                                        f"road {rid} s={end_s:g} lane {lid}",
                                    )
                                )

        # NOTE: road.lane.link.no_link -- both structural readings tried during
        # development produced real false positives against the official ASAM
        # calibration set: (a) flagging any lane with a present-but-empty <link/>
        # (no predecessor/successor) fired 907 times there -- pervasive, idiomatic
        # tool output, not a defect signal; (b) flagging any predecessor/successor
        # declared on a lane whose road-level link at that edge is elementType=
        # "junction" fired repeatedly on GT_esmini/test/odr_fixtures/official/examples/
        # Ex_Slip_Lane/Ex_Slip_Lane.xodr, an official example that legitimately
        # declares direct lane-level links right at a junction boundary (a short
        # "slip lane" whose connection to the junction is unambiguous). Distinguishing
        # a legitimate unambiguous junction-edge lane link from one that should have
        # been omitted requires resolving the neighboring junction's <connection>/
        # <laneLink> topology (how many candidate lanes could this id plausibly mean),
        # i.e. the same judgment call as the sibling rule road.lane.link.use_junctions
        # -- not decidable from this lane's own attributes alone. See status report
        # (classified gap_ambiguous, no code path here).

        # ---- road.lane.layer.lane_group_width_temporary ----------------------------
        # "For each lane group" (ASAM OpenDRIVE 1.9.0 Section 11.3 "Lane groups": the
        # <left> and <right> elements of a <laneSection> are the two lane groups;
        # <center> carries no width) -- checked SEPARATELY per side, not as a combined
        # left+right total (a left-side excess cannot be offset by right-side slack).
        # type="border" lanes are excluded from the sum (see _lane_group_width_sum
        # docstring: a border is explicitly a soft, non-rigid edge per ASAM's own lane
        # type definition). Sampled at the *start* s of each temporary laneSection
        # (exact, no approximation at that point).
        #
        # Geometric extent, not a raw width+offset sum: <laneOffset> shifts the whole
        # lane-section reference line in +t. The RIGHT group's outward edge (distance
        # from the road reference line) is therefore sum(width) - offset (a larger
        # offset shifts the reference line toward +t, i.e. *away* from the right edge,
        # shrinking its reach), while the LEFT group's outward edge is sum(width) +
        # offset. Comparing raw width+offset sums (the original implementation)
        # conflated the two and mis-signed the comparison whenever perm/temp offsets
        # differed -- confirmed false positive on T2_width_FP_offset_sign.xodr, which
        # also demonstrates the companion bug of evaluating a side with zero lanes on
        # both layers (offset-only "width" is meaningless there -- skipped below).
        if perm is not None and temp is not None:
            perm_secs = _lane_sections_sorted(perm)
            temp_secs = _lane_sections_sorted(temp)
            for t_idx, t_sec in enumerate(temp_secs):
                target_s = _fnum(t_sec.get("s"))
                # find covering permanent section (largest perm s <= target_s)
                p_sec = None
                p_idx = None
                for i, ps in enumerate(perm_secs):
                    if _fnum(ps.get("s")) <= target_s + 1e-9:
                        p_sec = ps
                        p_idx = i
                    else:
                        break
                if p_sec is None:
                    continue  # unresolvable (already flagged by layer_mandatory_permanent)
                p_start = _fnum(p_sec.get("s"))
                temp_off = _laneoffset_at(temp, target_s)
                perm_off = _laneoffset_at(perm, target_s)
                for side in ("left", "right"):
                    temp_total, temp_ok, temp_n = _lane_group_width_sum(
                        t_sec, side, 0.0, {"border"}
                    )
                    perm_total, perm_ok, perm_n = _lane_group_width_sum(
                        p_sec, side, target_s - p_start, {"border"}
                    )
                    if not (temp_ok and perm_ok):
                        continue  # a <border>-defined lane in play -> cannot compute safely
                    if temp_n == 0 and perm_n == 0:
                        continue  # side has no non-border lanes on either layer -- nothing to compare
                    if side == "right":
                        temp_extent = temp_total - temp_off
                        perm_extent = perm_total - perm_off
                    else:
                        temp_extent = temp_total + temp_off
                        perm_extent = perm_total + perm_off
                    if temp_extent > perm_extent + 1e-3:
                        flags.append(
                            (
                                "road.lane.layer.lane_group_width_temporary",
                                f"road {rid} s={target_s:g} ({side}側, border型除く): "
                                f"temporary層 幅(offset込み)={temp_extent:.3f} > "
                                f"permanent層 幅(offset込み)={perm_extent:.3f}",
                                f"road {rid} s={target_s:g} {side}",
                            )
                        )

        # ---- road.lane.link.temporary_layer_section_link_permanent -----------------
        # At the aggregate start/end of the temporary layer's lane-section run, every
        # drivable (@type="driving") non-zero-width lane must be linked to a permanent
        # lane -- checked bidirectionally: either this lane's own predecessor/successor
        # explicitly (or by omission-default) targets layer="permanent", OR the matching
        # permanent lane (same id, in the permanent laneSection whose s coincides with
        # this boundary) declares the reciprocal successor/predecessor with
        # layer="temporary" pointing back at this lane. (Both authoring directions are
        # seen in the official ASAM example set.)
        if perm is not None and temp is not None:
            temp_secs = _lane_sections_sorted(temp)
            perm_secs = _lane_sections_sorted(perm)
            if temp_secs:

                def _perm_section_at(target_s):
                    for ps in perm_secs:
                        if abs(_fnum(ps.get("s")) - target_s) < _STOL:
                            return ps
                    return None

                def _perm_reciprocal(perm_sec, lid, want_tag):
                    """True if perm_sec has a lane `lid` whose <link> contains a
                    `want_tag` (predecessor/successor) entry with layer=='temporary'
                    and matching id."""
                    if perm_sec is None:
                        return False
                    for side in ("left", "right"):
                        grp = perm_sec.find(side)
                        if grp is None:
                            continue
                        for plane in grp.findall("lane"):
                            if _lane_id(plane) != lid:
                                continue
                            plink = plane.find("link")
                            if plink is None:
                                continue
                            for e in plink.findall(want_tag):
                                if e.get("layer") == "temporary" and e.get("id") == lid:
                                    return True
                    return False

                first_sec = temp_secs[0]
                first_s = _fnum(first_sec.get("s"))
                perm_at_first = _perm_section_at(first_s)
                for side in ("left", "right"):
                    grp = first_sec.find(side)
                    if grp is None:
                        continue
                    for lane in grp.findall("lane"):
                        if lane.get("type") != "driving":
                            continue
                        w0 = _lane_width_at(lane, 0.0)
                        if w0 is None or abs(w0) < _EPS:
                            continue
                        lid = _lane_id(lane)
                        link = lane.find("link")
                        preds = link.findall("predecessor") if link is not None else []
                        own_ok = any(
                            (p.get("layer") or "permanent") == "permanent"
                            for p in preds
                        )
                        if not own_ok and not _perm_reciprocal(
                            perm_at_first, lid, "successor"
                        ):
                            flags.append(
                                (
                                    "road.lane.link.temporary_layer_section_link_permanent",
                                    f"road {rid} temporary層区間開始(s={first_s:g}) drivable lane {lid}"
                                    f"(幅{w0:.2f}) がpermanent層にリンクされていない",
                                    f"road {rid} s={first_s:g} lane {lid}",
                                )
                            )

                last_idx = len(temp_secs) - 1
                last_sec = temp_secs[last_idx]
                last_len = _section_length(road, temp, last_sec, last_idx, temp_secs)
                if last_len is not None:
                    last_end_s = _fnum(last_sec.get("s")) + last_len
                    perm_at_last = _perm_section_at(last_end_s)
                    for side in ("left", "right"):
                        grp = last_sec.find(side)
                        if grp is None:
                            continue
                        for lane in grp.findall("lane"):
                            if lane.get("type") != "driving":
                                continue
                            wend = _lane_width_at(lane, last_len)
                            if wend is None or abs(wend) < _EPS:
                                continue
                            lid = _lane_id(lane)
                            link = lane.find("link")
                            succs = (
                                link.findall("successor") if link is not None else []
                            )
                            own_ok = any(
                                (s2.get("layer") or "permanent") == "permanent"
                                for s2 in succs
                            )
                            if not own_ok and not _perm_reciprocal(
                                perm_at_last, lid, "predecessor"
                            ):
                                flags.append(
                                    (
                                        "road.lane.link.temporary_layer_section_link_permanent",
                                        f"road {rid} temporary層区間終了(s={last_end_s:g}) "
                                        f"drivable lane {lid}(幅{wend:.2f}) がpermanent層にリンクされていない",
                                        f"road {rid} s={last_end_s:g} lane {lid}",
                                    )
                                )

        # ---- road.lane_section.new_lanesec_link_temp_to_perm ------------------------
        # Wherever a permanent<->temporary cross-layer lane link is actually declared,
        # the permanent layer must own a laneSection boundary exactly at that s (a lane's
        # predecessor/successor is only ever evaluable at its own section's edge, so
        # placing a cross-layer link mid-section is only possible by first splitting the
        # permanent layer there). Boundaries that coincide with the road's own start/end
        # are exempt: the permanent layer's first/last section inherently reaches there
        # without requiring a *new* split.
        #
        # Two independent bugs fixed here (see fixbriefs/road_lane_layer_link_section.md):
        # (1) A cross-layer link must actually declare the OPPOSITE @layer -- a temp lane
        #     linking temp<->temp (@layer="temporary" on its own predecessor/successor,
        #     e.g. T1_newls_FP_temp_to_temp_only.xodr) is an ordinary same-layer link, not
        #     evidence a permanent split is required. The original code only tested
        #     `link.find(tag) is not None`, ignoring @layer entirely.
        # (2) The START and END boundaries are independent conditions: a real cross-link
        #     declared only at the start must not force a check at the end too
        #     (T1c_newls_FP_end_boundary.xodr). The original code OR'd all evidence into
        #     one global `cross_declared` flag and then checked both boundaries whenever
        #     it was true.
        if perm is not None and temp is not None:
            temp_secs = _lane_sections_sorted(temp)
            perm_secs = _lane_sections_sorted(perm)
            if temp_secs:
                perm_s_values = [_fnum(ps.get("s")) for ps in perm_secs]

                def _layer_cross_declared_on(sec, tags, want_layer):
                    """True if `sec` declares a predecessor/successor (any tag in `tags`)
                    whose @layer equals `want_layer` -- i.e. an explicit cross-layer link,
                    not an ordinary same-layer (temp<->temp / perm<->perm) one."""
                    for side in ("left", "right"):
                        grp = sec.find(side)
                        if grp is None:
                            continue
                        for lane in grp.findall("lane"):
                            link = lane.find("link")
                            if link is None:
                                continue
                            for tag in tags:
                                for e in link.findall(tag):
                                    if e.get("layer") == want_layer:
                                        return True
                    return False

                first_sec = temp_secs[0]
                first_s = _fnum(first_sec.get("s"))
                last_idx = len(temp_secs) - 1
                last_sec = temp_secs[last_idx]
                last_len = _section_length(road, temp, last_sec, last_idx, temp_secs)
                last_end_s = (
                    _fnum(last_sec.get("s")) + last_len
                    if last_len is not None
                    else None
                )

                # START boundary: evidence is either the temp section's own predecessor
                # declaring layer="permanent", OR a permanent laneSection AT s=first_s
                # declaring a successor with layer="temporary" pointing back.
                start_cross = _layer_cross_declared_on(
                    first_sec, ("predecessor",), "permanent"
                )
                if not start_cross:
                    for ps in perm_secs:
                        if abs(_fnum(ps.get("s")) - first_s) < _STOL:
                            if _layer_cross_declared_on(
                                ps, ("successor",), "temporary"
                            ):
                                start_cross = True
                                break

                # END boundary: symmetric -- temp's own successor declaring
                # layer="permanent", OR a permanent laneSection AT s=last_end_s declaring
                # a predecessor with layer="temporary".
                end_cross = False
                if last_end_s is not None:
                    end_cross = _layer_cross_declared_on(
                        last_sec, ("successor",), "permanent"
                    )
                    if not end_cross:
                        for ps in perm_secs:
                            if abs(_fnum(ps.get("s")) - last_end_s) < _STOL:
                                if _layer_cross_declared_on(
                                    ps, ("predecessor",), "temporary"
                                ):
                                    end_cross = True
                                    break

                if start_cross and (road_len_v is None or (first_s > _STOL)):
                    if not any(abs(ps - first_s) < _STOL for ps in perm_s_values):
                        flags.append(
                            (
                                "road.lane_section.new_lanesec_link_temp_to_perm",
                                f"road {rid} temporary層開始 s={first_s:g} と一致する permanent層"
                                "laneSection境界が無い（cross-layer link用の新セクション未定義）",
                                f"road {rid} s={first_s:g}",
                            )
                        )
                if (
                    end_cross
                    and last_end_s is not None
                    and (road_len_v is None or last_end_s < road_len_v - _STOL)
                ):
                    if not any(abs(ps - last_end_s) < _STOL for ps in perm_s_values):
                        flags.append(
                            (
                                "road.lane_section.new_lanesec_link_temp_to_perm",
                                f"road {rid} temporary層終了 s={last_end_s:g} と一致する permanent層"
                                "laneSection境界が無い（cross-layer link用の新セクション未定義）",
                                f"road {rid} s={last_end_s:g}",
                            )
                        )

    return flags
