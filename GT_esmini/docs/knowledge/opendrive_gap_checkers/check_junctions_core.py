"""Annex F gap-rule checkers — category-group "junctions_core".

Implements the structurally-checkable subset of the assigned rule list
(rules_junctions_core.json). Rules that genuinely require road-geometry
evaluation (boundary polygon closure/ordering, spatial junction overlap) or
an interpretive "was this junction actually necessary" judgment call are
NOT implemented here — see the structured report for their classification.

Implemented (6):
  - junctions.type_default_no_linked_road : <connection> of a default/virtual
    junction must not carry @linkedRoad (that attribute only exists on
    t_junction_connection_direct per OpenDRIVE_Junction.xsd).
  - junctions.type_direct_no_conn_road    : <connection> of a direct junction
    must not carry @connectingRoad (direct junctions link roads directly,
    they have no connecting road of their own).
  - junctions.boundary.only_for_common_junctions : <boundary> is only a valid
    child of a @type="default" (common) junction per schema
    (t_junction_common is the only complex type that includes it).
  - junctions.common.direct_junction_attributes  : @overlapZone (on
    <laneLink>) is only valid under a @type="direct" junction.
  - junctions.common.junctions_no_pred_succ : a <junction> element must not
    carry its own <link>/<predecessor>/<successor> child (unlike <road>,
    junctions do not have those; only <connection><predecessor/successor>
    exists, and only for virtual connections).
  - junctions.common.virtual_junction_attributes : @mainRoad/@orientation/
    @sStart/@sEnd are only valid on a @type="virtual" junction.

Implemented, PARTIAL_DETERMINISTIC (1), see impl_briefs/junctions_core.md:
  - junctions.common.not_only_two : only the clear-cut structural sub-case
    (exactly 2 participating roads on a @type="default" junction, not
    exempted by junctionGroup membership or crossPath-hosting) is flagged;
    the "was this junction actually necessary" judgment residual is left
    uncovered (see the trailing comment block below).

Classified away (6): see run_checks docstring / structured report.
"""
import xml.etree.ElementTree as ET


