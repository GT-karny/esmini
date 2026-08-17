#!/usr/bin/env python3
"""検知器の向きの実証（spine-work:derived-report-lint）。

意図的な違反データを作り、各検査が **実際に発火する** ことを確かめる。
KG の実データは一切触らない（fixture のコピーに対して実行する）。
2026-07-20 に「規約と反転した検知器が警報疲れを育てた」実例があるため、
検査を足したら必ずこの実証を通す。
"""
import copy
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_knowledge_graph as kg  # noqa: E402

import yaml  # noqa: E402

FAILED = []


def expect(name, hit, want=True):
    ok = bool(hit) == want
    print(f"  [{'OK ' if ok else 'MISS'}] {name}")
    if not ok:
        FAILED.append(name)


def load(p):
    return yaml.safe_load(Path(p).read_text(encoding="utf-8"))


NS = load(kg.NAMESPACES_YAML)
GRAPH = load(kg.GRAPH_YAML)
NAMESPACES = {n["slug"]: n for n in NS["namespaces"] if n.get("slug")}
CATALOGS = kg.load_catalogs()


def run_values(catalogs, namespaces):
    errs = []
    kg.check_catalog_values(catalogs, namespaces, errs.append)
    return errs


print("== 1. 値域チェック（hard）==")
# 基準: 無改変で 0件
expect("baseline clean", run_values(CATALOGS, NAMESPACES), want=False)

bad = copy.deepcopy(CATALOGS)
bad["signal"][0]["exposure"] = ["osi", "telemetry"]  # 語彙外
expect("exposure 語彙外を検出", [e for e in run_values(bad, NAMESPACES) if "telemetry" in e])

bad = copy.deepcopy(CATALOGS)
bad["signal"][0]["state"] = "(c)"
expect("state 語彙外を検出", [e for e in run_values(bad, NAMESPACES) if "(c)" in e])

bad = copy.deepcopy(CATALOGS)
bad["signal"][0]["state"] = "（a）"  # 全角括弧の表記ゆれ
expect("state 全角表記ゆれを検出", [e for e in run_values(bad, NAMESPACES) if "未知の state" in e])

bad = copy.deepcopy(CATALOGS)
bad["gate"][0]["covers"] = "  "
expect("covers 空を検出", [e for e in run_values(bad, NAMESPACES) if "covers" in e])

bad = copy.deepcopy(CATALOGS)
bad["gate"][0]["blocking"] = "true"  # 文字列は不可
expect("blocking 非boolを検出", [e for e in run_values(bad, NAMESPACES) if "blocking" in e])

ns_bad = copy.deepcopy(NAMESPACES)
ns_bad["signal"]["face"] = "4"
expect("face 語彙外を検出", [e for e in run_values(CATALOGS, ns_bad) if "未知の face" in e])

ns_bad = copy.deepcopy(NAMESPACES)
del ns_bad["gate"]["face"]
expect("face 欠落を検出", [e for e in run_values(CATALOGS, ns_bad) if "face タグがありません" in e])

# 2026-08-06 追加。'implemented'（語彙外だが**もっともらしい**綴り）が4件混入し、
# --spine-matrix の ③実装 のどのカウンタにも入らないまま「未実装req」に実装済み要求を
# 7件並べていた。値の誤りではなく数え落としなので、症状は「増えない」＝目視では出ない。
bad = copy.deepcopy(CATALOGS)
bad["vd-func"][0]["status"] = "implemented"
expect("vd-func status 語彙外を検出",
       [e for e in run_values(bad, NAMESPACES) if "未知の status" in e])

bad = copy.deepcopy(CATALOGS)
del bad["vd-func"][0]["status"]
expect("vd-func status 欠落を検出",
       [e for e in run_values(bad, NAMESPACES) if "未知の status" in e])

print("== 1a. acceptance_ladder（段階受入, hard）==")
# 2026-08-02 追加。段は「要求のどこまで出来ているか」の真実源で、graph.yaml の
# verifies 辺（要求単位）では表現できない粒度を引き受ける。ここが壊れると辺だけが残り
# 「要求全体が検証済み」と読める＝過大申告が黙って復活する。


def ladder_req(catalogs):
    """acceptance_ladder を持つ最初の要求の index を返す。"""
    return next(i for i, r in enumerate(catalogs["req-vd-ad"])
                if r.get("acceptance_ladder"))


LI = ladder_req(CATALOGS)

bad = copy.deepcopy(CATALOGS)
bad["req-vd-ad"][LI]["acceptance_ladder"][0]["met"] = "true"  # 文字列は不可
expect("met の非boolを検出",
       [e for e in run_values(bad, NAMESPACES) if "met は bool" in e])

bad = copy.deepcopy(CATALOGS)
bad["req-vd-ad"][LI]["acceptance_ladder"][0]["verified_by"] = "matcher:speed_above"
expect("verified_by の非リストを検出",
       [e for e in run_values(bad, NAMESPACES) if "verified_by はリスト" in e])

