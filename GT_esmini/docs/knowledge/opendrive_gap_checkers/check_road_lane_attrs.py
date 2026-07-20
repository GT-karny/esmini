"""Gap-rule checkers for category-group "road_lane_attrs" (Annex F rules not covered by
qc-opendrive). Pure xml.etree.ElementTree, stdlib only. See CONTRACT.md for the shared
module contract (run_checks signature, corpus/origin methodology, flag tuple shape).

Implements (bare rule_name -> what is checked):
  road.lane.access.center_lane_no_acc_rule     : center lane (id=0) has no <access> children
  road.lane.access.elem_asc_order              : <access> sOffset ascending per lane
  road.lane.border.elem_asc_order              : <border> sOffset ascending per lane
  road.lane.border.exclusive_offset_border     : <border> not used together with <laneOffset> (road-level)
  road.lane.border.exclusive_width_border      : <border> and <width> not mixed across lanes of one laneSection
  road.lane.height.center_lane_no_height       : center lane has no <height> children
  road.lane.height.elem_asc_order              : <height> sOffset ascending per lane
  road.lane.lane_properties.elem_asc_order     : per spec section 11.7 "Lane geometry" (whose intro names
                                                  width/border/height as its three "lane geometry" element
                                                  types and states this exact rule once, generically, before
                                                  drilling into each), the union of border/width/height
                                                  elem_asc_order -- same underlying invariant as those three,
                                                  reported under its own F.6.12.20 "lane_properties" rule_name.
  road.lane.material.center_lane_no_material   : center lane has no <material> children
  road.lane.material.elem_asc_order            : <material> sOffset ascending per lane
  road.lane.road_mark.elem_asc_order           : <roadMark> sOffset ascending per lane (incl. center)
  road.lane.rule.elem_asc_order                : <rule> sOffset ascending per lane
  road.lane.speed.center_lane_no_spd_lmt       : center lane has no <speed> children
  road.lane.speed.elem_asc_order               : <speed> sOffset ascending per lane
  road.lane.width.elem_asc_order               : <width> sOffset ascending per lane
  road.lane.width.lane_width_validity          : width(ds) = a+b*ds+c*ds^2+d*ds^3 >= 0 over each
                                                  width element's validity interval (closed-form cubic
                                                  minimum over [0, interval_length], not a geometry/coord
                                                  evaluation -- pure polynomial algebra on the attributes).
  road.lane.width.no_width_with_border         : same underlying check as border.exclusive_width_border,
                                                  reported under its own (1.9.0) rule_name too.
  road.lane.width.width_defined_whole_section  : a lane that uses <width> must have one at sOffset=0

Classified away from implemented_gt (see reason field in the reporting layer):
  road.lane.road_mark.only_outer      -> gap_ambiguous
  road.lane.road_mark.position_outer_half -> gap_geometry_math
"""

import xml.etree.ElementTree as ET  # noqa: F401  (kept for parity with sibling modules / type hints)


EPS = 1e-6


def _fnum(x, d=0.0):
    try:
        return float(x)
    except (TypeError, ValueError):
        return d


def _lane_id(lane):
    return lane.get("id")


def _iter_lr_lanes(laneSection):
    """yield (side, lane) for left/right lanes of one laneSection (center excluded)."""
    for side in ("left", "right"):
        grp = laneSection.find(side)
        if grp is None:
            continue
        for lane in grp.findall("lane"):
            yield side, lane


def _asc_order_flags(rule_name, road, rid, tag, include_center=False):
    """Generic '<tag> elements shall be defined in ascending order according to the
    s-coordinate' check, scoped per-lane (each lane's own <tag> series is independent)."""
    flags = []
    lanes_el = road.find("lanes")
    if lanes_el is None:
        return flags
    for ls in lanes_el.findall("laneSection"):
        s_ls = ls.get("s")
        scoped = list(_iter_lr_lanes(ls))
        if include_center:
            center = ls.find("center")
            if center is not None:
                scoped += [("center", lane) for lane in center.findall("lane")]
        for side, lane in scoped:
            prev = None
            for el in lane.findall(tag):
                v = _fnum(el.get("sOffset"))
                if prev is not None and v < prev - EPS:
                    flags.append((
                        rule_name,
                        f"{side} lane id={_lane_id(lane)} <{tag}> sOffset={v:g} が直前の{prev:g}より小さい（降順）",
                        f"road {rid} s={s_ls}",
                    ))
                prev = v
    return flags


