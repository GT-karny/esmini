"""Failure-cause extraction from esmini log.txt (audit WEB-2 / PY-3 / CORE-3).

esmini writes every diagnostic to its file log (log.txt) and leaves stderr
almost empty (audit CORE-1), so the "stderr head" the UI used to show carried
no real cause. This module mines log.txt for the actual failure and is the
single source shared by the web backend (self-contained for PyInstaller/Electron
like services/xosc_paths.py) and resources/scenario_authoring/validate_catalog.py
(canonical package import, see that file).

Handles the log's known quirks:
  - the same failure is logged 2-3 times (CORE-3) -> fold on the message body;
  - XML syntax errors surface at [info]/[warn] with "Error parsing" (CORE-2);
  - healthy runs still emit some [error] noise (CORE-9 / GT-6) -> rank it below
    any genuine cause so the reported line is not the noise;
  - a 0xC0000135 exit means GT_Sim died before writing any log (DLL staging).
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

# esmini re-logs the same failure via LOG_ERROR_AND_QUIT -> catch -> return-value
# check (audit CORE-3); the inner message is often wrapped as "Exception: <text>".
_EXCEPTION_PREFIX = "Exception: "

# Lines that appear even on a clean exit (audit CORE-9 / GT-6): collected but
# ranked below any real error so they never win the "final cause" slot.
_NOISE_MARKERS = (
    "Unsupported object type:",
    "Failed closing socket",
)

# Generic re-log wrappers from the CORE-3 chain (playerbase/esminiLib): they end
# the log but carry no cause, so rank them below the specific error they wrap.
_GENERIC_MARKERS = (
    "Failed to initialize scenario player",
    "Failed to initialize GT_esmini",
)

# Windows STATUS_DLL_NOT_FOUND (0xC0000135) shows up as these signed/unsigned
# ints depending on the shell (audit §5): the process never started, so log.txt
# is empty. Translate the code into an actionable cause.
_DLL_MISSING_CODES = (0xC0000135, -1073741515, 3221225781)
_DLL_MISSING_MSG = "DLL ステージング不足の可能性 (exit 0xC0000135 / STATUS_DLL_NOT_FOUND)"

_NO_CAUSE_MSG = "原因不明 (log.txt に [error]/Exception 行なし)"

_SEVERITY_TAGS = ("[error]", "[warn]", "[info]", "[debug]")
_PARSE_MARKER = "Error parsing"


@dataclass
class LogExtract:
    """Result of mining a run's log for its failure cause."""

    summary: str            # single best-guess final cause
    error_lines: list[str]  # deduped candidate messages, chronological order

    def as_message(self, limit: int = 2000) -> str:
        """Render a UI-facing error_message: final cause + recent error lines."""
        parts = [self.summary]
        extra = [line for line in self.error_lines if line != self.summary]
        if extra:
            parts.append("")
            parts.append("Recent errors:")
            parts.extend(f"  {line}" for line in extra)
        return "\n".join(parts)[:limit]


def _read_tail(path: str | Path | None, tail_lines: int) -> list[str]:
    if path is None:
        return []
    p = Path(path)
    if not p.is_file():
        return []
    try:
        text = p.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    return text.splitlines()[-tail_lines:]


def _is_candidate(line: str) -> bool:
    if "[error]" in line:
        return True
    if "Exception" in line:
        return True
    return _PARSE_MARKER in line  # CORE-2: XML syntax errors log at [info]/[warn]


def _rank(cleaned: str) -> int:
    """0 = specific cause, 1 = generic wrapper (CORE-3), 2 = known noise."""
    if any(marker in cleaned for marker in _NOISE_MARKERS):
        return 2
    if any(marker in cleaned for marker in _GENERIC_MARKERS):
        return 1
    return 0


def _clean(line: str) -> str:
    """Reduce a raw log line to its bare message for dedup and display.

    Strips everything up to the last severity tag (handles the doubled
    ``[error] Exception: [] [error] <text>`` form from CORE-3), the empty ``[]``
    timestamp field, and a leading ``Exception: `` wrapper.
    """
    s = line.strip()
    positions = [s.rfind(tag) + len(tag) for tag in _SEVERITY_TAGS if tag in s]
    if positions:
        s = s[max(positions):].strip()
    if s.startswith("[]"):
        s = s[2:].strip()
    if s.startswith(_EXCEPTION_PREFIX):
        s = s[len(_EXCEPTION_PREFIX):].strip()
    return s


def extract_failure(
    log_path: str | Path | None,
    stdout_path: str | Path | None = None,
    exit_code: int | None = None,
    tail_lines: int = 200,
) -> LogExtract:
    """Extract the failure cause from a run's log.txt (fallback: stdout.txt).

    Returns a :class:`LogExtract` whose ``summary`` is the single most-likely
    cause (last non-noise error nearest the tail) and whose ``error_lines`` is
    the deduped candidate list. A DLL-missing exit code overrides both.
    """
    lines = _read_tail(log_path, tail_lines)
    if not lines:
        lines = _read_tail(stdout_path, tail_lines)

    candidates: list[tuple[str, int]] = []
    seen: set[str] = set()
    for raw in lines:
        if not _is_candidate(raw):
            continue
        cleaned = _clean(raw)
        if not cleaned or cleaned in seen:
            continue
        seen.add(cleaned)
        candidates.append((cleaned, _rank(cleaned)))

    error_lines = [cleaned for cleaned, _ in candidates]

    # Final cause = the candidate nearest the tail with the best (lowest) rank.
    summary = ""
    for want in (0, 1, 2):
        for cleaned, rank in reversed(candidates):
            if rank == want:
                summary = cleaned
                break
        if summary:
            break

    if exit_code is not None and exit_code in _DLL_MISSING_CODES:
        summary = _DLL_MISSING_MSG
        if _DLL_MISSING_MSG not in error_lines:
            error_lines = [_DLL_MISSING_MSG, *error_lines]

    if not summary:
        summary = _NO_CAUSE_MSG

    return LogExtract(summary=summary, error_lines=error_lines)
