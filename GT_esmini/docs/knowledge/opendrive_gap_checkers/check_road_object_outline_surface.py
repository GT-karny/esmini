"""Gap-rule checkers for category-group "road_object_outline_surface"
(ASAM OpenDRIVE 1.9.0 Annex F, categories road.object.outline / road.object.repeating /
road.object.surface — the subset not implementable via plain XML structural inspection is
classified away, see the parent report).

Implemented (structural / attribute-only, no road-geometry evaluation):
  - road.object.outline.exactly_one_outer         : <outlines> must contain exactly one
                                                      <outline outer="true"> (default true)
  - road.object.outline.outline_followed_by_corner : <outline> children must be >=2
                                                      cornerRoad XOR >=2 cornerLocal XOR
                                                      >=1 curveLocal
  - road.object.repeating.attributes_with_outline_skeleton : @lengthStart/@lengthEnd/
                                                      @widthStart/@widthEnd (+ non-zero
                                                      @heightStart/@heightEnd) on <repeat>
                                                      shall not appear when the object has
                                                      <outlines>/<outline>/<skeleton>
  - road.object.repeating.no_widthstart_end_with_radius : <repeat> @widthStart/@widthEnd
                                                      shall not appear when @radius is set
  - road.object.repeating.outline_use_cornerlocal  : repeated (has <repeat>) objects with
                                                      an outline shall not use <cornerRoad>
  - road.object.repeating.valid_s_length           : <repeat> @s/@length/@distance shall be
                                                      non-negative and stay within the
                                                      parent road's s-domain [0, @length]
  - road.object.surface.only_for_angular_boxes     : <surface> only allowed on objects with
                                                      an angular (length/width) bounding
                                                      volume -- not @radius, not outline(s)
  - road.object.surface.only_one_crg_file          : an object's <surface> shall not carry
                                                      more than one <CRG> element
  - road.object.surface.repeat_discretely_not_continously : a <surface> object's <repeat>
                                                      shall not have @distance == 0
                                                      (continuous repeat)

Everything else in this category-group requires actual road/CRG geometry evaluation
(curvature, elevation, bounding-volume/polygon overlap, CRG grid math) and is classified
gap_geometry_math in the structured report -- see CONTRACT.md taxonomy.
"""


def _fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def _is_outer(outline_elem):
    """@outer defaults to "true" per XSD (t_bool, default="true")."""
    v = outline_elem.get("outer")
    if v is None:
        return True
    return v.strip().lower() == "true"


def _outline_elements(obj):
    """All <outline> elements belonging to an <object>: the legacy direct child (1.4-style,
    at most one per schema) plus every <outline> inside a <outlines> wrapper (1.9-style).
    """
    elems = []
    direct = obj.find("outline")
    if direct is not None:
        elems.append(direct)
    wrapper = obj.find("outlines")
    if wrapper is not None:
        elems.extend(wrapper.findall("outline"))
    return elems


