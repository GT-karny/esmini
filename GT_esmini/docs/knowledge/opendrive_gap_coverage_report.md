# OpenDRIVE Annex F gap-rule 監査 α — カバレッジ報告

> **面3資産監査トラック α**（capability_model_3face）: 「XSD/既存checker が見逃す ruleable ミスを潰す」。
> ASAM OpenDRIVE 1.9.0 Annex F 規範ルール **275** のうち、off-the-shelf `qc-opendrive` が実装する **16** を除く
> **gap 259** を GT checker 化（XML直パース・stdlib のみ・依存なし）。多エージェント実装→独立検証→敵対的監査の3段。

## 1. 会計（accounting）— 完了条件: 未トリアージ 0

| 指標 | 値 |
| :-- | --: |
| 分母 (Annex F 規範ルール) | **275** |
| qc-opendrive 実装 (既存) | 16 |
| **GT 新規実装 (implemented_gt)** | **197** |
| 被覆率 (qc+GT)/分母 | **77.5%**（監査前 5.8%） |
| gap_geometry_math (幾何計算要・理由分類) | 47 |
| gap_ambiguous (仕様が実装判断要・理由分類) | 15 |
| gap_niche | 0 |
| gap_deferred | 0 |
| **gap_untriaged (未分類=残債)** | **0** |

gap 259 の内訳 = implemented_gt 197 … ではなく、**gap 259 = implemented_gt 197 + gap_geometry_math 47 + gap_ambiguous 15 （未トリアージ 0）**。全 gap ルールが実装 or 理由付き分類 = **会計クローズ**。

## 2. カテゴリ別 内訳（gap 259 のトップカテゴリ集計）

| top category | implemented_gt | gap_geometry_math | gap_ambiguous | 計 |
| :-- | --: | --: | --: | --: |
| road | 164 | 25 | 11 | 200 |
| junctions | 27 | 22 | 4 | 53 |
| ids | 3 | 0 | 0 | 3 |
| header | 2 | 0 | 0 | 2 |
| defaultRegulations | 1 | 0 | 0 | 1 |

## 3. 逐次敵対的監査（19グループ懐疑エージェント・反例fixture実測）

全19グループの checker を、反例 xodr fixture を自作して実測反証。**検出欠陥 42 件**。

| verdict | 件数 | 重大度 |
| :-- | --: | :-- |
| CLASSIFICATION_DISPUTED | 16 | med:1, low:15 |
| FALSE_POSITIVE | 15 | high:1, med:8, low:6 |
| FALSE_NEGATIVE | 9 | med:2, low:7 |
| MISENCODED | 2 | med:1, low:1 |

**med+high の correctness 欠陥 12 件を Sonnet エージェントで修正済み**（校正セット発火 20→11、GT違反 104→94）。修正内訳:

