"""Gap-rule checkers: OpenDRIVE Annex F group 'road_lane_core'.

Implements the road.lane.* lane-section / lane-numbering rules assigned to this group
(see scratchpad/checks_gap/rules_road_lane_core.json), i.e. Annex F rules not covered by
qc-opendrive. Pure xml.etree.ElementTree, stdlib-only. Follows the shared module contract
in scratchpad/checks_gap/CONTRACT.md: run_checks(file_path, root, roads, road_ids,
junctions, junction_ids) -> list[(rule_name, detail, location)].

Rules implemented here (bare rule_name as it appears in rules_road_lane_core.json):
  - road.lane.center_elem_definition : exactly one <center> per <laneSection>
  - road.lane.center_lane            : road has >=1 lane layer with an actual center lane
  - road.lane.center_lane_id         : center lane's @id == 0
  - road.lane.center_lane_no_width   : center lane has no <width> child
  - road.lane.center_lane_singular   : exactly one <lane> inside <center>
  - road.lane.lane_id_unique         : lane ids unique per laneSection *and* <lanes> layer
  - road.lane.lane_listing           : left->center->right lane ids listed in descending order
  - road.lane.lane_order             : id nearest the center is 1 (left) / -1 (right)
  - road.lane.lane_order_no_gaps     : abs(id) run per side has no internal gap
  - road.lane.lane_sect_first        : first laneSection (per layer) has @s == 0.0
  - road.lane.lane_sect_min_amount   : each <lanes> element has >=1 <laneSection>
  - road.lane.lane_section_drivable  : laneSection has a <left> or <right> group (skipped
    when center lane @type="none" -- the corpus-confirmed "non-drivable island" pattern)
  - road.lane.lanes_numbered_correctly : left ids positive, right ids negative
  - road.lane.s_attr_value           : every <laneSection> has an @s attribute
  - road.lane.lane_reverse_left_right : PARTIAL_DETERMINISTIC -- flags only the
    airtight whole-laneSection signature: a two-sided laneSection (both <left> and
    <right> contain >=1 @type="driving" lane) where EVERY driving lane on BOTH sides
    carries @direction="reversed". Per 11.3.1's default-direction formulas this
    symmetric all-reversed pattern is indistinguishable from a wholesale RHT<->LHT
    flip via @direction (explicitly forbidden by Annex F.6.12.11), independent of the
    road's own @rule. Any partial/mixed/single-side reversal (the sanctioned
    contraflow-lane use of @direction=reversed, see 09_lane_attributes_18.xodr) is
    deliberately left uncovered -- see impl_briefs/road_lane_core.md DO_NOT_IMPLEMENT.
"""


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    def fnum(x):
        try:
            return float(x)
        except (TypeError, ValueError):
            return None

    def lane_ids(grp):
        out = []
        if grp is None:
            return out
        for lane in grp.findall("lane"):
            try:
                out.append(int(lane.get("id")))
            except (TypeError, ValueError):
                pass
        return out

    def driving_lanes(grp):
        """<lane> elements with @type='driving' directly under a <left>/<right>
        group; empty list if grp is None (matches lane_ids' no-side convention)."""
        out = []
        if grp is None:
            return out
        for lane in grp.findall("lane"):
            if lane.get("type") == "driving":
                out.append(lane)
        return out

    for rid, r in roads.items():
        lanes_elems = r.findall("lanes")

        # Road-wide abs(id) sets per side, gathered across ALL laneSections of this
        # road (any layer) -- used below by road.lane.lane_order_no_gaps to make the
        # per-section gap check cross-section aware: a lane numbering scheme that keeps
        # an id "reserved" for a lane that only exists in a DIFFERENT section of the
        # same road (e.g. a sidewalk kept at a stable id=-6 while ordinary driving
        # lanes -3..-5 come and go between sections) is a sanctioned stable-id-across-
        # sections authoring pattern, not a genuine numbering gap -- confirmed false
        # positive on I2_stable_id_across_sections_FP.xodr and on the official ASAM
        # calibration file UC_5Road_Junction.xodr road 82 s=0.
        road_side_absids = {"left": set(), "right": set()}
        for ls_all in r.iter("laneSection"):
            road_side_absids["left"].update(
                abs(i) for i in lane_ids(ls_all.find("left"))
            )
            road_side_absids["right"].update(
                abs(i) for i in lane_ids(ls_all.find("right"))
            )

        # --- road.lane.lane_sect_min_amount: each <lanes> element needs >=1 <laneSection> ---
        for lanes_el in lanes_elems:
            layer = lanes_el.get("layer") or "default"
            if len(lanes_el.findall("laneSection")) == 0:
                flags.append(
                    (
                        "road.lane.lane_sect_min_amount",
                        f"<lanes layer={layer}> に <laneSection> が0個",
                        f"road {rid}",
                    )
                )

        # --- road.lane.lane_sect_first: first laneSection (doc order, per layer) @s == 0.0 ---
        # Only meaningful for the permanent/default layer: a "temporary" <lanes> layer is by
        # definition a bounded overlay (see XSD assert tying layer="permanent" to laneSection
        # having no explicit @length) and legitimately starts mid-road, e.g. a roadworks patch
        # at s=2000 (confirmed empirically: both official-set hits were layer="temporary").
        for lanes_el in lanes_elems:
            layer = lanes_el.get("layer") or "default"
            if layer == "temporary":
                continue
            secs = lanes_el.findall("laneSection")
            if not secs:
                continue
            first_s = fnum(secs[0].get("s"))
            if first_s is None or abs(first_s) > 1e-6:
                flags.append(
                    (
                        "road.lane.lane_sect_first",
                        f"<lanes layer={layer}> の最初のlaneSection s={secs[0].get('s')!r} (0.0であるべき)",
                        f"road {rid}",
                    )
                )

        # --- road.lane.center_lane: road-wide, >=1 lane layer with an actual center lane ---
        has_center_anywhere = False
        for lanes_el in lanes_elems:
            for ls in lanes_el.findall("laneSection"):
                c = ls.find("center")
                if c is not None and c.find("lane") is not None:
                    has_center_anywhere = True
                    break
            if has_center_anywhere:
                break
        if not has_center_anywhere:
            flags.append(
                (
                    "road.lane.center_lane",
                    "road全体でcenter laneを持つlane layer(laneSection)が1つも無い",
                    f"road {rid}",
                )
            )

        # --- road.lane.lane_id_unique: unique per laneSection AND per <lanes> layer ---
        for lanes_el in lanes_elems:
            layer = lanes_el.get("layer") or "default"
            for ls in lanes_el.findall("laneSection"):
                s = ls.get("s")
                ids = []
                for side in ("left", "center", "right"):
                    grp = ls.find(side)
                    if grp is None:
                        continue
                    for lane in grp.findall("lane"):
                        lid = lane.get("id")
                        if lid is not None:
                            ids.append(lid)
                dup = sorted(
                    {i for i in ids if ids.count(i) > 1}, key=lambda v: (len(v), v)
                )
                if dup:
                    flags.append(
                        (
                            "road.lane.lane_id_unique",
                            f"lane id {dup} が laneSection 内(layer={layer})で重複",
                            f"road {rid} s={s}",
                        )
                    )

        # --- per-laneSection checks (layer-agnostic; r.iter covers both permanent/temporary) ---
        for ls in r.iter("laneSection"):
            s = ls.get("s")
            loc = f"road {rid} s={s}"

            # road.lane.s_attr_value
            if ls.get("s") is None:
                flags.append(
                    (
                        "road.lane.s_attr_value",
                        "laneSection に @s 属性が無い",
                        f"road {rid}",
                    )
                )

            centers = ls.findall("center")

            # road.lane.center_elem_definition
            if len(centers) != 1:
                flags.append(
                    (
                        "road.lane.center_elem_definition",
                        f"<center> 要素数={len(centers)}（各s座標につき1であるべき）",
                        loc,
                    )
                )

            for center in centers:
                clanes = center.findall("lane")

                # road.lane.center_lane_singular
                if len(clanes) != 1:
                    flags.append(
                        (
                            "road.lane.center_lane_singular",
                            f"<center> 内 <lane> 要素数={len(clanes)}（常に1であるべき）",
                            loc,
                        )
                    )

                for cl in clanes:
                    cid = cl.get("id")
                    # road.lane.center_lane_id
                    if cid != "0":
                        flags.append(
                            (
                                "road.lane.center_lane_id",
                                f"center laneの id={cid!r} (0であるべき)",
                                loc,
                            )
                        )
                    # road.lane.center_lane_no_width
                    if cl.find("width") is not None:
                        flags.append(
                            (
                                "road.lane.center_lane_no_width",
                                "center laneに<width>要素が使われている(禁止)",
                                loc,
                            )
                        )

            left = ls.find("left")
            right = ls.find("right")
            single_side = (ls.get("singleSide") or "").strip().lower() == "true"
            # A center lane explicitly typed "none" marks the laneSection as a deliberate
            # non-drivable surface (traffic island / median outline host, etc.) -- confirmed
            # against the official ASAM Ex_SmoothObjectOutline_traffic_island example, and
            # empirically the *only* pattern producing a center-only laneSection anywhere in
            # the corpus. Center type anything else (missing/"driving"/...) with no left/right
            # is still flagged as a genuine drivability gap.
            center_type_none = any(
                (cl.get("type") == "none")
                for c in ls.findall("center")
                for cl in c.findall("lane")
            )

            # road.lane.lane_section_drivable
            if (
                left is None
                and right is None
                and not single_side
                and not center_type_none
            ):
                flags.append(
                    (
                        "road.lane.lane_section_drivable",
                        "laneSectionに<left>も<right>も無く、走行可能レーンが定義されていない",
                        loc,
                    )
                )

            left_ids = lane_ids(left)
            right_ids = lane_ids(right)

            # road.lane.lane_reverse_left_right (PARTIAL_DETERMINISTIC): only the
            # airtight whole-section signature -- both sides have >=1 driving lane
            # (a one-sided laneSection has no RHT/LHT counterpart to swap against,
            # out of scope) AND every driving lane on BOTH sides is @direction=
            # "reversed" (literal, absent/"standard"/"both" do not count). Partial/
            # mixed/single-side reversal is the sanctioned contraflow-lane use of
            # @direction=reversed and is deliberately left unflagged (ambiguous
            # residual, see impl_briefs/road_lane_core.md).
            left_driving = driving_lanes(left)
            right_driving = driving_lanes(right)
            if left_driving and right_driving:
                all_driving = left_driving + right_driving
                if all((lane.get("direction") == "reversed") for lane in all_driving):
                    rev_ids = [lane.get("id") for lane in all_driving]
                    flags.append(
                        (
                            "road.lane.lane_reverse_left_right",
                            f"laneSection内の走行レーン全て(id={rev_ids})が"
                            f"@direction='reversed'（左右対称の一括反転はRHT/LHT切替に"
                            f"該当する可能性、11.3.1/F.6.12.11で禁止）",
                            loc,
                        )
                    )

            # road.lane.lanes_numbered_correctly
            for lid in left_ids:
                if lid <= 0:
                    flags.append(
                        (
                            "road.lane.lanes_numbered_correctly",
                            f"<left>内 lane id={lid} は正の値であるべき",
                            loc,
                        )
                    )
            for lid in right_ids:
                if lid >= 0:
                    flags.append(
                        (
                            "road.lane.lanes_numbered_correctly",
                            f"<right>内 lane id={lid} は負の値であるべき",
                            loc,
                        )
                    )

            # road.lane.lane_order: id nearest the center must be 1 (left) / -1 (right).
            # Skipped when the sign itself is already wrong (lanes_numbered_correctly covers
            # that case; "nearest to center" is ill-defined once sign is broken).
            if left_ids and all(i > 0 for i in left_ids):
                if min(left_ids) != 1:
                    flags.append(
                        (
                            "road.lane.lane_order",
                            f"<left> 中心に最も近いlane id={min(left_ids)} (1から始まるべき)",
                            loc,
                        )
                    )
            if right_ids and all(i < 0 for i in right_ids):
                if max(right_ids) != -1:
                    flags.append(
                        (
                            "road.lane.lane_order",
                            f"<right> 中心に最も近いlane id={max(right_ids)} (-1から始まるべき)",
                            loc,
                        )
                    )

            # road.lane.lane_order_no_gaps: abs(id) run per side, no internal gap --
            # cross-section aware: a "missing" id that exists as a lane in ANOTHER
            # laneSection of this same road (stable-id-across-sections pattern) is not
            # a genuine gap, see road_side_absids comment above.
            for label, ids in (("left", left_ids), ("right", right_ids)):
                absids = sorted(set(abs(i) for i in ids))
                if absids and (absids[-1] - absids[0] + 1) != len(absids):
                    full_range = set(range(absids[0], absids[-1] + 1))
                    genuine_missing = sorted(
                        full_range - set(absids) - road_side_absids[label]
                    )
                    if genuine_missing:
                        flags.append(
                            (
                                "road.lane.lane_order_no_gaps",
                                f"{label} group ids(abs)={absids} に欠番がある"
                                f"(欠番id={genuine_missing}、road内の他区間にも存在しない)",
                                loc,
                            )
                        )

            # road.lane.lane_listing: doc order left->center->right should be strictly
            # descending by id ("should" recommendation, still flagged as a review item)
            seq = []
            if left is not None:
                seq.extend(left_ids)
            for center in centers:
                seq.extend(lane_ids(center))
            if right is not None:
                seq.extend(right_ids)
            for i in range(1, len(seq)):
                if seq[i] >= seq[i - 1]:
                    flags.append(
                        (
                            "road.lane.lane_listing",
                            f"lane掲載順がID降順になっていない: {seq}",
                            loc,
                        )
                    )
                    break

    return flags