def _has_outline(obj):
    return obj.find("outline") is not None or obj.find("outlines") is not None


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    for rid, r in roads.items():
        road_length = _fnum(r.get("length"))

        for obj in r.iter("object"):
            oid = obj.get("id")
            loc = f"road {rid} object id={oid}"

            # --- road.object.outline.exactly_one_outer ---
            for wrapper in obj.findall("outlines"):
                outlines = wrapper.findall("outline")
                outer_ids = [o.get("id") for o in outlines if _is_outer(o)]
                if len(outer_ids) != 1:
                    flags.append(
                        (
                            "road.object.outline.exactly_one_outer",
                            f"object id={oid} の<outlines>内で outer=true な<outline>が{len(outer_ids)}件"
                            f"（outline id={outer_ids}, 全体{len(outlines)}件）",
                            loc,
                        )
                    )

            # --- road.object.outline.outline_followed_by_corner ---
            for outl in _outline_elements(obj):
                nCR = len(outl.findall("cornerRoad"))
                nCL = len(outl.findall("cornerLocal"))
                nCurve = len(outl.findall("curveLocal"))
                ok = (
                    (nCR >= 2 and nCL == 0 and nCurve == 0)
                    or (nCL >= 2 and nCR == 0 and nCurve == 0)
                    or (nCurve >= 1 and nCR == 0 and nCL == 0)
                )
                if not ok:
                    flags.append(
                        (
                            "road.object.outline.outline_followed_by_corner",
                            f"object id={oid} outline id={outl.get('id')}: cornerRoad={nCR} "
                            f"cornerLocal={nCL} curveLocal={nCurve}"
                            "（cornerRoad>=2 か cornerLocal>=2 か curveLocal>=1 のいずれか単一種別のみを満たすべき）",
                            loc,
                        )
                    )

            has_outline_like = _has_outline(obj) or obj.find("skeleton") is not None
            repeats = obj.findall("repeat")

            # --- road.object.repeating.attributes_with_outline_skeleton ---
            if has_outline_like:
                for rep in repeats:
                    bad = [
                        a
                        for a in ("lengthStart", "lengthEnd", "widthStart", "widthEnd")
                        if rep.get(a) is not None
                    ]
                    for a in ("heightStart", "heightEnd"):
                        v = rep.get(a)
                        fv = _fnum(v)
                        if v is not None and fv is not None and abs(fv) > 1e-9:
                            bad.append(a)
                    if bad:
                        flags.append(
                            (
                                "road.object.repeating.attributes_with_outline_skeleton",
                                f"object id={oid}（<outlines>/<outline>/<skeleton>あり）の<repeat>に "
                                f"{','.join(bad)} が指定されている（outline/skeleton持ちオブジェクトには非適用）",
                                loc,
                            )
                        )

            # --- road.object.repeating.no_widthstart_end_with_radius ---
            if obj.get("radius") is not None:
                for rep in repeats:
                    bad = [
                        a for a in ("widthStart", "widthEnd") if rep.get(a) is not None
                    ]
                    if bad:
                        flags.append(
                            (
                                "road.object.repeating.no_widthstart_end_with_radius",
                                f"object id={oid}（@radius={obj.get('radius')}）の<repeat>に "
                                f"{','.join(bad)} が指定されている（@radius設定時は非適用）",
                                loc,
                            )
                        )

            # --- road.object.repeating.outline_use_cornerlocal ---
            if repeats:
                for outl in _outline_elements(obj):
                    nCR = len(outl.findall("cornerRoad"))
                    if nCR > 0:
                        flags.append(
                            (
                                "road.object.repeating.outline_use_cornerlocal",
                                f"repeatされる object id={oid} の outline id={outl.get('id')} が "
                                f"cornerRoad を{nCR}件使用（cornerLocal を使うべき）",
                                loc,
                            )
                        )

            # --- road.object.repeating.valid_s_length ---
            for rep in repeats:
                s = _fnum(rep.get("s"))
                length = _fnum(rep.get("length"))
                distance = _fnum(rep.get("distance"))
                if s is None or length is None:
                    continue
                if s < -1e-6:
                    flags.append(
                        (
                            "road.object.repeating.valid_s_length",
                            f"object id={oid} repeat @s={s:g} が負",
                            loc,
                        )
                    )
                if length < -1e-6:
                    flags.append(
                        (
                            "road.object.repeating.valid_s_length",
                            f"object id={oid} repeat @length={length:g} が負",
                            loc,
                        )
                    )
                if distance is not None and distance < -1e-6:
                    flags.append(
                        (
                            "road.object.repeating.valid_s_length",
                            f"object id={oid} repeat @distance={distance:g} が負",
                            loc,
                        )
                    )
                if road_length is not None and road_length > 0:
                    if s > road_length + 1e-2:
                        flags.append(
                            (
                                "road.object.repeating.valid_s_length",
                                f"object id={oid} repeat @s={s:g} が road @length={road_length:g} を超過",
                                f"road {rid} object id={oid} s={s:g}",
                            )
                        )
                    elif s + length > road_length + 1e-2:
                        flags.append(
                            (
                                "road.object.repeating.valid_s_length",
                                f"object id={oid} repeat @s+@length={s + length:g} が road @length={road_length:g} を超過"
                                f"（{s + length - road_length:+.3g}）",
                                f"road {rid} object id={oid} s={s:g}",
                            )
                        )

            # --- road.object.surface.only_for_angular_boxes ---
            surf = obj.find("surface")
            if surf is not None:
                reasons = []
                if obj.get("radius") is not None:
                    reasons.append(f"@radius={obj.get('radius')}（円形境界）")
                if _has_outline(obj):
                    reasons.append("<outline(s)>あり")
                if reasons:
                    flags.append(
                        (
                            "road.object.surface.only_for_angular_boxes",
                            f"object id={oid} は {' / '.join(reasons)} だが <surface> を含む"
                            "（<surface>は角形（length/width）境界のオブジェクトのみ許可）",
                            loc,
                        )
                    )

                # --- road.object.surface.only_one_crg_file ---
                crgs = surf.findall("CRG")
                if len(crgs) > 1:
                    crg_files = sorted(
                        {c.get("file") for c in crgs if c.get("file") is not None}
                    )
                    flags.append(
                        (
                            "road.object.surface.only_one_crg_file",
                            f"object id={oid} の<surface>内に<CRG>が{len(crgs)}件（files={crg_files}）",
                            loc,
                        )
                    )

                # --- road.object.surface.repeat_discretely_not_continously ---
                for rep in repeats:
                    d = _fnum(rep.get("distance"))
                    if d is not None and abs(d) < 1e-9:
                        flags.append(
                            (
                                "road.object.surface.repeat_discretely_not_continously",
                                f"<surface>持ち object id={oid} の repeat @distance=0（連続repeat、離散のみ許可）",
                                loc,
                            )
                        )

    return flags
