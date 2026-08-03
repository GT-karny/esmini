# GT_esmini agent guidance

## Scope and safety

- `EnvironmentSimulator/` and `OSMP_FMU/` are pristine upstream/reference code. Do not modify them unless the user explicitly approves the upstream edit. Put product work in `GT_esmini/`.
- Treat `resources/` as trusted input data. Diagnose implementation behavior before changing scenario or road assets.
- Never use bare `python`, `py`, or `pip`. Use `DriverScript/.venv/Scripts/python.exe` for verification tooling and `GT_esmini/web/.venv/Scripts/python.exe` for web tooling.
- For GitHub write operations, always pin the fork: `-R GT-karny/esmini`.

## Required workflow

- Before ID-linked work, query the knowledge graph: `DriverScript/.venv/Scripts/python.exe scripts/check_knowledge_graph.py --query <namespace:id> --commits`.
- Cite a related namespaced knowledge-graph ID in non-exempt commit messages. Keep `wip`, merge, fixup, squash, and amend conventions unchanged.
- Build C++ from the repository root with CMake/VS2022. After C++ behavior changes, run `pwsh scripts/run_regression_gate.ps1`; use `pwsh scripts/run_gt_tests.ps1` for the unit gate.
- Preserve existing worktree changes. Do not reset, checkout, or remove unrelated files.

## Detailed project references

- Architecture, build protocols, gates, and release rules: `CLAUDE.md`.
- `GT_esmini/`, `scripts/`, and `DriverScript/` each have a local `CLAUDE.md` with component details; read the relevant one before substantial work in that subtree.
- Reusable Codex workflows live in `.codex/skills/`.
