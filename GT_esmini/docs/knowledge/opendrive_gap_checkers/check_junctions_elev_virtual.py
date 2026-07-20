"""Gap-rule checkers for category-group "junctions_elev_virtual".

Covers two Annex F subtrees:
  - junctions.elevation_grid.*  (F.4.10) -- mostly requires bicubic-interpolation /
    boundary-polygon geometry math (out of scope here, see contract); only the pure
    cardinality rule (only_one_elev_grid) is checkable via plain element counting.
  - junctions.virtual.* / junctions.virtual.connections.* / junctions.virtual.crossPath.*
    (F.4.13) -- virtual junctions describe connections within an uninterrupted main road
    (driveways, parking-lot entries, pedestrian crossings). Several rules here ARE
    checkable structurally (connection @type usage, controller absence, attribute
    cardinality, connecting-road attach-point vs. @sStart/@sEnd); others need real
    road-geometry (heading along s, inertial-frame lane-border smoothness, s/t
    containment of a crossing road) and are classified gap_geometry_math.

Key non-obvious fact used throughout: in this corpus's virtual-junction convention,
connecting roads of a virtual junction stay @junction="-1" (ordinary roads) -- unlike
common/direct junctions, membership is NOT signalled via road/@junction. The set of
"connecting roads" of a virtual junction has to be read off each <connection>'s
@connectingRoad attribute instead.

Rule names implemented (bare rule_name, per rules_junctions_elev_virtual.json):
  - junctions.elevation_grid.only_one_elev_grid
  - junctions.virtual.no_controllers
  - junctions.virtual.only_one_start_end
  - junctions.virtual.connecting_roads_start_end
  - junctions.virtual.connections.only_virtual_junctions

Implemented, PARTIAL_DETERMINISTIC (1), see impl_briefs/junctions_elev_virtual.md:
  - junctions.virtual.main_road_only : only the referential-integrity
    sub-case is checked -- @mainRoad (mandatory on t_junction_virtual) must
    resolve to a <road id=...> actually defined in the document. The
    participation-link and "branches off the main road ONLY" exclusivity
    judgment residuals are left uncovered.
"""


