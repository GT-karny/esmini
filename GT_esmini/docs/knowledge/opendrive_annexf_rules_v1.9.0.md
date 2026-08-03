# OpenDRIVE 意味ルール分母 — Annex F: Checker rules (normative) v1.9.0

> **これは `rule` 網羅台帳の分母（denominator）**。出典＝ASAM OpenDRIVE 1.9.0 仕様の
> Annex F「Checker rules (normative)」（オンラインHTMLのみ、DL不可。
> `publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/v1.9.0/specification/16_annexes/map_rules.html`
> から2026-07-17抽出）。UID体系 = `asam.net:xodr:<定義バージョン>:<rule full name>`。
>
> **【2026-07-18 訂正】機械パースの結果、規範ルールは総数 275**（初版「335」はWeb要約モデルの過大計数。
> 生HTMLの UID 定義を直読みした **275** が正）。機械可読の権威台帳＝**`opendrive_rule_ledger.yaml`**
> （275×status、生成器 `scratchpad/build_rule_ledger.py`）、gap集計＝`opendrive_rule_gap_report.md`。本書は補助。
>
> **総数 275。qc-opendrive 1.0.0 で一致 16 ＝ 被覆 5.8%、gap 259**（規範の94%が off-the-shelf 未チェック
> ＝「抜け漏れ」の実体）。qcの23 UID中7は Annex F 非該当（xml/basic系5＋`one_connection_element`→標準で
> `one_link_to_incoming`へ改名＋`lane_smoothness`はAnnex D側）。分子は隔離venv `scratchpad/qcvenv` にローカル既存。
>
> **注意**: これは分母の1ソース（前々ターンの6ソースのうち #1 標準規範）。他に Annex D 追加規則、
> UML制約、消費者(RoadManager)前提、バグ履歴 を和で足す。

## カテゴリ別 件数

| カテゴリ | 件数 | qc実装 |
| :-- | --: | :-- |
| defaultRegulations | 1 | |
| header | 2 | |
| **ids（参照整合）** | 3 | |
| **junctions** | 57 | connection系5 |
| performance | 1 | avoid_redundant_info ✓ |
| road.geometry | 18 | paramPoly3×3, ✓ |
| road.elevation / superelevation / shape / type | ~10 | |
| **road.lane（番号・順序・方向）** | ~15 | level_true_one_side ✓ |
| **road.lane.link（リンク相互性）** | 9 | across/new/zero_width ✓ |
| road.lane.* (access/border/width/material/mark/speed/height/layer 他) | ~40 | access/border/width ✓ |
| **road.linkage（道路間リンク相互性）** | 5 | is_junction_needed ✓ |
| road.object（outline/skeleton/marking/surface/validity 他） | 114 | |
| road.railroad（踏切・分岐器） | 7 | |
| **road.signal（LHT/RHT整合含む）** | 40 | |
| road.corner/curve local, CRG, cross_section_surface | ~30 | |

## ★ 我々が特に欲しかった「非自明ルール」（標準に規範定義済＝自作bundleで実装可）

### レーンID符号規約（RHT基準）
- `asam.net:xodr:1.4.0:road.lane.lanes_numbered_correctly` — 正id=左/負id=右（RHT視点）
- `asam.net:xodr:1.4.0:road.lane.lane_order` — 中心から連番
- `asam.net:xodr:1.4.0:road.lane.lane_order_no_gaps` — 番号に欠番なし
- `asam.net:xodr:1.4.0:road.lane.lane_reverse_left_right` — RHT/LHT切替で左右反転しない
- `asam.net:xodr:1.4.0:road.lane.center_lane_id` — 中心lane id=0

### ★ 通行方向（LHT/RHT）整合 ← `signals_orientation_lht_rht` fixture の本丸
- `asam.net:xodr:1.7.0:road.object.validty.left_hand_traffic_lane_ids` — LHT時のorientation↔lane対応
- `asam.net:xodr:1.7.0:road.object.validty.right_hand_traffic_lane_ids` — RHT時
- `asam.net:xodr:1.7.0:road.signal.reference.left_hand_traffic_lane_ids`
- `asam.net:xodr:1.7.0:road.signal.reference.right_hand_traffic_lane_ids`
- `asam.net:xodr:1.7.0:road.signal.validity.left_hand_traffic_lane_ids`
- `asam.net:xodr:1.7.0:road.signal.validity.right_hand_traffic_lane_ids`
- `asam.net:xodr:1.7.0:road.signal.reference.specify_direction` — @orientation必須