bad = copy.deepcopy(CATALOGS)
bad["req-vd-ad"][LI]["acceptance_ladder"][0]["verified_by"] = ["no_such_matcher"]
expect("verified_by の非matcher参照を検出",
       [e for e in run_values(bad, NAMESPACES) if "'matcher:<id>' 形式" in e])

bad = copy.deepcopy(CATALOGS)
bad["req-vd-ad"][LI]["acceptance_ladder"][0]["verified_by"] = ["matcher:no_such_one"]
expect("verified_by の未登録 matcher を検出",
       [e for e in run_values(bad, NAMESPACES) if "id_pattern 外" in e])

# 台帳の自己矛盾（未達なのに判定資産が通っている）
bad = copy.deepcopy(CATALOGS)
step = bad["req-vd-ad"][LI]["acceptance_ladder"][0]
step["met"] = False
step["verified_by"] = ["matcher:speed_above"]
expect("met:false なのに verified_by がある矛盾を検出",
       [e for e in run_values(bad, NAMESPACES) if "met: false なのに" in e])

print("== 1b. function_catalog の by:（実装元, hard）==")
# 2026-08-02 追加。by: に 'planner'/'driver' という未登録の擬似IDが6件入っていたのを
# 発見して足した検査。自由文字列を許すと同じ穴が黙って再発する。
bad = copy.deepcopy(CATALOGS)
bad["vd-func"][0]["by"] = "planner"
expect("by: の裸文字列を検出",
       [e for e in run_values(bad, NAMESPACES) if "名前空間付きIDではありません" in e])

bad = copy.deepcopy(CATALOGS)
bad["vd-func"][0]["by"] = "vd-component:no-such-component"
expect("by: の未登録 vd-component を検出",
       [e for e in run_values(bad, NAMESPACES) if "component_catalog_vd.yaml にありません" in e])

bad = copy.deepcopy(CATALOGS)
bad["vd-func"][0]["by"] = "vd-func:FUNC-001"
expect("by: の許容外名前空間を検出",
       [e for e in run_values(bad, NAMESPACES) if "実装元として許容されていません" in e])

bad = copy.deepcopy(CATALOGS)
bad["vd-func"][0]["by"] = "road"
expect("by: 'road' は許容（VD実装ユニットでないことを示す統制語）",
       [e for e in run_values(bad, NAMESPACES) if "by:" in e], want=False)

print("== 2. 規約2（恒久資産のファイル名の工程序数, hard）==")
with tempfile.TemporaryDirectory() as td:
    root = Path(td)
    (root / "GT_esmini" / "docs" / "knowledge").mkdir(parents=True)
    clean = root / "GT_esmini" / "docs" / "knowledge" / "signal_catalog.yaml"
    clean.write_text("x", encoding="utf-8")
    orig_root = kg.REPO_ROOT
    kg.REPO_ROOT = root
    try:
        errs = []
        kg.check_asset_naming(errs.append)
        expect("clean な資産名では発火しない", errs, want=False)
        (root / "GT_esmini" / "docs" / "knowledge" / "phase3_batch.yaml").write_text(
            "x", encoding="utf-8")
        errs = []
        kg.check_asset_naming(errs.append)
        expect("phase3_batch.yaml を検出", [e for e in errs if "phase3_batch" in e])
    finally:
        kg.REPO_ROOT = orig_root

print("== 2b. 行定義（claim_domains.yaml, hard）==")


def run_domains(catalogs, namespaces):
    errs = []
    kg.check_claim_domains(catalogs, namespaces, errs.append)
    return errs


expect("baseline clean", run_domains(CATALOGS, NAMESPACES), want=False)

bad = copy.deepcopy(CATALOGS)
bad["domain"][0]["signals"].append("no_such_signal")
expect("未知 signal 参照を検出",
       [e for e in run_domains(bad, NAMESPACES) if "no_such_signal" in e])

bad = copy.deepcopy(CATALOGS)
bad["domain"][0]["cell4"] = "◐"  # セル値の手書き混入（行定義は所属のみ）
expect("未知キー（セル値の手書き混入）を検出",
       [e for e in run_domains(bad, NAMESPACES) if "cell4" in e])

bad = copy.deepcopy(CATALOGS)
bad["domain"][0]["id"] = "phase3"  # 内容 slug でない（規約4）
expect("非内容slugの行 id を検出",
       [e for e in run_domains(bad, NAMESPACES) if "phase3" in e])

bad = copy.deepcopy(CATALOGS)
bad["domain"][0]["face"] = "3"  # 行は面1∪面2 の主張
expect("face=3 の行を検出",
       [e for e in run_domains(bad, NAMESPACES) if "face" in e])

bad = copy.deepcopy(CATALOGS)
bad["domain"][-1]["gates"] = ["no-such-gate"]
expect("未知 gate 参照を検出",
       [e for e in run_domains(bad, NAMESPACES) if "no-such-gate" in e])