def _fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    # -----------------------------------------------------------------
    # junctions.virtual.main_road_only (PARTIAL_DETERMINISTIC, 1.9.0)
    #   "Virtual junctions shall be used for branches off the main road
    #   only." Only the referential-integrity sub-case is checked: @mainRoad
    #   (mandatory on t_junction_virtual per OpenDRIVE_Junction.xsd Table 69)
    #   must resolve to an actually-defined <road id=...>. The participation
    #   link (does some connection actually connect to @mainRoad) and the
    #   exclusivity judgment ("branches off the main road ONLY", requiring
    #   real-world road-classification/topology reasoning) are left
    #   uncovered -- see impl_briefs/junctions_elev_virtual.md.
    # -----------------------------------------------------------------
    for jid, j in junctions.items():
        if j.get("type") != "virtual":
            continue
        main_id = j.get("mainRoad")
        if main_id is not None and main_id not in road_ids:
            flags.append((
                "junctions.virtual.main_road_only",
                f"virtual junction {jid} の @mainRoad={main_id} が未定義のroad id"
                "（@mainRoadは実在するroadを参照する必要がある）",
                f"junction {jid}",
            ))

    # -----------------------------------------------------------------
    # junctions.elevation_grid.only_one_elev_grid
    #   "A junction shall have only one elevation grid."
    #   Pure structural cardinality check: count direct <elevationGrid> children of
    #   <junction>. (XSD already caps this at maxOccurs=1 for schema-valid documents,
    #   so this is expected to be a no-op on well-formed corpora; it still guards
    #   hand-authored / non-schema-checked files that this pipeline never runs an XSD
    #   validator over.)
    # -----------------------------------------------------------------
    for jid, j in junctions.items():
        grids = j.findall("elevationGrid")
        if len(grids) > 1:
            flags.append((
                "junctions.elevation_grid.only_one_elev_grid",
                f"junction {jid} に <elevationGrid> が {len(grids)} 個定義されている（1個のみ許容）",
                f"junction {jid}",
            ))

    # -----------------------------------------------------------------
    # junctions.virtual.no_controllers
    #   "Virtual junctions shall not have controllers and therefore no traffic lights."
    # -----------------------------------------------------------------
    for jid, j in junctions.items():
        if j.get("type") != "virtual":
            continue
        ctrls = j.findall("controller")
        if ctrls:
            cids = [c.get("id") for c in ctrls]
            flags.append((
                "junctions.virtual.no_controllers",
                f"virtual junction {jid} に controller {cids} が定義されている"
                "（virtual junctionはcontroller/信号機を持てない）",
                f"junction {jid}",
            ))

    # -----------------------------------------------------------------
    # junctions.virtual.connections.only_virtual_junctions
    #   "Virtual connections shall only be defined in virtual junctions."
    #   Check every non-virtual junction (default/direct/crossing/unspecified) for
    #   <connection type="virtual"> children.
    # -----------------------------------------------------------------
    for jid, j in junctions.items():
        jtype = j.get("type") or "default"
        if jtype == "virtual":
            continue
        for conn in j.findall("connection"):
            if conn.get("type") == "virtual":
                flags.append((
                    "junctions.virtual.connections.only_virtual_junctions",
                    f"junction {jid}（type={jtype}）の connection id={conn.get('id')} が "
                    'type="virtual"（virtual junction以外での virtual connection定義は禁止）',
                    f"junction {jid} connection id={conn.get('id')}",
                ))

    # -----------------------------------------------------------------
    # junctions.virtual.only_one_start_end
    #   "There shall only be one @sStart and one @sEnd attribute for the virtual
    #   junction."
    #   XML attribute uniqueness on a single element is already guaranteed by the XML
    #   spec (a duplicate attribute name is a well-formedness error, not something an
    #   ElementTree-parsed document can even represent), so the only falsifiable,
    #   textually-supported reading left is cardinality of the *value*: @sStart/@sEnd
    #   (t_grEqZero, a plain scalar) must hold exactly one number -- unlike the
    #   list-typed attributes used elsewhere in the same Annex F neighbourhood (e.g.
    #   elevationGrid's left/center/right, t_junction_grid_position_list). Flag values
    #   that look like a whitespace-separated list instead of a single scalar.
    # -----------------------------------------------------------------
    for jid, j in junctions.items():
        if j.get("type") != "virtual":
            continue
        for attr in ("sStart", "sEnd"):
            raw = j.get(attr)
            if raw is None:
                continue
            tokens = raw.split()
            if len(tokens) > 1:
                flags.append((
                    "junctions.virtual.only_one_start_end",
                    f'virtual junction {jid} の @{attr}="{raw}" が単一値でない'
                    f"（{len(tokens)}個のトークン、リスト値の疑い）",
                    f"junction {jid}",
                ))

    # -----------------------------------------------------------------
    # junctions.virtual.connecting_roads_start_end
    #   "All connecting roads within the virtual junction shall either start or end at
    #   @sStart or at @sEnd."
    #   For each <connection> that names a real @connectingRoad, resolve where that
    #   road's own <link> attaches back to @mainRoad (elementS, or a contactPoint
    #   resolved via mainRoad's own @length) and confirm it lands on @sStart or @sEnd
    #   (tolerance 0.5m). Connections without @connectingRoad (the deprecated
    #   type="virtual" topological-only style) don't specify a connecting road per the
    #   spec text itself ("Virtual connections do not specify connecting roads") and are
    #   excluded from this check.
    # -----------------------------------------------------------------
    TOL = 0.5
    for jid, j in junctions.items():
        if j.get("type") != "virtual":
            continue
        main_id = j.get("mainRoad")
        s_start = _fnum(j.get("sStart"))
        s_end = _fnum(j.get("sEnd"))
        if not main_id or main_id not in roads or s_start is None or s_end is None:
            continue
        main_len = _fnum(roads[main_id].get("length"))

        def _attach_s(link_el):
            if link_el is None:
                return None
            for tag in ("predecessor", "successor"):
                e = link_el.find(tag)
                if e is None or e.get("elementType") != "road" or e.get("elementId") != main_id:
                    continue
                if e.get("elementS") is not None:
                    s = _fnum(e.get("elementS"))
                    if s is not None:
                        return s
                cp = e.get("contactPoint")
                if cp == "start":
                    return 0.0
                if cp == "end" and main_len is not None:
                    return main_len
            return None

        seen = set()
        for conn in j.findall("connection"):
            cid = conn.get("id")
            cr = conn.get("connectingRoad")
            if not cr or cr not in roads:
                continue
            s = _attach_s(roads[cr].find("link"))
            if s is None:
                # fall back to an anchor predecessor/successor embedded directly under
                # <connection> (a convention seen in this corpus's hand-authored
                # virtual-junction fixtures alongside the road's own <link>).
                s = _attach_s(conn)
            if s is None:
                continue
            key = (cr, round(s, 3))
            if key in seen:
                continue
            seen.add(key)
            if abs(s - s_start) <= TOL or abs(s - s_end) <= TOL:
                continue
            flags.append((
                "junctions.virtual.connecting_roads_start_end",
                f"virtual junction {jid}: connecting road {cr}（connection id={cid}）が "
                f"mainRoad {main_id} に s={s:g} で接続（sStart={s_start:g} にも "
                f"sEnd={s_end:g} にも一致しない）",
                f"junction {jid} road {cr}",
            ))

    return flags
