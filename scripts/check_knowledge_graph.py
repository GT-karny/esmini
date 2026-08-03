#!/usr/bin/env python3
"""Knowledge-graph structural lint (GT_esmini/docs/knowledge).

Default mode (--check, implied): validates the committed graph files —
  namespaces.yaml       registry of ID systems (single source of truth)
  graph.yaml            curated edges (judgment relations only)
  concept_vocabulary.yaml  committed OpenX Ontology concept snapshot

Checks:
  1. namespace slugs unique; id_pattern compiles; source_of_truth exists
     for status=active namespaces (file or directory).
  2. every edge endpoint is "<slug>:<local>" with a registered, non-reserved
     slug and a local id that fullmatches the namespace id_pattern.
  3. edge type is a registered *curated* type (generated types are extracted,
     never hand-written into graph.yaml).
  4. every "openx:*" reference exists in concept_vocabulary.yaml (the TTL is
     not in the repo; the vocabulary snapshot is the source of truth).
  5. no duplicate (from, to, type) edges.
  6. graph_view.md is not stale.
  7. catalog value ranges (signal_catalog.yaml / gate_catalog.yaml / face tags):
     exposure in osi|hvd|frame|derived|debug, state in (a)|(b)|(b')|●|(-),
     face in 1|2|3|cross, gate.covers non-empty, gate.blocking boolean.
     Until 2026-07-21 the lint only checked that each namespace's
     source_of_truth *existed* and never opened the node files, so every
     face:/exposure/state value had been inert, unverified data since it was
     registered (capability_model.md §7 spine-work:signal-gate-registration).
  8. 規約2 (§7.1): no process ordinal (phase3_/stage2_/...) in the filename of
     a permanent asset. Repo-wide hits at introduction: 0 — new violations only.

Matrix mode (--spine-matrix), NOT a gate: the capability-model matrix
(capability_model.md §2) rendered as a generated view — rows come from
claim_domains.yaml (membership only), cell values are computed from the
catalogs and graph edges every time. Uncomputable cells print ？ — that is
itself the detection of missing wiring, never a hand-scored value.

Report mode (--spine-report), NOT a gate: the "縦串の切れた列" derived report
(§6) plus the coupling-audit (§0.5) — ④観測欠 split into (a)未emit / (b)未配線,
⑥常設欠, ②刺激欠, and 面3→面2 の結合負債. Computed as ledger-minus-edges set
differences; never from hand-maintained flags (flags rot). A summary prints at
the end of --check but never changes its exit code: this ledger is *designed*
to be non-empty, so gating on it would be permanently red.

Extraction mode (--extract-commits): scans git history for ID tokens of the
namespaces flagged "extract: commit-mentions" and prints candidate
commit->ID edges as YAML. Tokens matching more than one namespace pattern
are flagged ambiguous — namespace collisions ("CORE-1", "R3", "P6") are a
documented property of this repo, so the extractor reports candidates and
never guesses.

Exit codes: 0 = clean, 1 = violations found, 2 = infrastructure error.
CI: .github/workflows/ci.yml test job (Linux/Release), hard gate.
Local run: DriverScript/.venv python (never system python).
"""

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except AttributeError:
    pass

try:
    import yaml
except ImportError:
    print("ERROR: pyyaml is required (pip install pyyaml)", file=sys.stderr)
    sys.exit(2)

REPO_ROOT = Path(__file__).resolve().parent.parent
FORK_REPO = "GT-karny/esmini"  # guarded write target; issues live here
KNOWLEDGE_DIR = REPO_ROOT / "GT_esmini" / "docs" / "knowledge"
NAMESPACES_YAML = KNOWLEDGE_DIR / "namespaces.yaml"
GRAPH_YAML = KNOWLEDGE_DIR / "graph.yaml"
VOCAB_YAML = KNOWLEDGE_DIR / "concept_vocabulary.yaml"
PATH_MAP_YAML = KNOWLEDGE_DIR / "path_map.yaml"
SIGNAL_CATALOG_YAML = KNOWLEDGE_DIR / "signal_catalog.yaml"
GATE_CATALOG_YAML = KNOWLEDGE_DIR / "gate_catalog.yaml"
REQ_CATALOG_YAML = KNOWLEDGE_DIR / "requirements_vd_ad.yaml"
SCENE_CATALOG_YAML = KNOWLEDGE_DIR / "scene_catalog_vd_ad.yaml"
FUNC_CATALOG_YAML = KNOWLEDGE_DIR / "function_catalog_vd_ad.yaml"
COMPONENT_CATALOG_YAML = KNOWLEDGE_DIR / "component_catalog_vd.yaml"
CLAIM_DOMAINS_YAML = KNOWLEDGE_DIR / "claim_domains.yaml"

# --- 値域（capability_model.md §2.2 / §2.3 / §4）---------------------------
# 語彙を1か所に置き、カタログとグラフの両方をこれで照合する。
EXPOSURE_VALUES = {"osi", "hvd", "frame", "derived", "debug"}
# ④観測の判定記号。"(-)" は非該当（別面に正当な観測経路あり）。
# 本文 §2.3 は活字上 "(–)"(EN DASH) で書かれている箇所があるが、カタログの実体と
# signal_catalog.yaml 自身のヘッダ定義は ASCII "(-)"。**両方を受理**する
# （表記ゆれで倒すのは検査の目的ではない。正準は ASCII 側）。
STATE_VALUES = {"(a)", "(b)", "(b')", "●", "(-)", "(–)"}
FACE_VALUES = {"1", "2", "3", "cross"}
# 面1が面3に対して負う唯一の観測IF（§0.2）。exposure がこの集合と交わらない
# signal を verdict に使う経路は結合負債。
CANONICAL_EXPOSURE = {"osi", "hvd"}

# matcher には catalog ファイルが無い（source_of_truth はコード）。台帳を
# graph.yaml の出現で近似すると **辺を1本も持たない matcher が母数から落ち**、
# 未観測 matcher を「0件＝clean」と誤報する（＝検知器の反転。2026-07-21 に実測で
# 踏んだ）。ゆえに namespace の source_of_truth である vd_metrics.py の
# eval_must ディスパッチから直接数える。
# **文の先頭の** `if kind ==` / `if kind in` だけをディスパッチ点とみなす。
# 分岐 *本体* にも `ident = ... if kind == "stopped_at_stop_sign"` のような
# kind 比較があり、これを分岐境界と誤認すると混在型（主判定=テレメトリ／一部 OSI）を
# 取り逃す（2026-07-21 実測: stopped_at_signal を OSI と誤分類していた）。
MATCHER_KIND_RE = re.compile(
    r"""^[ \t]*if\s+kind\s*(?:==|in)\s*\(?((?:\s*["'][a-z_]+["']\s*,?)+)""", re.M)
VD_METRICS_PY = REPO_ROOT / "GT_esmini" / "web" / "backend" / "services" / "vd_metrics.py"
# 面1(OSI GroundTruth)由来の観測は frame["scene"] にだけ入る（`_gt_to_scene` が埋める）。
# 分岐本体が "scene" を参照しなければ、その判定は面2 VD テレメトリ直結。
# ＝ capability_model.md §2.3a を手作業でなく機械が数えるための識別子。
SCENE_REF_RE = re.compile(r"""\[\s*["']scene["']\s*\]|\.get\(\s*["']scene["']""")
# 面2 VD テレメトリの読み出し（scene 以外の frame フィールド）。
TELEMETRY_REF_RE = re.compile(
    r"""frames?\s*\[[^\]]*\]\s*\[\s*["'](?!scene)[a-z_]+["']|"""
    r"""\[\s*["'](?:ego|driver|midlong|policy)["']\s*\]""")
# 判定本体がヘルパ関数に隠れている場合がある（`_sustained_stop` が主判定の
# テレメトリ読みを持つ）。文字列検査だけだと「OSI だけで判定している」と誤読するので
# 呼んでいるモジュール内ヘルパを1段だけ展開して評価する。
HELPER_CALL_RE = re.compile(r"\b(_[a-z_]+)\s*\(")

# --- 規約2（capability_model.md §7.1）: 恒久資産に工程名を付けない -------------
# 恒久資産のファイル名に工程の序数（phase3 / stage2 / step1 / wave4）を焼き込まない。
# 2026-07-20 の改名（55bb2bcc / 9ec1ae98）でレポ全体の実測ヒットは **0件**。
# ゆえに本検査は規約4と同型の「新設のみ弾く」形になり、hard にしても既存を巻き込まない。
PROCESS_ORDINAL_IN_FILENAME = re.compile(r"(?:phase|stage|step|wave)[-_]?\d", re.I)
# 恒久資産＝寿命が工程より長いもの。工程ドキュメント（docs/archive 等）は対象外。
PERMANENT_ASSET_GLOBS = (
    "GT_esmini/docs/knowledge/**/*.yaml",
    "GT_esmini/test/regression_baseline/**/*.yaml",
    "resources/xosc/verification/**/*",
    "resources/scenario_authoring/scenario_templates/**/*",
)

# --- 規約4（capability_model.md §7.1）: 不透明な採番の新設を禁じる ---
# id_pattern 全体が「序数・連番・単文字＋数字」だけで構成されるものを不透明とみなす。
# 例: "Phase[0-4]" / "F[1-6]" / "P(10|[0-9][ab]?)" / "M-[A-E]" / "SCN-[0-9]{3}"
ORDINAL_ID_PATTERN = re.compile(
    r"[A-Za-z_#-]{0,6}"                      # 短い接頭辞（Phase / SCN- / PR- / # など）
    r"[\[(][^\])]*[\])]"                     # 数字・英字1文字のクラス/選択
    r"(?:[\[({][^\])}]*[\])}]|[-_A-Za-z0-9?*+]){0,12}"
)

# 既存の不透明体系（凍結扱い）。参照面が大きく一斉移行しないため新設だけを弾く。
# 新しい ID をこれらに足さないこと（規約1 経過措置）。移行したら本リストから外す。
OPAQUE_LEGACY_NAMESPACES = {
    "proposal", "feature", "audit-debt", "audit-log", "audit-osc14",
    "debt-phase", "directive", "vd-phase", "vd-verif", "f1-milestone",
    "odr-plan", "odr-upstream-pr", "odr-pr-slice", "odr-stage",
    "fork-patch", "fork-marker", "odr-cluster", "odr-pending",
    "scene", "req-vd-ad", "vd-func", "scenario-variant",
    # 外部採番（本質的に不透明・対象外）
    "commit", "issue",
    # 列挙型（実体は内容 slug の列挙であって採番ではない）
    "policy", "matcher", "lineage", "openx",
}


