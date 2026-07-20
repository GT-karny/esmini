"""Gap-rule checkers for category-group "road_signal_core_boards" (Annex F, OpenDRIVE
1.7.0-1.9.0). Implements the structurally-checkable subset of:
  road.signal.signal_type / road.signal.use_country_code
  road.signal.boards.multi_board_have_sub_boards / multi_board_use_correct_type /
    multi_board_use_dynamic_true / static_board_use_correct_type /
    static_boards_no_single_signal
  road.signal.controller.valid_for_signals
  road.signal.gantry.vmsgroup_at_least_one_reference
  road.signal.dependency.multiple_dependency (unconditional-pass, see below)
  road.signal.gantry.all_variable_boards_same_gantry (deterministic sub-case only,
    see below and impl_briefs/road_signal_core_boards.md)

Not implemented (classified gap_ambiguous, see report): road.signal.priority.

Follows the parsing idiom of scratchpad/gap_rule_check.py (roads dict / road_ids /
junction_ids passed in by the integration layer -- this module never re-parses a file).
"""

# Attribute string values the spec documents as "not a specific type" sentinels for
# <signal>/@type and @subtype (t_road_signals_signal: "-1" / "none"), plus the
# ElementTree-None / empty-string cases for malformed hand-authored fixtures.
_NONSPECIFIC = {"-1", "none", "None", "NONE", "", None}

# Board-sentinel @type values (t_road_signals_signal_type: staticBoard/vmsBoard/
# multiBoard). These mark the PARENT <signal> as a physical board carrying one or
# more <sign> children -- the board itself is not a country-specific catalog
# entry, so @country is not meaningful on it (per spec 14_07 static-board
# examples: the board parent carries no @country, each child <sign> carries its
# own @country). Exempt them from road.signal.use_country_code to avoid flagging
# the parent for an attribute that belongs on its children instead
# (road.signal.use_country_code FALSE_POSITIVE, adv fixture
# country_fp_spec_staticboard.xodr).
_BOARD_SENTINEL_TYPES = {"staticBoard", "vmsBoard", "multiBoard"}


def _norm(v):
    return v.strip() if isinstance(v, str) else v