def run_checks(file_path, root, roads, road_ids, junctions, junction_ids):
    """See module contract in scratchpad/checks_gap/CONTRACT.md.

    Returns list[tuple[rule_name, detail, location]].
    """
    flags = []

    # --- junctions.common.not_only_two (PARTIAL_DETERMINISTIC) -------------
    # Annex F.4.5.3: "Junctions should not be used when only two roads meet."
    # Scope: @type absent/"default" (common) junctions only -- direct/virtual
    # are governed by their own Annex F sub-namespaces and excluded entirely
    # (2-road direct junctions, e.g. motorway on/off-ramps, are canonical).
    # Exempt (no finding) if the junction is a member of a <junctionGroup>
    # (roundabout arms etc.) or hosts >=1 <crossPath>. See impl_briefs/
    # junctions_core.md for the ambiguous residual left uncovered.
    junction_group_members = set()
    for jg in root.iter("junctionGroup"):
        for jref in jg.findall("junctionReference"):
            jr = jref.get("junction")
            if jr is not None:
                junction_group_members.add(jr)

    for jid, j in junctions.items():
        jtype = j.get("type") or "default"
        if jtype == "default":
            participant_roads = set()
            for conn in j.findall("connection"):
                for attr in ("incomingRoad", "connectingRoad"):
                    v = conn.get(attr)
                    if v is not None:
                        participant_roads.add(v)
            if (len(participant_roads) == 2
                    and jid not in junction_group_members
                    and not j.findall("crossPath")):
                flags.append((
                    "junctions.common.not_only_two",
                    f"junction {jid}(type=default) が丁度2本の道路（{sorted(participant_roads)}）"
                    "のみで構成（junctionGroup非所属・crossPathなし。2道路のみの接続には"
                    "junctionを使うべきでない、要確認）",
                    f"junction {jid}",
                ))

    for jid, j in junctions.items():
        jtype = j.get("type") or "default"  # schema: absent == "default" (common)

        # --- junctions.common.junctions_no_pred_succ ---------------------
        # Unlike <road>, a <junction> element has no <link>/<predecessor>/
        # <successor> of its own (findall() with a bare tag name only
        # matches direct children, so this does not mis-fire on the
        # legitimate <connection><predecessor/successor> of virtual
        # connections, which are nested one level deeper).
        for tag in ("link", "predecessor", "successor"):
            if j.find(tag) is not None:
                flags.append((
                    "junctions.common.junctions_no_pred_succ",
                    f"junction {jid} が直下に <{tag}> を持つ（junctionはpredecessor/successorを持たない）",
                    f"junction {jid}",
                ))

        # --- junctions.common.virtual_junction_attributes -----------------
        if jtype != "virtual":
            for attr in ("mainRoad", "orientation", "sStart", "sEnd"):
                v = j.get(attr)
                if v is not None:
                    flags.append((
                        "junctions.common.virtual_junction_attributes",
                        f"junction {jid}(type={jtype}) に @{attr}={v}（virtualジャンクションのみ許可）",
                        f"junction {jid}",
                    ))

        # --- junctions.boundary.only_for_common_junctions ------------------
        boundary = j.find("boundary")
        if boundary is not None and jtype != "default":
            flags.append((
                "junctions.boundary.only_for_common_junctions",
                f"junction {jid}(type={jtype}) に <boundary> が定義されている（boundaryはcommon(default)junctionのみ有効）",
                f"junction {jid}",
            ))

        # --- per-connection checks -----------------------------------------
        for conn in j.findall("connection"):
            cid = conn.get("id")

            # junctions.type_default_no_linked_road
            if jtype in ("default", "virtual"):
                lr = conn.get("linkedRoad")
                if lr is not None:
                    flags.append((
                        "junctions.type_default_no_linked_road",
                        f"junction {jid}(type={jtype}) connection id={cid} に @linkedRoad={lr}"
                        "（default/virtualジャンクションのconnectionには不可、direct専用属性）",
                        f"junction {jid} connection {cid}",
                    ))

            # junctions.type_direct_no_conn_road
            if jtype == "direct":
                cr = conn.get("connectingRoad")
                if cr is not None:
                    flags.append((
                        "junctions.type_direct_no_conn_road",
                        f"junction {jid}(type=direct) connection id={cid} に @connectingRoad={cr}"
                        "（directジャンクションのconnectionには不可、connectingRoadを持たない設計）",
                        f"junction {jid} connection {cid}",
                    ))

            # junctions.common.direct_junction_attributes (@overlapZone on <laneLink>)
            if jtype != "direct":
                for ll in conn.findall("laneLink"):
                    oz = ll.get("overlapZone")
                    if oz is not None:
                        flags.append((
                            "junctions.common.direct_junction_attributes",
                            f"junction {jid}(type={jtype}) connection id={cid} laneLink "
                            f"from={ll.get('from')} to={ll.get('to')} に @overlapZone={oz}"
                            "（overlapZoneはdirectジャンクションのみ許可）",
                            f"junction {jid} connection {cid}",
                        ))

    return flags