def view_hash() -> str:
    """Fingerprint of the render inputs, embedded in graph_view.md.
    CRLF is normalized so the hash is checkout-independent (Windows
    autocrlf working trees vs LF blobs vs Linux CI)."""
    h = hashlib.sha256()
    for p in (NAMESPACES_YAML, GRAPH_YAML):
        h.update(p.read_bytes().replace(b"\r\n", b"\n"))
    return h.hexdigest()[:16]


def load_yaml(path: Path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def load_catalogs() -> dict:
    """ノード台帳（catalog ファイル）を開く。

    現行 lint は namespace の source_of_truth の *実在* しか見ず、中身を開かなかった。
    ゆえに face:/exposure/state/covers は登録以来ずっと未検証の inert なデータだった
    （capability_model.md §7 `spine-work:signal-gate-registration` の積み残し）。
    """
    def rows(path: Path, key: str) -> list:
        if not path.is_file():
            return []
        doc = load_yaml(path) or {}
        return doc.get(key) or []

    return {
        "signal": rows(SIGNAL_CATALOG_YAML, "signals"),
        "gate": rows(GATE_CATALOG_YAML, "gates"),
        "req-vd-ad": rows(REQ_CATALOG_YAML, "requirements"),
        "scene": rows(SCENE_CATALOG_YAML, "scenes"),
        "scene-modifier": rows(SCENE_CATALOG_YAML, "modifiers"),
        "vd-func": rows(FUNC_CATALOG_YAML, "functions"),
        "vd-component": rows(COMPONENT_CATALOG_YAML, "components"),
        "domain": rows(CLAIM_DOMAINS_YAML, "domains"),
    }


def _ns_re(ns: dict):
    """namespace の id_pattern を返す。本体が差し込む `_re` が無ければ自前でコンパイル。"""
    rx = ns.get("_re")
    if rx is not None:
        return rx
    pat = ns.get("id_pattern")
    if not pat:
        return None
    try:
        return re.compile(pat)
    except re.error:
        return None


def check_catalog_values(catalogs: dict, namespaces: dict, err) -> None:
    """値域チェック（hard）。exposure / state / face が統制語彙内かを検証する。

    hard にした理由: これは「台帳が壊れていないか」という真の不変条件で、
    導入時点の実測違反は 0件（47 signal / 14 gate / 31 namespace）。既存を
    巻き込まずに固定できるものは最初から hard にするのが安全側。
    """
    for i, s in enumerate(catalogs["signal"]):
        where = f"signal_catalog.yaml signals[{i}] ({s.get('id', '?')})"
        if not s.get("id"):
            err(f"{where}: missing id")
        exposure = s.get("exposure")
        if not isinstance(exposure, list) or not exposure:
            err(f"{where}: exposure はリストで1つ以上必要です")
        else:
            for v in exposure:
                if v not in EXPOSURE_VALUES:
                    err(f"{where}: 未知の exposure '{v}' "
                        f"(許容: {'/'.join(sorted(EXPOSURE_VALUES))})")
        state = s.get("state")
        if state not in STATE_VALUES:
            err(f"{where}: 未知の state '{state}' "
                f"(許容: {'/'.join(sorted(STATE_VALUES))})")

    for i, g in enumerate(catalogs["gate"]):
        where = f"gate_catalog.yaml gates[{i}] ({g.get('id', '?')})"
        if not g.get("id"):
            err(f"{where}: missing id")
        # covers は自由文（統制語彙ではない）。存在と非空だけを強制する
        # ＝「名前から推測して書かない」規律（gate_catalog.yaml 冒頭）の最低限の足場。
        if not str(g.get("covers") or "").strip():
            err(f"{where}: covers が空です（実際に何を検証しているかを書く）")
        if not isinstance(g.get("blocking"), bool):
            err(f"{where}: blocking は true/false が必須です")

    for slug, ns in namespaces.items():
        face = ns.get("face")
        if face is None:
            err(f"namespace '{slug}': face タグがありません "
                f"(許容: {'/'.join(sorted(FACE_VALUES))}, capability_model.md §0.5)")
        elif str(face) not in FACE_VALUES:
            err(f"namespace '{slug}': 未知の face '{face}' "
                f"(許容: {'/'.join(sorted(FACE_VALUES))})")

    # acceptance_ladder（段階受入）の構造検査。
    # hard にした理由: 段は「要求のどこまで出来ているか」の真実源で、graph.yaml の
    # verifies 辺（要求単位）では表現できない粒度を一手に引き受ける。ここが壊れると
    # 辺だけが残り「要求全体が検証済み」と読める＝過大申告が黙って復活する。
    # `_re` は本体フローが後から差し込むコンパイル済みパターン。実証ハーネスは生 YAML を
    # 渡すので存在しない。無い場合に黙って検査を飛ばすと**検知器が沈黙する**ため、
    # id_pattern から自前でコンパイルする（2026-08-02 の実証で MISS として露見した）。
    matcher_re = _ns_re(namespaces.get("matcher", {}))
    for i, r in enumerate(catalogs["req-vd-ad"]):
        ladder = r.get("acceptance_ladder")
        if ladder is None:
            continue
        where = f"requirements_vd_ad.yaml requirements[{i}] ({r.get('id', '?')})"
        if not isinstance(ladder, list) or not ladder:
            err(f"{where}: acceptance_ladder はリストで1段以上必要です")
            continue
        for j, s in enumerate(ladder):
            sw = f"{where} ladder[{j}]"
            if not str(s.get("step") or "").strip():
                err(f"{sw}: step が空です")
            if not str(s.get("claim") or "").strip():
                err(f"{sw}: claim が空です")
            if not isinstance(s.get("met"), bool):
                err(f"{sw}: met は bool が必須です（'true' 等の文字列は不可）")
            vb = s.get("verified_by")
            if not isinstance(vb, list):
                err(f"{sw}: verified_by はリストが必須です（未検証なら空リスト）")
                continue
            for ref in vb:
                if not isinstance(ref, str) or not ref.startswith("matcher:"):
                    err(f"{sw}: verified_by の '{ref}' は 'matcher:<id>' 形式が必須です")
                elif matcher_re and not matcher_re.fullmatch(ref.split(":", 1)[1]):
                    err(f"{sw}: verified_by の matcher '{ref}' は matcher 名前空間の "
                        "id_pattern 外です（未登録の matcher）")
            # 未達の段に判定資産が付くのは「検証済みなのに未達」＝台帳の自己矛盾。
            if vb and s.get("met") is False:
                err(f"{sw}: met: false なのに verified_by があります"
                    "（判定資産が通っているなら met を上げるか、資産の対象段を見直す）")

    # function_catalog の by:（実装元）が名前空間付きIDであることを強制する。
    # hard にした理由: 2026-08-02 まで by: には 'planner'/'driver' という**未登録の擬似ID**が
    # 6件入っていた。ITrafficPolicy 以外の実装を指す名前空間が無かったことの露出で、
    # RouteLanePlan の realizes 辺が張れない事態として表面化するまで誰も気づかなかった。
    # 自由文字列を許すと同じ穴が黙って再発する。'road' だけは「VD の実装ユニットではなく
    # esmini RoadManager 由来」を意味する統制語として明示的に許す。
    component_ids = {c.get("id") for c in catalogs["vd-component"]}
    policy_re = _ns_re(namespaces.get("policy", {}))
    for i, f in enumerate(catalogs["vd-func"]):
        by = f.get("by")
        if by is None or by == "" or by == "road":
            continue
        where = f"function_catalog_vd_ad.yaml functions[{i}] ({f.get('id', '?')})"
        if ":" not in str(by):
            err(f"{where}: by: '{by}' は名前空間付きIDではありません"
                "（'vd-component:<slug>' / 'policy:<slug>' / 'road' / 空 のいずれか）")
            continue
        slug, local = str(by).split(":", 1)
        if slug == "vd-component":
            if local not in component_ids:
                err(f"{where}: by: の '{local}' が component_catalog_vd.yaml にありません")
        elif slug == "policy":
            if policy_re and not policy_re.fullmatch(local):
                err(f"{where}: by: の policy '{local}' は policy 名前空間の id_pattern 外です")
        else:
            err(f"{where}: by: の名前空間 '{slug}' は実装元として許容されていません")


# --- 行定義（claim_domains.yaml）の検査語彙 --------------------------------
# 行定義が持ってよいキー。ここに無いキーは typo か「セル値の手書き」の混入なので
# hard で弾く（行定義に手動フラグを持たせないための機械的な防波堤）。
DOMAIN_ALLOWED_KEYS = {
    "id", "title", "face", "note", "signals", "funcs", "reqs", "scenes",
    "gates", "features", "modifiers", "claim_namespaces", "impl_namespaces",
    "stimulus_paths",
}
# 行 id は内容 slug（§7.1 規約4）。ハイフンを1つ以上含む小文字英数。
DOMAIN_ID_RE = re.compile(r"[a-z][a-z0-9]*(-[a-z0-9]+)+")
# 行は面1∪面2 の主張（capability_model.md §0）。面3（検証環境自身）は行にならない。
DOMAIN_FACE_VALUES = {"1", "2"}


def check_claim_domains(catalogs: dict, namespaces: dict, err) -> None:
    """行定義（claim_domains.yaml）の hard 検査。

    行定義は所属（membership）だけを持ち、セル値は生成器が計算する。
    ここでは (1) 未知キー＝手動フラグ混入の防止、(2) 参照 ID が各台帳に
    実在すること、(3) 行 id が内容 slug であること、を強制する。
    参照切れを許すと行列が黙って痩せる（分母から落ちる）ため hard。
    """
    signal_ids = {s.get("id") for s in catalogs["signal"]}
    gate_ids = {g.get("id") for g in catalogs["gate"]}
    req_ids = {r.get("id") for r in catalogs["req-vd-ad"]}
    scene_ids = {s.get("id") for s in catalogs["scene"]}
    func_ids = {f.get("id") for f in catalogs["vd-func"]}
    modifier_axes = {m.get("axis") for m in catalogs["scene-modifier"]}
    feature_ns = namespaces.get("feature", {})

    seen_ids = set()
    for i, d in enumerate(catalogs["domain"]):
        where = f"claim_domains.yaml domains[{i}] ({d.get('id', '?')})"
        did = d.get("id")
        if not did:
            err(f"{where}: missing id")
        elif not DOMAIN_ID_RE.fullmatch(did):
            err(f"{where}: 行 id '{did}' は内容 slug ではありません"
                "（§7.1 規約4: 小文字英数＋ハイフン1つ以上）")
        elif did in seen_ids:
            err(f"{where}: duplicate domain id '{did}'")
        seen_ids.add(did)
        if not str(d.get("title") or "").strip():
            err(f"{where}: title が空です")
        if str(d.get("face")) not in DOMAIN_FACE_VALUES:
            err(f"{where}: face は 1|2 が必須です（行は面1∪面2 の主張。§0）")
        for key in d:
            if key not in DOMAIN_ALLOWED_KEYS:
                err(f"{where}: 未知のキー '{key}'（行定義は所属のみ・セル値は"
                    "生成器が計算する。許容: "
                    f"{', '.join(sorted(DOMAIN_ALLOWED_KEYS))}）")

        def members(key, universe, label):
            val = d.get(key)
            if val is None:
                return
            if val == "all":
                return
            if not isinstance(val, list):
                err(f"{where}: {key} はリストか 'all' が必要です")
                return
            for ref in val:
                if ref not in universe:
                    err(f"{where}: {key} の '{ref}' が{label}に存在しません")

        members("signals", signal_ids, " signal_catalog.yaml")
        members("gates", gate_ids, " gate_catalog.yaml")
        members("reqs", req_ids, " requirements_vd_ad.yaml")
        members("scenes", scene_ids, " scene_catalog_vd_ad.yaml")
        members("funcs", func_ids, " function_catalog_vd_ad.yaml")
        members("modifiers", modifier_axes, " scene_catalog の modifiers.axis")
        for key in ("claim_namespaces", "impl_namespaces"):
            for ref in d.get(key) or []:
                if ref not in namespaces:
                    err(f"{where}: {key} の '{ref}' は未登録 namespace です")
        for p in d.get("stimulus_paths") or []:
            if not (REPO_ROOT / p).exists():
                err(f"{where}: stimulus_paths の '{p}' が存在しません")
        rx = None
        try:
            rx = re.compile(feature_ns.get("id_pattern", ""))
        except re.error:
            rx = None
        for ref in d.get("features") or []:
            if rx and not rx.fullmatch(str(ref)):
                err(f"{where}: features の '{ref}' が feature の id_pattern に"
                    "一致しません")


def check_asset_naming(err) -> None:
    """規約2（§7.1）: 恒久資産のファイル名に工程序数を焼き込まない（hard）。

    工程（一時的）と成果物（恒久的）は寿命が違う。`phase3_*` は工程が終わっても
    走り続ける資産に工程名を残し、名前が古い主張を保存し続けた（規約3 と同根）。
    導入時点の実測ヒットは repo 全体で 0件＝新設のみを弾く検査。
    """
    for pattern in PERMANENT_ASSET_GLOBS:
        for path in REPO_ROOT.glob(pattern):
            if not path.is_file():
                continue
            if PROCESS_ORDINAL_IN_FILENAME.search(path.name):
                rel = path.relative_to(REPO_ROOT).as_posix()
                err(f"{rel}: 恒久資産のファイル名に工程序数が入っています"
                    "（capability_model.md §7.1 規約2）。内容・役割で命名し、"
                    "由来はファイル内メタデータ（origin: ...）として持ってください。")


def _inlined_helpers(module_src: str, branch_src: str) -> str:
    """分岐が呼ぶモジュール内ヘルパの本体を1段だけ連結して返す。

    主判定が `_sustained_stop()` のようなヘルパに入っている分岐を
    「OSI だけで判定している」と誤読しないための間接参照の解決
    （2026-07-21 実測: stopped_at_signal を OSI 単独と誤分類していた）。
    """
    out = []
    for name in set(HELPER_CALL_RE.findall(branch_src)):
        m = re.search(rf"^def {re.escape(name)}\(.*?(?=^def |\Z)",
                      module_src, re.M | re.S)
        if m:
            out.append(m.group(0))
    return "\n".join(out)


def spine_report(catalogs: dict, namespaces: dict, edges: list) -> dict:
    """派生レポート（§6）＋ coupling-audit（§0.5）＝ 恒久の「未検証台帳」。

    **手動フラグは持たない。**「台帳 − 辺」の集合差として毎回計算する
    （手で立てたフラグは必ず腐る、が本プログラムの一貫した方針）。
    出力は「主張 × 欠けた縦層」のリスト。

    向きの注意（2026-07-21）: coupling-audit を graph.yaml の辺だけで実装すると
    **常に 0件＝clean と報告する**。面3→面2 の直結は「引かなかった辺」として
    graph.yaml 末尾のコメントに逃がされており、辺として存在しないからである
    （§5.1）。実体は「matcher が signal 台帳の外を観測していること」なので、
    ここでは *辺の不在* と *exposure が正規IFと交わらないこと* から数える。
    """
    def face_of(ref: str) -> str | None:
        slug = ref.split(":", 1)[0] if isinstance(ref, str) and ":" in ref else None
        ns = namespaces.get(slug) if slug else None
        return str(ns.get("face")) if ns and ns.get("face") is not None else None

    def locals_of(slug: str) -> set:
        out = set()
        for e in edges:
            for ref in (e.get("from"), e.get("to")):
                if isinstance(ref, str) and ref.startswith(f"{slug}:"):
                    out.add(ref.split(":", 1)[1])
        return out

    signals = {s["id"]: s for s in catalogs["signal"] if s.get("id")}
    gates = {g["id"] for g in catalogs["gate"] if g.get("id")}
    reqs = [r["id"] for r in catalogs["req-vd-ad"] if r.get("id")]
    scenes = {s["id"]: s for s in catalogs["scene"] if s.get("id")}
    # matcher 台帳は真実源（vd_metrics.py の eval_must）から取る。グラフ出現で
    # 代用すると未結線 matcher が母数から落ちて 0件=clean と誤報する（上記コメント）。
    matchers = set()
    transport = {}   # matcher -> "osi" | "telemetry" | "mixed"
    if VD_METRICS_PY.is_file():
        body = VD_METRICS_PY.read_text(encoding="utf-8", errors="replace")
        hits = list(MATCHER_KIND_RE.finditer(body))
        for n, mo in enumerate(hits):
            kinds = re.findall(r"""["']([a-z_]+)["']""", mo.group(1))
            matchers.update(kinds)
            # 分岐本体＝次のディスパッチまで。そこに scene 参照があるか。
            end = hits[n + 1].start() if n + 1 < len(hits) else len(body)
            branch = body[mo.end():end] + _inlined_helpers(body, body[mo.end():end])
            reads_osi = SCENE_REF_RE.search(branch) is not None
            reads_face2 = TELEMETRY_REF_RE.search(branch) is not None
            for k in kinds:
                if reads_osi and reads_face2:
                    transport[k] = "mixed"
                elif reads_osi:
                    transport[k] = "osi"
                else:
                    transport[k] = "telemetry"
    # グラフにしか現れない matcher（実装が消えた等）も台帳に含めて可視化する。
    matchers |= locals_of("matcher")

    observes = {}       # matcher -> {signal local id}
    sustained = set()   # sustained-by の from（matcher / req）
    verified_reqs = {}  # req -> {matcher}
    stimulated = set()  # stimulated-by の from（req / scene）
    for e in edges:
        f, t, ty = e.get("from"), e.get("to"), e.get("type")
        if not isinstance(f, str) or not isinstance(t, str):
            continue
        if ty == "observes" and f.startswith("matcher:"):
            observes.setdefault(f.split(":", 1)[1], set()).add(
                t.split(":", 1)[1] if t.startswith("signal:") else t)
        elif ty == "sustained-by":
            sustained.add(f)
        elif ty == "verifies":
            verified_reqs.setdefault(t, set()).add(f)
        elif ty == "stimulated-by":
            stimulated.add(f)

    F = {k: [] for k in (
        "obs_unemitted", "obs_unwired", "obs_verdict_on_unemitted",
        "emit_state_mismatch", "sustain_missing", "stimulus_missing",
        "coupling_unobserved", "coupling_non_canonical", "coupling_face2_transport",
        "coupling_mixed_anchor", "coupling_direct_edge",
    )}

    observed_signals = {s for v in observes.values() for s in v}

    # -- ④観測欠: (a)未emit と (b)未配線 は打ち手が全く違う（混同しないこと）------
    for sid, s in signals.items():
        state, exposure = s.get("state"), set(s.get("exposure") or [])
        wired = sid in observed_signals
        if state == "(a)":
            F["obs_unemitted"].append(
                f"signal:{sid} — 未emit（真の観測不能）。打ち手＝emit の新設"
                f" [exposure: {','.join(sorted(exposure)) or '-'}]")
            if wired:
                F["obs_verdict_on_unemitted"].append(
                    f"signal:{sid} — state=(a) なのに observes する matcher がある"
                    f"（{', '.join(sorted(m for m, v in observes.items() if sid in v))}）"
                    "＝台帳と辺の矛盾。どちらかが古い。")
        elif state in ("(b)", "(b')") and not wired:
            kind = "誤配線" if state == "(b')" else "emit済み未配線"
            F["obs_unwired"].append(
                f"signal:{sid} — {kind}。打ち手＝配線（emit ではない）"
                f" [exposure: {','.join(sorted(exposure)) or '-'}]")
        # 台帳内の自己整合: (a)＝未emit なら emit 欄は空のはず
        emit = s.get("emit")
        if state == "(a)" and emit:
            F["emit_state_mismatch"].append(
                f"signal:{sid} — state=(a)（未emit）なのに emit 欄に実装がある: {emit}")
        elif state in ("(b)", "(b')", "●") and not emit:
            F["emit_state_mismatch"].append(
                f"signal:{sid} — state={state}（emit済み前提）なのに emit 欄が空")

    # -- ⑥常設欠: matcher があるのに sustained-by 先の gate が無い ---------------
    for req, ms in sorted(verified_reqs.items()):
        if req in sustained:
            continue
        covering = sorted(m for m in ms if m in sustained)
        if not covering:
            F["sustain_missing"].append(
                f"{req} — 判定 matcher {sorted(ms)} はあるが常設ゲートに乗っていない"
                "（一度検証したきり）")

    # -- ②刺激欠: stimulated-by 資産が無い主張 -----------------------------------
    for req in reqs:
        if f"req-vd-ad:{req}" not in stimulated:
            F["stimulus_missing"].append(f"req-vd-ad:{req} — 発火させる資産が未結線")
    for sid, sc in sorted(scenes.items()):
        if sc.get("coverage") != "covered" and f"scene:{sid}" not in stimulated:
            F["stimulus_missing"].append(
                f"scene:{sid} — coverage={sc.get('coverage')} かつ刺激資産が未結線")

    # -- coupling-audit（§0.5）: 面3の verdict が正規IF(signal)を経由しているか ----
    for m in sorted(matchers):
        obs = observes.get(m)
        if not obs:
            F["coupling_unobserved"].append(
                f"matcher:{m} — observes 辺が無い＝判定源が signal 台帳の外"
                "（面3→面2 直結の疑い。§5.1 の「引けなかった辺」）")
            continue
        for sid in sorted(obs):
            s = signals.get(sid)
            if s is None:
                continue
            exposure = set(s.get("exposure") or [])
            if exposure & CANONICAL_EXPOSURE:
                continue
            # exposure=debug は「観測できるが verdict-trust 対象外」(§2.2)。
            # verdict 経路に現れること自体が debt（実装側の規約は gt.dbg.* 接頭辞）。
            why = ("verdict-trust 対象外の debug 量"
                   if "debug" in exposure else "面2投影/内部量")
            F["coupling_non_canonical"].append(
                f"matcher:{m} -> signal:{sid} — {why} を判定に使用 "
                f"[exposure: {','.join(sorted(exposure))}]。正規IF(osi/hvd)非経由。")

    # 実装が実際にどの面から読んでいるか（§2.3a の実数を機械が数える）。
    # KG の observes 辺は「どの signal か」しか持たず「どの経路で読むか」を持たない
    # ため、辺だけでは面2依存を数えられない（ego_speed は osi にも frame にも在る）。
    # 純テレメトリ（OSI を一切読まない）と混在（OSI 幾何＋面2 ego アンカー）は
    # 置き換えコストが違うので分けて出す。潰すと「16/16 が負債」になり指標が死ぬ。
    for m in sorted(matchers):
        mode = transport.get(m)
        if mode == "telemetry":
            F["coupling_face2_transport"].append(
                f"matcher:{m} — frame['scene'](面1 OSI)を一切読まない"
                f"＝面3→面2 直結（§0.3 の結合負債の実体） [/{len(matchers)} matcher]")
        elif mode == "mixed":
            F["coupling_mixed_anchor"].append(
                f"matcher:{m} — OSI(scene)を読むが面2テレメトリ(ego/driver 等)も"
                "アンカーに使う＝SUT非依存ではない [/"
                f"{len(matchers)} matcher]")

    for i, e in enumerate(edges):
        f, t = e.get("from"), e.get("to")
        if face_of(f) == "3" and face_of(t) == "2":
            F["coupling_direct_edge"].append(
                f"graph.yaml edge[{i}] {f} -[{e.get('type')}]-> {t} — "
                "面3→面2 直結（正規IF signal を経由していない）")

    return F


SPINE_SECTIONS = [
    ("obs_unemitted", "④観測欠 (a) 未emit＝真の観測不能（打ち手: emit 新設）"),
    ("obs_unwired", "④観測欠 (b) emit済み未配線（打ち手: 配線）"),
    ("obs_verdict_on_unemitted", "④整合破れ: 未emit signal を観測する辺がある"),
    ("emit_state_mismatch", "④台帳の自己矛盾: state と emit 欄が食い違う"),
    ("sustain_missing", "⑥常設欠: matcher はあるが常設ゲートに乗っていない"),
    ("stimulus_missing", "②刺激欠: 発火させる資産が未結線"),
    ("coupling_unobserved", "coupling: 判定源が signal 台帳の外"),
    ("coupling_non_canonical", "coupling: 正規IF(osi/hvd)非経由の量で判定"),
    ("coupling_face2_transport", "coupling: 判定が面1 OSI を一切読まない（§2.3a の実数）"),
    ("coupling_mixed_anchor", "coupling: OSI を読むが面2アンカー併用（SUT非依存でない）"),
    ("coupling_direct_edge", "coupling: 面3→面2 の直結辺"),
]


def spine_report_cmd(out_path: str | None) -> int:
    """--spine-report: 「主張 × 欠けた縦層」の恒久台帳を出力する（報告のみ）。"""
    registry = load_yaml(NAMESPACES_YAML)
    graph = load_yaml(GRAPH_YAML)
    namespaces = {ns["slug"]: ns for ns in registry.get("namespaces", []) if ns.get("slug")}
    findings = spine_report(load_catalogs(), namespaces, graph.get("edges", []))

    lines = ["# 未検証台帳（縦串の切れた列 ＋ 結合負債）", "",
             "> 生成物。`check_knowledge_graph.py --spine-report` で再生成する。",
             "> 手動フラグは持たない＝「台帳 − 辺」の集合差として毎回計算している。", ""]
    for key, title in SPINE_SECTIONS:
        items = findings[key]
        lines.append(f"## {title} — {len(items)} 件")
        lines.extend(f"- {x}" for x in items) if items else lines.append("- （なし）")
        lines.append("")
    text = "\n".join(lines)
    if out_path:
        Path(out_path).write_text(text, encoding="utf-8")
        print(f"spine report -> {out_path} "
              f"({sum(len(v) for v in findings.values())} findings)")
    else:
        print(text)
    return 0


# --- 能力モデル行列（capability_model.md §2 の生成ビュー）---------------------
# セル記号は §2 の凡例を踏襲: ● 充足 / ◐ 部分 / ○ 希薄 / ✕ 欠 / ？ 計算不能。
# ？＝行定義に該当台帳の所属が無い（結線の欠落そのものの検出）。手書き行列に
# あった †(deferred) は「要求化しない判断」＝台帳から計算できない判断であり、
# 生成ビューでは？になる（判断の一次記録は claim_domains.yaml の note と
# capability_model.md §8-3 が持つ）。
SYM_NA = "？"

# ④観測: state がこの集合なら「観測面として充足」（(-) は別面に正当な経路あり）。
OBS_SATISFIED = {"●", "(-)", "(–)"}


def _resolve(val, catalog_rows):
    """行定義の 'all' / id リストを台帳の行リストへ解決する。"""
    if val == "all":
        return list(catalog_rows)
    if not val:
        return []
    by_id = {r.get("id"): r for r in catalog_rows}
    return [by_id[x] for x in val if x in by_id]


def spine_matrix(catalogs: dict, namespaces: dict, edges: list) -> list:
    """行 × スパイン①-⑥ のセルを台帳と辺から計算する（手動フラグなし）。

    真実源: claim_domains.yaml（行の所属のみ）＋ 各台帳（signal/gate/req/scene/
    vd-func カタログ）＋ graph.yaml の辺（observes/verifies/sustained-by/
    stimulated-by/realizes）。返り値は行ごとの {id,title,cells,evidence}。
    """
    # -- 辺の索引 --------------------------------------------------------------
    observes = {}        # matcher local -> {signal local}
    verifies = {}        # req ref ("req-vd-ad:X") -> {matcher local}
    sustained = {}       # from ref -> {gate local}
    stimulated = {}      # from ref -> {asset ref}
    realizers = {}       # req ref -> [func rows]（realizes: vd-func -> req）
    func_rows = {f.get("id"): f for f in catalogs["vd-func"]}
    for e in edges:
        f, t, ty = e.get("from"), e.get("to"), e.get("type")
        if not isinstance(f, str) or not isinstance(t, str):
            continue
        if ty == "observes" and f.startswith("matcher:") and t.startswith("signal:"):
            observes.setdefault(f.split(":", 1)[1], set()).add(t.split(":", 1)[1])
        elif ty == "verifies" and f.startswith("matcher:"):
            verifies.setdefault(t, set()).add(f.split(":", 1)[1])
        elif ty == "sustained-by" and t.startswith("gate:"):
            sustained.setdefault(f, set()).add(t.split(":", 1)[1])
        elif ty == "stimulated-by":
            stimulated.setdefault(f, set()).add(t)
        elif ty == "realizes" and f.startswith("vd-func:"):
            fr = func_rows.get(f.split(":", 1)[1])
            if fr:
                realizers.setdefault(t, []).append(fr)

    signal_by_id = {s.get("id"): s for s in catalogs["signal"]}
    gate_by_id = {g.get("id"): g for g in catalogs["gate"]}
    modifier_by_axis = {m.get("axis"): m for m in catalogs["scene-modifier"]}

    def ratio_sym(r):
        return "●" if r >= 0.999 else "◐" if r >= 0.4 else "○" if r > 0 else "✕"

    out = []
    for d in catalogs["domain"]:
        sig_ids = set(d.get("signals") or [])
        reqs = _resolve(d.get("reqs"), catalogs["req-vd-ad"])
        funcs = _resolve(d.get("funcs"), catalogs["vd-func"])
        scenes = _resolve(d.get("scenes"), catalogs["scene"])
        features = [str(x) for x in d.get("features") or []]
        member_gates = list(d.get("gates") or [])
        claim_ns = list(d.get("claim_namespaces") or [])
        cells, ev = {}, []

        # ---- ① 主張・受入: 要求台帳（定量受入の有無）/ feature / 独自台帳 ----
        if reqs:
            quant = [r for r in reqs if r.get("acceptance") or r.get("response")]
            cells[1] = ("●" if len(quant) == len(reqs) else "◐",
                        f"req {len(reqs)}（定量受入 {len(quant)}）")
            ev.append(f"①: req-vd-ad {len(reqs)}件、うち定量受入(acceptance/response)"
                      f"つき {len(quant)}件")
        elif claim_ns:
            counts = ", ".join(
                f"{ns}:{namespaces.get(ns, {}).get('count', '?')}" for ns in claim_ns)
            cells[1] = ("●", f"台帳 {counts}")
            ev.append(f"①: 独自主張台帳 {counts}")
        elif features:
            cells[1] = ("◐", f"feature {','.join(features)}のみ（定量受入なし）")
            ev.append(f"①: feature {features} のみ＝要求化されていない")
        else:
            cells[1] = (SYM_NA, "主張台帳なし")
            ev.append("①: 主張台帳（req/feature/独自namespace）が行定義に無い")

        # ---- ② 刺激: req の stimulated-by / scene coverage / modifiers / 資産 --
        parts = []
        if reqs:
            stim = [r for r in reqs
                    if stimulated.get(f"req-vd-ad:{r.get('id')}")]
            parts.append((len(stim) / len(reqs),
                          f"req刺激 {len(stim)}/{len(reqs)}"))
        if scenes:
            cov = sum(1 for s in scenes if s.get("coverage") == "covered")
            par = sum(1 for s in scenes if s.get("coverage") == "partial")
            parts.append(((cov + 0.5 * par) / len(scenes),
                          f"scene covered {cov}/partial {par}/計 {len(scenes)}"))
        for ax in d.get("modifiers") or []:
            m = modifier_by_axis.get(ax) or {}
            c = m.get("coverage")
            parts.append(({"covered": 1.0, "partial": 0.5}.get(c, 0.0),
                          f"modifier {ax}={c}"))
        for p in d.get("stimulus_paths") or []:
            parts.append((1.0 if (REPO_ROOT / p).exists() else 0.0, f"資産 {p}"))
        if parts:
            r = sum(x for x, _ in parts) / len(parts)
            cells[2] = (ratio_sym(r), "、".join(t for _, t in parts))
            ev.append(f"②: {'、'.join(t for _, t in parts)}")
        else:
            cells[2] = (SYM_NA, "刺激台帳なし")
            ev.append("②: 刺激台帳（req/scene/modifier/資産パス）が行定義に無い")

        # ---- ③ 実装: req を built 実装が支えるか（realizes 辺）/ funcs 内訳 ----
        if reqs:
            def best(req):
                sts = {f.get("status") for f in
                       realizers.get(f"req-vd-ad:{req.get('id')}", [])}
                return ("built" if "built" in sts
                        else "partial" if "partial" in sts
                        else "none")
            built_r = [r for r in reqs if best(r) == "built"]
            part_r = [r for r in reqs if best(r) == "partial"]
            sym = ("●" if len(built_r) == len(reqs)
                   else "◐" if built_r or part_r else "✕")
            detail = f"req実装 built {len(built_r)}/partial {len(part_r)}/{len(reqs)}"
            if funcs:
                c = {k: sum(1 for f in funcs if f.get("status") == k)
                     for k in ("built", "partial", "none", "oos")}
                detail += (f"｜func built {c['built']}/partial {c['partial']}"
                           f"/none {c['none']}/oos {c['oos']}")
            cells[3] = (sym, detail)
            ev.append(f"③: {detail}"
                      + (f"（未実装req: {[r.get('id') for r in reqs if best(r) == 'none']}）"
                         if len(built_r) + len(part_r) < len(reqs) else ""))
        elif funcs:
            c = {k: sum(1 for f in funcs if f.get("status") == k)
                 for k in ("built", "partial", "none", "oos")}
            sym = "●" if c["built"] else "◐" if c["partial"] else "✕"
            cells[3] = (sym, f"func built {c['built']}/partial {c['partial']}"
                             f"/none {c['none']}/oos {c['oos']}")
        elif d.get("impl_namespaces"):
            counts = ", ".join(
                f"{ns}:{namespaces.get(ns, {}).get('count', '?')}"
                for ns in d["impl_namespaces"])
            cells[3] = ("●", f"台帳 {counts}")
            ev.append(f"③: 独自実装台帳 {counts}（census で機械検証済み）")
        else:
            cells[3] = (SYM_NA, "実装台帳なし")
            ev.append("③: 実装台帳（vd-func / req realizes）が行定義に無い＝"
                      "vd-func 以外のドメインに機能カタログが存在しない")

        # ---- ④ 観測: member signal の state 内訳（emit 基準・§2.3）------------
        if sig_ids:
            states = [signal_by_id[s].get("state") for s in sorted(sig_ids)
                      if s in signal_by_id]
            n = {"●": 0, "(-)": 0, "(a)": 0, "(b)": 0, "(b')": 0}
            for st in states:
                key = "(-)" if st in ("(-)", "(–)") else st
                if key in n:
                    n[key] += 1
            sat = n["●"] + n["(-)"]
            if sat == len(states):
                sym = "●"
            elif sat:
                sym = "◐"
            elif n["(b)"] or n["(b')"]:
                sym = "○"
            else:
                sym = "✕"
            detail = " ".join(f"{k}{v}" for k, v in n.items() if v)
            cells[4] = (sym, detail)
            ev.append(f"④: {len(states)} signal — {detail}")
        else:
            cells[4] = (SYM_NA, "signal 所属なし")
            ev.append("④: 行定義に signal が無い")

        # ---- ⑤ 判定: member signal を observes / member req を verifies する
        #      matcher。matcher が無くても gate 内オラクル（ODR golden・統合テスト
        #      アサーション）があれば ◐（§1.1: golden は回帰であって真偽ではない）。
        matchers_d = {m for m, sigs in observes.items() if sigs & sig_ids}
        req_refs = {f"req-vd-ad:{r.get('id')}" for r in reqs}
        for ref in req_refs:
            matchers_d |= verifies.get(ref, set())
        gates_d = set(member_gates)
        for m in matchers_d:
            gates_d |= sustained.get(f"matcher:{m}", set())
        for ref in req_refs:
            gates_d |= sustained.get(ref, set())
        for fe in features:
            gates_d |= sustained.get(f"feature:{fe}", set())
        if matchers_d:
            verified = [r for r in reqs
                        if verifies.get(f"req-vd-ad:{r.get('id')}")]
            sym = ("●" if reqs and len(verified) == len(reqs) else "◐")
            detail = f"matcher {len(matchers_d)}"
            if reqs:
                detail += f"（req検証 {len(verified)}/{len(reqs)}）"
            cells[5] = (sym, detail)
            ev.append(f"⑤: matcher {sorted(matchers_d)}"
                      + (f"、verifies 未結線 req: "
                         f"{[r.get('id') for r in reqs if not verifies.get('req-vd-ad:' + str(r.get('id')))]}"
                         if reqs and len(verified) < len(reqs) else ""))
        elif gates_d:
            cells[5] = ("◐", "gate内オラクル（matcher 台帳外）")
            ev.append(f"⑤: matcher 0。判定は gate 内オラクル {sorted(gates_d)} に内包"
                      "（golden/テストアサーション＝回帰オラクル。§1.1）")
        else:
            cells[5] = ("✕", "matcher 0・gate内オラクルなし")
            ev.append("⑤: この行の signal/req に達する matcher が 1件も無い")

        # ---- ⑥ 常設: 到達 gate の blocking で採点 ------------------------------
        if gates_d:
            rows = [gate_by_id[g] for g in sorted(gates_d) if g in gate_by_id]
            blocking = [g for g in rows if g.get("blocking") is True]
            sym = "●" if blocking else "◐"
            names = ", ".join(
                f"{g.get('id')}({'hard' if g.get('blocking') else 'warn'})"
                for g in rows)
            cells[6] = (sym, names)
            ev.append(f"⑥: {names}")
        else:
            cells[6] = ("✕", "常設ゲート到達なし")
            ev.append("⑥: sustained-by でも gates 所属でも常設ゲートに到達しない")

        out.append({"id": d.get("id"), "title": d.get("title"),
                    "cells": cells, "evidence": ev})
    return out


def spine_matrix_cmd(out_path: str | None) -> int:
    """--spine-matrix: 能力モデル行列（§2 の生成ビュー）を出力する。"""
    registry = load_yaml(NAMESPACES_YAML)
    graph = load_yaml(GRAPH_YAML)
    namespaces = {ns["slug"]: ns for ns in registry.get("namespaces", [])
                  if ns.get("slug")}
    rows = spine_matrix(load_catalogs(), namespaces, graph.get("edges", []))

    lines = [
        "# repo横断 能力モデル行列（生成ビュー）", "",
        "> 生成物。`check_knowledge_graph.py --spine-matrix` で再生成する。",
        "> 行定義: claim_domains.yaml（所属のみ）。セル値は台帳"
        "（signal/gate/req/scene/vd-func）と graph.yaml の辺から毎回計算する。",
        "> 凡例: ● 充足 / ◐ 部分 / ○ 希薄 / ✕ 欠 / **？ 計算不能＝台帳未結線**"
        "（？こそ結線の欠落の検出。手書き時代の †(deferred) 判断は "
        "claim_domains.yaml の note と capability_model.md §8-3 に残る）。", "",
        "| 主張ドメイン | ①主張 | ②刺激 | ③実装 | ④観測 | ⑤判定 | ⑥常設 |",
        "| :-- | :-- | :-- | :-- | :-- | :-- | :-- |",
    ]
    for r in rows:
        cells = " | ".join(
            f"{r['cells'][i][0]} <sub>{r['cells'][i][1]}</sub>"
            for i in range(1, 7))
        lines.append(f"| **{r['title']}** | {cells} |")
    lines.append("")
    lines.append("## 行ごとの計算根拠")
    for r in rows:
        lines.append("")
        lines.append(f"### {r['title']}（`{r['id']}`）")
        lines.extend(f"- {x}" for x in r["evidence"])
    lines.append("")
    text = "\n".join(lines)
    if out_path:
        Path(out_path).write_text(text, encoding="utf-8")
        print(f"spine matrix -> {out_path} ({len(rows)} domains)")
    else:
        print(text)
    return 0


def brief(ref: str) -> int:
    """--brief: 作業キックオフブリーフを真実源からその場で生成する。

    セッションプロンプトに手書きされてきた「固有文脈」のうち **導出可能な部分**
    （ノード現況・縦串の結線状態・本件該当の未検証項目・関連ファイル・近傍とコミット）
    を組み立てる。手書きの事実転記は必ず腐る（「25 unit sources」「初回実測88件」の轍）
    ＝導出できるものはプロンプトに書かずに本コマンドで拾う、が目的。
    **判断（スコープ・並列所有・受入基準）は生成しない** — それは決定であって検索ではない。
    """
    registry = load_yaml(NAMESPACES_YAML)
    graph = load_yaml(GRAPH_YAML)
    ns_by_slug = {n["slug"]: n for n in registry.get("namespaces", []) if n.get("slug")}
    edges = graph.get("edges", []) or []
    catalogs = load_catalogs()

    # bare-id 解決（--query と同じ規則: 一意なら補完・曖昧なら拒否）
    if ":" not in ref or ref.split(":", 1)[0] not in ns_by_slug:
        cands = []
        for ns in ns_by_slug.values():
            try:
                if re.fullmatch(ns.get("id_pattern", ""), ref or ""):
                    cands.append(ns["slug"])
            except re.error:
                continue
        if len(cands) == 1:
            ref = f"{cands[0]}:{ref}"
            print(f"(resolved bare id to {ref})")
        else:
            print(f"ERROR: '{ref}' は{'どの名前空間にも一致しない' if not cands else f'曖昧 {sorted(cands)}'}"
                  " — 名前空間で修飾すること", file=sys.stderr)
            return 1
    slug, local = ref.split(":", 1)

    def nbrs(node: str, etype: str, direction: str) -> list:
        out = []
        for e in edges:
            if e.get("type") != etype:
                continue
            if direction in ("out", "both") and e.get("from") == node:
                out.append(e.get("to"))
            if direction in ("in", "both") and e.get("to") == node:
                out.append(e.get("from"))
        return [x for x in out if isinstance(x, str)]

    print(f"# キックオフブリーフ: {ref}")
    print("# 真実源からその場で導出（プロンプトへの手書き転記は禁物＝腐る）")
    print()

    # --- 1. ノードのカタログ行 ------------------------------------------------
    print("## ノード（カタログ行）")
    row = next((r for r in catalogs.get(slug, []) if r.get("id") == local), None)
    if row:
        for k, v in row.items():
            if k != "note":
                print(f"- {k}: {v}")
        if row.get("note"):
            print(f"- note: {str(row['note']).strip()}")
    else:
        src = ns_by_slug[slug].get("source_of_truth", "?")
        print(f"- （{slug} のノード台帳に行なし — source_of_truth: {src} を直接参照）")
    print()

    # --- 2. 縦串の現況（realizes で結ばれた req を軸に ②⑤④⑥）---------------
    reqs: set = set()
    if slug == "req-vd-ad":
        reqs.add(ref)
    else:
        for r in nbrs(ref, "realizes", "out") + nbrs(ref, "verifies", "out"):
            if r.startswith("req-vd-ad:"):
                reqs.add(r)
    sig_by_id = {s.get("id"): s for s in catalogs.get("signal", [])}
    gate_by_id = {g.get("id"): g for g in catalogs.get("gate", [])}

    print("## 縦串の現況（①req → ②刺激 / ⑤matcher → ④signal / ⑥gate）")
    if not reqs and slug not in ("signal", "gate", "matcher"):
        print("- 結線済みの req-vd-ad なし ＝ **①主張が未接続**（realizes 辺から始める）")
    for rq in sorted(reqs):
        print(f"- {rq}")
        stim = nbrs(rq, "stimulated-by", "out")
        print(f"  - ②刺激: {', '.join(stim) if stim else '**未結線（②刺激欠）**'}")
        matchers = sorted(set(nbrs(rq, "verifies", "in")))
        if not matchers:
            print("  - ⑤判定: **matcher なし（⑤欠）**")
        for m in matchers:
            obs, gts = nbrs(m, "observes", "out"), nbrs(m, "sustained-by", "out")
            obs_s = []
            for o in obs:
                s = sig_by_id.get(o.split(":", 1)[1]) if o.startswith("signal:") else None
                obs_s.append(f"{o}[{s.get('state')} {'/'.join(s.get('exposure', []))}]" if s else o)
            gt_s = []
            for g in gts:
                gr = gate_by_id.get(g.split(":", 1)[1]) if g.startswith("gate:") else None
                gt_s.append(f"{g}[{'blocking' if gr and gr.get('blocking') else 'non-blocking'}]" if gr else g)
            print(f"  - ⑤ {m}")
            print(f"    - ④観測: {', '.join(obs_s) if obs_s else '**observes 未結線**'}")
            print(f"    - ⑥常設: {', '.join(gt_s) if gt_s else '**sustained-by 未結線**'}")
    if slug == "signal":
        readers = sorted(set(nbrs(ref, "observes", "in")))
        print(f"- この signal を observes する matcher: "
              f"{', '.join(readers) if readers else '**なし＝(b) 候補**'}")
    if slug == "gate":
        held = sorted(set(nbrs(ref, "sustained-by", "in")))
        print(f"- この gate へ sustained-by: {', '.join(held) if held else '（なし）'}")
    print()

    # --- 3. 未検証台帳のうち本件に該当する行 ---------------------------------
    findings = spine_report(catalogs, ns_by_slug, edges)
    tokens = {local} | {r.split(":", 1)[1] for r in reqs}
    print("## 未検証台帳のうち本件該当（--spine-report の部分集合）")
    hit = False
    for key, _title in SPINE_SECTIONS:
        for item in findings.get(key, []):
            if any(t in str(item) for t in tokens):
                print(f"- [{key}] {item}")
                hit = True
    if not hit:
        print("- （該当なし）")
    print()

    # --- 4. 関連ファイル（path_map の逆引き）--------------------------------
    pm = load_yaml(PATH_MAP_YAML) or {}
    closure = {ref} | reqs
    globs = [m.get("glob") for m in pm.get("mappings", [])
             if any(str(i) in closure for i in (m.get("ids") or []))]
    print("## 関連ファイル（path_map 逆引き）")
    for g in globs or ["（マッピングなし — 実装後に path_map.yaml への追記を検討）"]:
        print(f"- {g}")
    print()

    # --- 5. 近傍とコミット（既存 --query に委譲）-----------------------------
    print("## 近傍（--query depth 2 + commits）")
    query(ref, 2, True, False)
    print()
    print("## 指し先（判断系＝ここには生成されない）")
    print("- 作法: /kg スキル「新機能・新資産を追加するときのチェックリスト」")
    print("- 命名・凍結例外: capability_model.md §7.1 ／ 検証スパイン定義: 同 §1")
    print("- 教訓・罠: セッション自動読込の MEMORY.md から関連行を辿って該当 memory を読む")
    print("- スコープ・並列セッションのファイル所有・受入基準は**人が決めてプロンプトに書く**")
    return 0


def check() -> int:
    errors = []

    def err(msg: str):
        errors.append(msg)

    for path in (NAMESPACES_YAML, GRAPH_YAML, VOCAB_YAML):
        if not path.is_file():
            print(f"ERROR: missing {path}", file=sys.stderr)
            return 2

    registry = load_yaml(NAMESPACES_YAML)
    graph = load_yaml(GRAPH_YAML)
    vocab = load_yaml(VOCAB_YAML)

    # -- 1. namespace registry ------------------------------------------------
    namespaces = {}
    for ns in registry.get("namespaces", []):
        slug = ns.get("slug")
        if not slug:
            err("namespaces.yaml: entry without slug")
            continue
        if slug in namespaces:
            err(f"namespaces.yaml: duplicate slug '{slug}'")
            continue
        try:
            ns["_re"] = re.compile(ns["id_pattern"])
        except (re.error, KeyError) as e:
            err(f"namespace '{slug}': bad id_pattern ({e})")
            ns["_re"] = None
        # 規約4（capability_model.md §7.1）: 新しい ID 体系に不透明な採番を使わない。
        # 既存19体系は参照面が大きく一斉移行しないため OPAQUE_LEGACY で凍結扱いにし、
        # **新設のみ**を弾く。序数は共有資源を消費し、中身を語らず、挿入・並べ替えで壊れる。
        # 散文だけの規約は守られない（2026-07-20 の実例: フックが規約と反転していた）ため
        # ここで機械化する。
        if slug not in OPAQUE_LEGACY_NAMESPACES:
            pat = ns.get("id_pattern", "")
            if ORDINAL_ID_PATTERN.fullmatch(pat.strip()):
                err(
                    f"namespace '{slug}': id_pattern '{pat}' は不透明な採番です。"
                    "新しい ID 体系は内容由来の slug にしてください"
                    "（capability_model.md §7.1 規約4）。既存体系の凍結扱いが必要なら "
                    "OPAQUE_LEGACY_NAMESPACES に追加してください。"
                )
        status = ns.get("status", "active")
        if status not in ("active", "reserved", "implicit"):
            err(f"namespace '{slug}': unknown status '{status}'")
        if status == "active":
            src = ns.get("source_of_truth")
            if not src:
                err(f"namespace '{slug}': active but no source_of_truth")
            elif not (REPO_ROOT / src).exists():
                err(f"namespace '{slug}': source_of_truth not found: {src}")
        namespaces[slug] = ns

    curated_types = set(registry.get("edge_types", {}).get("curated", []))
    generated_types = set(registry.get("edge_types", {}).get("generated", []))
    if not curated_types:
        err("namespaces.yaml: edge_types.curated is empty")

    # -- vocabulary index -----------------------------------------------------
    concept_ids = set()
    for c in vocab.get("concepts", []):
        cid = c.get("id")
        if not cid:
            err("concept_vocabulary.yaml: concept without id")
            continue
        if cid in concept_ids:
            err(f"concept_vocabulary.yaml: duplicate concept '{cid}'")
        concept_ids.add(cid)

    component_ids = {c.get("id") for c in load_catalogs()["vd-component"]}

    # -- 2..5 edges -----------------------------------------------------------
    def check_ref(ref: str, where: str):
        if not isinstance(ref, str) or ":" not in ref:
            err(f"{where}: reference '{ref}' is not '<namespace>:<local-id>'")
            return
        slug, local = ref.split(":", 1)
        ns = namespaces.get(slug)
        if ns is None:
            err(f"{where}: unregistered namespace '{slug}' in '{ref}'")
            return
        if ns.get("status") == "reserved":
            err(f"{where}: namespace '{slug}' is reserved (no source yet) — "
                f"activate it in namespaces.yaml before referencing")
        if ns.get("_re") and not ns["_re"].fullmatch(local):
            err(f"{where}: local id '{local}' does not match "
                f"id_pattern of namespace '{slug}'")
        if slug == "openx" and local not in concept_ids:
            err(f"{where}: openx concept '{local}' is not in "
                f"concept_vocabulary.yaml (add it there first)")
        # vd-component は内容 slug パターンなので、正規表現だけでは何でも通ってしまう
        # （openx と同型の問題）。台帳の実在を要求しないと catalog が飾りになる。
        if slug == "vd-component" and local not in component_ids:
            err(f"{where}: vd-component '{local}' is not in "
                f"component_catalog_vd.yaml (add it there first)")

    seen = set()
    edges = graph.get("edges", [])
    for i, e in enumerate(edges):
        where = f"graph.yaml edge[{i}]"
        for field in ("from", "to", "type"):
            if field not in e:
                err(f"{where}: missing '{field}'")
        if "from" in e:
            check_ref(e["from"], where)
        if "to" in e:
            check_ref(e["to"], where)
        etype = e.get("type")
        if etype and etype not in curated_types:
            if etype in generated_types:
                err(f"{where}: type '{etype}' is a generated type — "
                    f"extracted from git history, never hand-written here")
            else:
                err(f"{where}: unknown edge type '{etype}'")
        src = e.get("source")
        if src and not (REPO_ROOT / src).exists():
            err(f"{where}: source file not found: {src}")
        key = (e.get("from"), e.get("to"), etype)
        if key in seen:
            err(f"{where}: duplicate edge {key}")
        seen.add(key)

    # -- 5b. path_map: globs valid, ids registered ------------------------------
    if PATH_MAP_YAML.is_file():
        pm = load_yaml(PATH_MAP_YAML)
        for i, m in enumerate(pm.get("mappings", [])):
            where = f"path_map.yaml mapping[{i}]"
            if not m.get("glob"):
                err(f"{where}: missing glob")
            ids = m.get("ids") or []
            if not ids:
                err(f"{where}: empty ids")
            for ref in ids:
                check_ref(ref, where)
        for i, e in enumerate(pm.get("exempt", [])):
            if not e.get("glob"):
                err(f"path_map.yaml exempt[{i}]: missing glob")

    # -- 6. generated view must match its inputs -------------------------------
    if not GRAPH_VIEW_MD.is_file():
        err("graph_view.md is missing — generate it with --render")
    else:
        text = GRAPH_VIEW_MD.read_text(encoding="utf-8")
        m = re.search(r"generated-from: sha256:([0-9a-f]{16})", text)
        if not m:
            err("graph_view.md has no generated-from marker — regenerate with --render")
        elif m.group(1) != view_hash():
            err("graph_view.md is STALE (graph.yaml/namespaces.yaml changed) — "
                "regenerate with --render")

    # -- 7. カタログ値域 ＋ 行定義 ＋ 規約2（いずれも hard・導入時点の実測違反 0件）
    catalogs = load_catalogs()
    check_catalog_values(catalogs, namespaces, err)
    check_claim_domains(catalogs, namespaces, err)
    check_asset_naming(err)

    if errors:
        print(f"knowledge graph check: {len(errors)} violation(s)")
        for msg in errors:
            print(f"  - {msg}")
        return 1
    print(f"knowledge graph check: OK "
          f"({len(namespaces)} namespaces, {len(edges)} curated edges, "
          f"{len(concept_ids)} openx concepts, "
          f"{len(catalogs['signal'])} signals, {len(catalogs['gate'])} gates, "
          f"{len(catalogs['domain'])} claim domains, "
          f"view fresh)")

    # -- 8. 派生レポート ＋ coupling-audit（**報告のみ・ゲートにしない**）----------
    # ここを hard にしない理由: この台帳は *設計上ずっと非空* である（未検証の主張を
    # 数え上げるのが目的で、0件になるのは全スパインを縫い終えた時だけ）。ゲートに
    # すると恒久的に赤＝警報疲れを育て、規約4 と同じ轍を踏む。CI では件数の推移を
    # 見る指標として出し、個票は --spine-report で読む。
    findings = spine_report(catalogs, namespaces, edges)
    total = sum(len(v) for v in findings.values())
    print(f"spine report: {total} 件（未検証台帳・報告のみ / 詳細は --spine-report）")
    for key, title in SPINE_SECTIONS:
        if findings[key]:
            print(f"  - {title}: {len(findings[key])}")
    return 0


def extract_commits(out_path: str | None) -> int:
    registry = load_yaml(NAMESPACES_YAML)
    extractable = []
    for ns in registry.get("namespaces", []):
        if ns.get("extract") == "commit-mentions":
            extractable.append((ns["slug"], re.compile(rf"\b(?:{ns['id_pattern']})\b")))
    # proposal is deliberately included read-only: zero hits today is the
    # baseline; the "(proposal P<n>)" citation convention should grow it.
    prop = next((n for n in registry["namespaces"] if n["slug"] == "proposal"), None)
    if prop:
        extractable.append(("proposal", re.compile(r"(?<=proposal )" + prop["id_pattern"] + r"\b")))

    try:
        log = subprocess.run(
            ["git", "log", "--format=%h\x01%s\x01%b\x02", "--all"],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            cwd=REPO_ROOT, check=True,
        ).stdout
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"ERROR: git log failed: {e}", file=sys.stderr)
        return 2

    mentions = []
    for record in log.split("\x02"):
        record = record.strip()
        if not record:
            continue
        parts = record.split("\x01")
        if len(parts) < 2:
            continue
        sha, subject = parts[0].strip(), parts[1]
        body = parts[2] if len(parts) > 2 else ""
        text = subject + "\n" + body
        found = {}  # token -> [slugs]
        for slug, rx in extractable:
            for m in rx.finditer(text):
                found.setdefault(m.group(0), []).append(slug)
        for token, slugs in sorted(found.items()):
            mentions.append({
                "commit": sha,
                "subject": subject[:100],
                "id": token,
                "namespaces": sorted(set(slugs)),
                "ambiguous": len(set(slugs)) > 1,
                "type": "refs",
            })

    ambiguous = sum(1 for m in mentions if m["ambiguous"])
    doc = {
        "summary": {
            "mentions": len(mentions),
            "commits": len({m["commit"] for m in mentions}),
            "ambiguous": ambiguous,
        },
        "mentions": mentions,
    }
    text = yaml.safe_dump(doc, allow_unicode=True, sort_keys=False, width=120)
    if out_path:
        Path(out_path).write_text(text, encoding="utf-8")
        print(f"extracted {len(mentions)} mentions "
              f"({ambiguous} ambiguous) -> {out_path}")
    else:
        print(text)
    return 0


