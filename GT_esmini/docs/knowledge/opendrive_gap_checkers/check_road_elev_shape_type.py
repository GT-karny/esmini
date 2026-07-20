"""OpenDRIVE Annex F gap-rule checkers -- category-group "road_elev_shape_type".

Implements (all UID prefix asam.net:xodr:<ver>:...):
  - road.elevation.elem_asc_order       : <elevation> elements ascending by s
  - road.superelevation.elem_asc_order  : <superelevation> elements ascending by s
  - road.shape.elem_asc_order           : <shape> elements ascending by (s, t)
  - road.type.elem_asc_order            : <type> elements ascending by s
  - road.type.only_alpha_2_country_codes: <type>/@country must be ISO 3166-1 alpha-2
                                           (or the OpenDRIVE generic keyword), never alpha-3
                                           or a deprecated full country-name enum value.
  - road.use_cases.shape_elements_start_right : for each s-group with >=2 <shape> elements
                                           (a genuinely piecewise, one-sided shape definition)
                                           the minimum t-value must be negative (right side
                                           first). A single shape entry per s-group is exempt
                                           (e.g. a lone t~=0 anchor whose polynomial spans the
                                           full width -- a legitimate constant crossfall, see
                                           spec 10.5.3 -- is not "one-sided").

Not implemented (classified in the structured report, not here):
  - road.elevation.elev_along_ref_line       (gap_ambiguous -- schema-tautological)
  - road.shape.t_definition_coverage         (gap_geometry_math -- needs lane-width poly3 eval)
  - road.type.create_new_type_in_parent      (gap_ambiguous -- see report reasoning)
  - road.type.lane_type_may_differ_from_parent (gap_ambiguous -- permissive "may", no forbidden state)

Pure stdlib xml.etree.ElementTree. Matches the parsing idiom of scratchpad/gap_rule_check.py
(roads dict / road_ids / junction_ids passed in by the integration layer -- not re-derived here).
"""
from collections import defaultdict

_EPS = 1e-6


def _fnum(x, d=0.0):
    try:
        return float(x)
    except (TypeError, ValueError):
        return d


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    for rid, r in roads.items():
        # ---------------------------------------------------------------
        # road.elevation.elem_asc_order
        # ---------------------------------------------------------------
        ep = r.find("elevationProfile")
        if ep is not None:
            prev_s = None
            for e in ep.findall("elevation"):
                s = _fnum(e.get("s"))
                if prev_s is not None and s < prev_s - _EPS:
                    flags.append((
                        "road.elevation.elem_asc_order",
                        f"elevation s={s:g} が直前の elevation s={prev_s:g} より小さい（s昇順違反）",
                        f"road {rid} s={s:g}",
                    ))
                prev_s = s

        # ---------------------------------------------------------------
        # road.superelevation.elem_asc_order
        # ---------------------------------------------------------------
        lp = r.find("lateralProfile")
        if lp is not None:
            prev_s = None
            for se in lp.findall("superelevation"):
                s = _fnum(se.get("s"))
                if prev_s is not None and s < prev_s - _EPS:
                    flags.append((
                        "road.superelevation.elem_asc_order",
                        f"superelevation s={s:g} が直前の superelevation s={prev_s:g} より小さい（s昇順違反）",
                        f"road {rid} s={s:g}",
                    ))
                prev_s = s

            # -----------------------------------------------------------
            # road.shape.elem_asc_order  +  road.use_cases.shape_elements_start_right
            # -----------------------------------------------------------
            shapes = lp.findall("shape")
            if shapes:
                prev_s = prev_t = None
                for sh in shapes:
                    s = _fnum(sh.get("s"))
                    t = _fnum(sh.get("t"))
                    if prev_s is not None:
                        if s < prev_s - _EPS:
                            flags.append((
                                "road.shape.elem_asc_order",
                                f"shape s={s:g} が直前の shape s={prev_s:g} より小さい（s昇順違反）",
                                f"road {rid} s={s:g} t={t:g}",
                            ))
                        elif abs(s - prev_s) <= _EPS and t < prev_t - _EPS:
                            flags.append((
                                "road.shape.elem_asc_order",
                                f"同一 s={s:g} 内で shape t={t:g} が直前の t={prev_t:g} より小さい（t昇順違反）",
                                f"road {rid} s={s:g} t={t:g}",
                            ))
                    prev_s, prev_t = s, t

                groups = defaultdict(list)
                for sh in shapes:
                    s = _fnum(sh.get("s"))
                    t = _fnum(sh.get("t"))
                    groups[round(s, 6)].append(t)
                for s_key, tlist in groups.items():
                    if len(tlist) < 2:
                        # A single shape entry per s is not a piecewise one-sided
                        # definition -- e.g. a lone t~=0 anchor's polynomial spans
                        # the full cross-section (legitimate constant crossfall,
                        # spec 10.5.3). Only >=2 entries can genuinely establish
                        # "right side missing".
                        continue
                    min_t = min(tlist)
                    if min_t >= -_EPS:
                        flags.append((
                            "road.use_cases.shape_elements_start_right",
                            f"s={s_key:g} の shape 群（{len(tlist)}要素）の最小t={min_t:g} が負でない"
                            "（右側 t<0 の要素を持たない片側定義の疑い）",
                            f"road {rid} s={s_key:g}",
                        ))

        # ---------------------------------------------------------------
        # road.type.elem_asc_order  +  road.type.only_alpha_2_country_codes
        # ---------------------------------------------------------------
        prev_s = None
        for ty in r.findall("type"):
            s = _fnum(ty.get("s"))
            if prev_s is not None and s < prev_s - _EPS:
                flags.append((
                    "road.type.elem_asc_order",
                    f"type s={s:g} が直前の type s={prev_s:g} より小さい（s昇順違反）",
                    f"road {rid} s={s:g}",
                ))
            prev_s = s

            country = ty.get("country")
            if country and country != "OpenDRIVE" and len(country) != 2:
                flags.append((
                    "road.type.only_alpha_2_country_codes",
                    f"type s={s:g} の country=\"{country}\" がALPHA-2でない"
                    "（ALPHA-3または非標準の国名表記の疑い）",
                    f"road {rid} s={s:g}",
                ))

    return flags
