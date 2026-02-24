"""Python controller script discovery and inspection."""

from __future__ import annotations

import ast
import re
from pathlib import Path

from GT_esmini.web.backend.config import PYTHON_SCRIPT_DIRS, REPO_ROOT
from GT_esmini.web.backend.models.script import ScriptInfo


def _detect_classes(filepath: Path) -> list[str]:
    """Parse Python file AST to extract top-level class names."""
    try:
        source = filepath.read_text(encoding="utf-8", errors="ignore")
        tree = ast.parse(source)
        return [node.name for node in ast.walk(tree) if isinstance(node, ast.ClassDef)]
    except (SyntaxError, Exception):
        return []


def _category_from_dir(rel_dir: str) -> str:
    """Map directory to category."""
    if "pythondriver" in rel_dir:
        return "pythondriver"
    if "examples" in rel_dir:
        return "examples"
    if "realdriver" in rel_dir:
        return "realdriver"
    return "other"


def list_scripts() -> list[ScriptInfo]:
    """Scan configured directories for Python controller scripts."""
    results: list[ScriptInfo] = []
    seen: set[str] = set()

    for rel_dir in PYTHON_SCRIPT_DIRS:
        scan_dir = REPO_ROOT / rel_dir
        if not scan_dir.is_dir():
            continue

        for py_file in sorted(scan_dir.glob("*.py")):
            if py_file.name.startswith("_"):
                continue
            rel_path = str(py_file.relative_to(REPO_ROOT)).replace("\\", "/")
            if rel_path in seen:
                continue
            seen.add(rel_path)

            category = _category_from_dir(rel_dir)
            classes = _detect_classes(py_file)

            # Recommend if it's in pythondriver and has EmbeddedController or similar
            recommended = category == "pythondriver" and any(
                "Controller" in c or "Embedded" in c for c in classes
            )

            results.append(
                ScriptInfo(
                    path=rel_path,
                    name=py_file.name,
                    category=category,
                    classes=classes,
                    recommended=recommended,
                )
            )

    return results


def get_script_detail(script_path: str) -> ScriptInfo | None:
    """Get details for a specific script."""
    abs_path = REPO_ROOT / script_path
    if not abs_path.exists() or not abs_path.suffix == ".py":
        return None

    rel_path = str(abs_path.relative_to(REPO_ROOT)).replace("\\", "/")
    category = _category_from_dir(rel_path)
    classes = _detect_classes(abs_path)
    recommended = category == "pythondriver" and any(
        "Controller" in c or "Embedded" in c for c in classes
    )

    return ScriptInfo(
        path=rel_path,
        name=abs_path.name,
        category=category,
        classes=classes,
        recommended=recommended,
    )