GRAPH_VIEW_MD = KNOWLEDGE_DIR / "graph_view.md"


def fetch_issues():
    """All fork issues as [{number, title, body}] via gh, or None on failure."""
    import json
    try:
        out = subprocess.run(
            ["gh", "issue", "list", "-R", FORK_REPO, "--state", "all",
             "--limit", "200", "--json", "number,title,body"],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            check=True,
        ).stdout
        return json.loads(out)
    except (subprocess.CalledProcessError, FileNotFoundError, ValueError) as e:
        print(f"WARNING: gh issue list failed ({e})", file=sys.stderr)
        return None


def extract_issues(out_path: str | None) -> int:
    """Scan fork issue bodies for namespaced KG references (slug:local-id),
    validate each against the registry, and emit candidate issue->ID edges.
    The issue-side counterpart of --extract-commits."""
    registry = load_yaml(NAMESPACES_YAML)
    vocab = load_yaml(VOCAB_YAML)
    ns_by_slug = {n["slug"]: n for n in registry.get("namespaces", [])}
    concept_ids = {c.get("id") for c in vocab.get("concepts", [])}

    issues = fetch_issues()
    if issues is None:
        return 2

    slug_alt = "|".join(re.escape(s) for s in ns_by_slug)
    ref_rx = re.compile(rf"\b({slug_alt}):([A-Za-z0-9_#][A-Za-z0-9_.#-]*)")

    edges, invalid = [], 0
    for issue in issues:
        text = (issue.get("title") or "") + "\n" + (issue.get("body") or "")
        seen = set()
        for m in ref_rx.finditer(text):
            slug, local = m.group(1), m.group(2)
            ref = f"{slug}:{local}"
            if ref in seen:
                continue
            seen.add(ref)
            ns = ns_by_slug[slug]
            problem = None
            try:
                if not re.fullmatch(ns["id_pattern"], local):
                    problem = f"local id does not match {slug} id_pattern"
            except re.error:
                problem = "namespace pattern broken"
            if slug == "openx" and local not in concept_ids:
                problem = "openx concept not in concept_vocabulary.yaml"
            if problem:
                invalid += 1
            edges.append({
                "from": f"issue:{issue['number']}",
                "title": (issue.get("title") or "")[:80],
                "to": ref,
                "type": "refs",
                **({"problem": problem} if problem else {}),
            })

    doc = {
        "summary": {
            "issues_scanned": len(issues),
            "refs": len(edges),
            "invalid_refs": invalid,
            "issues_without_refs": sum(
                1 for i in issues
                if not ref_rx.search((i.get("title") or "") + "\n" + (i.get("body") or ""))
            ),
        },
        "edges": edges,
    }
    text = yaml.safe_dump(doc, allow_unicode=True, sort_keys=False, width=120)
    if out_path:
        Path(out_path).write_text(text, encoding="utf-8")
        print(f"extracted {len(edges)} issue refs from {len(issues)} issues "
              f"({invalid} invalid) -> {out_path}")
    else:
        print(text)
    return 1 if invalid else 0


