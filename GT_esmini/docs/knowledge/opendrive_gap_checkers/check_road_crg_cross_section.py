"""OpenDRIVE Annex F gap-rule checkers: category-group "road_crg_cross_section".

Covers two Annex F families:
  - road.crg.*                 (F.6.7, <road>/<junction> <surface><CRG> attribute rules)
  - road.cross_section_surface.* (F.6.8, <lateralProfile><crossSectionSurface> rules)

Pure stdlib xml.etree.ElementTree, no re-parsing of files (root/roads/junctions are
handed in by the integration layer). See CONTRACT.md for the module contract.

Implemented rules (8 of 8 road.crg + 4 of 6 road.cross_section_surface = 12 implemented,
2 classified gap_geometry_math):
  road.crg.attach_vs_friction
  road.crg.friction_no_z_offset_scale
  road.crg.h_offset_only_genuine_global
  road.crg.junction
  road.crg.no_opposite
  road.crg.only_on_per_s
  road.crg.s_t_offset_no_global
  road.crg.use_last_entry
  road.cross_section_surface.no_shape_superelevation
  road.cross_section_surface.start_end_match_with_refline
  road.cross_section_surface.use_strip
  road.cross_section_surface.use_width

Not implemented (geometry evaluation required, see reason in structured report):
  road.cross_section_surface.height        -> gap_geometry_math
  road.cross_section_surface.lane_def_valid -> gap_geometry_math
"""

# <CRG> is t_road_surface_CRG (0..* inside <surface>). @mode is required, one of:
CRG_MODES_ALLOWING_HOFFSET = {"genuine", "global"}
CRG_MODES_ALLOWING_OPPOSITE = {"attached", "attached0"}

# <strip> ids: 1 = inner-left, -1 = inner-right, 2 = outer-left, -2 = outer-right.
CSS_SIDES = (("left", "1", "2"), ("right", "-1", "-2"))
CSS_STRIP_SUBELEMS = ("width", "constant", "linear", "quadratic", "cubic")


def _fnum(x, default=None):
    try:
        return float(x)
    except (TypeError, ValueError):
        return default


def _owner_crg_map(owners, tag_kind):
    """owners: dict[id, Element] (roads or junctions).
    Returns dict[id, list[Element]] of direct <surface>/<CRG> children only
    (NOT <object>/<surface>/<CRG>, which is a different, unrelated type)."""
    out = {}
    for oid, elem in owners.items():
        surf = elem.find("surface")
        if surf is None:
            continue
        crgs = surf.findall("CRG")
        if crgs:
            out[oid] = crgs
    return out


def _check_crg_entry(flags, crg, loc):
    mode = crg.get("mode")
    purpose = crg.get("purpose") or "elevation"
    orientation = crg.get("orientation")
    file_ = crg.get("file")

    # road.crg.attach_vs_friction
    if mode == "attached" and purpose == "friction":
        flags.append(
            (
                "road.crg.attach_vs_friction",
                f"CRG file={file_} mode=attached と purpose=friction が併用されている",
                loc,
            )
        )

    # road.crg.friction_no_z_offset_scale
    if purpose == "friction":
        got = [a for a in ("zOffset", "zScale") if crg.get(a) is not None]
        if got:
            flags.append(
                (
                    "road.crg.friction_no_z_offset_scale",
                    f"CRG file={file_} purpose=friction で {'/'.join(got)} が指定されている（friction には不可）",
                    loc,
                )
            )

    # road.crg.h_offset_only_genuine_global
    if crg.get("hOffset") is not None and mode not in CRG_MODES_ALLOWING_HOFFSET:
        flags.append(
            (
                "road.crg.h_offset_only_genuine_global",
                f"CRG file={file_} mode={mode} で hOffset が指定されている（genuine/global 以外では不可）",
                loc,
            )
        )

    # road.crg.no_opposite
    if orientation == "opposite" and mode not in CRG_MODES_ALLOWING_OPPOSITE:
        flags.append(
            (
                "road.crg.no_opposite",
                f"CRG file={file_} mode={mode} で orientation=opposite が指定されている（attached/attached0 以外では不可）",
                loc,
            )
        )

    # road.crg.s_t_offset_no_global
    if mode == "global":
        got = [a for a in ("sOffset", "tOffset") if crg.get(a) is not None]
        if got:
            flags.append(
                (
                    "road.crg.s_t_offset_no_global",
                    f"CRG file={file_} mode=global で {'/'.join(got)} が指定されている（global では不可）",
                    loc,
                )
            )


