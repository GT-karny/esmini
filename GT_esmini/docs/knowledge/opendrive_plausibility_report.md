# OpenDRIVE 妥当性/外れ値 監査 (beta' phase-1) — 全コーパス

> **XSD も Annex F checker も通るが *非現実* な authored 値を検出**（in-range-but-wrong = ルール化困難な資産固有ミス）。flag は error でなく **review**。
> **公式ASAM=校正セット / 上流=対象外 / GT自作=真の監査対象**。生成=`scratchpad/plausibility_lint.py`。

- 検査ファイル: **208**（parse失敗 3）  flag 総数: **79**

## origin別（files / flags）— GT自作が actionable

| origin | files | flags |
| :-- | --: | --: |
| GT:resources/xodr | 32 | 4 |
| GT:fixtures(lht等) | 36 | 0 |
| GT:generated(catalog) | 13 | 0 |
| official(ASAM=校正セット) | 36 | 10 |
| upstream/other(対象外) | 58 | 60 |
| other | 33 | 5 |

## origin × 種別

| origin | tight_curvature | implausible_lane_width | steep_grade | degenerate_geometry | parse_error |
| :-- | --: | --: | --: | --: | --: |
| GT:resources/xodr | 1 | 2 | 0 | 0 | 1 |
| GT:fixtures(lht等) | 0 | 0 | 0 | 0 | 0 |
| GT:generated(catalog) | 0 | 0 | 0 | 0 | 0 |
| official(ASAM=校正セット) | 6 | 3 | 0 | 1 | 0 |
| upstream/other(対象外) | 0 | 40 | 20 | 0 | 0 |
| other | 1 | 2 | 0 | 0 | 2 |

## ★ parse_error（全 3件・XML非整形＝壊れ資産）

- `GT_esmini/test/scenarios/lht_junction_osi_intersection.xodr` — not well-formed (invalid token): line 70, column 55  [other]
- `resources/xodr/virtual_junction_23.xodr` — not well-formed (invalid token): line 8, column 66  [GT:resources/xodr]
- `test_results/web/projects/7352c84d516e/virtual_junction_23.xodr` — not well-formed (invalid token): line 8, column 66  [other]

## ★ GT自作資産の flag（監査対象・全 4件、先頭80）

- **implausible_lane_width** `12m` — resources/xodr/parking_demo.xodr :: road 3 lane 2  [GT:resources/xodr]
- **implausible_lane_width** `12m` — resources/xodr/parking_demo.xodr :: road 3 lane -2  [GT:resources/xodr]
- **tight_curvature** `curv=0.4 (R=2.50m)` — resources/xodr/soderleden.xodr :: road 7 s=0.0000000000000000e+00  [GT:resources/xodr]
- **parse_error** `not well-formed (invalid token): line 8, column 66` — resources/xodr/virtual_junction_23.xodr ::   [GT:resources/xodr]

## コーパス分布（境界校正用: min / p1 / p50 / p99 / max）

| 量 | n | min | p1 | p50 | p99 | max | 採用境界 |
| :-- | --: | --: | --: | --: | --: | --: | :-- |
| drive_w | 2422 | 0 | 0 | 3.5 | 5.5 | 28 | (2.2, 5.5) |
| speed_kmh | 109 | 30 | 30 | 50 | 130 | 130 | (5, 150) |
| geom_len | 2596 | 0.0127059 | 0.137094 | 10.5217 | 500 | 10000 | (0.05, None) |
| curv | 440 | 0 | 0.000497636 | 0.0869565 | 0.5 | 1.25 | (None, 0.34) |
| slope | 776 | 0 | 0 | 0 | 0.613105 | 1 | (None, 0.18) |
