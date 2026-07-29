"""Gap-rule checker group: road_signal_ref_validity (Annex F category-group)

Implements (see rules_road_signal_ref_validity.json for verbatim descriptions):
  - road.signal.reference.from_lower_equal_to   : <signalReference><validity fromLane/toLane>
                                                   fromLane must be <= toLane.
  - road.signal.reference.specify_direction     : every <signalReference> must carry a
                                                   non-empty @orientation attribute.
  - road.signal.reference.used_for_signals_only : <signalReference>/@id must resolve to a
                                                   <signal>/@id defined somewhere in the file
                                                   (global id namespace, not per-road).
  - road.signal.semantics.no_semantics_without_category : a <signal>'s (or a board sub-<sign>'s
                                                   -- both extend the same abstract
                                                   t_road_signals_signal base type, and spec 14.8
                                                   Table 135 itself frames the rule in terms of
                                                   "<signal> or <sign>") <semantics> block must
                                                   not contain ONLY "supplementary*" elements
                                                   (supplementaryTime/Allows/Prohibits/Distance/
                                                   Environment/Explanatory) without at least one
                                                   real category element (speed/lane/priority/
                                                   prohibited/routing/streetname/tourist/warning/
                                                   parking) -- supplementary elements have "no
                                                   meaning on their own" per spec 14.8.1.
  - road.signal.validity.right_hand_traffic_lane_ids / .left_hand_traffic_lane_ids
    road.signal.reference.right_hand_traffic_lane_ids / .left_hand_traffic_lane_ids
                                                 : PARTIAL_DETERMINISTIC re-classification
                                                   (see impl_briefs/road_signal_ref_validity.md).
                                                   For a <signal> (validity.*) / <signalReference>
                                                   (reference.*) with @orientation in ('+','-'),
                                                   aggregate all lane ids referenced across its
                                                   own <validity> children (lane id 0 neutral) into
                                                   has_pos/has_neg. The RHT/LHT-derived expected
                                                   sign is checked; flag ONLY the deterministic
                                                   "wrong sign present, correct sign absent"
                                                   sub-case (mirrors road.object.validty.*_hand_
                                                   traffic_lane_ids in check_road_object_core.py).
                                                   The "spans both signs while orientation !=
                                                   'none'" sub-case (has_pos AND has_neg both
                                                   True) is intentionally NOT flagged here -- see
                                                   the docstring note below and the impl brief for
                                                   why that residual stays an ambiguous, uncovered
                                                   case.

NOT implemented / left uncovered (documented ambiguous residual, see structured report):
the "spans both positive and negative lane ids while @orientation != 'none'" sub-case of the
four *_hand_traffic_lane_ids rules above. Empirical corpus investigation (see
scratchpad/checks_gap/explore_lane_ids*.py and impl_briefs/road_signal_ref_validity.md) found
this exact idiom recurring with clean, apparently-deliberate bounds in official fixtures
(UC_Simple-X-Junction-TrafficLights.xodr validity=(-3,3), UC_2Lane-RoundAbout-3Arms.xodr
validity=(-3,4)) and in GT-authored resources (fabriksgatan_traffic_lights.xodr,
straight_500m_signs.xodr) for plausible whole-cross-section sign semantics (priority-road,
no-stopping, traffic-light). Annex F defines no attribute letting a checker distinguish a
literal per-lane <validity> from a deliberate whole-cross-section span, so this sub-case
remains a spec-unresolved interpretive judgment call and is deliberately left unflagged (the
narrower "wrong-sign-only" sub-case implemented above IS fully deterministic and checkable).
"""

_CATEGORY_TAGS = {
    "lane",
    "parking",
    "priority",
    "prohibited",
    "routing",
    "speed",
    "streetname",
    "tourist",
    "warning",
}
_SUPPLEMENTARY_TAGS = {
    "supplementaryTime",
    "supplementaryAllows",
    "supplementaryProhibits",
    "supplementaryDistance",
    "supplementaryEnvironment",
    "supplementaryExplanatory",
}


def _as_int(x):
    try:
        return int(x)
    except (TypeError, ValueError):
        return None


def _traffic_hand(road):
    """<road>@rule, default RHT per XSD documentation (rule missing => RHT assumed).
    Mirrors check_road_object_core.py's _traffic_hand exactly (kept local -- modules
    in this suite are self-contained, no cross-module imports)."""
    r = road.get("rule") if road is not None else None
    return r if r in ("RHT", "LHT") else "RHT"


_DRIVING_LANE_TYPES = {
    "driving",
    "entry",
    "exit",
    "offRamp",
    "onRamp",
    "connectingRamp",
    "bidirectional",
}