def render(out_path: str | None) -> int:
    """Generate the human-readable view (Mermaid + adjacency tables)."""
    registry = load_yaml(NAMESPACES_YAML)
    graph = load_yaml(GRAPH_YAML)
    vocab = load_yaml(VOCAB_YAML)
    ns_by_slug = {n["slug"]: n for n in registry.get("namespaces", [])}
    edges = graph.get("edges", [])

    refs = []
    for e in edges:
        refs.append(e["from"])
        refs.append(e["to"])
    nodes = list(dict.fromkeys(refs))  # unique, insertion order

    def nid(ref: str) -> str:
        return "n_" + re.sub(r"[^0-9A-Za-z_]", "_", ref)

    def nlabel(ref: str) -> str:
        slug, local = ref.split(":", 1)
        return local.split("#", 1)[1] if slug == "openx" else local

    by_slug: dict[str, list] = {}
    for ref in nodes:
        by_slug.setdefault(ref.split(":", 1)[0], []).append(ref)

    mermaid = ["flowchart LR"]
    for slug, members in by_slug.items():
        title = ns_by_slug.get(slug, {}).get("title", slug)
        mermaid.append(f'  subgraph sg_{re.sub(r"[^0-9A-Za-z_]", "_", slug)}["{slug}｜{title}"]')
        for ref in members:
            mermaid.append(f'    {nid(ref)}["{nlabel(ref)}"]')
        mermaid.append("  end")
    for e in edges:
        a, b, t = nid(e["from"]), nid(e["to"]), e["type"]
        if t == "concerns":
            mermaid.append(f"  {a} -. {t} .-> {b}")
        else:
            mermaid.append(f"  {a} -->|{t}| {b}")

    by_type: dict[str, list] = {}
    for e in edges:
        by_type.setdefault(e["type"], []).append(e)

    lines = [
        "# Knowledge Graph View",
        "",
        "> **GENERATED — do not edit.** Source of truth: `graph.yaml` / `namespaces.yaml`.",
        "> Regenerate: `DriverScript/.venv/Scripts/python.exe scripts/check_knowledge_graph.py --render`",
        "",
        f"<!-- generated-from: sha256:{view_hash()} -->",
        "",
        f"ノード {len(nodes)}・辺 {len(edges)}（curatedのみ。commit由来の辺は "
        f"`--extract-commits` で別途抽出）",
        "",
        "```mermaid",
        *mermaid,
        "```",
        "",
        "## 辺の一覧（type別）",
    ]
    for t in sorted(by_type):
        lines += ["", f"### {t} ({len(by_type[t])})", "",
                  "| from | to | note |", "| :--- | :--- | :--- |"]
        for e in by_type[t]:
            lines.append(f"| `{e['from']}` | `{e['to']}` | {e.get('note', '')} |")

    # OpenX concept reverse index: which dev items touch each domain concept.
    ja_by_id = {c.get("id"): c.get("ja", "") for c in vocab.get("concepts", [])}
    openx_index: dict[str, set] = {}
    for e in edges:
        for a, b in ((e["from"], e["to"]), (e["to"], e["from"])):
            if a.startswith("openx:"):
                openx_index.setdefault(a, set()).add(b)
    if openx_index:
        lines += ["", "## OpenX概念 逆引き",
                  "",
                  "curated辺のみ。Issue/コミット言及まで含めた逆引きは "
                  "`--query openx:Domain#<Name> --issues --commits`。",
                  "",
                  "| 概念 | 定義 | 接続ノード |", "| :--- | :--- | :--- |"]
        for ref in sorted(openx_index):
            local = ref.split(":", 1)[1]
            links = ", ".join(f"`{b}`" for b in sorted(openx_index[ref]))
            lines.append(f"| `{local}` | {ja_by_id.get(local, '')} | {links} |")
    lines.append("")

    target = Path(out_path) if out_path else GRAPH_VIEW_MD
    target.write_text("\n".join(lines), encoding="utf-8")
    print(f"rendered {len(nodes)} nodes / {len(edges)} edges -> {target}")
    return 0