def _check_signal_like(el, kind, rid, flags, loc_extra=""):
    """type/subtype "specificity" + @country presence, shared by <signal> and <sign>
    (t_road_signals_board_sign extends t_road_signals_signal -- same attribute set)."""
    sid = el.get("id")
    loc = f"road {rid} {kind} id={sid}{loc_extra}"
    styp = _norm(el.get("type"))
    ssub = _norm(el.get("subtype"))
    # road.signal.signal_type: "Signals shall have a specific type and subtype."
    # Only flag when BOTH axes are non-specific -- @subtype="-1" alone is the
    # extremely common, legitimate "no subtype variant in this country's catalog"
    # idiom (seen throughout the official ASAM examples, e.g. type="205" subtype="-1"),
    # so subtype-alone must never fire. A signal with neither axis specific (e.g.
    # type="-1" subtype="-1", used in this repo for bare road-marking arrows encoded
    # as signals) genuinely carries no identifying type information.
    if styp in _NONSPECIFIC and ssub in _NONSPECIFIC:
        flags.append((
            "road.signal.signal_type",
            f"{kind} id={sid}: type={el.get('type')!r} subtype={el.get('subtype')!r} は非specific（type/subtypeともに未指定/-1/none）",
            loc,
        ))
    # road.signal.use_country_code: "A country code shall be added to refer to
    # country-specific rules using the @country attribute." Only meaningful once the
    # signal actually carries a specific type (a fully generic/-1 signal has no
    # country-specific rule to refer to in the first place, so exempt it here to
    # avoid double-flagging the same root cause under two rules). Board-sentinel
    # types (staticBoard/vmsBoard/multiBoard) are also exempt: @country belongs on
    # the child <sign> elements of the board, not on the board parent itself (see
    # _BOARD_SENTINEL_TYPES).
    country = _norm(el.get("country"))
    if not country and styp not in _NONSPECIFIC and styp not in _BOARD_SENTINEL_TYPES:
        flags.append((
            "road.signal.use_country_code",
            f"{kind} id={sid}: type={el.get('type')!r} だが @country が未指定",
            loc,
        ))


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    flags = []

    all_signal_ids = set()
    # road.signal.gantry.all_variable_boards_same_gantry (deterministic sub-case):
    # top-level <signal id="..."> lookup (xpath .//road/signals/signal only -- NOT
    # nested board <sign> elements) and the subset of those ids whose <signal>
    # actually carries a <vmsBoard> child (i.e. IS a variable message board).
    signal_by_id = {}
    vmsboard_signal_ids = set()

    for rid, r in roads.items():
        signals_el = r.find("signals")
        if signals_el is None:
            continue
        for sig in signals_el.findall("signal"):
            sid = sig.get("id")
            if sid is not None:
                all_signal_ids.add(sid)
                signal_by_id[sid] = sig

            # road.signal.dependency.multiple_dependency: <dependency> is declared
            # maxOccurs="unbounded" minOccurs="0" at BOTH occurrence sites in the
            # XSD -- t_road_signals_signal (OpenDRIVE_Signal.xsd, covers <signal>
            # and board <sign>) and t_road_signals_board (covers <staticBoard>/
            # <vmsBoard>). The rule text ("A signal may have multiple
            # dependencies") is a permissive cardinality statement already fully
            # granted by the schema, not a prohibition -- there is no upper bound
            # or other structural condition on count(./dependency) that any
            # schema-valid document could violate. Iterate for
            # completeness/documentation; this can never append a flag (see
            # impl_briefs/road_signal_core_boards.md).
            for _dep_holder in [sig] + sig.findall("staticBoard") + sig.findall("vmsBoard"):
                len(_dep_holder.findall("dependency"))  # no upper bound to compare against

            _check_signal_like(sig, "signal", rid, flags)

            static_boards = sig.findall("staticBoard")
            vms_boards = sig.findall("vmsBoard")
            has_static = len(static_boards) > 0
            has_vms = len(vms_boards) > 0
            if has_vms and sid is not None:
                vmsboard_signal_ids.add(sid)
            sig_type = sig.get("type")
            dynamic = sig.get("dynamic")

            # road.signal.boards.multi_board_have_sub_boards +
            # road.signal.boards.multi_board_use_dynamic_true (both sides of the
            # XSD 1.1 t_road_signals_signal_road assert, which qc-opendrive's
            # validator does not evaluate -- genuine gap):
            #   not(@type='multiBoard') or (@dynamic=true() and staticBoard>=1 and vmsBoard>=1)
            if sig_type == "multiBoard":
                if not (has_static and has_vms):
                    flags.append((
                        "road.signal.boards.multi_board_have_sub_boards",
                        f"signal id={sid}: type=multiBoard だが staticBoard={len(static_boards)}件 "
                        f"vmsBoard={len(vms_boards)}件（各1件以上必要）",
                        f"road {rid} signal id={sid}",
                    ))
                # t_yesNo domain is literally "yes"/"no" (not xs:boolean "true"/"false");
                # the schema assert's `@dynamic=true()` maps to @dynamic="yes" in-domain,
                # consistent with this repo's own canonical multiBoard fixture (21_boards_vmsgroup_19).
                if dynamic != "yes":
                    flags.append((
                        "road.signal.boards.multi_board_use_dynamic_true",
                        f"signal id={sid}: type=multiBoard だが @dynamic={dynamic!r}（'yes' であるべき）",
                        f"road {rid} signal id={sid}",
                    ))

            # road.signal.boards.multi_board_use_correct_type (converse direction: any
            # signal that structurally IS a multi-board -- carries both board kinds --
            # must declare @type="multiBoard").
            if has_static and has_vms and sig_type != "multiBoard":
                flags.append((
                    "road.signal.boards.multi_board_use_correct_type",
                    f"signal id={sid}: staticBoard+vmsBoard を両方持つが @type={sig_type!r}（'multiBoard' であるべき）",
                    f"road {rid} signal id={sid}",
                ))

            # road.signal.boards.static_board_use_correct_type: a signal carrying ONLY
            # staticBoard children (no vmsBoard -- i.e. not a multi-board) should declare
            # @type="staticBoard".
            if has_static and not has_vms and sig_type != "staticBoard":
                flags.append((
                    "road.signal.boards.static_board_use_correct_type",
                    f"signal id={sid}: staticBoard のみを持つが @type={sig_type!r}（'staticBoard' であるべき）",
                    f"road {rid} signal id={sid}",
                ))

            for sb in static_boards:
                signs = sb.findall("sign")
                # road.signal.boards.static_boards_no_single_signal: a board combines
                # multiple signs onto one physical sheet -- exactly one sign defeats
                # the purpose ("a stop sign on a single sheet of metal").
                if len(signs) == 1:
                    flags.append((
                        "road.signal.boards.static_boards_no_single_signal",
                        f"signal id={sid}: staticBoard が sign 1個のみ（単一標識にboardを使うべきでない）",
                        f"road {rid} signal id={sid}",
                    ))
                for sn in signs:
                    snid = sn.get("id")
                    if snid is not None:
                        all_signal_ids.add(snid)
                    _check_signal_like(sn, "sign", rid, flags, loc_extra=f" (board of signal {sid})")

    # road.signal.controller.valid_for_signals: top-level <controller> (t_controller,
    # direct child of <OpenDRIVE>) -- NOT the same-named but semantically distinct
    # <junction>/<controller> (t_junction_controller, synchronization-group reference by
    # controller @id, no signalId at all) -- root.findall keeps this to direct children.
    for ctrl in root.findall("controller"):
        cid = ctrl.get("id")
        controls = ctrl.findall("control")
        if len(controls) == 0:
            flags.append((
                "road.signal.controller.valid_for_signals",
                f"controller id={cid}: control（対象signal）が1つも無い",
                f"controller {cid}",
            ))
        for c in controls:
            sidref = c.get("signalId")
            if not sidref:
                flags.append((
                    "road.signal.controller.valid_for_signals",
                    f"controller id={cid}: control@signalId が未指定",
                    f"controller {cid}",
                ))
            elif sidref not in all_signal_ids:
                flags.append((
                    "road.signal.controller.valid_for_signals",
                    f"controller id={cid}: control@signalId={sidref!r} は未定義signal（このcontrollerはどのsignalにも有効でない可能性）",
                    f"controller {cid}",
                ))

    # road.signal.gantry.vmsgroup_at_least_one_reference: the structurally-checkable
    # half of "Each gantry shall have one <vmsGroup> element with at least one
    # <vmsBoardReference> element" -- flags a <vmsGroup> with zero references. (The
    # "each gantry has exactly one vmsGroup" direction needs a gantry<->vmsGroup
    # identity link the schema does not provide -- remains uncovered, same reason
    # documented for the ambiguous residual of the sibling rule
    # all_variable_boards_same_gantry below.)
    for vg in root.findall("vmsGroup"):
        vgid = vg.get("id")
        refs = vg.findall("vmsBoardReference")
        if len(refs) == 0:
            flags.append((
                "road.signal.gantry.vmsgroup_at_least_one_reference",
                f"vmsGroup id={vgid}: vmsBoardReference が1つも無い",
                f"vmsGroup {vgid}",
            ))

        # road.signal.gantry.all_variable_boards_same_gantry -- DETERMINISTIC
        # SUB-CASE ONLY (see impl_briefs/road_signal_core_boards.md for the
        # ambiguous residual that is intentionally left uncovered: actual
        # co-location on one physical gantry via s/t proximity or the optional
        # <reference elementType="object"> link). This is the structural
        # precondition for the rule to even be evaluable: every
        # <vmsBoardReference@signalId> must resolve to a <signal> that actually
        # carries a <vmsBoard> child -- otherwise it cannot be a "variable
        # message board" member of the group in the first place.
        for ref in refs:
            rsid = ref.get("signalId")
            loc = f"vmsGroup {vgid} vmsBoardReference signalId={rsid}"
            if rsid not in signal_by_id:
                flags.append((
                    "road.signal.gantry.all_variable_boards_same_gantry",
                    f"vmsGroup id={vgid}: vmsBoardReference@signalId={rsid!r} は未定義signal（参照切れ）",
                    loc,
                ))
            elif rsid not in vmsboard_signal_ids:
                flags.append((
                    "road.signal.gantry.all_variable_boards_same_gantry",
                    f"vmsGroup id={vgid}: vmsBoardReference@signalId={rsid!r} のsignalはvmsBoardを持たない（可変情報板でない）",
                    loc,
                ))

    return flags