def _center_no_child_flags(rule_name, road, rid, tag):
    flags = []
    lanes_el = road.find("lanes")
    if lanes_el is None:
        return flags
    for ls in lanes_el.findall("laneSection"):
        center = ls.find("center")
        if center is None:
            continue
        for lane in center.findall("lane"):
            elems = lane.findall(tag)
            if elems:
                flags.append((
                    rule_name,
                    f"center lane(id={_lane_id(lane)}) に <{tag}> が {len(elems)}件定義されている",
                    f"road {rid} s={ls.get('s')}",
                ))
    return flags


def _exclusive_offset_border_flags(road, rid):
    """road.lane.border.exclusive_offset_border: <border> shall not be used together with
    <laneOffset> (both live under <lanes>; scoping the search to that subtree keeps this
    distinct from the unrelated <object>/<borders>/<border> element)."""
    flags = []
    lanes_el = road.find("lanes")
    if lanes_el is None:
        return flags
    laneoffsets = lanes_el.findall("laneOffset")
    if not laneoffsets:
        return flags
    for ls in lanes_el.findall("laneSection"):
        s_ls = ls.get("s")
        for side, lane in _iter_lr_lanes(ls):
            borders = lane.findall("border")
            if borders:
                flags.append((
                    "road.lane.border.exclusive_offset_border",
                    f"road に laneOffset {len(laneoffsets)}件あり、かつ {side} lane id={_lane_id(lane)} に "
                    f"<border> {len(borders)}件（併用不可）",
                    f"road {rid} s={s_ls}",
                ))
    return flags


def _border_width_exclusive_flags(road, rid):
    """road.lane.border.exclusive_width_border + road.lane.width.no_width_with_border: two
    Annex F rule_names for the identical constraint -- <border> and <width> shall not be
    mixed across the lanes of the same lane group (laneSection's left+right lanes)."""
    flags = []
    lanes_el = road.find("lanes")
    if lanes_el is None:
        return flags
    for ls in lanes_el.findall("laneSection"):
        s_ls = ls.get("s")
        border_lanes = []
        width_lanes = []
        for side, lane in _iter_lr_lanes(ls):
            lid = _lane_id(lane)
            if lane.find("border") is not None:
                border_lanes.append(f"{side}:{lid}")
            if lane.find("width") is not None:
                width_lanes.append(f"{side}:{lid}")
        if border_lanes and width_lanes:
            detail = (f"border使用lane {border_lanes} と width使用lane {width_lanes} が同一laneSection内に混在")
            loc = f"road {rid} s={s_ls}"
            flags.append(("road.lane.border.exclusive_width_border", detail, loc))
            flags.append(("road.lane.width.no_width_with_border", detail, loc))
    return flags


def _lane_properties_asc_order_flags(road, rid):
    """road.lane.lane_properties.elem_asc_order: this is the generic rule stated once at
    the top of spec section 11.7 "Lane geometry" ("Lane geometries of identical types
    shall be defined in ascending order"), whose intro explicitly enumerates the three
    element types it covers: "Examples of lane geometry are lane width, lane border, and
    lane height." (11_07_lane_geometry.html). So this is the union of the width/border/
    height elem_asc_order checks (border+width being the per-lane shape choice, height
    being independently usable alongside either), reported under the F.6.12.20
    'lane_properties' rule_name. NOTE: do not extend this to material/speed/access/rule --
    those live under the separate "11.8 Additional lane properties" section and already
    have their own independent elem_asc_order rules; they are not "lane geometry"."""
    flags = []
    for tag in ("border", "width", "height"):
        flags += _asc_order_flags("road.lane.lane_properties.elem_asc_order", road, rid, tag)
    return flags


def _cubic_min_on_interval(a, b, c, d, length):
    """Minimum of f(x) = a + b*x + c*x^2 + d*x^3 over the closed interval x in [0, length].
    Pure polynomial algebra (no coordinate-frame / geometry evaluation): endpoints plus any
    stationary point(s) of the derivative that fall inside the interval."""
    candidates = [0.0, length]
    if abs(d) < 1e-12:
        if abs(c) > 1e-12:
            xc = -b / (2.0 * c)
            if 0.0 <= xc <= length:
                candidates.append(xc)
    else:
        A, B, C = 3.0 * d, 2.0 * c, b
        disc = B * B - 4.0 * A * C
        if disc >= 0:
            sq = disc ** 0.5
            for xc in ((-B + sq) / (2.0 * A), (-B - sq) / (2.0 * A)):
                if 0.0 <= xc <= length:
                    candidates.append(xc)
    vals = [(a + b * x + c * x * x + d * x * x * x, x) for x in candidates]
    return min(vals, key=lambda t: t[0])


