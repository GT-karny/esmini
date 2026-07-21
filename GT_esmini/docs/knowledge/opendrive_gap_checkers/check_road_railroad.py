"""
Gap-rule checkers for category-group "road_railroad" (Annex F 1.9.0):
  road.railroad.one_rail_per_road
  road.railroad.rail_lane_width_validity          -> PARTIAL_DETERMINISTIC: flags only
    the degenerate/sanity sub-case (a rail/tram lane's <width> @a <= 0 or absent, i.e.
    non-positive) -- a real rail-bound vehicle necessarily has strictly positive width,
    so this floor is provable without the spec's missing numeric vehicle-width
    threshold. Does NOT validate against an actual vehicle-gauge/reference-width
    minimum -- see impl_briefs/road_railroad.md DO_NOT_IMPLEMENT.
  road.railroad.rail_refline_centered
  road.railroad.platforms.min_amount
  road.railroad.platforms.min_segments
  road.railroad.segment.segments_per_platform_min_amount
  road.railroad.stations.one_platform_per_station
  road.railroad.switch.check_switch_conn
  road.railroad.switch.single_switch_no_partner

Pure xml.etree.ElementTree, stdlib only. See CONTRACT.md for the module contract
(scratchpad/checks_gap/CONTRACT.md) and scratchpad/gap_rule_check.py for the parsing
idiom this follows.

Modeling notes (derived from the two official calibration fixtures that actually use
<railroad>/<station>: GT_esmini/test/odr_fixtures/official/examples/Ex_Railway-Station/
and Ex_Railway-Switch/):
  - A physical rail/tram track is modeled as ONE lane (type="rail" or "tram") per
    <road>, not as two lanes for the two physical rails. "one rail lane per road"
    therefore means: at most one such lane per laneSection cross-section (summed
    across left+right; a road with several consecutive laneSections each carrying
    the *same* continuing rail lane is fine and must NOT be flagged -- so the check
    is scoped per-laneSection, not summed over the whole road length).
  - "centered" is achieved in both official fixtures by laneOffset.a == width.a / 2
    (with sign per side), i.e. the reference line (t=0) sits exactly on the
    midpoint of the rail/tram lane's t-span. Verified against all 4 railroad-carrying
    roads in the corpus: predicted center offset == 0 in all four cases.
  - laneOffset and width are evaluated as full cubic polynomials (a+b*ds+c*ds^2+
    d*ds^3), not just the @a constant term -- a non-constant laneOffset ramp
    evaluated downstream of its @s genuinely shifts the reference line, and
    reading only @a understates that shift, producing a false "not centered"
    finding (fixed post-adversarial-audit; see rail_refline_centered below).
  - rail_refline_centered is scoped to *dedicated* rail/tram roads (no driving/
    other traffic lane anywhere on the road). A road that also carries a
    driving lane is the common, valid street-running-tram / rail-beside-road
    pattern, where the reference line is centered on the roadway, not the
    track -- that pattern must not be flagged (fixed post-adversarial-audit).
"""

import xml.etree.ElementTree as ET  # noqa: F401  (root is already parsed by caller; kept for type clarity)


def _fnum(x, default=0.0):
    try:
        return float(x)
    except (TypeError, ValueError):
        return default


def _poly3(a, b, c, d, ds):
    """Standard OpenDRIVE cubic-polynomial evaluation: a + b*ds + c*ds^2 + d*ds^3."""
    return a + b * ds + c * ds * ds + d * ds * ds * ds


