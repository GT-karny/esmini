# §A 機械チェッカーで担保できる部分 — カタログと回し方

ここは XSD / qc-opendrive / GT gap checker が**自動で○×できる**領域。作った/生成したものは手段を問わず必ず回す。
「この規約は機械で担保できる（=Alayer）」ものの要点カタログ＋各チェッカーの実行手順。

## A-1. チェッカーの回し方（3層）

Python は必ず `DriverScript/.venv/Scripts/python.exe`（bare python はガードフックが拒否）。

1. **XSD 妥当性** — 型・出現数のみ。`scripts/run_odr_conformance.py --profile quick`（schema + RM プローブ）。
2. **GT gap checker** — Annex F 規範ルール197件を XML 直パースで検査（qc-opendrive が拾わない層）:
   ```
   DriverScript/.venv/Scripts/python.exe GT_esmini/docs/knowledge/opendrive_gap_checkers/gap_rule_check_master.py
   ```
   出力 `GT_esmini/docs/knowledge/opendrive_gap_rule_report.md` の origin 別集計で自分のファイルの違反を確認。
   単一ファイル/ツール出力を検査するなら、モジュールを import し
   `run_checks(file, root, roads, road_ids, junctions, junction_ids)` を呼ぶ小ドライバを書く（master runner が手本）。
3. **RoadManager ロード** — esmini/GT_Sim に headless ロードし経路が通るか（意味の最終裏取り）。
4. パーサ/道路資産を触るなら `/gates`（②回帰 + ③ODR適合フル）。

**ツール自作時のゲート化**: 生成パイプライン末尾に 2（必要なら 3）を組み込み、違反>0 なら出力を止める＝規格外を"出さない"CI。

## A-2. 機械で担保できる規約カタログ（作る前に知れば手戻りゼロ）

各項の末尾は台帳 `opendrive_rule_ledger.yaml` の rule 名（status=implemented_gt/qc）。

### レーンid・番号
- 中心 id=**0**、幅なし。正id=中心の左 / 負id=右（+s方向を見て）。中心から連番・欠番なし。laneSection内で id一意。
- rule: `center_lane_id` `lanes_numbered_correctly` `lane_order` `lane_order_no_gaps` `id_unique_in_lane_section`
- **中心レーン(id=0)を type="driving" にしない**（走行レーンではない。type="none"等）。

### LHT/RHT と信号の向き
- `<road @rule>`（省略時 **RHT**）。ファイル内で RHT/LHT 混在させない。
- signal/signalReference は `@orientation` 必須。`@orientation` と `<validity>` 車線id符号の整合:
  - RHT: `+`→**負(右)**の走行車線のみ / `-`→正(左)のみ。
  - LHT: 逆（`+`→正 / `-`→負）。
  - ※driving車線が対象。両側にまたがる正当な標識は `orientation="none"`。
- rule: `signal.validity/reference.{left,right}_hand_traffic_lane_ids` `reference.specify_direction` `rule_hand_uniformity`

### 幾何連続
- `<geometry>` は s昇順・隙間なく連続、最初は s=0、Σlength = `<road @length>`。s を持つ全リスト（elevation/superelevation/shape/type/laneOffset）は昇順。
- rule: `geometry.elem_asc_order` `refline_no_gaps` `length_sum_geometries` 各 `*.elem_asc_order`

### リンク相互性・参照整合
- road A の successor→B なら B 側にも A への predecessor/successor（双方向）。direct junction の `@linkedRoad`、virtual junction の `@mainRoad` も参照解決。
- road/junction id はファイル内一意。link/connection が指す id は定義済み。
- rule: `both_sides_consistency` `only_ref_defined_ids` `id_unique_in_class`

### junction 文脈（誤りの最大要因）
- connecting road(`@junction!=-1`)は通常道路とリンク意味論が違う。**直接の road↔road 相互リンクを張らない**（リンクは junction の `<connection>` 経由）。入口/出口 road 側は junction(`elementType="junction"`)を指す。
- direct junction の `<connection>` は `@connectingRoad` を持たない（`@linkedRoad`）。
- rule: `junctions.direct.connecting_road_attribute_usage` `road_connectivity` 等

### 幅・テーパー
- lane width は負にしない。幅0への滑らかなテーパー（車線begin/end）は正当（違反扱いしない）。
- rule: `width.lane_width_validity`

**このカタログ全体を通過しても §B は担保されない** → `beyond_checker.md` へ。