def query(ref: str, depth: int, include_commits: bool,
          include_issues: bool = False) -> int:
    """Print everything related to a node: neighbours ring by ring, plus
    (optionally) commit mentions. The pre-work context-gathering command."""
    registry = load_yaml(NAMESPACES_YAML)
    graph = load_yaml(GRAPH_YAML)
    vocab = load_yaml(VOCAB_YAML)
    ns_by_slug = {n["slug"]: n for n in registry.get("namespaces", [])}

    # Resolve a bare token to a namespace when unambiguous; otherwise demand one.
    if ":" not in ref or ref.split(":", 1)[0] not in ns_by_slug:
        candidates = []
        for ns in ns_by_slug.values():
            try:
                if re.fullmatch(ns["id_pattern"], ref):
                    candidates.append(ns["slug"])
            except re.error:
                continue
        if len(candidates) == 1:
            ref = f"{candidates[0]}:{ref}"
            print(f"(resolved bare id to {ref})")
        elif not candidates:
            print(f"ERROR: '{ref}' matches no registered namespace pattern",
                  file=sys.stderr)
            return 1
        else:
            print(f"ERROR: '{ref}' is ambiguous across namespaces: "
                  f"{', '.join(sorted(candidates))} — qualify it "
                  f"(e.g. {sorted(candidates)[0]}:{ref})", file=sys.stderr)
            return 1

    slug, local = ref.split(":", 1)
    ns = ns_by_slug[slug]
    print(f"node   : {ref}")
    print(f"system : {ns.get('title', slug)}  [{ns.get('entity_type', '?')}]")
    print(f"source : {ns.get('source_of_truth', '?')}")
    if slug == "openx":
        c = next((c for c in vocab.get("concepts", []) if c.get("id") == local), None)
        if c:
            print(f"concept: {c.get('ja', '')}  (branch: {c.get('branch', '?')})")

    adjacency: dict[str, list] = {}
    for e in graph.get("edges", []):
        adjacency.setdefault(e["from"], []).append(e)
        adjacency.setdefault(e["to"], []).append(e)

    visited = {ref}
    frontier = [ref]
    printed = set()
    any_edge = False
    for ring in range(1, depth + 1):
        ring_lines = []
        next_frontier = []
        for node in frontier:
            for e in adjacency.get(node, []):
                key = (e["from"], e["to"], e["type"])
                if key in printed:
                    continue
                printed.add(key)
                other = e["to"] if e["from"] == node else e["from"]
                arrow = "->" if e["from"] == node else "<-"
                note = f"  # {e['note']}" if e.get("note") else ""
                ring_lines.append(f"  {node} {arrow} [{e['type']}] {other}{note}")
                if other not in visited:
                    visited.add(other)
                    next_frontier.append(other)
        if ring_lines:
            any_edge = True
            print(f"\nedges (distance {ring}):")
            for line in sorted(ring_lines):
                print(line)
        frontier = next_frontier
        if not frontier:
            break
    if not any_edge:
        print("\nno curated edges touch this node")

    if include_commits:
        try:
            log = subprocess.run(
                ["git", "log", "--format=%h %s", "--all"],
                capture_output=True, text=True, encoding="utf-8",
                errors="replace", cwd=REPO_ROOT, check=True,
            ).stdout
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"WARNING: git log failed ({e}); skipping commit mentions")
            return 0
        token = f"#{local}" if slug == "issue" else local
        rx = re.compile(rf"(?<![0-9A-Za-z_-]){re.escape(token)}(?![0-9A-Za-z])")
        hits = [line for line in log.splitlines() if rx.search(line)]
        print(f"\ncommit mentions of '{token}': {len(hits)}")
        for line in hits[:20]:
            print(f"  {line}")
        if len(hits) > 20:
            print(f"  ... ({len(hits) - 20} more)")

    if include_issues:
        issues = fetch_issues()
        if issues is not None:
            token = f"#{local}" if slug == "issue" else local
            rx = re.compile(
                rf"(?<![0-9A-Za-z_-]){re.escape(token)}(?![0-9A-Za-z])")
            hits = [i for i in issues
                    if rx.search((i.get("title") or "") + "\n" + (i.get("body") or ""))
                    or f"{slug}:{local}" in (i.get("body") or "")]
            print(f"\nissue mentions of '{token}': {len(hits)}")
            for i in hits[:20]:
                print(f"  #{i['number']} {i['title']}")
    return 0