def _lane_offset_at(lanes_elem, s):
    """Value of <laneOffset> applicable at road-s `s`, evaluated as the FULL
    cubic polynomial (a + b*ds + c*ds^2 + d*ds^3, ds = s - entry@s) of the last
    laneOffset entry whose @s <= s; 0.0 if none (matches the OpenDRIVE default
    of no lateral shift). Reading only @a (the constant term) undercounts the
    offset for any non-constant (b/c/d != 0) laneOffset -- e.g. a linear ramp
    laneOffset evaluated several metres downstream of its @s genuinely shifts
    the reference line, and skipping that shift produces a false "not centered"
    finding (road.railroad.rail_refline_centered FALSE_POSITIVE, adv fixture
    r3_fp_nonconstant_laneoffset.xodr: a=0,b=0.35 evaluated at s=5 -> 1.75, not 0).
    """
    best_s = None
    best_coeffs = (0.0, 0.0, 0.0, 0.0)
    for lo in lanes_elem.findall("laneOffset"):
        los = _fnum(lo.get("s"))
        if los <= s + 1e-9 and (best_s is None or los > best_s):
            best_s = los
            best_coeffs = (
                _fnum(lo.get("a")),
                _fnum(lo.get("b")),
                _fnum(lo.get("c")),
                _fnum(lo.get("d")),
            )
    if best_s is None:
        return 0.0
    ds = s - best_s
    return _poly3(*best_coeffs, ds)


def _lane_first_width_a(lane_elem):
    """Value of the <width> entry with the smallest sOffset (i.e. the width at
    the start of the lane's validity within this laneSection), evaluated as the
    full cubic polynomial at its own local origin (ds=0 relative to that entry's
    @sOffset -- the width poly's domain is section-local sOffset, not road-s)."""
    widths = lane_elem.findall("width")
    if not widths:
        return 0.0
    w0 = min(widths, key=lambda w: _fnum(w.get("sOffset")))
    a, b, c, d = (
        _fnum(w0.get("a")),
        _fnum(w0.get("b")),
        _fnum(w0.get("c")),
        _fnum(w0.get("d")),
    )
    return _poly3(a, b, c, d, 0.0)


_RAIL_TYPES = ("rail", "tram")

# Lane types compatible with a "dedicated rail road" -- i.e. do not indicate the
# road also carries driving/other motorized traffic. A road whose lanes are all
# drawn from this set (plus rail/tram) is a genuine dedicated track, where the
# reference line is expected to be centered on the rail/tram pair. A road that
# ALSO has a driving (or other traffic-carrying) lane is the common, valid
# street-running-tram / rail-beside-road pattern -- there the reference line is
# centered on the roadway as a whole, not on the track, so the centering check
# does not apply (road.railroad.rail_refline_centered FALSE_POSITIVE: adv
# fixtures r3_fp_street_tram.xodr, r1_neg_mixed_one_rail.xodr).
_RAIL_COMPATIBLE_NON_RAIL_TYPES = {
    "none",
    "border",
    "shoulder",
    "sidewalk",
    "walking",
    "median",
    "curb",
    None,
}


def _road_is_dedicated_rail(road_elem):
    """True iff every non-rail/tram lane on this road is a non-traffic support
    lane (border/shoulder/sidewalk/walking/median/curb/none) -- i.e. the road
    carries no driving/other traffic lane anywhere in any laneSection."""
    lanes_elem = road_elem.find("lanes")
    if lanes_elem is None:
        return False
    for ls in lanes_elem.findall("laneSection"):
        for side in ("left", "right"):
            grp = ls.find(side)
            if grp is None:
                continue
            for lane in grp.findall("lane"):
                ltype = lane.get("type")
                if ltype in _RAIL_TYPES:
                    continue
                if ltype not in _RAIL_COMPATIBLE_NON_RAIL_TYPES:
                    return False
    return True


def _check_one_rail_per_road(rid, road_elem, flags):
    lanes_elem = road_elem.find("lanes")
    if lanes_elem is None:
        return
    for ls in lanes_elem.findall("laneSection"):
        s = _fnum(ls.get("s"))
        railish = []
        for side in ("left", "right"):
            grp = ls.find(side)
            if grp is None:
                continue
            for lane in grp.findall("lane"):
                if lane.get("type") in _RAIL_TYPES:
                    railish.append((side, lane.get("id"), lane.get("type")))
        if len(railish) > 1:
            desc = ", ".join(f"{side}:id={lid}({lt})" for side, lid, lt in railish)
            flags.append(
                (
                    "road.railroad.one_rail_per_road",
                    f"同一laneSectionに rail/tram 車線が{len(railish)}本（{desc}）。1本のみであるべき",
                    f"road {rid} s={s:g}",
                )
            )


