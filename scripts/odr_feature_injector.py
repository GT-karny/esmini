#!/usr/bin/env python
"""odr_feature_injector.py -- recipe-driven OpenDRIVE feature injector.

WHY THIS EXISTS
---------------
This is a generalization of
``resources/scenario_authoring/road_catalog/priority_injector.py``. Whereas
priority_injector injects one hard-coded construct (junction <priority>), this
tool applies an *arbitrary* ordered list of edit operations described by a YAML
recipe, so that OpenDRIVE 1.6-1.9 idiom fixtures can be authored on top of
existing repository roads (see GT_esmini/docs/archive/odr_1619_program/opendrive_16_19_support_plan.md
P0, cluster 0/2).

``scenariogeneration`` caps revMinor at 5 and cannot emit 1.6+ constructs, so
the repo has zero 1.8/1.9 assets. Rather than hand-writing whole xodr files (or
redistributing ASAM sample files), each fixture is derived from a *repository*
base road by a small, reviewable recipe -- keeping the generated fixtures pure
derivatives of files we already own.

RECIPE FORMAT (YAML)
--------------------
    id: g1_lanesection_length_19
    base: resources/xodr/straight_500m.xodr   # repo-relative; NEVER an ASAM file
    rev: "9"                                   # target header@revMinor
    ops:
      - {op: set_attr,     xpath: "...", attr: "...", value: "..."}
      - {op: add_element,  xpath: "<parent>", name: "...", attrs: {...}, before: "<sibling tag or null>"}
      - {op: add_xml,      xpath: "<parent>", xml: "<raw XML snippet>", before: "..."}
      - {op: remove_element, xpath: "..."}

- ``xpath`` is an lxml ElementTree XPath evaluated against the document root
  (the ``<OpenDRIVE>`` element). It must match exactly one element for
  ``set_attr`` / ``remove_element`` and exactly one *parent* element for the add
  ops (a recipe that matches 0 or >1 elements is a hard error -- fail loud).
- ``before`` (add ops, optional): the tag name of the first existing child of
  the parent before which the new node is inserted (schema-ordered insertion).
  If omitted / null / not found, the node is appended as the last child.
- ``add_xml`` parses a raw XML snippet (single root element) so nested
  structures can be injected in one op.

HEADER NORMALISATION (determinism)
----------------------------------
Every run sets, on the <header> element:
  * ``name``     = recipe id
  * ``date``     = "2026-07-02T00:00:00"  (fixed -- no wall-clock timestamps)
  * ``revMinor`` = the recipe ``rev``
This guarantees byte-stable output across machines and runs.

DETERMINISM
-----------
lxml preserves attribute insertion order, and this tool never introduces a
timestamp beyond the fixed header date, so a given (recipe, base) pair always
produces byte-identical output. The tree is re-indented with a fixed 4-space
step before writing.

USAGE
-----
    odr_feature_injector.py <recipe.yaml | dir-of-recipes> \
        [--out-dir GT_esmini/test/odr_fixtures/generated]

    # importable
    from odr_feature_injector import apply_recipe
    out_path = apply_recipe("recipes/g1.yaml", "generated/")

Run under DriverScript/.venv (lxml + pyyaml).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import yaml
from lxml import etree

# Repository root = two levels up from this file (scripts/ -> repo root).
REPO_ROOT = Path(__file__).resolve().parents[1]

# Fixed header date for deterministic output.
FIXED_HEADER_DATE = "2026-07-02T00:00:00"

DEFAULT_OUT_DIR = REPO_ROOT / "GT_esmini" / "test" / "odr_fixtures" / "generated"

# Recipe bases must be repository files (own IP), never redistributed ASAM
# packages. We forbid recipes from pointing at the ASAM 1.9 sample tree.
_FORBIDDEN_BASE_PREFIXES = (
    "thirdparty/opendrive",
)


class RecipeError(RuntimeError):
    """Raised for any malformed recipe or ambiguous/empty XPath match."""


# ---------------------------------------------------------------------------
# XPath helpers
# ---------------------------------------------------------------------------
def _find_one(root: etree._Element, xpath: str, *, context: str) -> etree._Element:
    """Return the single element matching *xpath* under *root* or raise.

    An XPath that matches zero or more-than-one elements is a recipe bug and is
    reported loudly rather than silently picking the first match.
    """
    matches = root.xpath(xpath)
    if not isinstance(matches, list):
        raise RecipeError(f"{context}: xpath {xpath!r} did not evaluate to a node set")
    elems = [m for m in matches if isinstance(m, etree._Element)]
    if len(elems) == 0:
        raise RecipeError(f"{context}: xpath {xpath!r} matched no elements")
    if len(elems) > 1:
        raise RecipeError(
            f"{context}: xpath {xpath!r} matched {len(elems)} elements (expected exactly 1)"
        )
    return elems[0]


def _insert_child(parent: etree._Element, node: etree._Element, before: str | None) -> None:
    """Insert *node* into *parent*, before the first child named *before*.

    If *before* is falsy or no such child exists, *node* is appended last. This
    honours the schema element order the recipe author chooses.
    """
    if before:
        for idx, child in enumerate(parent):
            if isinstance(child.tag, str) and child.tag == before:
                parent.insert(idx, node)
                return
    parent.append(node)


# ---------------------------------------------------------------------------
# Operations
# ---------------------------------------------------------------------------
def _op_set_attr(root: etree._Element, spec: dict) -> None:
    xpath = spec["xpath"]
    attr = spec["attr"]
    value = spec["value"]
    el = _find_one(root, xpath, context="set_attr")
    el.set(attr, str(value))


def _op_add_element(root: etree._Element, spec: dict) -> None:
    parent = _find_one(root, spec["xpath"], context="add_element")
    node = etree.SubElement(parent, spec["name"])  # created at end; reposition below
    parent.remove(node)  # detach so we can place it deterministically
    for k, v in (spec.get("attrs") or {}).items():
        node.set(k, str(v))
    _insert_child(parent, node, spec.get("before"))


def _op_add_xml(root: etree._Element, spec: dict) -> None:
    parent = _find_one(root, spec["xpath"], context="add_xml")
    snippet = spec["xml"]
    try:
        node = etree.fromstring(snippet)
    except etree.XMLSyntaxError as exc:
        raise RecipeError(f"add_xml: could not parse xml snippet: {exc}") from exc
    _insert_child(parent, node, spec.get("before"))


def _op_remove_element(root: etree._Element, spec: dict) -> None:
    el = _find_one(root, spec["xpath"], context="remove_element")
    parent = el.getparent()
    if parent is None:
        raise RecipeError("remove_element: cannot remove the document root")
    parent.remove(el)


_OPS = {
    "set_attr": _op_set_attr,
    "add_element": _op_add_element,
    "add_xml": _op_add_xml,
    "remove_element": _op_remove_element,
}


# ---------------------------------------------------------------------------
# Header normalisation
# ---------------------------------------------------------------------------
def _normalise_header(root: etree._Element, recipe_id: str, rev: str) -> None:
    header = root.find("header")
    if header is None:
        raise RecipeError("base xodr has no <header> element")
    header.set("name", recipe_id)
    header.set("date", FIXED_HEADER_DATE)
    header.set("revMinor", str(rev))


# ---------------------------------------------------------------------------
# Recipe application
# ---------------------------------------------------------------------------
def _resolve_base(base: str) -> Path:
    base_norm = base.replace("\\", "/")
    for bad in _FORBIDDEN_BASE_PREFIXES:
        if base_norm.startswith(bad):
            raise RecipeError(
                f"recipe base {base!r} points inside {bad!r}; bases must be "
                f"repository-owned xodr files (not redistributed ASAM samples)"
            )
    path = (REPO_ROOT / base_norm).resolve()
    if not path.exists():
        raise RecipeError(f"recipe base not found: {path}")
    return path


def apply_recipe(recipe_path: str | Path, out_dir: str | Path = DEFAULT_OUT_DIR) -> Path:
    """Apply a single recipe and write ``<recipe id>.xodr`` into *out_dir*.

    Returns the path written.
    """
    recipe_path = Path(recipe_path)
    with recipe_path.open("r", encoding="utf-8") as fh:
        recipe = yaml.safe_load(fh)

    if not isinstance(recipe, dict):
        raise RecipeError(f"{recipe_path}: recipe is not a mapping")
    for key in ("id", "base", "rev", "ops"):
        if key not in recipe:
            raise RecipeError(f"{recipe_path}: recipe missing required key {key!r}")

    recipe_id = str(recipe["id"])
    rev = str(recipe["rev"])
    base_path = _resolve_base(str(recipe["base"]))

    # remove_blank_text so re-indentation produces stable, clean output.
    parser = etree.XMLParser(remove_blank_text=True)
    tree = etree.parse(str(base_path), parser)
    root = tree.getroot()

    _normalise_header(root, recipe_id, rev)

    ops = recipe["ops"] or []
    if not isinstance(ops, list):
        raise RecipeError(f"{recipe_path}: 'ops' must be a list")
    for i, spec in enumerate(ops):
        if not isinstance(spec, dict) or "op" not in spec:
            raise RecipeError(f"{recipe_path}: op #{i} is not a mapping with an 'op' key")
        op_name = spec["op"]
        handler = _OPS.get(op_name)
        if handler is None:
            raise RecipeError(f"{recipe_path}: op #{i} unknown op {op_name!r}")
        try:
            handler(root, spec)
        except RecipeError:
            raise
        except Exception as exc:  # surface op index for debuggability
            raise RecipeError(f"{recipe_path}: op #{i} ({op_name}) failed: {exc}") from exc

    # Re-indent for a clean, deterministic layout (fixed 4-space step).
    etree.indent(tree, space="    ")

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    dst = out_dir / f"{recipe_id}.xodr"
    tree.write(str(dst), encoding="UTF-8", xml_declaration=True, pretty_print=True)
    return dst


def _iter_recipe_files(target: Path):
    if target.is_dir():
        yield from sorted(target.glob("*.yaml"))
        yield from sorted(target.glob("*.yml"))
    else:
        yield target


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Apply OpenDRIVE feature-injection recipe(s) to repository base roads."
    )
    parser.add_argument(
        "recipe",
        type=Path,
        help="Recipe YAML file, or a directory of recipe YAMLs to apply.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=DEFAULT_OUT_DIR,
        help=f"Output directory for generated .xodr (default: {DEFAULT_OUT_DIR}).",
    )
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    recipe_files = list(_iter_recipe_files(args.recipe))
    if not recipe_files:
        print(f"[injector] no recipe files found at {args.recipe}", file=sys.stderr)
        return 2
    rc = 0
    for rf in recipe_files:
        try:
            dst = apply_recipe(rf, args.out_dir)
            print(f"[injector] {rf.name} -> {dst}")
        except RecipeError as exc:
            print(f"[injector] ERROR {rf.name}: {exc}", file=sys.stderr)
            rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
