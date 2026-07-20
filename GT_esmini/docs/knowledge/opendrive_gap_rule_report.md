# OpenDRIVE gap-rule 監査 — Annex F 未実装ルールの自作 checker suite（拡張版）

> qc-opendrive が実装しない Annex F 規範ルール（gap 259）を XML 直パースで自作 checker 化。
> 19 カテゴリモジュール（`scratchpad/checks_gap/check_*.py`）を統合し 208 ファイルへ一括適用。
> 公式ASAM=校正セット（発火≒0が期待・発火は要精査） / GT自作=**監査対象** / 上流=対象外。
> flag は error でなく **review**（助言）。

- モジュール数 **19**  検査 **208** files（parse失敗 3／実行時例外 0）
- 違反総数 **259**  ├ GT自作 **94**  └ 公式ASAM **21**

## origin別（files / 違反数）

| origin | files | 違反 |
| :-- | --: | --: |
| GT:resources/xodr | 32 | 54 |
| GT:test | 10 | 16 |
| GT:handauthored | 30 | 11 |
| GT:generated | 13 | 13 |
| official(ASAM) | 36 | 21 |
| upstream(対象外) | 58 | 138 |
| other | 29 | 6 |

## カテゴリモジュール別 違反数

| module | 件数 |
| :-- | --: |
| check_road_lane_core | 82 |
| check_road_geometry_linkage | 60 |
| check_road_object_core | 29 |
| check_road_signal_core_boards | 22 |
| check_road_object_outline_surface | 21 |
| check_road_object_marking | 10 |
| check_road_signal_ref_validity | 10 |
| check_misc_header_ids | 8 |
| check_road_lane_layer_link_section | 6 |
| check_road_corner_curve | 3 |
| check_junctions_elev_virtual | 3 |
| check_road_lane_attrs | 2 |
| check_road_crg_cross_section | 1 |
| check_junctions_core | 1 |
| check_road_elev_shape_type | 1 |

## ルール別 違反数（上位40）

| rule | 件数 |
| :-- | --: |
| road.lane.lane_listing | 78 |
| road.geometry.spiral.curvature_change | 52 |
| road.signal.signal_type | 20 |
| road.object.orientation | 13 |
| road.object.circular_vs_angular | 11 |
| road.object.repeating.valid_s_length | 10 |
| road.object.marking.markings_with_outline | 10 |
| road.signal.validity.right_hand_traffic_lane_ids | 10 |
| ids.only_ref_defined_ids | 8 |
| road.linkage.both_sides_consistency | 4 |
| road.lane.center_lane_no_width | 4 |
| road.object.outline.exactly_one_outer | 4 |
| road.object.repeating.attributes_with_outline_skeleton | 4 |
| road.corner_local.first_id_zero | 3 |
| junctions.virtual.connecting_roads_start_end | 3 |
| road.geometry.arc.no_zero_curvature | 2 |
| road.object.validty.check_parent_orientation | 2 |
| road.object.validty.right_hand_traffic_lane_ids | 2 |
| road.lane.material.elem_asc_order | 2 |
| road.length_sum_geometries | 2 |
| road.lane.link.temporary_layer_section_link_permanent | 2 |
| road.lane_section.valid_length | 2 |
| road.object.repeating.outline_use_cornerlocal | 2 |
| road.lane.layer.length_only_temporary | 1 |
| road.object.type_attr | 1 |
| road.crg.s_t_offset_no_global | 1 |
| road.signal.use_country_code | 1 |
| road.signal.boards.static_boards_no_single_signal | 1 |
| junctions.common.direct_junction_attributes | 1 |
| road.object.surface.only_for_angular_boxes | 1 |
| road.elevation.elem_asc_order | 1 |
| road.lane_section.lane_long_zero_width | 1 |

## ⚠ 公式ASAM校正セットで発火したルール（21件）＝要精査

| rule | 件数 |
| :-- | --: |
| road.signal.validity.right_hand_traffic_lane_ids | 10 |
| ids.only_ref_defined_ids | 4 |
| road.object.marking.markings_with_outline | 2 |
| road.lane.lane_listing | 1 |
| road.corner_local.first_id_zero | 1 |
| road.object.outline.exactly_one_outer | 1 |
| road.elevation.elem_asc_order | 1 |
| road.length_sum_geometries | 1 |

## ★ GT自作資産の違反（監査対象・全 94件、先頭150）

