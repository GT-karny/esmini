"""Gap-rule checkers for category-group "road_object_core" (Annex F, road.object.*).

Covers <object>, <bridge>, <tunnel>, <objectReference> elements under <road><objects>:
shape mutual-exclusivity, required-by-rule attributes not required by XSD
(@orientation, @type on <object>; @s/@t sanity), outline id uniqueness, border
useCompleteOutline/cornerReference consistency, fromLane<=toLane on <validity>
children of <bridge>/<tunnel>/<objectReference>/<object>, and the LHT/RHT lane-sign
consistency rules for <object><validity> vs. the parent's @orientation.

Pure stdlib xml.etree.ElementTree, no re-parsing inside run_checks (root is passed in).
See scratchpad/checks_gap/CONTRACT.md for the module contract this file implements.
"""


def _fnum(x):
    """Best-effort float parse; None on failure (never raises)."""
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def _fint(x):
    """Best-effort int parse; None on failure (never raises)."""
    try:
        return int(x)
    except (TypeError, ValueError):
        return None


def _is_true(v):
    """t_bool truthiness for an *explicit* attribute string. None (attribute absent)
    is NOT coerced to a default here -- see useCompleteOutline discussion in the
    docstring of _check_borders_use_complete_outline for why."""
    return v in ("true", "1")


def _traffic_hand(road):
    """<road>@rule, default RHT per XSD documentation (rule missing => RHT assumed)."""
    r = road.get("rule") if road is not None else None
    return r if r in ("RHT", "LHT") else "RHT"


def _iter_road_objects(roads):
    """Yield (rid, road, object_elem) for every <object> under road/objects/object."""
    for rid, road in roads.items():
        objs = road.find("objects")
        if objs is None:
            continue
        for obj in objs.findall("object"):
            yield rid, road, obj