def _laneSection_length(lanes_el, ls, road):
    """Length of a laneSection: explicit @length if present, else (next laneSection's
    @s - this laneSection's @s), else (road's own @length - this @s) for the last
    section. Note @s on <laneSection> is an absolute road s-coordinate, while <width>
    (and access/height/.../rule) @sOffset is relative to the laneSection start -- so
    callers must use this *length*, not an absolute end-s, against @sOffset."""
    L = ls.get("length")
    if L is not None:
        try:
            return float(L)
        except ValueError:
            pass
    s0 = _fnum(ls.get("s"))
    all_ls = lanes_el.findall("laneSection")
    ss = sorted(_fnum(x.get("s")) for x in all_ls)
    later = [s for s in ss if s > s0 + EPS]
    if later:
        return later[0] - s0
    rl = road.get("length")
    if rl is not None:
        try:
            return float(rl) - s0
        except ValueError:
            pass
    return None  # undeterminable


def _lane_width_validity_flags(road, rid):
    flags = []
    lanes_el = road.find("lanes")
    if lanes_el is None:
        return flags
    for ls in lanes_el.findall("laneSection"):
        s_ls = ls.get("s")
        ls_len = _laneSection_length(lanes_el, ls, road)
        for side, lane in _iter_lr_lanes(ls):
            widths = lane.findall("width")
            if not widths:
                continue
            lid = _lane_id(lane)
            for i, w in enumerate(widths):
                s_off = _fnum(w.get("sOffset"))
                a = _fnum(w.get("a"))
                b = _fnum(w.get("b"))
                c = _fnum(w.get("c"))
                d = _fnum(w.get("d"))
                if i + 1 < len(widths):
                    interval = _fnum(widths[i + 1].get("sOffset")) - s_off
                elif ls_len is not None:
                    interval = ls_len - s_off
                else:
                    interval = 0.0
                interval = max(interval, 0.0)
                min_val, min_x = _cubic_min_on_interval(a, b, c, d, interval)
                if min_val < -1e-6:
                    flags.append((
                        "road.lane.width.lane_width_validity",
                        f"{side} lane id={lid} width(sOffset={s_off:g}) が区間内 ds={min_x:g} で "
                        f"負値 {min_val:.4g}（a={a:g} b={b:g} c={c:g} d={d:g}, 区間長={interval:g}）",
                        f"road {rid} s={s_ls}",
                    ))
    return flags


def _width_defined_whole_section_flags(road, rid):
    flags = []
    lanes_el = road.find("lanes")
    if lanes_el is None:
        return flags
    for ls in lanes_el.findall("laneSection"):
        s_ls = ls.get("s")
        for side, lane in _iter_lr_lanes(ls):
            widths = lane.findall("width")
            if not widths:
                continue  # lane doesn't use <width> at all (border, or neither) -- out of scope
            min_s = min(_fnum(w.get("sOffset")) for w in widths)
            if min_s > EPS:
                flags.append((
                    "road.lane.width.width_defined_whole_section",
                    f"{side} lane id={_lane_id(lane)} の最初の<width>が sOffset={min_s:g} "
                    f"(!=0)、lane section先頭を幅未定義のまま開始",
                    f"road {rid} s={s_ls}",
                ))
    return flags


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    for rid, road in roads.items():
        # --- ascending-order families (per-lane <tag> series) ---
        flags += _asc_order_flags("road.lane.access.elem_asc_order", road, rid, "access")
        flags += _asc_order_flags("road.lane.border.elem_asc_order", road, rid, "border")
        flags += _asc_order_flags("road.lane.height.elem_asc_order", road, rid, "height")
        flags += _asc_order_flags("road.lane.material.elem_asc_order", road, rid, "material")
        flags += _asc_order_flags("road.lane.road_mark.elem_asc_order", road, rid, "roadMark", include_center=True)
        flags += _asc_order_flags("road.lane.rule.elem_asc_order", road, rid, "rule")
        flags += _asc_order_flags("road.lane.speed.elem_asc_order", road, rid, "speed")
        flags += _asc_order_flags("road.lane.width.elem_asc_order", road, rid, "width")
        flags += _lane_properties_asc_order_flags(road, rid)

        # --- center-lane-forbidden-children family ---
        flags += _center_no_child_flags("road.lane.access.center_lane_no_acc_rule", road, rid, "access")
        flags += _center_no_child_flags("road.lane.height.center_lane_no_height", road, rid, "height")
        flags += _center_no_child_flags("road.lane.material.center_lane_no_material", road, rid, "material")
        flags += _center_no_child_flags("road.lane.speed.center_lane_no_spd_lmt", road, rid, "speed")

        # --- border/width/laneOffset exclusivity ---
        flags += _exclusive_offset_border_flags(road, rid)
        flags += _border_width_exclusive_flags(road, rid)

        # --- width validity + coverage ---
        flags += _lane_width_validity_flags(road, rid)
        flags += _width_defined_whole_section_flags(road, rid)

    return flags
