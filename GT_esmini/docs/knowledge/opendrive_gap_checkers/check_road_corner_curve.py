"""Gap-rule checkers for category-group "road_corner_curve" (Annex F, ASAM
OpenDRIVE 1.9.0) — object <outline> corner/curve element rules:
  - road.corner_local.*  : <cornerLocal> cardinality / id convention / exclusivity
  - road.corner_road.*   : <cornerRoad> cardinality / id convention / exclusivity
  - road.curve_local.*   : <curveLocal> cardinality (the length/continuity/paramPoly3
                            p-range rules require actual curve-geometry evaluation and
                            are classified gap_geometry_math, not implemented here)

Pure XML structure/attribute inspection (stdlib xml.etree.ElementTree only), matching
the parsing idiom of scratchpad/gap_rule_check.py (roads dict keyed by @id, etc.).

Outline discovery handles both the OpenDRIVE 1.4-legacy direct-child form
(<object><outline/></object>) and the 1.8+ wrapper form
(<object><outlines><outline/>...</outlines></object>) per the XSD
(t_road_objects_object: optional single <outline> OR optional single <outlines>
wrapping 1..N <outline> elements).

Implemented rule_names (exact bare names, from rules_road_corner_curve.json):
  road.corner_local.element_min_amount
  road.corner_local.first_id_zero
  road.corner_local.mandatory_id_with_markings
  road.corner_local.no_mixing_road_local
  road.corner_local.sequential_id_values
  road.corner_road.corner_road_local_exclusivity
  road.corner_road.element_min_amount
  road.corner_road.first_id_zero
  road.corner_road.mandatory_id_with_markings
  road.corner_road.sequential_id_values
  road.curve_local.element_min_amount

Deferred to gap_geometry_math (need curve/position evaluation, not attribute reading):
  road.curve_local.continuous_curve_local
  road.curve_local.length_match
  road.curve_local.paramPoly3.arcLength_range
  road.curve_local.paramPoly3.normalized_range
"""

import xml.etree.ElementTree as ET  # noqa: F401  (kept for parity with sibling checkers / type hints)


def _outlines_of_object(obj):
    """Yield <outline> elements belonging to an <object>, covering both the
    1.4-legacy direct-child form and the 1.8+ <outlines> wrapper form."""
    out = []
    direct = obj.find("outline")
    if direct is not None:
        out.append(direct)
    wrapper = obj.find("outlines")
    if wrapper is not None:
        out.extend(wrapper.findall("outline"))
    return out


def _corner_id_int(elem):
    """Parse a corner/curve element's @id as int; None if absent or unparsable."""
    v = elem.get("id")
    if v is None:
        return None
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


