"""Gap-rule checkers for category-group road.object.marking / road.object.object_marking
(Annex F, OpenDRIVE 1.7.0 / 1.9.0).

Covers <object><markings><marking><cornerReference> (object markings: crosswalk / parking
space stripe definitions, traffic-island edge markings, etc.) and the placement rules that
govern where <markings> may legally live relative to <outline>/<outlines>. ASAM renamed the
category between spec versions (road.object.marking @1.7.0 -> road.object.object_marking
@1.9.0, coinciding with <curveLocal> being added to the outline model); both category names
appear in our assigned rule list and are implemented as distinct findings (a marking missing
@color is simultaneously a violation of both UIDs).

Structure recap (OpenDRIVE_Object.xsd):
  <object> -> optional <outline> (single, pre-1.9 flat form) OR <outlines><outline>*  (1+)
  <object> -> optional <markings><marking>*</markings>   (object-level, @side-anchored)
  <outline> -> optional <markings><marking>*</markings>  (outline-level, cornerReference-anchored)
  <marking> -> @side  XOR  >=2 <cornerReference id=.../>   (mutually exclusive, per XSD assert)
  <outline> -> choice of >=2 <cornerRoad>, >=2 <cornerLocal>, or >=1 <curveLocal>, each
               carrying an optional @id (only needed when referenced by a cornerReference)

Implemented (structural / attribute-presence / cross-element-count / referential-integrity
only -- "which corner points are contiguous/ordered" is answerable purely from the outline's
own XML document order, no coordinate/geometry computation needed):
  - road.object.marking.colour                                (+ object_marking twin)
  - road.object.marking.markings_with_outline
  - road.object.marking.markings_without_outline
  - road.object.marking.no_cornerreference_if_no_outline
  - road.object.marking.no_outline_side_attr
  - road.object.marking.outline_corner_reference_count         (>=2 always; pre-curveLocal wording)
  - road.object.marking.complete_or_partial_on_outline          (cornerReference @id must resolve
    within the SAME outline that structurally owns the marking)
  - road.object.object_marking.colour
  - road.object.object_marking.outline_corner_reference_count   (curveLocal-aware: >=1 if the
    owning outline uses <curveLocal>, else >=2 -- refines the 1.7.0 flat rule above)
  - road.object.object_marking.include_points_between_cornerReferences (referenced ids must
    form one contiguous run in the outline's own point order; wrap-around allowed if closed)
  - road.object.object_marking.keep_id_ordered                  (cornerReference @id order must
    follow the outline's own point order, forward or reverse; single wrap allowed if closed)
  - road.object.object_marking.enclosed_outline_marking          (2 same-id cornerReference is
    the "full enclosure" idiom -- only meaningful/valid when the owning outline is closed)

All 12 assigned rules end up implemented_gt; none deferred/classified away.
"""


def _outline_points(outline):
    """(point_type, ordered_ids, id_set) for one <outline> element. point_type is one of
    'cornerRoad' / 'cornerLocal' / 'curveLocal' / None. Points missing @id (legal when the
    outline carries no markings/cornerReference) are skipped from ids/id_set."""
    for tag in ("cornerRoad", "cornerLocal", "curveLocal"):
        pts = outline.findall(tag)
        if pts:
            ids = [p.get("id") for p in pts if p.get("id") is not None]
            return tag, ids, set(ids)
    return None, [], set()


def _outlines_of(obj):
    """All <outline> elements of an <object>: unwrapped single form + <outlines><outline>*."""
    result = []
    direct = obj.find("outline")
    if direct is not None:
        result.append(direct)
    wrapper = obj.find("outlines")
    if wrapper is not None:
        result.extend(wrapper.findall("outline"))
    return result


def _collect_markings(obj):
    """[(markings_elem, immediate_parent_elem), ...] for every <markings> anywhere under
    obj (obj itself is the base for parent-tracking; ElementTree has no .getparent()).
    """
    out = []

    def rec(e, parent):
        if e.tag == "markings":
            out.append((e, parent))
        for c in e:
            rec(c, e)

    for c in obj:
        rec(c, obj)
    return out


def _count_descents(seq):
    return sum(1 for a, b in zip(seq, seq[1:]) if a > b)


def _count_ascents(seq):
    return sum(1 for a, b in zip(seq, seq[1:]) if a < b)


def _ordered_ok(indices, closed):
    """Does `indices` (positions within the outline's own point list, in the order the
    marking's <cornerReference> elements were written) follow the outline's point order,
    forward or reverse? A closed outline may wrap once (one descent/ascent tolerated).
    """
    if len(indices) < 2:
        return True
    desc = _count_descents(indices)
    asc = _count_ascents(indices)
    if closed:
        return desc <= 1 or asc <= 1
    return desc == 0 or asc == 0


