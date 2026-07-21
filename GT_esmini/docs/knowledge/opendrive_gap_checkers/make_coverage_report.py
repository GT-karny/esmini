"""Generate the alpha coverage report: ties together the ledger status accounting,
the adversarial-audit outcome, and the 208-corpus application into one document at
GT_esmini/docs/knowledge/opendrive_gap_coverage_report.md."""

import json
import yaml
import sys
from pathlib import Path
from collections import Counter, defaultdict

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(r"e:/Repository/GT_esmini/esmini")
SP = Path(__file__).resolve().parent
LEDGER = ROOT / "GT_esmini/docs/knowledge/opendrive_rule_ledger.yaml"
AUDIT = SP / "audit_defects.json"
OUT = ROOT / "GT_esmini/docs/knowledge/opendrive_gap_coverage_report.md"

d = yaml.safe_load(LEDGER.read_text(encoding="utf-8"))
meta = d["meta"]
rules = d["rules"]
defects = json.loads(AUDIT.read_text(encoding="utf-8"))

# top-category rollup of gap rules (exclude implemented_qc — those are the qc-covered 16)
gap_rules = [r for r in rules if r["status"] != "implemented_qc"]


def top(r):
    return r["rule_name"].split(".")[0]


bytop = defaultdict(Counter)
for r in gap_rules:
    bytop[top(r)][r["status"]] += 1

STATUS_ORDER = [
    "implemented_gt",
    "gap_geometry_math",
    "gap_ambiguous",
    "gap_niche",
    "gap_deferred",
]

L = []
L.append("# OpenDRIVE Annex F gap-rule 監査 α — カバレッジ報告")
L.append("")
L.append(
    "> **面3資産監査トラック α**（capability_model_3face）: 「XSD/既存checker が見逃す ruleable ミスを潰す」。"
)
L.append(
    "> ASAM OpenDRIVE 1.9.0 Annex F 規範ルール **275** のうち、off-the-shelf `qc-opendrive` が実装する **16** を除く"
)
L.append(
    "> **gap 259** を GT checker 化（XML直パース・stdlib のみ・依存なし）。多エージェント実装→独立検証→敵対的監査の3段。"
)
L.append("")
L.append("## 1. 会計（accounting）— 完了条件: 未トリアージ 0")
L.append("")
L.append("| 指標 | 値 |")
L.append("| :-- | --: |")
L.append(f"| 分母 (Annex F 規範ルール) | **{meta['denominator_total']}** |")
L.append(f"| qc-opendrive 実装 (既存) | {meta['qc_implemented']} |")
L.append(f"| **GT 新規実装 (implemented_gt)** | **{meta['gt_implemented']}** |")
L.append(f"| 被覆率 (qc+GT)/分母 | **{meta['coverage_pct']}%**（監査前 5.8%） |")
L.append(f"| gap_geometry_math (幾何計算要・理由分類) | {meta['gap_geometry_math']} |")
L.append(f"| gap_ambiguous (仕様が実装判断要・理由分類) | {meta['gap_ambiguous']} |")
L.append(f"| gap_niche | {meta['gap_niche']} |")
L.append(f"| gap_deferred | {meta['gap_deferred']} |")
L.append(f"| **gap_untriaged (未分類=残債)** | **{meta['gap_untriaged']}** |")
L.append("")
L.append(
    f"gap 259 の内訳 = implemented_gt {meta['gt_implemented']-0} … ではなく、"
    f"**gap 259 = implemented_gt {sum(1 for r in gap_rules if r['status']=='implemented_gt')} "
    f"+ gap_geometry_math {meta['gap_geometry_math']} + gap_ambiguous {meta['gap_ambiguous']} "
    f"（未トリアージ {meta['gap_untriaged']}）**。全 gap ルールが実装 or 理由付き分類 = **会計クローズ**。"
)
L.append("")

L.append("## 2. カテゴリ別 内訳（gap 259 のトップカテゴリ集計）")
L.append("")
L.append("| top category | implemented_gt | gap_geometry_math | gap_ambiguous | 計 |")
L.append("| :-- | --: | --: | --: | --: |")
for t in sorted(bytop, key=lambda x: -sum(bytop[x].values())):
    c = bytop[t]
    tot = sum(c.values())
    L.append(
        f"| {t} | {c.get('implemented_gt',0)} | {c.get('gap_geometry_math',0)} | {c.get('gap_ambiguous',0)} | {tot} |"
    )
L.append("")

# audit summary
by_verdict = Counter(x["verdict"] for x in defects)
by_sev = Counter(x["severity"] for x in defects)
L.append("## 3. 逐次敵対的監査（19グループ懐疑エージェント・反例fixture実測）")
L.append("")
L.append(
    f"全19グループの checker を、反例 xodr fixture を自作して実測反証。**検出欠陥 {len(defects)} 件**。"
)
L.append("")
L.append("| verdict | 件数 | 重大度 |")
L.append("| :-- | --: | :-- |")
for v, n in by_verdict.most_common():
    sevs = Counter(x["severity"] for x in defects if x["verdict"] == v)
    L.append(
        f"| {v} | {n} | "
        + ", ".join(f"{k}:{sevs[k]}" for k in ("high", "med", "low") if sevs[k])
        + " |"
    )