# ---------------------------------------------------------------------------
# Non-implemented rules (classification only, no checker code):
#
#   junctions.no_overlap
#       -> gap_geometry_math: "no overlap" is a 2D spatial predicate over the
#          junction's physical footprint (inertial-frame polygon from
#          reference-line geometry + boundary/connecting-road widths).
#          Pure attribute inspection cannot evaluate it.
#
#   junctions.boundary.close_gap_with_new_roads
#       -> gap_geometry_math: this is the "how to fix it" phrasing of the
#          same closed-boundary requirement as segments_close_boundry below
#          -- to know whether "existing roads are not sufficient to define a
#          closed junction boundary" you must first compute whether the
#          boundary closes in inertial space, which needs full road-geometry
#          evaluation, not just attribute presence.
#
#   junctions.boundary.segments_close_boundry
#   junctions.boundary.segments_counter_clockwise_order
#       -> gap_geometry_math: literally the "junction-boundary closed-polygon
#          ordering" case the contract calls out by name -- requires
#          evaluating each <segment>'s actual (x,y) position along its
#          referenced road (joint segments perpendicular to a road end,
#          lane segments along a lane's outer edge over an s-range) and
#          testing loop closure / winding order on the resulting polygon.
#
#   junctions.boundary.segments_for_each_conn_road
#       -> gap_geometry_math (evidence-based, not assumed): a plausible
#          structural proxy is "every road referenced as @incomingRoad/
#          @linkedRoad by a <connection> must also appear as a <segment
#          roadId=...>". Empirically checked against the corpus: it holds
#          exactly on the two GT-authored 4-way boundary fixtures
#          (g6_junction_boundary_18.xodr, g8_junction_grid_18.xodr -- 0
#          missing), but produces a false positive against the *official*
#          ASAM calibration fixture test/odr_fixtures/official/use_cases/
#          UC_Junction/UC_5Road_Junction.xodr (junction 1: incomingRoad
#          "670" never appears in a boundary segment, while several
#          *connecting* roads such as "323"/"384"/"408" do appear, despite
#          not being incoming/linked roads at all). That means which roads a
#          boundary segment must "reach" is governed by which road is
#          physically exposed on the junction's outer perimeter after
#          layout -- a geometric fact, not something derivable from
#          connection/crossPath road-id attributes alone. Forcing the naive
#          proxy would violate the "official set stays clean" calibration
#          bar, so this is classified as geometry-gap rather than
#          implemented on a proxy known to misfire.
#
#   junctions.common.not_only_two (now PARTIAL_DETERMINISTIC, see above)
#       -> The original trial probe's road-count method (whatever it counted)
#          found 10 corpus junctions with "exactly 2 participating roads",
#          several inside the official ASAM set. Re-verified directly against
#          this rule's own CHECK_LOGIC definition of "participating roads"
#          (the union of connection/@incomingRoad and connection/@
#          connectingRoad only): test/odr_fixtures/official/use_cases/
#          UC_2Lane-RoundAbout-3Arms/UC_2Lane-RoundAbout-3Arms.xodr's
#          junctions 42/43/44 each have 5 participating roads under that
#          definition, not 2 -- out of scope on road-count alone, no
#          exemption needed. .../UC_Motorway-Exit-Entry/UC_Motorway-Exit-
#          Entry-DirectJunction.xodr junctions 1/5 genuinely have exactly 2
#          participating roads each, but both are @type="direct" -- excluded
#          from this rule's scope entirely (separate Annex F sub-namespace,
#          2-road direct junctions are the canonical on/off-ramp shape). The
#          non-DirectJunction UC_Motorway-Exit-Entry.xodr variant has no
#          2-road @type="default" junctions at all (all >=3 roads). So the
#          exemption-qualified sub-case implemented above produces zero
#          flags on the full 208-file corpus, official set included -- a
#          deterministic, calibration-clean structural check. What remains
#          genuinely ambiguous is the *exemption list itself*: it is not
#          declared exhaustive by the spec text, so other legitimate reasons
#          for a 2-road common junction (elevationGrid/boundary hosting, a
#          deliberate lane-count/width transition) are conceivable and not
#          enumerated -- a flag under the implemented test is a candidate for
#          human review, not a guaranteed violation. That residual is left
#          uncovered.
#
#   junctions.common.when_to_use
#       -> gap_ambiguous: the general/parent form of not_only_two above
#          ("shall only be used when roads cannot be linked directly...
#          ambiguities caused when a road has two or more possible
#          predecessor/successor roads"). Deciding whether a given junction
#          was *necessary* to resolve a real routing ambiguity -- as opposed
#          to being used where a direct road-to-road link plus explicit
#          @contactPoint would have sufficed -- requires reconstructing and
#          reasoning about the whole road-network's alternative-routing
#          topology, which the spec text does not reduce to a deterministic,
#          attribute-level pass/fail condition.
# ---------------------------------------------------------------------------