def _check_crg_overlap(flags, crgs, owner_loc):
    """road.crg.only_on_per_s + road.crg.use_last_entry: multiple CRG entries with the
    same @purpose whose [sStart, sEnd) ranges overlap within the same road/junction.
    Detection is plain numeric interval overlap on the given attributes (no coordinate
    transform / geometry evaluation needed)."""
    by_purpose = {}
    for crg in crgs:
        purpose = crg.get("purpose") or "elevation"
        by_purpose.setdefault(purpose, []).append(crg)

    for purpose, group in by_purpose.items():
        if len(group) < 2:
            continue
        enriched = []
        for crg in group:
            s0 = _fnum(crg.get("sStart"), 0.0)
            s1 = _fnum(crg.get("sEnd"), s0)
            enriched.append((s0, s1, crg))
        enriched.sort(key=lambda t: (t[0], crgs.index(t[2])))
        for i in range(len(enriched) - 1):
            s0a, s1a, crga = enriched[i]
            s0b, s1b, crgb = enriched[i + 1]
            if s0b < s1a - 1e-6:
                flags.append(
                    (
                        "road.crg.only_on_per_s",
                        f"purpose={purpose} の CRG が s範囲で重複: "
                        f"[{s0a:g},{s1a:g}] file={crga.get('file')} と "
                        f"[{s0b:g},{s1b:g}] file={crgb.get('file')}",
                        owner_loc,
                    )
                )
                last = crga if crgs.index(crga) > crgs.index(crgb) else crgb
                flags.append(
                    (
                        "road.crg.use_last_entry",
                        f"purpose={purpose} で位置が重複する CRG エントリが複数あり；"
                        f"ファイル中の出現順で最後（file={last.get('file')}）のみ有効、他は無視される",
                        owner_loc,
                    )
                )


def _check_crg_junction(flags, junctions, road_crg, junction_crg):
    for jid, j in junctions.items():
        if jid not in junction_crg:
            continue
        conn_roads = set()
        for conn in j.findall("connection"):
            cr = conn.get("connectingRoad")
            if cr:
                conn_roads.add(cr)
        for cr in sorted(conn_roads):
            if cr in road_crg:
                flags.append(
                    (
                        "road.crg.junction",
                        f"junction {jid} に <surface><CRG> があるが、"
                        f"所属する connectingRoad {cr} にも <surface><CRG> がある",
                        f"junction {jid} road {cr}",
                    )
                )


def _check_css_no_shape_superelevation(flags, roads):
    for rid, r in roads.items():
        lp = r.find("lateralProfile")
        if lp is None:
            continue
        css = lp.find("crossSectionSurface")
        if css is None:
            continue
        conflicts = []
        if lp.findall("superelevation"):
            conflicts.append("superelevation")
        if lp.findall("shape"):
            conflicts.append("shape")
        if conflicts:
            flags.append(
                (
                    "road.cross_section_surface.no_shape_superelevation",
                    f"crossSectionSurface と {'/'.join(conflicts)} が同一road内に併存",
                    f"road {rid}",
                )
            )


def _first_coeff_s(container):
    if container is None:
        return None
    c = container.find("coefficients")
    return _fnum(c.get("s"), None) if c is not None else None


def _all_coeff_s(container):
    if container is None:
        return []
    return [_fnum(c.get("s"), None) for c in container.findall("coefficients")]