- **junctions.direct.road_connectivity** [FALSE_NEGATIVE/med] — Also flag a direct junction whose varying (non-hub) side resolves to <2 distinct road ids -- including junctions with a single (incomingRoad,linkedRoa
- **junctions.direct.split_or_merge** [MISENCODED/med] — Reclassify as gap_geometry_math (true crossing-traffic detection needs lane geometry), OR stop asserting 'crossing traffic'/'Figure 92' on non-crossin
- **header.proj.max_one_proj** [FALSE_POSITIVE/med] — If the CDATA contains '+proj=pipeline', treat the subsequent '+proj=' as pipeline +step stages (single definition) and do not flag; only count multipl
- **ids.id_unique_in_class** [FALSE_POSITIVE/med] — Restrict file-global uniqueness to road/junction/junctionGroup/controller (drop object & signal), OR exempt id="0" placeholders, OR scope object/signa
- **ids.only_ref_defined_ids** [FALSE_NEGATIVE/med] — Also validate connection @linkedRoad and junction @mainRoad (and @crossingRoad/@roadAtStart/@roadAtEnd) against road_ids.
- **road.use_cases.shape_elements_start_right** [FALSE_POSITIVE/med] — Only flag an s-group that has >=2 shape entries whose minimum t >= 0 (a genuinely one-sided piecewise shape); exempt a single shape entry at t~=0 whos
- **road.lane.layer.lane_group_width_temporary** [FALSE_POSITIVE/high] — Use extent = sum(width) - laneOffset for the right group and sum(width) + laneOffset for the left group; skip a side that has no lanes; add tolerance;
- **road.lane.link.multiple_connections** [FALSE_POSITIVE/med] — Restrict to drivable lane types (reuse module's _DRIVING_LANE_TYPES) AND treat a lane that tapers to/from 0 at a section boundary as a legitimate lane
- **road.lane_section.new_lanesec_link_temp_to_perm** [FALSE_POSITIVE/med] — In _cross_declared_on / _perm_cross_declared require the link entry's @layer to be the opposite layer (temp lane link with layer=='permanent', or perm
- **road.lane.lane_order_no_gaps** [FALSE_POSITIVE/med] — Make the gap check cross-section aware: union lane ids across the road's lane sections and suppress a per-section gap when the missing intermediate id
- **road.signal.use_country_code** [FALSE_POSITIVE/med] — Exempt board-sentinel @type values ('staticBoard','vmsBoard','multiBoard') from the country requirement - @country belongs on the child <sign> element
- **road.railroad.rail_refline_centered** [FALSE_POSITIVE/med] — Evaluate laneOffset and <width> as full polynomials at the laneSection s (not just @a) to kill the non-constant-offset false positive; and scope the c

### 残 low 欠陥（29件）= 既知事項（次パス候補）
うち **CLASSIFICATION_DISPUTED 16 件**（監査が gap_* 分類に「構造プロキシで拾えるのでは」と反論した upgrade 候補）、低優先 FALSE_POSITIVE/FALSE_NEGATIVE 数件。会計には影響せず、報告のみ。

## 4. コーパス 208 への適用（GT自作資産の違反レポート）

原本 = `opendrive_gap_rule_report.md`（origin別・ルール別・GT違反先頭150）。要点:

- 検査 **208** files（parse失敗 3 = 意図的破壊 handauthored fixture）／**実行時例外 0**
- **GT自作の違反 94 件**（監査対象）: resources/xodr 54 / test 16 / handauthored 11 / generated 13
- 公式ASAM校正セット発火 **11 件** … **10件 genuine**（不完全な公式doc抜粋の dangling ref、実SHOULD違反 — 2独立エージェントが実XMLで確認）＋ **1件 低優先FP**（`road.corner_local.first_id_zero`、既知・次パス）
- upstream(EnvironmentSimulator) 138 件 = **対象外**（バケット隔離）

## 5. 成果物

| 成果物 | パス |
| :-- | :-- |
| checker suite (19モジュール+master+契約) | `GT_esmini/docs/knowledge/opendrive_gap_checkers/` |
| status反映 台帳 | `GT_esmini/docs/knowledge/opendrive_rule_ledger.yaml` |
| カバレッジ報告 (本書) | `GT_esmini/docs/knowledge/opendrive_gap_coverage_report.md` |
| 208適用 GT違反レポート | `GT_esmini/docs/knowledge/opendrive_gap_rule_report.md` |
| 監査 findings (raw 42件) | `.../opendrive_gap_checkers/audit_defects.json` |
| 最終 status マップ | `.../opendrive_gap_checkers/final_status.json` |

**再実行**: `DriverScript/.venv/Scripts/python.exe GT_esmini/docs/knowledge/opendrive_gap_checkers/gap_rule_check_master.py`
（コーパス208へ全19モジュール適用 → `opendrive_gap_rule_report.md` 再生成）。

## 6. 手法（FP回避の要）

1. **origin分類**: 公式ASAM=校正（発火≒0期待）/ GT自作=監査対象 / upstream=対象外。
2. **junction文脈認識**: connecting road(@junction≠-1)はリンク意味論が違う → 各ルールで文脈特例化（前セッションの991FP級誤爆を回避）。
3. **意図的負fixture＋敵対的反例**: handauthored 負fixture＋監査エージェント自作の反例 xodr で発火/非発火を実証。
4. **flag = review（助言）**、error ではない。origin別集計。
5. **多エージェント3段**: 実装(19) → 独立検証(19) → 敵対的監査(19)。実装/検証/監査は Sonnet SubAgent。
