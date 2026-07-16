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

    if errors:
        print(f"knowledge graph check: {len(errors)} violation(s)")
        for msg in errors:
            print(f"  - {msg}")
        return 1
    print(f"knowledge graph check: OK "
          f"({len(namespaces)} namespaces, {len(edges)} curated edges, "
          f"{len(concept_ids)} openx concepts, view fresh)")
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
    if args.query:
        return query(args.query, args.depth, args.commits, args.issues)
    return check()


if __name__ == "__main__":
    sys.exit(main())