def _check_rail_lane_width_validity(rid, road_elem, flags):
    """road.railroad.rail_lane_width_validity (PARTIAL_DETERMINISTIC): for every
    <lane> with @type in {"rail","tram"} anywhere in the road's lane layers, flag
    any <width> child whose @a (the width value at that segment's local ds=0,
    i.e. the constant term itself -- no polynomial evaluation needed since ds=0)
    is <= 0 or absent (missing @a reads as 0.0 via _fnum's default, correctly
    treated as non-positive). A real rail-bound vehicle has strictly positive
    width, so a degenerate/absent width can never satisfy "at least the width of
    rail-bound vehicles" regardless of the spec's unstated numeric threshold --
    this is the provable floor. Does NOT check against an actual vehicle-gauge
    minimum (no such value exists anywhere in the OpenDRIVE 1.9 spec); a
    positive-but-narrow width (e.g. a<=1.0) is deliberately left unflagged."""
    lanes_elem = road_elem.find("lanes")
    if lanes_elem is None:
        return
    for ls in lanes_elem.findall("laneSection"):
        s = _fnum(ls.get("s"))
        for side in ("left", "center", "right"):
            grp = ls.find(side)
            if grp is None:
                continue
            for lane in grp.findall("lane"):
                if lane.get("type") not in _RAIL_TYPES:
                    continue
                for w in lane.findall("width"):
                    a = _fnum(w.get("a"))
                    if a <= 0:
                        flags.append(
                            (
                                "road.railroad.rail_lane_width_validity",
                                f"{side} {lane.get('type')}車線(id={lane.get('id')}) の "
                                f"<width sOffset={w.get('sOffset')!r}> @a={w.get('a')!r} "
                                f"（0以下または未指定、rail-bound車両の幅として不正）",
                                f"road {rid} s={s:g}",
                            )
                        )


def _check_rail_refline_centered(rid, road_elem, flags, tol=0.05):
    lanes_elem = road_elem.find("lanes")
    if lanes_elem is None:
        return
    # Scope: only dedicated rail/tram roads. A road that also carries a
    # driving (or other traffic) lane is the common, valid street-running-tram
    # / rail-beside-road pattern -- the reference line is legitimately centered
    # on the roadway there, not on the track, so this rule does not apply.
    if not _road_is_dedicated_rail(road_elem):
        return
    for ls in lanes_elem.findall("laneSection"):
        s = _fnum(ls.get("s"))
        origin_t = _lane_offset_at(lanes_elem, s)
        for side, sign in (("left", 1), ("right", -1)):
            grp = ls.find(side)
            if grp is None:
                continue
            lane_list = []
            for lane in grp.findall("lane"):
                try:
                    lid = int(lane.get("id"))
                except (TypeError, ValueError):
                    continue
                lane_list.append((abs(lid), lane))
            lane_list.sort(key=lambda x: x[0])
            cum = 0.0
            for _absid, lane in lane_list:
                w = _lane_first_width_a(lane)
                inner_t = origin_t + sign * cum
                outer_t = inner_t + sign * w
                ltype = lane.get("type")
                if ltype in _RAIL_TYPES:
                    center_t = (inner_t + outer_t) / 2.0
                    if abs(center_t) > tol:
                        flags.append(
                            (
                                "road.railroad.rail_refline_centered",
                                f"{side} {ltype}車線(id={lane.get('id')}) の中心 t={center_t:.3f}"
                                f"（基準線 t=0 が軌道対の中心になっていない、laneOffset={origin_t:g} width={w:g}）",
                                f"road {rid} s={s:g}",
                            )
                        )
                cum += w