def _sequential_violation(elems):
    """None if @id values are absent/unparsable for any element (can't judge), or if
    present ids increase by exactly 1 in document order. Otherwise the actual id list,
    for the detail message."""
    if not elems:
        return None
    ids = [_corner_id_int(e) for e in elems]
    if any(i is None for i in ids):
        return None
    for i in range(1, len(ids)):
        if ids[i] != ids[i - 1] + 1:
            return ids
    return None


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    """See module contract in scratchpad/checks_gap/CONTRACT.md."""
    flags = []

    for rid, r in roads.items():
        objs_container = r.find("objects")
        objs = objs_container.findall("object") if objs_container is not None else []
        for obj in objs:
            oid = obj.get("id", "?")
            for outline in _outlines_of_object(obj):
                outline_id = outline.get("id")
                oloc = "road {} object id={} outline id={}".format(
                    rid, oid, outline_id if outline_id is not None else "?"
                )

                corner_roads = outline.findall("cornerRoad")
                corner_locals = outline.findall("cornerLocal")
                curve_locals = outline.findall("curveLocal")
                markings = outline.find("markings")

                # --- element_min_amount: one rule per element kind ---
                if corner_locals and len(corner_locals) < 2:
                    flags.append(
                        (
                            "road.corner_local.element_min_amount",
                            "cornerLocal が{}個（outline内に2個以上必要）".format(
                                len(corner_locals)
                            ),
                            oloc,
                        )
                    )
                if corner_roads and len(corner_roads) < 2:
                    flags.append(
                        (
                            "road.corner_road.element_min_amount",
                            "cornerRoad が{}個（outline内に2個以上必要）".format(
                                len(corner_roads)
                            ),
                            oloc,
                        )
                    )
                if not corner_roads and not corner_locals and not curve_locals:
                    flags.append(
                        (
                            "road.curve_local.element_min_amount",
                            "outline内にcornerRoad/cornerLocal/curveLocalが1つも無い（curveLocalは1個以上必要）",
                            oloc,
                        )
                    )

                # --- exclusivity: no mixing of corner/curve kinds within one outline ---
                if corner_roads and corner_locals:
                    flags.append(
                        (
                            "road.corner_local.no_mixing_road_local",
                            "cornerRoad {}個 と cornerLocal {}個 が同一outlineに混在".format(
                                len(corner_roads), len(corner_locals)
                            ),
                            oloc,
                        )
                    )
                kinds_present = [
                    n
                    for n, lst in (
                        ("cornerRoad", corner_roads),
                        ("cornerLocal", corner_locals),
                        ("curveLocal", curve_locals),
                    )
                    if lst
                ]
                if len(kinds_present) > 1:
                    detail_parts = []
                    for n, lst in (
                        ("cornerRoad", corner_roads),
                        ("cornerLocal", corner_locals),
                        ("curveLocal", curve_locals),
                    ):
                        if lst:
                            detail_parts.append("{}x{}".format(n, len(lst)))
                    flags.append(
                        (
                            "road.corner_road.corner_road_local_exclusivity",
                            "同一outlineに複数種の輪郭要素が混在: "
                            + ", ".join(detail_parts),
                            oloc,
                        )
                    )

                # --- mandatory_id_with_markings: every corner needs @id when <markings> present ---
                if markings is not None:
                    missing_cl = [
                        i for i, c in enumerate(corner_locals) if c.get("id") is None
                    ]
                    if missing_cl:
                        flags.append(
                            (
                                "road.corner_local.mandatory_id_with_markings",
                                "markings有りだが@id無しのcornerLocalが{}個（index {}, 0始まり）".format(
                                    len(missing_cl), missing_cl
                                ),
                                oloc,
                            )
                        )
                    missing_cr = [
                        i for i, c in enumerate(corner_roads) if c.get("id") is None
                    ]
                    if missing_cr:
                        flags.append(
                            (
                                "road.corner_road.mandatory_id_with_markings",
                                "markings有りだが@id無しのcornerRoadが{}個（index {}, 0始まり）".format(
                                    len(missing_cr), missing_cr
                                ),
                                oloc,
                            )
                        )

                # --- first_id_zero: first listed corner's @id should be 0 ---
                if corner_locals:
                    fid = _corner_id_int(corner_locals[0])
                    if fid is not None and fid != 0:
                        flags.append(
                            (
                                "road.corner_local.first_id_zero",
                                "最初のcornerLocal @id={}（0であるべき）".format(fid),
                                oloc,
                            )
                        )
                if corner_roads:
                    fid = _corner_id_int(corner_roads[0])
                    if fid is not None and fid != 0:
                        flags.append(
                            (
                                "road.corner_road.first_id_zero",
                                "最初のcornerRoad @id={}（0であるべき）".format(fid),
                                oloc,
                            )
                        )

                # --- sequential_id_values: @id should increase by 1 top-to-bottom ---
                bad = _sequential_violation(corner_locals)
                if bad is not None:
                    flags.append(
                        (
                            "road.corner_local.sequential_id_values",
                            "cornerLocal @id列 {} が+1連番でない".format(bad),
                            oloc,
                        )
                    )
                bad = _sequential_violation(corner_roads)
                if bad is not None:
                    flags.append(
                        (
                            "road.corner_road.sequential_id_values",
                            "cornerRoad @id列 {} が+1連番でない".format(bad),
                            oloc,
                        )
                    )

    return flags