print("== 2c. 行列生成器（--spine-matrix）＝セルが台帳と辺に追随すること ==")
mat = {r["id"]: r for r in kg.spine_matrix(CATALOGS, NAMESPACES, GRAPH["edges"])}
expect("行数＝行定義数", len(mat) == len(CATALOGS["domain"]))
expect("台帳の無い行の①は？（黙って埋まらない）",
       mat["manual-drive"]["cells"][1][0] == kg.SYM_NA)
expect("AutoLight ⑥ は F6 sustained-by 辺経由で ◐",
       mat["autolight-environment"]["cells"][6][0] == "◐")

# 辺を消すとセルが落ちる＝セル値が辺から計算されている証拠
edges = [e for e in copy.deepcopy(GRAPH["edges"])
         if not (e.get("from") == "feature:F6" and e.get("type") == "sustained-by")]
mat2 = {r["id"]: r for r in kg.spine_matrix(CATALOGS, NAMESPACES, edges)}
expect("F6 sustained-by 辺を消すと AutoLight ⑥ が ✕ に落ちる",
       mat2["autolight-environment"]["cells"][6][0] == "✕")

# signal 台帳の state を悪化させるとセルが落ちる＝台帳追随の証拠
bad = copy.deepcopy(CATALOGS)
for s in bad["signal"]:
    if s["id"] in (bad["domain"][0].get("signals") or []):
        s["state"] = "(a)"
mat3 = {r["id"]: r for r in kg.spine_matrix(bad, NAMESPACES, GRAPH["edges"])}
expect("member signal を全て (a) にすると④が ✕ に落ちる",
       mat3[bad["domain"][0]["id"]]["cells"][4][0] == "✕")

print("== 3. 派生レポート / coupling-audit ==")
base = kg.spine_report(CATALOGS, NAMESPACES, GRAPH["edges"])
# 2026-07-21 の ②刺激結線（req->policy の stimulated-by 5辺）は face3->face2 として
# 意図的に計上されている（graph.yaml 末尾 R5: 監査側で除外するか資産側を face:3 化
# するかは親判断待ち）。それ以外の直結辺が無いことを確認する。
expect("面3→面2 直結辺: 既知の stimulated-by(req→policy) 以外は 0件",
       [x for x in base["coupling_direct_edge"] if "stimulated-by" not in x],
       want=False)
expect("整合破れ: 実データでは 0件", base["obs_verdict_on_unemitted"], want=False)

# 面3(matcher) -> 面2(policy) の直結辺を注入
edges = copy.deepcopy(GRAPH["edges"])
edges.append({"from": "matcher:lane_keep", "to": "policy:lead_vehicle",
              "type": "observes", "source": "CLAUDE.md"})
inj = kg.spine_report(CATALOGS, NAMESPACES, edges)
expect("注入した面3→面2 直結辺を検出", inj["coupling_direct_edge"])

# 未emit(a) の signal を観測する辺を注入
unemitted = next(s["id"] for s in CATALOGS["signal"] if s.get("state") == "(a)")
edges = copy.deepcopy(GRAPH["edges"])
edges.append({"from": "matcher:speed_above", "to": f"signal:{unemitted}",
              "type": "observes", "source": "CLAUDE.md"})
inj = kg.spine_report(CATALOGS, NAMESPACES, edges)
expect("未emit signal を観測する辺を検出", inj["obs_verdict_on_unemitted"])

# observes 辺を全部消すと未観測 matcher が増える（母数がグラフ非依存であることの確認）
edges = [e for e in copy.deepcopy(GRAPH["edges"]) if e.get("type") != "observes"]
inj = kg.spine_report(CATALOGS, NAMESPACES, edges)
expect(f"observes 全削除で未観測 matcher が全数になる "
       f"({len(inj['coupling_unobserved'])}件)",
       len(inj["coupling_unobserved"]) > len(base["coupling_unobserved"]))

# (b) の signal に観測辺を足すと「未配線」から消える＝(a)/(b) の打ち手の区別が効く
wired_b = next(s["id"] for s in CATALOGS["signal"]
               if s.get("state") == "(b)"
               and any(s["id"] in x for x in base["obs_unwired"]))
edges = copy.deepcopy(GRAPH["edges"])
edges.append({"from": "matcher:speed_above", "to": f"signal:{wired_b}",
              "type": "observes", "source": "CLAUDE.md"})
inj = kg.spine_report(CATALOGS, NAMESPACES, edges)
expect(f"(b) signal を配線すると未配線から外れる ({wired_b})",
       len(inj["obs_unwired"]) == len(base["obs_unwired"]) - 1)

print()
if FAILED:
    print(f"FAILED: {len(FAILED)} — {FAILED}")
    sys.exit(1)
print("すべての検知器が意図した向きで発火した")
