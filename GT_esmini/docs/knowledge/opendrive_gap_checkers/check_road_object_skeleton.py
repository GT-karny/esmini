"""Gap-rule checkers for category-group road.object.skeleton (Annex F, OpenDRIVE 1.9.0).

Covers the <object><skeleton><polyline><vertexRoad|vertexLocal> structure that lets an
object's shape be described more closely than its bounding box (gantries, custom poles,
etc.). Structural/attribute-only checks per the shared gap-rule contract; the pure
3D-geometry rules (point-in-bounding-volume tests) are classified gap_geometry_math and
are NOT implemented here (see rules_road_object_skeleton.json / structured report).

Implemented (structural, attribute-presence / element-count / cross-attribute only):
  - polyline_followed_by_vertex        : polyline must have >=2 vertexRoad XOR >=2 vertexLocal
  - vertex_local.element_min_amount    : if vertexLocal used at all, need >=2
  - vertex_road.element_min_amount     : if vertexRoad used at all, need >=2
  - vertex_local.no_mixing_road_local  : no vertexRoad+vertexLocal mixed in one polyline
  - vertex_road.polyline_elements      : mirror of the above (both fire together)
  - vertex_local.vertex_local_elements : a vertexLocal shall not carry both @radius and @width/@length
  - vertex_road.no_radius_with_width_length : same, for vertexRoad
  - use_radius_or_width_length         : all vertex elements of one polyline must agree on
                                          radius-mode vs width/length-mode (no partial/mixed mode)

Not implemented here (see report): points_boundary_inside_box, points_inside_box,
points_requirements (gap_geometry_math - need 3D coordinate transform + bounding-volume
overlap); vertex_local.linear_interpolation, vertex_road.linear_interpolation
(gap_ambiguous - descriptive consumer-side interpolation behavior, no author-violable
condition distinct from use_radius_or_width_length above).
"""


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    def vmode(v):
        has_r = v.get("radius") is not None
        has_w = v.get("width") is not None
        has_l = v.get("length") is not None
        return has_r, has_w, has_l

    for rid, r in roads.items():
        for obj in r.iter("object"):
            oid = obj.get("id")
            skel = obj.find("skeleton")
            if skel is None:
                continue
            for pi, poly in enumerate(skel.findall("polyline")):
                pid = poly.get("id")
                pid_disp = pid if pid is not None else f"#{pi}"
                loc = f"road {rid} object id={oid} polyline id={pid_disp}"

                vroad = poly.findall("vertexRoad")
                vlocal = poly.findall("vertexLocal")
                nR, nL = len(vroad), len(vlocal)

                # --- no_mixing_road_local / polyline_elements (mirror pair) ---
                if nR > 0 and nL > 0:
                    flags.append(
                        (
                            "road.object.skeleton.vertex_local.no_mixing_road_local",
                            f"polyline id={pid_disp} に vertexRoad({nR}件) と vertexLocal({nL}件) が混在",
                            loc,
                        )
                    )
                    flags.append(
                        (
                            "road.object.skeleton.vertex_road.polyline_elements",
                            f"polyline id={pid_disp} に vertexLocal({nL}件) と vertexRoad({nR}件) が混在",
                            loc,
                        )
                    )

                # --- polyline_followed_by_vertex: exactly one kind, >=2 of it ---
                valid_pure = (nR >= 2 and nL == 0) or (nL >= 2 and nR == 0)
                if not valid_pure:
                    flags.append(
                        (
                            "road.object.skeleton.polyline_followed_by_vertex",
                            f"polyline id={pid_disp}: vertexRoad={nR}件, vertexLocal={nL}件"
                            "（いずれか一方を2個以上で構成する必要）",
                            loc,
                        )
                    )

                # --- element_min_amount, scoped to whichever kind is actually used ---
                if 0 < nL < 2:
                    flags.append(
                        (
                            "road.object.skeleton.vertex_local.element_min_amount",
                            f"polyline id={pid_disp}: vertexLocal が{nL}件のみ（2件以上必要）",
                            loc,
                        )
                    )
                if 0 < nR < 2:
                    flags.append(
                        (
                            "road.object.skeleton.vertex_road.element_min_amount",
                            f"polyline id={pid_disp}: vertexRoad が{nR}件のみ（2件以上必要）",
                            loc,
                        )
                    )

                # --- per-vertex radius-vs-width/length exclusivity + polyline-wide mode ---
                modes = set()
                for v in vlocal:
                    has_r, has_w, has_l = vmode(v)
                    if has_r and (has_w or has_l):
                        flags.append(
                            (
                                "road.object.skeleton.vertex_local.vertex_local_elements",
                                f"polyline id={pid_disp} vertexLocal id={v.get('id')}: "
                                "@radius と @width/@length を同時指定",
                                loc,
                            )
                        )
                    if has_r:
                        modes.add("radius")
                    if has_w or has_l:
                        modes.add("widthlength")
                    if not (has_r or has_w or has_l):
                        modes.add("none")
                for v in vroad:
                    has_r, has_w, has_l = vmode(v)
                    if has_r and (has_w or has_l):
                        flags.append(
                            (
                                "road.object.skeleton.vertex_road.no_radius_with_width_length",
                                f"polyline id={pid_disp} vertexRoad id={v.get('id')}: "
                                "@radius と @width/@length を同時指定",
                                loc,
                            )
                        )
                    if has_r:
                        modes.add("radius")
                    if has_w or has_l:
                        modes.add("widthlength")
                    if not (has_r or has_w or has_l):
                        modes.add("none")

                # use_radius_or_width_length: one polyline must not mix radius-mode
                # vertices with width/length-mode vertices, nor partially omit the
                # chosen mode on some vertices ("for all of its vertex elements").
                determinate = modes - {"none"}
                if len(determinate) > 1 or (determinate and "none" in modes):
                    flags.append(
                        (
                            "road.object.skeleton.use_radius_or_width_length",
                            f"polyline id={pid_disp}: 頂点間で @radius / @width+@length の使用が不統一"
                            f"（modes={sorted(modes)}）",
                            loc,
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

    print("\nsample flags (up to 40):")
    for f, r, d, loc in all_flags[:40]:
        print(f"  [{r}] {rel(f)} :: {loc} -- {d}")

    if "-v" in sys.argv:
        print("\nALL flags:")
        for f, r, d, loc in all_flags:
            print(f"  [{r}] {rel(f)} :: {loc} -- {d}")