def _iter_road_children(roads, tag):
    """Yield (rid, road, elem) for every direct <objects>/<tag> child (bridge/tunnel/
    objectReference), which is where the schema places them (siblings of <object>)."""
    for rid, road in roads.items():
        objs = road.find("objects")
        if objs is None:
            continue
        for e in objs.findall(tag):
            yield rid, road, e


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    # ---- road.object.circular_vs_angular ------------------------------------
    # XSD assert: not(@radius or @width or @length) or (@radius and not(@width or
    # @length)) or (@width and @length and not(@radius)). We check the mutual-
    # exclusivity half only (radius together with width/length): that's the part
    # the Annex F rule text actually states ("possibilities are mutually
    # exclusive"); a bare @width without @length (or vice versa) is a separate,
    # unassigned XSD-shape concern, not this rule.
    for rid, road, obj in _iter_road_objects(roads):
        oid = obj.get("id")
        has_radius = obj.get("radius") is not None
        has_wl = obj.get("width") is not None or obj.get("length") is not None
        if has_radius and has_wl:
            flags.append((
                "road.object.circular_vs_angular",
                f"object id={oid} が @radius と @width/@length を同時に指定（円形/角形は排他）",
                f"road {rid} object id={oid}",
            ))

        # ---- road.object.orientation ----------------------------------------
        if obj.get("orientation") is None:
            flags.append((
                "road.object.orientation",
                f"object id={oid} に @orientation が未指定（有効方向の明示が必須）",
                f"road {rid} object id={oid}",
            ))

        # ---- road.object.s_t_coords -------------------------------------------
        s_val, t_val = _fnum(obj.get("s")), _fnum(obj.get("t"))
        if s_val is None or t_val is None:
            flags.append((
                "road.object.s_t_coords",
                f"object id={oid} の @s/@t が欠落または数値でない（s={obj.get('s')!r}, t={obj.get('t')!r}）",
                f"road {rid} object id={oid}",
            ))

        # ---- road.object.type_attr -------------------------------------------
        if obj.get("type") is None:
            flags.append((
                "road.object.type_attr",
                f"object id={oid} に @type が未指定",
                f"road {rid} object id={oid}",
            ))

        # ---- road.object.borders.different_outlineids -------------------------
        outlines_wrap = obj.find("outlines")
        if outlines_wrap is not None:
            seen = {}
            for outl in outlines_wrap.findall("outline"):
                oid_val = outl.get("id")
                if oid_val is None:
                    continue
                seen.setdefault(oid_val, 0)
                seen[oid_val] += 1
            for oid_val, cnt in seen.items():
                if cnt > 1:
                    flags.append((
                        "road.object.borders.different_outlineids",
                        f"object id={oid} の <outlines> 内で outline id={oid_val} が {cnt} 回重複",
                        f"road {rid} object id={oid}",
                    ))

        # ---- road.object.borders.useCompleteOutline_true -----------------------
        # NOTE: the schema doc says omitted @useCompleteOutline defaults to "true",
        # but the official ASAM calibration set itself uses an *omitted*
        # @useCompleteOutline together with explicit <cornerReference> children
        # (UC_2Lane-RoundAbout-3Arms.xodr) -- i.e. real authors treat "omitted" as
        # "not necessarily complete", not as an enforced default=true. We therefore
        # only fire on an *explicit* useCompleteOutline="true"/"1", matching the
        # calibration set's own behaviour (zero false positives there).
        borders = obj.find("borders")
        if borders is not None:
            for border in borders.findall("border"):
                uc = border.get("useCompleteOutline")
                crefs = border.findall("cornerReference")
                if _is_true(uc) and crefs:
                    flags.append((
                        "road.object.borders.useCompleteOutline_true",
                        f"object id={oid} border outlineId={border.get('outlineId')} は "
                        f"useCompleteOutline=true なのに cornerReference が {len(crefs)} 件定義",
                        f"road {rid} object id={oid}",
                    ))

        # ---- road.object.validty.* (fromLane<=toLane, orientation subset) -----
        orientation = obj.get("orientation")
        hand = _traffic_hand(road)
        for v in obj.findall("validity"):
            fl, tl = _fint(v.get("fromLane")), _fint(v.get("toLane"))
            if fl is None or tl is None:
                continue
            if fl > tl:
                flags.append((
                    "road.object.validty.from_lower_equal_to",
                    f"object id={oid} validity fromLane={fl} > toLane={tl}",
                    f"road {rid} object id={oid}",
                ))
                continue  # sign-based checks below assume a well-ordered range
            neg_present = min(fl, tl) < 0
            pos_present = max(fl, tl) > 0
            spans_both = neg_present and pos_present

            if orientation in ("+", "-") and spans_both:
                flags.append((
                    "road.object.validty.check_parent_orientation",
                    f"object id={oid} @orientation={orientation!r} だが validity "
                    f"[{fl},{tl}] が正負両方のレーンidにまたがる（親orientationの部分集合であるべき）",
                    f"road {rid} object id={oid}",
                ))

            if orientation in ("+", "-"):
                if hand == "RHT":
                    expect_neg = orientation == "+"
                else:  # LHT
                    expect_neg = orientation == "-"
                rule_name = ("road.object.validty.right_hand_traffic_lane_ids"
                             if hand == "RHT" else
                             "road.object.validty.left_hand_traffic_lane_ids")
                if expect_neg and pos_present:
                    flags.append((
                        rule_name,
                        f"object id={oid} ({hand}) @orientation={orientation!r} は負のレーンidのみ許容だが "
                        f"validity [{fl},{tl}] に正idを含む",
                        f"road {rid} object id={oid}",
                    ))
                elif (not expect_neg) and neg_present:
                    flags.append((
                        rule_name,
                        f"object id={oid} ({hand}) @orientation={orientation!r} は正のレーンidのみ許容だが "
                        f"validity [{fl},{tl}] に負idを含む",
                        f"road {rid} object id={oid}",
                    ))

    # ---- road.object.bridges.* -----------------------------------------------
    for rid, road, bridge in _iter_road_children(roads, "bridge"):
        bid = bridge.get("id")
        if bridge.get("type") is None:
            flags.append((
                "road.object.bridges.type_definition",
                f"bridge id={bid} に @type が未指定",
                f"road {rid} bridge id={bid}",
            ))
        for v in bridge.findall("validity"):
            fl, tl = _fint(v.get("fromLane")), _fint(v.get("toLane"))
            if fl is not None and tl is not None and fl > tl:
                flags.append((
                    "road.object.bridges.from_lower_equal_to",
                    f"bridge id={bid} validity fromLane={fl} > toLane={tl}",
                    f"road {rid} bridge id={bid}",
                ))

    # ---- road.object.tunnels.* -----------------------------------------------
    for rid, road, tunnel in _iter_road_children(roads, "tunnel"):
        tid = tunnel.get("id")
        if tunnel.get("type") is None:
            flags.append((
                "road.object.tunnels.type_definition",
                f"tunnel id={tid} に @type が未指定",
                f"road {rid} tunnel id={tid}",
            ))
        for v in tunnel.findall("validity"):
            fl, tl = _fint(v.get("fromLane")), _fint(v.get("toLane"))
            if fl is not None and tl is not None and fl > tl:
                flags.append((
                    "road.object.tunnels.from_lower_equal_to",
                    f"tunnel id={tid} validity fromLane={fl} > toLane={tl}",
                    f"road {rid} tunnel id={tid}",
                ))

    # ---- road.object.reference.from_lower_equal_to (<objectReference>) -------
    for rid, road, oref in _iter_road_children(roads, "objectReference"):
        refid = oref.get("id")
        for v in oref.findall("validity"):
            fl, tl = _fint(v.get("fromLane")), _fint(v.get("toLane"))
            if fl is not None and tl is not None and fl > tl:
                flags.append((
                    "road.object.reference.from_lower_equal_to",
                    f"objectReference id={refid} validity fromLane={fl} > toLane={tl}",
                    f"road {rid} objectReference id={refid}",
                ))

    return flags
