#!/usr/bin/env python3
"""Detect the CI ``black-format`` lint failure locally, before pushing.

The lint job in ``.github/workflows/ci.yml`` runs ``pre-commit run black-format
--all-files``. It is the first job in the CI graph, so a single unformatted .py
file blocks every build/test job behind it -- and the failure only becomes
visible minutes after a push. This script answers the same question locally.

The file selection and the black arguments are NOT duplicated here. They are
read out of ``.pre-commit-config.yaml`` (the ``black-format`` hook's ``entry`` /
``files`` / ``exclude``), so this checker and CI cannot drift apart. black
itself must be the version pinned in ``support/python/requirements.txt`` --
formatting is version-dependent, and a newer local black would "fix" files in a
way the pinned CI black then rejects. The check warns when they disagree.

Two modes:

  (default)   every tracked .py file CI would look at -- run this before a push
  --staged    only what is about to be committed, read from the index rather
              than the worktree, so a partially-staged file is judged on the
              content that will actually land. Used by the pre-commit hook
              (``scripts/git-hooks/pre-commit``).

Nothing is ever reformatted: the checker is read-only and prints the exact
command to fix what it found. That command is deliberately scoped to the
offending files, never ``--all-files`` -- in this repo several agents share one
worktree, and a repo-wide reformat sweeps up their uncommitted edits.

    DriverScript/.venv/Scripts/python.exe scripts/check_black_format.py
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

import yaml


def _make_output_encoding_safe() -> None:
    """Never let a console encoding turn a PASS into a non-zero exit.

    git runs the pre-commit hook with stdout on the console's ANSI codepage, not
    UTF-8 (cp932 on a Japanese Windows checkout). Any non-ASCII byte we print
    then raises UnicodeEncodeError *after* the check itself succeeded, so the
    hook blocks a commit whose files are perfectly formatted -- the gate fails
    exactly in the path where it should pass. Degrading unencodable characters
    is always preferable to that: the exit code must reflect black's verdict and
    nothing else.
    """
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(errors="replace")
            except (ValueError, OSError):
                pass


_make_output_encoding_safe()

REPO_ROOT = Path(__file__).resolve().parents[1]
PRE_COMMIT_CONFIG = REPO_ROOT / ".pre-commit-config.yaml"
REQUIREMENTS = REPO_ROOT / "support" / "python" / "requirements.txt"
HOOK_ID = "black-format"

# git accepts arbitrarily many paths, but Windows caps a command line at ~32k
# characters. Chunk so a repo-wide run can never hit that ceiling.
_BATCH = 100


class ConfigError(RuntimeError):
    """The pre-commit config is missing, malformed, or lost its black hook."""


def _load_hook() -> dict:
    if not PRE_COMMIT_CONFIG.is_file():
        raise ConfigError(f"not found: {PRE_COMMIT_CONFIG}")
    config = yaml.safe_load(PRE_COMMIT_CONFIG.read_text(encoding="utf-8")) or {}
    for repo in config.get("repos", []) or []:
        for hook in repo.get("hooks", []) or []:
            if hook.get("id") == HOOK_ID:
                return hook
    raise ConfigError(f"no '{HOOK_ID}' hook in {PRE_COMMIT_CONFIG.name}")


def _black_args(hook: dict) -> list[str]:
    """The flags CI passes to black, minus the executable name itself."""
    entry = (hook.get("entry") or "").split()
    if not entry or "black" not in entry[0]:
        raise ConfigError(f"'{HOOK_ID}' entry does not invoke black: {entry!r}")
    return entry[1:] + list(hook.get("args") or [])


def _selectors(hook: dict) -> tuple[re.Pattern, re.Pattern | None]:
    """Compile the hook's include/exclude regexes.

    ``exclude`` is written as a YAML folded scalar carrying an ``(?x)`` verbose
    pattern, so the folding whitespace is insignificant to the regex engine --
    it compiles as-is.
    """
    include = re.compile(hook.get("files") or r"\.py$")
    raw_exclude = hook.get("exclude")
    return include, re.compile(raw_exclude) if raw_exclude else None


def _git(*args: str) -> list[str]:
    out = subprocess.run(
        ["git", *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [line for line in out.split("\0") if line]


def _candidates(staged: bool) -> list[str]:
    if staged:
        # ACMR: skip deletions -- a removed file has no content to format.
        return _git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z")
    return _git("ls-files", "-z")


def _select(paths: list[str], hook: dict) -> list[str]:
    include, exclude = _selectors(hook)
    return [
        p
        for p in paths
        if include.search(p) and not (exclude and exclude.search(p))
    ]


def _pinned_black_version() -> str | None:
    if not REQUIREMENTS.is_file():
        return None
    for line in REQUIREMENTS.read_text(encoding="utf-8").splitlines():
        name, sep, version = line.strip().partition("==")
        if sep and name == "black":
            return version
    return None


def _warn_on_version_skew(black_cmd: list[str]) -> None:
    pinned = _pinned_black_version()
    if not pinned:
        return
    proc = subprocess.run(
        [*black_cmd, "--version"], capture_output=True, text=True, cwd=REPO_ROOT
    )
    if proc.returncode != 0:
        return
    local = re.search(r"\b(\d+\.\d+(?:\.\d+)?)\b", proc.stdout)
    if local and local.group(1) != pinned:
        print(
            f"[warn] local black {local.group(1)} != CI pin {pinned} "
            f"({REQUIREMENTS.relative_to(REPO_ROOT).as_posix()}). "
            "Formatting differs between versions, so this result may not match CI.",
            file=sys.stderr,
        )


def _check_worktree(black_cmd: list[str], args: list[str], paths: list[str]) -> list[str]:
    """Check files as they sit on disk. Returns the paths black would rewrite."""
    failed: list[str] = []
    for i in range(0, len(paths), _BATCH):
        batch = paths[i : i + _BATCH]
        proc = subprocess.run(
            [*black_cmd, "--check", "--quiet", *args, *batch],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        if proc.returncode == 0:
            continue
        # --quiet still reports each file it would reformat on stderr; fall back
        # to re-checking one by one if black said nothing useful (e.g. a syntax
        # error, which exits 123 without naming the file in a parseable way).
        named = re.findall(r"would reformat (.+)", proc.stderr)
        if named:
            failed.extend(Path(n.strip()).as_posix() for n in named)
        else:
            failed.extend(_check_worktree(black_cmd, args, batch) if len(batch) > 1 else batch)
    return failed


def _check_index(black_cmd: list[str], args: list[str], paths: list[str]) -> list[str]:
    """Check the *staged* content, which is what the commit will contain.

    A file can be formatted on disk while its staged version is not (only part
    of it was added), so the worktree is the wrong thing to judge here.
    """
    failed: list[str] = []
    for path in paths:
        blob = subprocess.run(
            ["git", "show", f":{path}"],
            cwd=REPO_ROOT,
            capture_output=True,
            check=False,
        )
        if blob.returncode != 0:
            continue  # unreadable index entry (e.g. unmerged) -- leave to git
        proc = subprocess.run(
            [*black_cmd, "--check", "--quiet", *args, "-"],
            cwd=REPO_ROOT,
            input=blob.stdout,
            capture_output=True,
        )
        if proc.returncode != 0:
            failed.append(path)
    return failed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check python formatting exactly as the CI lint job does."
    )
    parser.add_argument(
        "--staged",
        action="store_true",
        help="check only staged files, judged on their indexed content",
    )
    parser.add_argument(
        "--black",
        default=None,
        help="black executable (default: this interpreter's 'python -m black')",
    )
    opts = parser.parse_args(argv)

    try:
        hook = _load_hook()
        args = _black_args(hook)
    except ConfigError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 2

    black_cmd = [opts.black] if opts.black else [sys.executable, "-m", "black"]
    _warn_on_version_skew(black_cmd)

    paths = _select(_candidates(opts.staged), hook)
    if not paths:
        print("[black] nothing to check.")
        return 0

    checker = _check_index if opts.staged else _check_worktree
    failed = sorted(set(checker(black_cmd, args, paths)))

    scope = "staged" if opts.staged else "tracked"
    if not failed:
        print(f"[black] OK -- {len(paths)} {scope} file(s) already formatted.")
        return 0

    print(
        f"\n[FAIL] black would reformat {len(failed)} of {len(paths)} {scope} file(s).\n"
        "       The CI 'lint' job runs first, so this blocks every build and test job.\n",
        file=sys.stderr,
    )
    for path in failed:
        print(f"  {path}", file=sys.stderr)
    print(
        "\nFix exactly these files (do NOT run black repo-wide — this worktree is\n"
        "shared, and a sweep would reformat other sessions' uncommitted edits):\n\n"
        "  DriverScript/.venv/Scripts/python.exe -m black "
        + " ".join(args)
        + " \\\n    "
        + " \\\n    ".join(failed)
        + "\n",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