- **road.lane.layer.length_only_temporary** road 1 permanent層 laneSection s=0.0000000000000000e+00 が@lengthを使用（temporary層限定の属性） — GT_esmini/test/odr_fixtures/generated/g1_lanesection_length_19.xodr :: road 1 s=0.0000000000000000e+00  [GT:generated]
- **road.lane.link.temporary_layer_section_link_permanent** road 1 temporary層区間開始(s=0) drivable lane -1(幅3.50) がpermanent層にリンクされていない — GT_esmini/test/odr_fixtures/generated/g2_lanes_layer_19.xodr :: road 1 s=0 lane -1  [GT:generated]
- **road.lane.link.temporary_layer_section_link_permanent** road 1 temporary層区間終了(s=500) drivable lane -1(幅3.50) がpermanent層にリンクされていない — GT_esmini/test/odr_fixtures/generated/g2_lanes_layer_19.xodr :: road 1 s=500 lane -1  [GT:generated]
- **road.object.orientation** object id=9001 に @orientation が未指定（有効方向の明示が必須） — GT_esmini/test/odr_fixtures/generated/g4_curvelocal_corner_19.xodr :: road 1 object id=9001  [GT:generated]
- **road.object.type_attr** object id=9001 に @type が未指定 — GT_esmini/test/odr_fixtures/generated/g4_curvelocal_corner_19.xodr :: road 1 object id=9001  [GT:generated]
- **ids.only_ref_defined_ids** junction 2 connection.connectingRoad=99 が未定義road — GT_esmini/test/odr_fixtures/handauthored/02_invalid_junction_connection_14.xodr :: junction 2  [GT:handauthored]
- **road.crg.s_t_offset_no_global** CRG file=./does_not_exist.crg mode=global で sOffset/tOffset が指定されている（global では不可） — GT_esmini/test/odr_fixtures/handauthored/14_crg_offsets_19.xodr :: road 1 s=0  [GT:handauthored]
- **road.signal.use_country_code** signal id=300: type='205' だが @country が未指定 — GT_esmini/test/odr_fixtures/handauthored/17_userdata_dataquality_15.xodr :: road 1 signal id=300  [GT:handauthored]
- **road.signal.boards.static_boards_no_single_signal** signal id=1: staticBoard が sign 1個のみ（単一標識にboardを使うべきでない） — GT_esmini/test/odr_fixtures/handauthored/21_boards_vmsgroup_19.xodr :: road 1 signal id=1  [GT:handauthored]
- **road.signal.signal_type** sign id=None: type=None subtype=None は非specific（type/subtypeともに未指定/-1/none） — GT_esmini/test/odr_fixtures/handauthored/21_boards_vmsgroup_19.xodr :: road 1 sign id=None (board of signal 1)  [GT:handauthored]
- **junctions.common.direct_junction_attributes** junction 900(type=default) connection id=0 laneLink from=-1 to=-1 に @overlapZone=0.5（overlapZoneはdirectジャンクションのみ許可） — GT_esmini/test/odr_fixtures/handauthored/21_common_junction_crosspath_19.xodr :: junction 900 connection 0  [GT:handauthored]
- **junctions.virtual.connecting_roads_start_end** virtual junction 888: connecting road 2（connection id=0）が mainRoad 1 に s=100 で接続（sStart=95 にも sEnd=105 にも一致しない） — GT_esmini/test/odr_fixtures/handauthored/23_virtual_junction_17.xodr :: junction 888 road 2  [GT:handauthored]
- **junctions.virtual.connecting_roads_start_end** virtual junction 888: connecting road 2（connection id=0）が mainRoad 1 に s=100 で接続（sStart=95 にも sEnd=105 にも一致しない） — GT_esmini/test/odr_fixtures/handauthored/23b_virtual_junction_lht_17.xodr :: junction 888 road 2  [GT:handauthored]
- **junctions.virtual.connecting_roads_start_end** virtual junction 999: connecting road 2（connection id=0）が mainRoad 1 に s=120 で接続（sStart=115 にも sEnd=125 にも一致しない） — GT_esmini/test/odr_fixtures/handauthored/23c_virtual_junction_parse_variants_17.xodr :: junction 999 road 2  [GT:handauthored]
- **ids.only_ref_defined_ids** junction 999 connection.connectingRoad=99 が未定義road — GT_esmini/test/odr_fixtures/handauthored/23c_virtual_junction_parse_variants_17.xodr :: junction 999  [GT:handauthored]
- **road.object.surface.only_for_angular_boxes** object id=100 は <outline(s)>あり だが <surface> を含む（<surface>は角形（length/width）境界のオブジェクトのみ許可） — GT_esmini/test/odr_fixtures/handauthored/26_object_details_19.xodr :: road 1 object id=100  [GT:handauthored]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.0690948 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_priority_lht.xodr :: road 102 s=10.4714  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0690948 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_priority_lht.xodr :: road 103 s=10.4714  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0690948 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_priority_lht.xodr :: road 104 s=10.4714  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.0690948 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_priority_lht.xodr :: road 105 s=10.4714  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.00524121 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_lht.xodr :: road 100 s=13.3117  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.102714 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_lht.xodr :: road 102 s=8.52113  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0458065 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_lht.xodr :: road 103 s=11.8716  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0394262 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_lht.xodr :: road 104 s=12.2151  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.118045 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_lht.xodr :: road 105 s=7.76528  [GT:test]
- **road.lane_section.valid_length** road 104 (permanent層) laneSection s=37.448 の長さ=7.10543e-15 <= 0 — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_lht.xodr :: road 104 s=37.448  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.00524121 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_rht.xodr :: road 100 s=13.3117  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.102714 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_rht.xodr :: road 102 s=8.52113  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0458065 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_rht.xodr :: road 103 s=11.8716  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0394262 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_rht.xodr :: road 104 s=12.2151  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.118045 (no curvature change; use <arc> or <line> instead) — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_rht.xodr :: road 105 s=7.76528  [GT:test]
- **road.lane_section.valid_length** road 104 (permanent層) laneSection s=37.448 の長さ=7.10543e-15 <= 0 — GT_esmini/test/odr_fixtures/lht/4way_skew_2lane_rht.xodr :: road 104 s=37.448  [GT:test]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.0690948 (no curvature change; use <arc> or <line> instead) — resources/scenario_authoring/road_catalog/generated/4way_priority__main_ns.xodr :: road 102 s=10.4714  [GT:generated]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0690948 (no curvature change; use <arc> or <line> instead) — resources/scenario_authoring/road_catalog/generated/4way_priority__main_ns.xodr :: road 103 s=10.4714  [GT:generated]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0690948 (no curvature change; use <arc> or <line> instead) — resources/scenario_authoring/road_catalog/generated/4way_priority__main_ns.xodr :: road 104 s=10.4714  [GT:generated]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.0690948 (no curvature change; use <arc> or <line> instead) — resources/scenario_authoring/road_catalog/generated/4way_priority__main_ns.xodr :: road 105 s=10.4714  [GT:generated]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0690948 (no curvature change; use <arc> or <line> instead) — resources/scenario_authoring/road_catalog/generated/t_junction__a90.xodr :: road 101 s=10.4714  [GT:generated]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.0690948 (no curvature change; use <arc> or <line> instead) — resources/scenario_authoring/road_catalog/generated/t_junction__a90.xodr :: road 102 s=10.4714  [GT:generated]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.0690948 (no curvature change; use <arc> or <line> instead) — resources/scenario_authoring/road_catalog/generated/t_junction_priority__a90.xodr :: road 101 s=10.4714  [GT:generated]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==0.0690948 (no curvature change; use <arc> or <line> instead) — resources/scenario_authoring/road_catalog/generated/t_junction_priority__a90.xodr :: road 102 s=10.4714  [GT:generated]
- **road.object.orientation** object id=7 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/crest-curve.xodr :: road 0 object id=7  [GT:resources/xodr]
- **road.object.repeating.valid_s_length** object id=2 repeat @s+@length=1464.53 が road @length=1464.43 を超過（+0.1） — resources/xodr/e6mini.xodr :: road 0 object id=2 s=0.1  [GT:resources/xodr]
- **road.object.repeating.valid_s_length** object id=4 repeat @s+@length=1466.43 が road @length=1464.43 を超過（+2） — resources/xodr/e6mini.xodr :: road 0 object id=4 s=2  [GT:resources/xodr]
- **road.object.repeating.valid_s_length** object id=5 repeat @s+@length=1466.43 が road @length=1464.43 を超過（+2） — resources/xodr/e6mini.xodr :: road 0 object id=5 s=2  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 0, -1, -2, -3] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 0 s=0  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 0, -1, -2, -3, -4] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 0 s=50  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 0, -1, -2, -3, -4] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 0 s=175  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 0, -1, -2, -3] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 1 s=0  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 3, 0, -1, -2, -3] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 1 s=10  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 3, 0, -1, -2, -3] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 1 s=100  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 0, -1, -2, -3] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 3 s=0  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 0, -1, -2, -3] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 5 s=0  [GT:resources/xodr]
- **road.lane.lane_listing** lane掲載順がID降順になっていない: [1, 2, 0, -1, -2, -3] — resources/xodr/highway_example_with_merge_and_split.xodr :: road 6 s=0  [GT:resources/xodr]
- **road.signal.signal_type** signal id=0: type='-1' subtype='-1' は非specific（type/subtypeともに未指定/-1/none） — resources/xodr/multi_intersections.xodr :: road 202 signal id=0  [GT:resources/xodr]
- **road.signal.signal_type** signal id=0: type='-1' subtype='-1' は非specific（type/subtypeともに未指定/-1/none） — resources/xodr/multi_intersections.xodr :: road 202 signal id=0  [GT:resources/xodr]
- **road.signal.signal_type** signal id=0: type='-1' subtype='-1' は非specific（type/subtypeともに未指定/-1/none） — resources/xodr/multi_intersections.xodr :: road 202 signal id=0  [GT:resources/xodr]
- **road.signal.signal_type** signal id=0: type='-1' subtype='-1' は非specific（type/subtypeともに未指定/-1/none） — resources/xodr/multi_intersections.xodr :: road 202 signal id=0  [GT:resources/xodr]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.184253 (no curvature change; use <arc> or <line> instead) — resources/xodr/parking_demo.xodr :: road 100 s=3.92677  [GT:resources/xodr]
- **road.geometry.spiral.curvature_change** spiral curvStart==curvEnd==-0.184253 (no curvature change; use <arc> or <line> instead) — resources/xodr/parking_demo.xodr :: road 101 s=3.92677  [GT:resources/xodr]
- **road.object.orientation** object id=3 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 1 object id=3  [GT:resources/xodr]
- **road.object.orientation** object id=4 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 1 object id=4  [GT:resources/xodr]
- **road.object.orientation** object id=5 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 1 object id=5  [GT:resources/xodr]
- **road.object.orientation** object id=6 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 1 object id=6  [GT:resources/xodr]
- **road.object.orientation** object id=7 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 1 object id=7  [GT:resources/xodr]
- **road.object.orientation** object id=8 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 1 object id=8  [GT:resources/xodr]
- **road.object.orientation** object id=100 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 1 object id=100  [GT:resources/xodr]
- **road.object.orientation** object id=101 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 1 object id=101  [GT:resources/xodr]
- **road.object.orientation** object id=11 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 3 object id=11  [GT:resources/xodr]
- **road.object.orientation** object id=12 に @orientation が未指定（有効方向の明示が必須） — resources/xodr/parking_demo.xodr :: road 3 object id=12  [GT:resources/xodr]
- **road.object.marking.markings_with_outline** object id=1 は outline を使用するが <markings> が <object> 直下にある（<outline>内に配置すべき） — resources/xodr/parking_demo.xodr :: road 1 object id=1  [GT:resources/xodr]
- **road.object.marking.markings_with_outline** object id=2 は outline を使用するが <markings> が <object> 直下にある（<outline>内に配置すべき） — resources/xodr/parking_demo.xodr :: road 1 object id=2  [GT:resources/xodr]
- **road.object.marking.markings_with_outline** object id=3 は outline を使用するが <markings> が <object> 直下にある（<outline>内に配置すべき） — resources/xodr/parking_demo.xodr :: road 1 object id=3  [GT:resources/xodr]
- **road.object.marking.markings_with_outline** object id=4 は outline を使用するが <markings> が <object> 直下にある（<outline>内に配置すべき） — resources/xodr/parking_demo.xodr :: road 1 object id=4  [GT:resources/xodr]
- **road.object.marking.markings_with_outline** object id=5 は outline を使用するが <markings> が <object> 直下にある（<outline>内に配置すべき） — resources/xodr/parking_demo.xodr :: road 1 object id=5  [GT:resources/xodr]
- **road.object.marking.markings_with_outline** object id=6 は outline を使用するが <markings> が <object> 直下にある（<outline>内に配置すべき） — resources/xodr/parking_demo.xodr :: road 1 object id=6  [GT:resources/xodr]
- **road.object.marking.markings_with_outline** object id=7 は outline を使用するが <markings> が <object> 直下にある（<outline>内に配置すべき） — resources/xodr/parking_demo.xodr :: road 1 object id=7  [GT:resources/xodr]
- **road.object.marking.markings_with_outline** object id=8 は outline を使用するが <markings> が <object> 直下にある（<outline>内に配置すべき） — resources/xodr/parking_demo.xodr :: road 1 object id=8  [GT:resources/xodr]
- **road.object.repeating.attributes_with_outline_skeleton** object id=4（<outlines>/<outline>/<skeleton>あり）の<repeat>に heightStart,heightEnd が指定されている（outline/skeleton持ちオブジェクトには非適用） — resources/xodr/parking_demo.xodr :: road 1 object id=4  [GT:resources/xodr]
- **road.object.repeating.attributes_with_outline_skeleton** object id=6（<outlines>/<outline>/<skeleton>あり）の<repeat>に lengthStart,lengthEnd,widthStart,widthEnd,heightStart,heightEnd が指定されている（outline/skeleton持ちオブジェクトには非適用） — resources/xodr/parking_demo.xodr :: road 1 object id=6  [GT:resources/xodr]
- **road.object.repeating.outline_use_cornerlocal** repeatされる object id=6 の outline id=0 が cornerRoad を4件使用（cornerLocal を使うべき） — resources/xodr/parking_demo.xodr :: road 1 object id=6  [GT:resources/xodr]
- **road.object.repeating.attributes_with_outline_skeleton** object id=8（<outlines>/<outline>/<skeleton>あり）の<repeat>に lengthStart,lengthEnd,widthStart,widthEnd,heightStart,heightEnd が指定されている（outline/skeleton持ちオブジェクトには非適用） — resources/xodr/parking_demo.xodr :: road 1 object id=8  [GT:resources/xodr]
- **road.object.repeating.outline_use_cornerlocal** repeatされる object id=8 の outline id=0 が cornerRoad を4件使用（cornerLocal を使うべき） — resources/xodr/parking_demo.xodr :: road 1 object id=8  [GT:resources/xodr]
- **road.object.repeating.valid_s_length** object id=100 repeat @s+@length=210 が road @length=200 を超過（+10） — resources/xodr/parking_demo.xodr :: road 1 object id=100 s=10  [GT:resources/xodr]
- **road.object.outline.exactly_one_outer** object id=101 の<outlines>内で outer=true な<outline>が7件（outline id=['0', '1', '2', '3', '4', '5', '6'], 全体7件） — resources/xodr/parking_demo.xodr :: road 1 object id=101  [GT:resources/xodr]
- **road.object.repeating.attributes_with_outline_skeleton** object id=101（<outlines>/<outline>/<skeleton>あり）の<repeat>に heightStart,heightEnd が指定されている（outline/skeleton持ちオブジェクトには非適用） — resources/xodr/parking_demo.xodr :: road 1 object id=101  [GT:resources/xodr]
- **road.object.repeating.valid_s_length** object id=101 repeat @s+@length=210 が road @length=200 を超過（+10） — resources/xodr/parking_demo.xodr :: road 1 object id=101 s=10  [GT:resources/xodr]
- **road.object.repeating.valid_s_length** object id=11 repeat @s+@length=31.3 が road @length=30.1 を超過（+1.2） — resources/xodr/parking_demo.xodr :: road 3 object id=11 s=1.3  [GT:resources/xodr]
- **road.object.repeating.valid_s_length** object id=12 repeat @s+@length=31.3 が road @length=30.1 を超過（+1.2） — resources/xodr/parking_demo.xodr :: road 3 object id=12 s=1.3  [GT:resources/xodr]
- **road.linkage.both_sides_consistency** road 7.predecessor->road 2(contactPoint=end) is not reciprocated on road 2's side — resources/xodr/soderleden.xodr :: road 7  [GT:resources/xodr]
- **road.linkage.both_sides_consistency** road 7.successor->road 1(contactPoint=end) is not reciprocated on road 1's side — resources/xodr/soderleden.xodr :: road 7  [GT:resources/xodr]
- **road.object.validty.check_parent_orientation** object id=0 @orientation='+' だが validity [-3,3] が正負両方のレーンidにまたがる（親orientationの部分集合であるべき） — resources/xodr/straight_500m_signs.xodr :: road 1 object id=0  [GT:resources/xodr]
- **road.object.validty.right_hand_traffic_lane_ids** object id=0 (RHT) @orientation='+' は負のレーンidのみ許容だが validity [-3,3] に正idを含む — resources/xodr/straight_500m_signs.xodr :: road 1 object id=0  [GT:resources/xodr]
- **road.signal.signal_type** signal id=1: type='' subtype='' は非specific（type/subtypeともに未指定/-1/none） — resources/xodr/straight_500m_signs.xodr :: road 1 signal id=1  [GT:resources/xodr]
- **road.lane_section.lane_long_zero_width** road 2 lane -2 が s=0〜300（300m）にわたり幅0 — resources/xodr/tunnels.xodr :: road 2 s=0 lane -2  [GT:resources/xodr]