def _check_platform_and_station_rules(root, flags):
    for station in root.iter("station"):
        sid = station.get("id")
        platforms = station.findall("platform")
        if len(platforms) == 0:
            flags.append(
                (
                    "road.railroad.platforms.min_amount",
                    f"station {sid} に platform が0個定義されている（最低1個必要）",
                    f"station {sid}",
                )
            )
            flags.append(
                (
                    "road.railroad.stations.one_platform_per_station",
                    f"station {sid} の後に platform 要素が1つも続いていない",
                    f"station {sid}",
                )
            )
        for platform in platforms:
            pid = platform.get("id")
            segments = platform.findall("segment")
            if len(segments) == 0:
                flags.append(
                    (
                        "road.railroad.platforms.min_segments",
                        f"platform {pid}（station {sid}）に segment が0個定義されている（最低1個必要）",
                        f"station {sid} platform {pid}",
                    )
                )
                flags.append(
                    (
                        "road.railroad.segment.segments_per_platform_min_amount",
                        f"platform {pid}（station {sid}）に segment が1つも無い",
                        f"station {sid} platform {pid}",
                    )
                )


def _collect_switches(root):
    switches = []
    for road_elem in root.iter("road"):
        rid = road_elem.get("id")
        rr = road_elem.find("railroad")
        if rr is None:
            continue
        for sw in rr.findall("switch"):
            mt = sw.find("mainTrack")
            st = sw.find("sideTrack")
            pt = sw.find("partner")
            switches.append(
                {
                    "id": sw.get("id"),
                    "road_id": rid,
                    "main_id": mt.get("id") if mt is not None else None,
                    "side_id": st.get("id") if st is not None else None,
                    "partner_id": pt.get("id") if pt is not None else None,
                }
            )
    return switches


def _check_switch_rules(root, flags):
    switches = _collect_switches(root)
    if not switches:
        return

    # check_switch_conn: "Main tracks shall not be used to connect two switches."
    # A road that is some switch's mainTrack (its own home track) must never also
    # appear as some (other) switch's sideTrack (the connector role belongs to
    # side tracks only, per the official Ex_Railway-Switch pattern where the
    # sideTrack road -- never a mainTrack road -- is shared between two switches).
    main_ids = {s["main_id"] for s in switches if s["main_id"]}
    side_ids = {s["side_id"] for s in switches if s["side_id"]}
    overlap = main_ids & side_ids
    for s in switches:
        if s["main_id"] and s["main_id"] in overlap:
            flags.append(
                (
                    "road.railroad.switch.check_switch_conn",
                    f"switch {s['id']} の mainTrack（road {s['main_id']}）が、"
                    f"他のswitchの sideTrack としても参照されている（メイントラックでswitch同士を接続してはならない）",
                    f"road {s['road_id']} switch {s['id']}",
                )
            )

    # single_switch_no_partner: partner links must be reciprocal. A switch with no
    # <partner> child is "single"; no other switch may point at it via <partner>.
    by_id = {s["id"]: s for s in switches if s["id"]}
    for s in switches:
        pid = s["partner_id"]
        if not pid:
            continue
        partner = by_id.get(pid)
        if partner is None:
            continue  # dangling partner ref -> referential-integrity territory, not ours
        if not partner["partner_id"]:
            flags.append(
                (
                    "road.railroad.switch.single_switch_no_partner",
                    f"switch {s['id']} が switch {pid} を partner として参照しているが、"
                    f"switch {pid} 自体は partner 要素を持たない単独switch（相互参照になっていない）",
                    f"road {s['road_id']} switch {s['id']}",
                )
            )


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    for rid, road_elem in roads.items():
        _check_one_rail_per_road(rid, road_elem, flags)
        _check_rail_lane_width_validity(rid, road_elem, flags)
        _check_rail_refline_centered(rid, road_elem, flags)

    _check_platform_and_station_rules(root, flags)
    _check_switch_rules(root, flags)

    return flags