def _driving_lane_ids(road):
    """Union of driving-type lane ids across all lane sections of the road. The RHT/LHT
    validity rule is about driving-DIRECTION semantics, so only driving lanes are in its
    scope. Guarding on this set kills the false-positive storm observed on the official
    ASAM set (40 -> 10): pedestrian/sidewalk connectors (walking lanes), border/shoulder/
    stop/none lanes, and one-sided cross-sections that have no driving lane of the
    expected sign must not be flagged -- the rule cannot demand negative-lane validity on
    a road that has no negative driving lane. The 10 residual flags are genuine literal
    violations on the ASAM motorway main roads (real driving lanes of both signs present,
    validity on the wrong-sign driving lane only)."""
    ids = set()
    if road is None:
        return ids
    for ls in road.iter("laneSection"):
        for side in ("left", "right"):
            g = ls.find(side)
            if g is None:
                continue
            for lane in g.findall("lane"):
                if lane.get("type") in _DRIVING_LANE_TYPES:
                    lid = _as_int(lane.get("id"))
                    if lid is not None:
                        ids.add(lid)
    return ids


def _lane_id_signs(elem, driving_ids):
    """Aggregate has_pos/has_neg over the DRIVING lane ids that elem's <validity>
    (fromLane..toLane) ranges actually span (elem is a <signal> or <signalReference>).
    Lane id 0 (center) is neutral. Non-driving lanes within a validity span are ignored
    -- the RHT/LHT rule is a driving-direction convention (see _driving_lane_ids).
    Malformed / fromLane>toLane validity children are skipped (that mismatch is
    road.signal.reference.from_lower_equal_to's concern, not this one's)."""
    has_pos = has_neg = False
    for v in elem.findall("validity"):
        fl, tl = _as_int(v.get("fromLane")), _as_int(v.get("toLane"))
        if fl is None or tl is None or fl > tl:
            continue
        for lid in range(min(fl, tl), max(fl, tl) + 1):
            if lid == 0 or lid not in driving_ids:
                continue
            if lid >= 1:
                has_pos = True
            elif lid <= -1:
                has_neg = True
    return has_pos, has_neg


def _road_has_expected_driving_side(driving_ids, expected):
    """True iff the road has at least one driving lane of the EXPECTED sign. Only then is
    it meaningful to flag validity that sits on the wrong-sign driving lane only."""
    if expected == "neg":
        return any(lid <= -1 for lid in driving_ids)
    return any(lid >= 1 for lid in driving_ids)


def _expected_sign(hand, orient):
    """RHT: '+' -> only negative lane ids expected ('neg'); '-' -> only positive ('pos').
    LHT: '+' -> only positive lane ids expected ('pos'); '-' -> only negative ('neg').
    """
    if hand == "RHT":
        return "neg" if orient == "+" else "pos"
    return "pos" if orient == "+" else "neg"


