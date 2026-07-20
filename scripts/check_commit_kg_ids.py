#!/usr/bin/env python3
"""コミットメッセージ中の知識グラフ ID 引用を検査する（CLAUDE.md R4）。

**単一の真実源**: 受理する ID の文法は `GT_esmini/docs/knowledge/namespaces.yaml` の
`slug` + `id_pattern` から**生成**する。ハードコードした許可リストは持たない。

  背景（2026-07-20）: 従来は commit-msg フックと .claude/hooks/gt_guard.ps1 が
  同一の許可リストを**二重にハードコード**しており、次の2点で規約と反転していた。
    (a) 規約（namespaces.yaml 冒頭・CLAUDE.md §9）が命じる修飾形 `<slug>:<id>` を
        1つも認識しなかった（`vd-func:FUNC-001` 等が「ID 無し」と判定された）。
    (b) 逆に namespaces.yaml 自身が「衝突するため使うな」とした裸の序数
        （`Phase3` 等）を明示的に許可していた。
  結果、規約を守るほど警告が出る状態になり、警報疲れで無視される習慣を育てた。
  本スクリプトはその反転を解消し、名前空間が増えたら自動追随する。

判定:
  * 修飾 ID（`<slug>:<local-id>`）を含む      -> OK（exit 0, 無出力）
  * 裸の legacy ID のみ（`F6` / `SUB-1` 等）  -> OK だが修飾を促す hint（exit 0）
  * ID をまったく含まない                     -> 警告（exit 2）
  * 裸の序数（`Phase3` / `フェーズ3` / `P3`）  -> 所属の明記を促す hint（判定とは独立）

exit code は gt_guard.ps1 が 'ask' 判定に使う。commit-msg フックは advisory
（常に 0 で抜ける）なので、ブロックはしない。

使い方:
  check_commit_kg_ids.py <msgfile>   # ファイルから読む（commit-msg フック）
  check_commit_kg_ids.py -           # 標準入力から読む（gt_guard.ps1）
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # venv 外から呼ばれた場合は黙って通す（advisory のため）
    sys.exit(0)

REPO_ROOT = Path(__file__).resolve().parents[1]
NAMESPACES = REPO_ROOT / "GT_esmini" / "docs" / "knowledge" / "namespaces.yaml"

# 裸でも従来から通っていた ID 形（後方互換。hint を出しつつ受理する）
LEGACY_BARE = re.compile(
    r"(\bF[1-6]\b|\bR[0-5](-U[1-4])?\b"
    r"|\b(CTL|SUB|VD|CORE|WEB|FE|SCR|BLD|TST|BND|MSC|Critic|GT|PY|XSD|ES)-[0-9]+\b"
    r"|\b(proposal|plan)\s+P[0-9]+\b|\bGT_ODR\b|\bGT_LHT\b)"
)

# issue 参照は namespaces.yaml の外にある正当な参照
ISSUE_REF = re.compile(r"#[0-9]+\b")

# 所属を持たない序数（今回の phase3 問題）。`slug:` が直前に無いものだけを拾う。
BARE_ORDINAL = re.compile(r"(?<![\w:-])(?:Phase|phase|フェーズ|P)\s?[0-9]+[a-e]?\b")

EXEMPT_FIRST_LINE = re.compile(r"^(wip\b|WIP\b|Merge|merge|fixup!|squash!)", re.IGNORECASE)


def load_qualified_pattern() -> re.Pattern | None:
    """namespaces.yaml から `<slug>:<id_pattern>` の受理正規表現を生成する。"""
    try:
        data = yaml.safe_load(NAMESPACES.read_text(encoding="utf-8"))
    except Exception:
        return None
    namespaces = (data or {}).get("namespaces") or []
    alts = []
    for ns in namespaces:
        slug = ns.get("slug")
        if not slug:
            continue
        local = ns.get("id_pattern") or r"[A-Za-z0-9_.-]+"
        alts.append(rf"{re.escape(slug)}:(?:{local})")
    if not alts:
        return None
    return re.compile(r"(?<![\w-])(?:" + "|".join(alts) + r")")


def main() -> int:
    if len(sys.argv) < 2:
        return 0
    src = sys.argv[1]
    try:
        text = sys.stdin.read() if src == "-" else Path(src).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return 0

    first_line = text.lstrip().splitlines()[0] if text.strip() else ""
    if EXEMPT_FIRST_LINE.match(first_line) or "--amend" in text:
        return 0

    out = sys.stderr
    try:
        out.reconfigure(encoding="utf-8")  # cp932 環境で非ASCIIが落ちるのを防ぐ
    except Exception:
        pass

    qualified = load_qualified_pattern()
    has_qualified = bool(qualified and qualified.search(text))
    has_legacy = bool(LEGACY_BARE.search(text)) or bool(ISSUE_REF.search(text))

    # 所属の無い序数（判定とは独立の hint）
    for m in BARE_ORDINAL.finditer(text):
        print(
            f"commit-msg [R4] hint: 所属の無い序数 '{m.group(0)}' があります。"
            "どのプログラムの段階かを明記してください（例 vd-phase:Phase3 / spine-phase:Phase3）。",
            file=out,
        )
        break

    if has_qualified:
        return 0

    if has_legacy:
        print(
            "commit-msg [R4] hint: 裸の ID が使われています。"
            "名前空間で修飾すると一意になります（例 F6 -> feature:F6）。",
            file=out,
        )
        return 0

    print("", file=out)
    print("commit-msg [R4]: 知識グラフ ID の引用がありません。", file=out)
    print("  commit->ID の辺を機械抽出できるよう、関連 ID を引用してください:", file=out)
    print("    vd-func:FUNC-001 / signal:ego_accel_long / feature:F6 / fixes #30 ...", file=out)
    print("  文法の真実源: GT_esmini/docs/knowledge/namespaces.yaml", file=out)
    return 2


if __name__ == "__main__":
    sys.exit(main())