### レーン/道路リンク相互性
- `asam.net:xodr:1.4.0:road.lane.link.lanes_across_laneSections` — セクション跨ぎ双方向（qc実装✓）
- `asam.net:xodr:1.9.0:road.linkage.both_sides_consistency` — **道路間リンクの相互整合**（qc未）
- `asam.net:xodr:1.4.0:road.lane.link.multiple_connections` — split/merge
- `asam.net:xodr:1.7.0:road.lane.link.zero_width_at_start/end`（qc実装✓）

### 参照整合（referential integrity）
- `asam.net:xodr:1.4.0:ids.id_unique_in_class`
- `asam.net:xodr:1.4.0:ids.id_unique_in_lane_section`
- `asam.net:xodr:1.4.0:ids.only_ref_defined_ids` — 定義済みidのみ参照可

### 幾何・参照線連続（#31/資産破綻に効く）
- `asam.net:xodr:1.4.0:road.geometry.refline_no_gaps` — 参照線連続
- `asam.net:xodr:1.4.0:road.geometry.refline_no_kinks` — 滑らかさ
- `asam.net:xodr:1.9.0:road.length_sum_geometries` — 道路長=幾何和
- `asam.net:xodr:1.9.0:junctions.connection.smooth_fit` / `junctions.direct.linked_lane_smoothness` — 接続lane滑らか

## 全カテゴリのUID（分母本体）

> 完全列挙。qc実装済みは末尾 `✓`。（junctions 57 / road.object 114 / road.signal 40 が最大群＝
> 未チェックの非自明ルールの主戦場）。

**ids(3)**: id_unique_in_class / id_unique_in_lane_section / only_ref_defined_ids
**header(2)**: offset.centered_coords / proj.max_one_proj
**defaultRegulations(1)**: only_speed_priority

**junctions.boundary(5)**: close_gap_with_new_roads / only_for_common_junctions / segments_close_boundry / segments_counter_clockwise_order / segments_for_each_conn_road
**junctions.common(5)**: direct_junction_attributes / junctions_no_pred_succ / not_only_two / virtual_junction_attributes / when_to_use
**junctions.connection(8)**: connect_road_no_incoming_road ✓ / end_opposite_linkage ✓ / lane_change_one_con_road / no_connecting_road_direct / no_lane_change_for_mult_con_roads / one_link_to_incoming ✓ / smooth_fit / start_along_linkage ✓
**junctions.crossing(3)**: only_one_high_prio / only_road_sections / s_start_end_coverage
**junctions.cross_path(6)**: correct_junction_id / disregard_cross_road_evelation / lane_linkage / only_connect_correct_type / start_end_contained / within_junction_area
**junctions.direct(9)**: connecting_road_attribute_usage / correct_type_linked_road_usage / flat_exits_entries / linked_lane_smoothness / overlap_zone_coverage / overlap_zone_exclusivity / road_connectivity / road_ramp_heading / split_or_merge
**junctions.elevation_grid(5)**: entry_exit_smoothness / only_one_elev_grid / perpendicular_vectors / polynome_coefficient_values / valid_for_entire_boundry
**junctions.geometry(3)**: correct_junction_boundry / only_one_line_element / ref_line_definition
**junctions.priority(2)**: high_and_low_attr / no_signals
**junctions(他)**: no_overlap / type_default_no_linked_road / type_direct_no_conn_road
**junctions.virtual(8)**: connecting_roads_start_end / heading_equal_mainroad / linked_lanes_smooth_fit / main_road_only / no_controllers / only_one_start_end / connections.only_virtual_junctions / crossPath.cross_road_check_s_t