def _contiguous_ok(idx_set, n_pts, closed):
    """Do the referenced point positions form ONE contiguous run in the outline's own point
    order (linearly, or -- for closed outlines -- cyclically, i.e. the complement is itself
    one contiguous run)?"""
    if len(idx_set) <= 1:
        return True
    s = sorted(idx_set)
    if (s[-1] - s[0] + 1) == len(s):
        return True
    if not closed:
        return False
    missing = sorted(i for i in range(n_pts) if i not in idx_set)
    if not missing:
        return True
    return (missing[-1] - missing[0] + 1) == len(missing)


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    for rid, r in roads.items():
        for obj in r.iter("object"):
            oid = obj.get("id")
            loc_obj = f"road {rid} object id={oid}"

            outlines = _outlines_of(obj)
            has_outline = len(outlines) > 0
            markings_pairs = _collect_markings(obj)

            outline_info = {}
            for ol in outlines:
                ptype, ordered_ids, idset = _outline_points(ol)
                outline_info[id(ol)] = (ptype, ordered_ids, idset)

            # --- markings_with_outline / markings_without_outline (placement) ---
            for m_elem, parent in markings_pairs:
                if has_outline:
                    if parent.tag != "outline":
                        flags.append(
                            (
                                "road.object.marking.markings_with_outline",
                                f"object id={oid} は outline を使用するが <markings> が "
                                f"<{parent.tag}> 直下にある（<outline>内に配置すべき）",
                                loc_obj,
                            )
                        )
                else:
                    if parent.tag != "object":
                        flags.append(
                            (
                                "road.object.marking.markings_without_outline",
                                f"object id={oid} は outline 未使用だが <markings> が "
                                f"<object> 直下ではなく <{parent.tag}> 直下にある",
                                loc_obj,
                            )
                        )

            # --- per-<marking> checks ---
            for m_elem, parent in markings_pairs:
                parent_tag = parent.tag
                ol_ptype = ol_ids = ol_idset = None
                ol_idattr = None
                closed = False
                if parent_tag == "outline":
                    ol_ptype, ol_ids, ol_idset = outline_info.get(
                        id(parent), (None, [], set())
                    )
                    ol_idattr = parent.get("id")
                    closed = parent.get("closed") == "true"

                for mi, mk in enumerate(m_elem.findall("marking")):
                    crefs = mk.findall("cornerReference")
                    ncref = len(crefs)
                    has_side = bool(mk.get("side"))
                    if parent_tag == "outline":
                        loc_m = f"{loc_obj} outline id={ol_idattr} marking#{mi}"
                    else:
                        loc_m = f"{loc_obj} marking#{mi}"

                    # --- colour (both category names -- same underlying attribute) ---
                    if not mk.get("color"):
                        detail = f"marking (parent=<{parent_tag}>) に @color が未定義"
                        flags.append(("road.object.marking.colour", detail, loc_m))
                        flags.append(
                            ("road.object.object_marking.colour", detail, loc_m)
                        )

                    if parent_tag == "object" and not has_outline:
                        # --- no_cornerreference_if_no_outline ---
                        if ncref > 0:
                            flags.append(
                                (
                                    "road.object.marking.no_cornerreference_if_no_outline",
                                    f"outline未使用objectのmarkingにcornerReferenceが{ncref}件ある",
                                    loc_m,
                                )
                            )
                        # --- no_outline_side_attr ---
                        if not has_side:
                            flags.append(
                                (
                                    "road.object.marking.no_outline_side_attr",
                                    "outline未使用objectのmarkingに@sideが未定義",
                                    loc_m,
                                )
                            )
                        continue

                    if parent_tag != "outline":
                        # markings misplaced directly under <outlines> (or elsewhere) already
                        # reported by markings_with_outline/without_outline above; the
                        # outline-relative checks below need a concrete owning outline.
                        continue

                    # --- outline_corner_reference_count: 1.7.0 flat >=2 ---
                    if not has_side and ncref < 2:
                        flags.append(
                            (
                                "road.object.marking.outline_corner_reference_count",
                                f"outline内markingのcornerReferenceが{ncref}件（2件以上必要）",
                                loc_m,
                            )
                        )

                    # --- outline_corner_reference_count: 1.9.0 curveLocal-aware ---
                    min_needed = 1 if ol_ptype == "curveLocal" else 2
                    if not has_side and ncref < min_needed:
                        flags.append(
                            (
                                "road.object.object_marking.outline_corner_reference_count",
                                f"outline(id={ol_idattr}, type={ol_ptype})内markingのcornerReferenceが"
                                f"{ncref}件（{min_needed}件以上必要）",
                                loc_m,
                            )
                        )

                    if ncref == 0:
                        continue

                    # --- complete_or_partial_on_outline: referential integrity ---
                    ref_ids_doc_order = [cr.get("id") for cr in crefs]
                    bad_ids = [
                        rid_ for rid_ in ref_ids_doc_order if rid_ not in ol_idset
                    ]
                    if bad_ids:
                        flags.append(
                            (
                                "road.object.marking.complete_or_partial_on_outline",
                                f"cornerReference id={bad_ids} が所属outline(id={ol_idattr})の"
                                f"corner点集合{sorted(ol_idset)}に存在しない",
                                loc_m,
                            )
                        )

                    id_to_index = {pid: i for i, pid in enumerate(ol_ids)}
                    resolved_indices = [
                        id_to_index[i] for i in ref_ids_doc_order if i in id_to_index
                    ]

                    # --- enclosed_outline_marking: 2 same-id refs => full enclosure idiom,
                    #     only meaningful on a closed outline ---
                    if (
                        ncref == 2
                        and ref_ids_doc_order[0] == ref_ids_doc_order[1]
                        and ref_ids_doc_order[0] in ol_idset
                    ):
                        if not closed:
                            flags.append(
                                (
                                    "road.object.object_marking.enclosed_outline_marking",
                                    f"cornerReference id={ref_ids_doc_order[0]} を2回参照する完全外周"
                                    f"markingだが所属outline(id={ol_idattr})が closed=true でない",
                                    loc_m,
                                )
                            )
                        continue  # this idiom is exempt from the ordering/contiguity checks below

                    if len(resolved_indices) < 2 or ol_ptype is None:
                        continue
                    n_pts = len(ol_ids)
                    idx_set = set(resolved_indices)

                    # --- include_points_between_cornerReferences: contiguous-run check ---
                    if not _contiguous_ok(idx_set, n_pts, closed):
                        s = sorted(idx_set)
                        missing_ids = [
                            ol_ids[i]
                            for i in range(s[0], s[-1] + 1)
                            if i not in idx_set
                        ]
                        flags.append(
                            (
                                "road.object.object_marking.include_points_between_cornerReferences",
                                f"cornerReference {ref_ids_doc_order} が outline(id={ol_idattr}) 上で"
                                f"連続していない（未参照の中間点 id={missing_ids}）",
                                loc_m,
                            )
                        )

                    # --- keep_id_ordered ---
                    if not _ordered_ok(resolved_indices, closed):
                        flags.append(
                            (
                                "road.object.object_marking.keep_id_ordered",
                                f"cornerReference の並び {ref_ids_doc_order} が outline(id={ol_idattr}) "
                                "の点順序と一致しない",
                                loc_m,
                            )
                        )

    return flags


