# §B チェッカーに含めきれない部分 — 全件と「どう自分/ツールで守るか」

XSD / qc-opendrive / GT gap checker が**原理的に○×できない**規範ルール。§A を全部通しても、ここを外すと規格外になる。
出典 ASAM OpenDRIVE 1.9.0 Annex F。台帳 `opendrive_rule_ledger.yaml` の status で該当（gap_ambiguous / gap_geometry_math）。
3系統: **B1 設計判断（永久に検査不能）/ B2 幾何が絡む（別手段で担保）/ B3 ソフト向け（作者は無関係）**。

---

## B1. 設計判断 — 機械検査が永久に不能【最重要】

道路網全体＋現実の意味を人が判断しないと○×が決まらない。gap checker は沈黙する。
**守り方**: 作る前に人が決める。ツール自作なら生成ロジックの分岐条件（入次数/出次数を数える等）に組み込む。

### 交差点(junction)の要否 ← ここを外すと意味が壊れる
- **junction は接続が曖昧なときだけ使う**（`junctions.common.when_to_use` / `not_only_two` / 参考: qc実装済 `road.linkage.is_junction_needed`）。
  - predecessor/successor 候補が**2つ以上**（合流・分岐・実交差点）→ junction。
  - 候補が**一意**（1本道の続き）→ junction を使わず road↔road 直接リンク。**2本が出会うだけなら junction で包まない**。
  - ★罠: **「A→B は1対1」でも、B の手前に別の道路も合流する（B の入次数≥2）なら junction 必要**。片側の出次数だけ見て判断しない。`<predecessor>` は maxOccurs=1 なので、そもそも2本目を直接書けない。
- **レーン接続は曖昧なら junction 経由**（`road.lane.link.use_junctions` / `no_link`）。一意なら lane 直リンク可。connecting road 内で重複する lane `<link>` を張らない。

### junction 内レーン変更の2流儀（どちらも正しい。意図して選ぶ）
- `junctions.connection.lane_change_one_con_road` / `no_lane_change_for_mult_con_roads` — 1本の connecting road に複数 `<laneLink>`=junction内レーン変更可 / 車線ごとに connecting road を分ける=レーン変更不可。禁止事項ではないので検査対象外。

### 無視してよい（禁止でも要求でもない・構造上正しくなる）
- 許可の文: `bridges.define_type` / `object.material.materials_may_differ` / `type.lane_type_may_differ_from_parent`。
- 構造上正しくなる: `elevation.elev_along_ref_line` / `road_mark.only_outer` / `type.create_new_type_in_parent`（書き方が1通りで間違えようがない）。

---

## B2. 幾何が絡む規約 — GT gap checker 未実装（47件）→ 別手段で担保

XML属性だけでは判定できず幾何計算（曲率・座標・多角形重なり・境界閉合・標高グリッド）が要る。
**守り方**: (a) 構築時に不変条件として保証、(b) 幾何を評価する外部ツール（フル qc-opendrive 等）や RoadManager ロードで裏取り。

### 参照線・曲線（折れ/接続点/曲率）
`geometry.refline_no_kinks`（連続geometryの接線を合わせる＝折れなし）/ `geometry.contact_point`（接続点の座標一致）/ `curve_local.continuous_curve_local` / `length_match` / `paramPoly3.arcLength_range` / `normalized_range`
→ **守り方**: 生成時に各 geometry 終端の (x,y,hdg) を次の始端に一致させる。spiral/arc の曲率連続を保つ。

### 重なり判定（道路/物体・6件）
`road.no_overlap_self` / `no_overlap_outside_junction` / `overlap_inside_junction` / `object.surface.no_bounding_box_overlap` / `crg_hidden_on_object_overlap` / `object_reference_on_overlap`
→ **守り方**: 生成時に道路/物体の占有領域が重ならないよう配置。RoadManager ロードで破綻を検出。

### junction 幾何（境界閉合/標高グリッド/接続滑らかさ・22件）
`junctions.no_overlap` / `boundary.close_gap_with_new_roads` / `segments_close_boundry` / `segments_counter_clockwise_order` / `segments_for_each_conn_road` / `connection.smooth_fit` / `direct.flat_exits_entries` / `linked_lane_smoothness` / `overlap_zone_coverage` / `road_ramp_heading` / `elevation_grid.*`(4) / `geometry.correct_junction_boundry` / `ref_line_definition` / `virtual.heading_equal_mainroad` / `linked_lanes_smooth_fit` / ほか
→ **守り方**: 交差点境界を反時計回りで閉じる。connecting road が本線と滑らかに（位置・向き・曲率）接続。標高グリッドは境界全体を覆う。

### 物体形状・断面（点がBB内/輪郭・13件）
`object.outline.inner_outline_touches_outer` / `points_inside_box` / `skeleton.points_inside_box` / `points_boundary_inside_box` / `points_requirements` / `surface.calculate_road_height` / `identical_local_coordinates` / `avoid_skewed_crg_surfacees` / `cross_section_surface.height` / `lane_def_valid` ほか
→ **守り方**: 物体の頂点/輪郭点が bounding box 内。CRG面のローカル座標整合。

### その他（3件）
`road.lane.road_mark.position_outer_half`（roadMark位置）/ `lane_section.lanesec_usage_lane_num`（区間内レーン数変化=幅多項式の根）/ `shape.t_definition_coverage`（shapeのt被覆）。

---

## B3. 読み取りソフト向けの規定 — 作者/ツールは無関係（4件）

xodr の中身の制約ではなく**シミュレータ（消費側）がどう振る舞うべきか**の指示。**xodr で"守る"対象ではない**。混同して不要な属性を足さない。
- `junctions.cross_path.disregard_cross_road_evelation`（crossPath処理時、交差道路の標高は無視）
- `road.object.skeleton.vertex_local.linear_interpolation` / `vertex_road.linear_interpolation`（頂点間は直線補間で描画）
- `road.signal.priority`（実在signalの規制を既定交通ルールより優先適用＝走行判断）

---

## まとめ

| 系統 | 件数 | 誰が守るか |
| :-- | --: | :-- |
| B1 設計判断 | 15(内 actionable=5) | **人/ツール設計**（junction要否・lane接続方式） |
| B2 幾何が絡む | 47 | **構築時の不変条件 or 幾何評価ツール/RoadManagerロード** |
| B3 ソフト向け | (B1の③に内包) | 作者は無関係（実装側の話） |

**§A（機械チェッカー通過）＋ B1・B2 を自分/ツールで保証 ＝ 規格に基づいた xodr**。