**road.overlap(3)**: no_overlap_outside_junction / no_overlap_self / overlap_inside_junction
**road.length(1)**: length_sum_geometries
**road.geometry(18)**: contact_point ✓ / elem_asc_order / one_geom_elem_per_spec / only_one_refline / refline_exists / refline_no_gaps / refline_no_kinks / s-value_sum / arc.no_zero_curvature / paramPoly3.arcLength_range ✓ / paramPoly3.length_match ✓ / paramPoly3.normalized_range ✓ / paramPoly3.valid_parameters / spiral.curvature_change
**road.elevation(2)**: elem_asc_order / elev_along_ref_line
**road.superelevation(1)**: elem_asc_order
**road.shape(2)**: elem_asc_order / t_definition_coverage
**road.type(4)**: create_new_type_in_parent / elem_asc_order / lane_type_may_differ_from_parent / only_alpha_2_country_codes
**road.lane center(5)**: center_elem_definition / center_lane / center_lane_id / center_lane_no_width / center_lane_singular
**road.lane 番号(6)**: lanes_numbered_correctly / lane_id_unique / lane_listing / lane_order / lane_order_no_gaps / lane_reverse_left_right
**road.lane section(2)**: lane_section_drivable / lane_sect_first
**road.lane.link(8)**: lanes_across_laneSections ✓ / multiple_connections / new_lane_appear ✓ / no_link / temporary_layer_section_link_permanent / use_junctions / zero_width_at_end ✓ / zero_width_at_start ✓
**road.lane.access(3)**: center_lane_no_acc_rule / elem_asc_order / no_mix_of_deny_or_allow ✓
**road.lane.border(4)**: elem_asc_order / exclusive_offset_border / exclusive_width_border / overlap_with_inner_lanes ✓
**road.lane.width(4)**: elem_asc_order / lane_width_validity(≥0) / no_width_with_border / width_defined_whole_section
**road.lane.height(2)** / **material(2)** / **road_mark(3)** / **rule(1)** / **speed(2)** / **properties(1)** / **lane_offset(2)**: 全て elem_asc_order＋個別制約
**road.lane.layer(6)**: center_lane_permanent / lane_group_width_temporary / lane_phys_attr_temporary / layer_limits / layer_mandatory_permanent / length_only_temporary
**road.lane_section(7)**: elem_asc_order / lanesec_length_limit_road / lanesec_usage_lane_num / lane_long_zero_width / lane_sect_req / new_lanesec_link_temp_to_perm / valid_length
**road.linkage(5)**: both_sides_consistency / is_junction_needed ✓ / junc_link_attribute_usage / road_link_attribute_usage / virtjunc_link_attribute_usage
**road.level(1)**: lane.level_true_one_side ✓

**road.object(114)**: general(4) / borders(2) / bridges(3) / marking(7) / material(1) / object_marking(6) / outline(4) / reference(1) / repeating(4) / skeleton(13) / surface(9) / tunnels(2) / validity(6: 含 left/right_hand_traffic_lane_ids) / corner_local(5) / corner_road(5) / curve_local(5) / cross_section_surface(6) / crg(8) …
**road.railroad(7)**: one_rail_per_road / rail_lane_width_validity / rail_refline_centered / platforms.min_amount / platforms.min_segments / switch.check_switch_conn / switch.single_switch_no_partner
**road.signal(40)**: general(3: priority/signal_type/use_country_code) / boards(5) / controller(1) / dependency(1) / gantry(2) / reference(6: 含 left/right_hand_traffic_lane_ids, specify_direction) / semantics(1) / validity(2: left/right_hand_traffic_lane_ids)
**road.use_cases(1)**: shape_elements_start_right
**performance(1)**: avoid_redundant_info ✓

## 派生する事実（capability_model §1.1 へ反映）

1. **分母＝335、分子(qc)≈18-23、被覆 ~5-7%**。「採用」で得られるのは氷山の一角。
2. **我々が欲しい LHT/lane-id/reciprocity は全て規範ルールとして存在**（`road.signal.validity.left/right_hand_traffic_lane_ids` 等）＝
   qc未実装だが標準定義済 → **自作 checker bundle で実装できる**（標準準拠・upstream貢献候補）。
3. **junctions(57)/object(114)/signal(40) が未チェックの非自明ルールの主戦場**。「他にも非自明ルールがあるはず」は実数で裏付いた。
4. これは分母の**1ソース**。UML制約・消費者前提・バグ履歴を足して初めて「抜け漏れなく」になる。