def _wrong_sign_only_violation(expected, has_pos, has_neg):
    """The deterministic checkable sub-case: the combined validity references real
    (non-zero) lane ids of ONLY the wrong sign, with no lane of the correct sign
    present anywhere among its own validity children. Deliberately returns False
    when has_pos and has_neg are both True (spans-both real lanes on both sides) --
    that sub-case is the documented ambiguous residual, left uncovered here."""
    if expected == "neg":
        return has_pos and not has_neg
    return has_neg and not has_pos


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    # Global signal-id namespace (signalReference/@id must resolve into this set, regardless
    # of which road hosts the <signal> vs which road hosts the <signalReference> -- that is
    # the entire point of the <signalReference> mechanism, cf. spec 14.5).
    all_signal_ids = set()
    for r in roads.values():
        for sig in r.iter("signal"):
            sid = sig.get("id")
            if sid is not None:
                all_signal_ids.add(sid)

    for rid, r in roads.items():
        hand = _traffic_hand(r)
        for sref in r.iter("signalReference"):
            ref_id = sref.get("id")
            s = sref.get("s")
            loc = f"road {rid} signalReference id={ref_id} s={s}"

            # --- road.signal.reference.specify_direction ---
            orient = sref.get("orientation")
            if orient is None or orient.strip() == "":
                flags.append(
                    (
                        "road.signal.reference.specify_direction",
                        f"signalReference id={ref_id} に @orientation が未指定（+/-/none のいずれかが必須）",
                        loc,
                    )
                )

            # --- road.signal.reference.used_for_signals_only ---
            if ref_id is not None and ref_id not in all_signal_ids:
                flags.append(
                    (
                        "road.signal.reference.used_for_signals_only",
                        f"signalReference id={ref_id} が参照する signal が未定義（signal要素のidとして存在しない）",
                        loc,
                    )
                )

            # --- road.signal.reference.from_lower_equal_to ---
            for v in sref.findall("validity"):
                fl, tl = _as_int(v.get("fromLane")), _as_int(v.get("toLane"))
                if fl is not None and tl is not None and fl > tl:
                    flags.append(
                        (
                            "road.signal.reference.from_lower_equal_to",
                            f"signalReference id={ref_id} の validity fromLane={fl} > toLane={tl}",
                            loc,
                        )
                    )

            # --- road.signal.reference.right_hand_traffic_lane_ids / .left_hand_traffic_lane_ids ---
            # Deterministic "wrong-sign-only" sub-case (see module docstring). SR is the
            # <signalReference> element itself -- it carries its own @orientation and its own
            # <validity> children (distinct from the <signal> it points to via @id), scoped to
            # the road R that physically hosts this <signalReference>, per the impl brief.
            if orient in ("+", "-"):
                driving_ids = _driving_lane_ids(r)
                has_pos, has_neg = _lane_id_signs(sref, driving_ids)
                if has_pos or has_neg:
                    expected = _expected_sign(hand, orient)
                    if _wrong_sign_only_violation(
                        expected, has_pos, has_neg
                    ) and _road_has_expected_driving_side(driving_ids, expected):
                        rule_name = (
                            "road.signal.reference.right_hand_traffic_lane_ids"
                            if hand == "RHT"
                            else "road.signal.reference.left_hand_traffic_lane_ids"
                        )
                        wrong_side = "正" if has_pos else "負"
                        right_side = "負" if expected == "neg" else "正"
                        flags.append(
                            (
                                rule_name,
                                f"signalReference id={ref_id} ({hand}) @orientation={orient!r} は"
                                f"{right_side}のレーンidのみ許容だが validity が{wrong_side}のレーンid"
                                f"のみを参照（{right_side}idは含まれない）",
                                loc,
                            )
                        )

    # --- road.signal.validity.right_hand_traffic_lane_ids / .left_hand_traffic_lane_ids ---
    # Deterministic "wrong-sign-only" sub-case (see module docstring). Mirrors
    # road.object.validty.*_hand_traffic_lane_ids in check_road_object_core.py, applied to
    # <signal> instead of <object>.
    for rid, r in roads.items():
        hand = _traffic_hand(r)
        driving_ids = _driving_lane_ids(r)
        for sig in r.iter("signal"):
            orient = sig.get("orientation")
            if orient not in ("+", "-"):
                continue
            has_pos, has_neg = _lane_id_signs(sig, driving_ids)
            if not has_pos and not has_neg:
                continue
            expected = _expected_sign(hand, orient)
            if _wrong_sign_only_violation(
                expected, has_pos, has_neg
            ) and _road_has_expected_driving_side(driving_ids, expected):
                rule_name = (
                    "road.signal.validity.right_hand_traffic_lane_ids"
                    if hand == "RHT"
                    else "road.signal.validity.left_hand_traffic_lane_ids"
                )
                sid = sig.get("id")
                wrong_side = "正" if has_pos else "負"
                right_side = "負" if expected == "neg" else "正"
                flags.append(
                    (
                        rule_name,
                        f"signal id={sid} ({hand}) @orientation={orient!r} は{right_side}のレーンid"
                        f"のみ許容だが validity が{wrong_side}のレーンidのみを参照"
                        f"（{right_side}idは含まれない）",
                        f"road {rid} signal id={sid}",
                    )
                )

    # --- road.signal.semantics.no_semantics_without_category ---
    # <semantics> is a child of the abstract t_road_signals_signal base type, which is extended
    # by BOTH <signal> (t_road_signals_signal_road) and <sign> (t_road_signals_board_sign, the
    # sub-signs nested under a gantry <signal>'s <staticBoard>/<vmsBoard>) -- spec 14.8 Table 135
    # itself frames the rule in terms of "<signal> or <sign>", so both element kinds are in scope.
    for rid, r in roads.items():
        for sig in list(r.iter("signal")) + list(r.iter("sign")):
            sem = sig.find("semantics")
            if sem is None:
                continue
            present = {child.tag for child in sem}
            has_category = bool(present & _CATEGORY_TAGS)
            has_supplementary = bool(present & _SUPPLEMENTARY_TAGS)
            if has_supplementary and not has_category:
                sid = sig.get("id")
                supp_list = sorted(present & _SUPPLEMENTARY_TAGS)
                flags.append(
                    (
                        "road.signal.semantics.no_semantics_without_category",
                        f"{sig.tag} id={sid} の <semantics> が supplementary要素{supp_list}のみで、"
                        f"意味付けを与えるcategory要素（speed/lane/priority/prohibited/routing/"
                        f"streetname/tourist/warning/parking）が無い",
                        f"road {rid} {sig.tag} id={sid}",
                    )
                )

    return flags