def _check_css_start_end(flags, roads):
    for rid, r in roads.items():
        lp = r.find("lateralProfile")
        if lp is None:
            continue
        css = lp.find("crossSectionSurface")
        if css is None:
            continue
        road_len = _fnum(r.get("length"), None)

        polys = []  # (label, container_element)
        to = css.find("tOffset")
        if to is not None:
            polys.append(("tOffset", to))
        ss = css.find("surfaceStrips")
        if ss is not None:
            for strip in ss.findall("strip"):
                sid = strip.get("id")
                for tag in CSS_STRIP_SUBELEMS:
                    sub = strip.find(tag)
                    if sub is not None:
                        polys.append((f"strip id={sid} {tag}", sub))

        for label, container in polys:
            s0 = _first_coeff_s(container)
            if s0 is not None and abs(s0) > 1e-6:
                flags.append(
                    (
                        "road.cross_section_surface.start_end_match_with_refline",
                        f"{label} の最初の <coefficients> が s={s0:g}（reference line 開始 s=0 と不一致）",
                        f"road {rid}",
                    )
                )
            if road_len is not None:
                for s in _all_coeff_s(container):
                    if s is not None and s > road_len + 1e-6:
                        flags.append(
                            (
                                "road.cross_section_surface.start_end_match_with_refline",
                                f"{label} の <coefficients s={s:g}> が road length={road_len:g} を超過"
                                f"（reference line 終端を越える）",
                                f"road {rid}",
                            )
                        )


def _check_css_strips(flags, roads):
    for rid, r in roads.items():
        lp = r.find("lateralProfile")
        if lp is None:
            continue
        css = lp.find("crossSectionSurface")
        if css is None:
            continue
        ss = css.find("surfaceStrips")
        if ss is None:
            continue
        by_id = {}
        for strip in ss.findall("strip"):
            sid = strip.get("id")
            if sid is not None:
                by_id[sid] = strip

        for side, inner, outer in CSS_SIDES:
            present = [sid for sid in (inner, outer) if sid in by_id]
            if len(present) == 1:
                sid = present[0]
                strip = by_id[sid]
                if sid != inner:
                    flags.append(
                        (
                            "road.cross_section_surface.use_strip",
                            f"{side}側で strip が1枚のみだが id={sid}（inner の id={inner} であるべき）",
                            f"road {rid}",
                        )
                    )
                if strip.find("width") is not None:
                    flags.append(
                        (
                            "road.cross_section_surface.use_strip",
                            f"{side}側 単一strip（id={sid}）に width が指定されている（単一strip時は指定禁止）",
                            f"road {rid}",
                        )
                    )
            elif len(present) == 2:
                inner_strip = by_id[inner]
                if inner_strip.find("width") is None:
                    flags.append(
                        (
                            "road.cross_section_surface.use_width",
                            f"{side}側で2 strip使用（id={inner},{outer}）だが "
                            f"inner strip（id={inner}）に width が未指定",
                            f"road {rid}",
                        )
                    )


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    road_crg = _owner_crg_map(roads, "road")
    junction_crg = _owner_crg_map(junctions, "junction")

    for rid, crgs in road_crg.items():
        for crg in crgs:
            s0 = _fnum(crg.get("sStart"), None)
            loc = f"road {rid} s={s0:g}" if s0 is not None else f"road {rid}"
            _check_crg_entry(flags, crg, loc)
        _check_crg_overlap(flags, crgs, f"road {rid}")

    for jid, crgs in junction_crg.items():
        for crg in crgs:
            s0 = _fnum(crg.get("sStart"), None)
            loc = f"junction {jid} s={s0:g}" if s0 is not None else f"junction {jid}"
            _check_crg_entry(flags, crg, loc)
        _check_crg_overlap(flags, crgs, f"junction {jid}")

    _check_crg_junction(flags, junctions, road_crg, junction_crg)

    _check_css_no_shape_superelevation(flags, roads)
    _check_css_start_end(flags, roads)
    _check_css_strips(flags, roads)

    return flags