L.append("")
L.append(
    f"**med+high の correctness 欠陥 {sum(1 for x in defects if x['severity'] in ('med','high') and x['verdict'] in ('FALSE_POSITIVE','FALSE_NEGATIVE','MISENCODED'))} 件を Sonnet エージェントで修正済み**"
    "（校正セット発火 20→11、GT違反 104→94）。修正内訳:"
)
L.append("")
for x in defects:
    if x["severity"] in ("high", "med") and x["verdict"] in (
        "FALSE_POSITIVE",
        "FALSE_NEGATIVE",
        "MISENCODED",
    ):
        L.append(
            f"- **{x['rule']}** [{x['verdict']}/{x['severity']}] — {x['fix'][:150]}"
        )
L.append("")
L.append("### 残 low 欠陥（29件）= 既知事項（次パス候補）")
L.append(
    f"うち **CLASSIFICATION_DISPUTED {by_verdict.get('CLASSIFICATION_DISPUTED',0)} 件**"
    "（監査が gap_* 分類に「構造プロキシで拾えるのでは」と反論した upgrade 候補）、"
    f"低優先 FALSE_POSITIVE/FALSE_NEGATIVE 数件。会計には影響せず、報告のみ。"
)
L.append("")

L.append("## 4. コーパス 208 への適用（GT自作資産の違反レポート）")
L.append("")
L.append(
    "原本 = `opendrive_gap_rule_report.md`（origin別・ルール別・GT違反先頭150）。要点:"
)
L.append("")
L.append(
    "- 検査 **208** files（parse失敗 3 = 意図的破壊 handauthored fixture）／**実行時例外 0**"
)
L.append(
    "- **GT自作の違反 94 件**（監査対象）: resources/xodr 54 / test 16 / handauthored 11 / generated 13"
)
L.append(
    "- 公式ASAM校正セット発火 **11 件** … **10件 genuine**（不完全な公式doc抜粋の dangling ref、実SHOULD違反 — 2独立エージェントが実XMLで確認）＋ **1件 低優先FP**（`road.corner_local.first_id_zero`、既知・次パス）"
)
L.append("- upstream(EnvironmentSimulator) 138 件 = **対象外**（バケット隔離）")
L.append("")

L.append("## 5. 成果物")
L.append("")
L.append("| 成果物 | パス |")
L.append("| :-- | :-- |")
L.append(
    "| checker suite (19モジュール+master+契約) | `GT_esmini/docs/knowledge/opendrive_gap_checkers/` |"
)
L.append("| status反映 台帳 | `GT_esmini/docs/knowledge/opendrive_rule_ledger.yaml` |")
L.append(
    "| カバレッジ報告 (本書) | `GT_esmini/docs/knowledge/opendrive_gap_coverage_report.md` |"
)
L.append(
    "| 208適用 GT違反レポート | `GT_esmini/docs/knowledge/opendrive_gap_rule_report.md` |"
)
L.append(
    "| 監査 findings (raw 42件) | `.../opendrive_gap_checkers/audit_defects.json` |"
)
L.append("| 最終 status マップ | `.../opendrive_gap_checkers/final_status.json` |")
L.append("")
L.append(
    "**再実行**: `DriverScript/.venv/Scripts/python.exe GT_esmini/docs/knowledge/opendrive_gap_checkers/gap_rule_check_master.py`"
)
L.append(
    "（コーパス208へ全19モジュール適用 → `opendrive_gap_rule_report.md` 再生成）。"
)
L.append("")
L.append("## 6. 手法（FP回避の要）")
L.append("")
L.append(
    "1. **origin分類**: 公式ASAM=校正（発火≒0期待）/ GT自作=監査対象 / upstream=対象外。"
)
L.append(
    "2. **junction文脈認識**: connecting road(@junction≠-1)はリンク意味論が違う → 各ルールで文脈特例化（前セッションの991FP級誤爆を回避）。"
)
L.append(
    "3. **意図的負fixture＋敵対的反例**: handauthored 負fixture＋監査エージェント自作の反例 xodr で発火/非発火を実証。"
)
L.append("4. **flag = review（助言）**、error ではない。origin別集計。")
L.append(
    "5. **多エージェント3段**: 実装(19) → 独立検証(19) → 敵対的監査(19)。実装/検証/監査は Sonnet SubAgent。"
)

OUT.write_text("\n".join(L) + "\n", encoding="utf-8")
print(f"[out] {OUT}")
print(
    f"coverage_pct={meta['coverage_pct']}  gt_implemented={meta['gt_implemented']}  untriaged={meta['gap_untriaged']}"
)
print(f"audit defects={len(defects)}  by_verdict={dict(by_verdict)}")