def suggest() -> int:
    """Classify the current changeset against path_map.yaml (unified commit
    workflow, R4): mapped -> candidate IDs to cite; exempt -> no ID needed;
    unknown -> committing agent judges. Advisory only, always exits 0."""
    import fnmatch

    pm = load_yaml(PATH_MAP_YAML) if PATH_MAP_YAML.is_file() else {}
    files, source = [], "none"
    for args, label in ((["git", "diff", "--cached", "--name-only"], "staged"),
                        (["git", "diff", "HEAD", "--name-only"], "worktree")):
        try:
            out = subprocess.run(args, capture_output=True, text=True,
                                 encoding="utf-8", errors="replace",
                                 cwd=REPO_ROOT, check=True).stdout
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
        files = [line.strip() for line in out.splitlines() if line.strip()]
        if files:
            source = label
            break
    if not files:
        print("verdict: none (no changed files detected)")
        return 0

    ids, unknown, exempt_n = [], [], 0
    for f in files:
        f = f.replace("\\", "/")
        hits = [m for m in pm.get("mappings", [])
                if m.get("glob") and fnmatch.fnmatch(f, m["glob"])]
        if hits:
            for m in hits:
                ids.extend(m.get("ids", []))
            continue
        is_exempt = any(
            e.get("glob") and fnmatch.fnmatch(f, e["glob"])
            and not (e.get("unless") and fnmatch.fnmatch(f, e["unless"]))
            for e in pm.get("exempt", []))
        if is_exempt:
            exempt_n += 1
        else:
            unknown.append(f)

    ids = sorted(set(ids))
    verdict = "mapped" if ids else ("exempt" if not unknown else "unknown")
    print(f"verdict: {verdict}")
    if ids:
        print("ids: " + " ".join(ids))
    print(f"files: {len(files)} ({source}; mapped-covered "
          f"{len(files) - exempt_n - len(unknown)}, exempt {exempt_n}, "
          f"unknown {len(unknown)})")
    for f in unknown[:5]:
        print(f"unknown: {f}")
    if len(unknown) > 5:
        print(f"unknown: ... ({len(unknown) - 5} more)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true",
                    help="validate graph files (default)")
    ap.add_argument("--extract-commits", action="store_true",
                    help="scan git history for commit->ID mention candidates")
    ap.add_argument("--extract-issues", action="store_true",
                    help="scan fork issue bodies for namespaced KG refs "
                         "(validates each; exit 1 on invalid refs)")
    ap.add_argument("--render", action="store_true",
                    help="generate graph_view.md (Mermaid + adjacency tables)")
    ap.add_argument("--suggest", action="store_true",
                    help="classify the current changeset via path_map.yaml "
                         "(mapped / exempt / unknown; advisory)")
    ap.add_argument("--spine-report", action="store_true",
                    help="「主張 × 欠けた縦層」の未検証台帳と結合負債を出力（報告のみ）")
    ap.add_argument("--spine-matrix", action="store_true",
                    help="能力モデル行列（capability_model.md §2 の生成ビュー）を出力")
    ap.add_argument("--brief", metavar="REF",
                    help="作業キックオフブリーフを生成（ノード現況・縦串の結線状態・"
                         "本件該当の未検証項目・関連ファイル・近傍。判断系は生成しない）")
    ap.add_argument("--query", metavar="REF",
                    help="show everything related to a node "
                         "(e.g. proposal:P13; bare ids resolved when unambiguous)")
    ap.add_argument("--depth", type=int, default=2,
                    help="neighbourhood depth for --query (default 2)")
    ap.add_argument("--commits", action="store_true",
                    help="with --query: also list commit mentions")
    ap.add_argument("--issues", action="store_true",
                    help="with --query: also list fork issue mentions (needs gh)")
    ap.add_argument("--out",
                    help="output file for --extract-commits / --extract-issues / --render")
    args = ap.parse_args()
    if args.extract_commits:
        return extract_commits(args.out)
    if args.extract_issues:
        return extract_issues(args.out)
    if args.render:
        return render(args.out)
    if args.spine_report:
        return spine_report_cmd(args.out)
    if args.spine_matrix:
        return spine_matrix_cmd(args.out)
    if args.suggest:
        return suggest()
    if args.brief:
        return brief(args.brief)
    if args.query:
        return query(args.query, args.depth, args.commits, args.issues)
    return check()


if __name__ == "__main__":
    sys.exit(main())