if __name__ == "__main__":
    import glob
    import sys
    import xml.etree.ElementTree as ET
    from collections import Counter
    from pathlib import Path

    ROOT = Path(r"e:/Repository/GT_esmini/esmini")
    files = []
    for f in glob.glob(str(ROOT / "**/*.xodr"), recursive=True):
        rp = str(Path(f).relative_to(ROOT)).replace("\\", "/")
        if rp.startswith(("thirdparty/", "dist/", "build/")) or "/build/" in rp:
            continue
        files.append(f)
    files = sorted(set(files))

    def rel(f):
        return str(Path(f).relative_to(ROOT)).replace("\\", "/")

    def bucket(f):
        rp = rel(f)
        if "test/odr_fixtures/official/" in rp:
            return "official(ASAM)"
        if rp.startswith("resources/xodr/"):
            return "GT:resources/xodr"
        if "scenario_authoring" in rp or "/generated/" in rp:
            return "GT:generated"
        if "GT_esmini/test/" in rp:
            return "GT:test"
        if rp.startswith("EnvironmentSimulator/") or "OSMP" in rp:
            return "upstream(out-of-scope)"
        return "other"

    all_flags = []  # (file, rule, detail, location)
    parse_err = 0
    exceptions = []
    for f in files:
        try:
            root = ET.parse(f).getroot()
        except Exception:
            parse_err += 1
            continue
        roads = {r.get("id"): r for r in root.iter("road")}
        road_ids = set(roads)
        junctions = {j.get("id"): j for j in root.iter("junction")}
        junction_ids = set(junctions)
        try:
            res = run_checks(f, root, roads, road_ids, junctions, junction_ids)
        except Exception as e:
            exceptions.append((f, repr(e)))
            continue
        for rule, detail, loc in res:
            all_flags.append((f, rule, detail, loc))

    byrule = Counter(r for _, r, _, _ in all_flags)
    byrule_official = Counter(
        r for f, r, _, _ in all_flags if bucket(f) == "official(ASAM)"
    )
    byrule_gt = Counter(r for f, r, _, _ in all_flags if bucket(f).startswith("GT:"))
    bkt_files = Counter(bucket(f) for f in files)

    print(
        f"files scanned: {len(files)}  parse_err(skipped): {parse_err}  exceptions: {len(exceptions)}"
    )
    for f, e in exceptions:
        print(f"  EXC {rel(f)}: {e}")
    print(f"total flags: {len(all_flags)}")
    print("files by bucket:", dict(bkt_files))
    print("\nby rule (total / official / GT-authored):")
    all_rule_names = sorted(set(byrule) | set(byrule_official) | set(byrule_gt))
    for rk in all_rule_names:
        print(
            f"  {rk:60s} {byrule.get(rk,0):4d} / {byrule_official.get(rk,0):4d} / {byrule_gt.get(rk,0):4d}"
        )

    print("\nsample flags (up to 60):")
    for f, r, d, loc in all_flags[:60]:
        print(f"  [{r}] {rel(f)} :: {loc} -- {d}")

    if "-v" in sys.argv:
        print("\nALL flags:")
        for f, r, d, loc in all_flags:
            print(f"  [{r}] {rel(f)} :: {loc} -- {d}")
