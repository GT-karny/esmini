# P6 Stage 8 handoff — upstream virtual-junction PR packaging

Stage 8 of the P6 virtual-junction plan
([odr_p6_virtual_junction_design.md](odr_p6_virtual_junction_design.md) §5) is a
**preparation** stage. This session produced the local branches and text
artifacts; **actual submission to upstream is the user's explicit action**. No
remote was contacted — nothing was fetched, pushed, or opened.

## What was produced

### Tooling (committed on `feature/odr1619-p6-vj`)
- `scripts/strip_gt_markers.py` — removes `[GT_ODR:*]` provenance markers
  (marker-only comment lines deleted; trailing markers trimmed off code lines).
  Idempotent; `--check` verifies zero markers remain; `--selftest` runs inline unit
  tests. Used by the branch generator.
- `scripts/make_vj_pr_branches.py` — generates the four PR branches from tag
  `v3.4.0`. Mechanism: `git diff v3.4.0..HEAD` per 2nd-class file, split into
  per-slice patches by the `vj-*` marker on each hunk's added lines (every git hunk
  maps to exactly one slice), applied cumulatively (stacked), markers stripped,
  clang-format applied if available.
- `scripts/vj_pr_assets.py` — the co-located helper carrying the upstream-clean
  unittest fixture, the per-slice `RoadManager_test.cpp` / `FollowRoute_test.cpp`
  additions, and the PR-D `OSIReporter.cpp` VIRTUAL lane-pairing branch.

### PR text (committed under `GT_esmini/upstream_pr/`)
- `PR-A.md` … `PR-D.md` — upstream-quality PR bodies.
- `issue-592-comment.md` — the design-sketch comment to post on issue #592 first.

### Local PR branches (NOT committed onto our branch — they are separate refs)
Stacked, each based on `v3.4.0`:

| branch | slice | contents |
| --- | --- | --- |
| `pr/vj-a-parse` | A | parse-only (data model + parsing) |
| `pr/vj-b-connect` | B | A + connectivity + membership + OSI classification |
| `pr/vj-c-routing` | C | B + position/path/route/router |
| `pr/vj-d-osi` | D | C + OSIReporter.cpp lane pairing |

They are STACKED (B contains A, etc.) because slice C references symbols
introduced in A/B; stacked branches are the safer review shape.

### Verification (2026-07-05, worktree `E:/Repository/GT_esmini/esmini_prbuild`, build dir `build_pr/`)

Each branch was configured/built from pure v3.4.0 externals (OSI 3.5.0
auto-downloaded — the GT tree's OSI 3.7.0 was NOT reused) and its tests run with
cwd = `build_pr/EnvironmentSimulator/Unittest` (the `../../../` fixture paths
resolve to the worktree root from there):

| branch | build | `VirtualJunctionTest.*` | FollowRoute VJ | full suites |
| --- | --- | --- | --- | --- |
| A | clean | 1/1 | — | — |
| B | clean | 2/2 | — | — |
| C | clean | 5/5 | 1/1 | — |
| D | clean (incl. esminiLib / OSIReporter.cpp) | 5/5 | 1/1 | RoadManager_test **131/131**, FollowRoute_test **21/21** |

The 131/131 full-suite run on D proves behavioural invariance: every
pre-existing upstream test passes with all four slices applied.

## How to regenerate the branches

From `feature/odr1619-p6-vj` with a clean tracked working tree:

```bash
DriverScript/.venv/Scripts/python.exe scripts/make_vj_pr_branches.py
```

The script deletes/recreates `pr/vj-a-parse` … `pr/vj-d-osi` (via `git checkout -B`)
and leaves you on `pr/vj-d-osi`; return with `git checkout feature/odr1619-p6-vj`.
Dry run: `--dry-run` prints the slice map and exits. Base tag / source override:
`--base v3.4.0 --from HEAD`.

> Note: `clang-format` must be on PATH for the touched files to be reformatted; if
> it is absent the generator warns and skips (the pristine source is already
> clang-formatted, so this is cosmetic). The upstream CI style is Google base with
> `AccessModifierOffset: -4` (repo-root `.clang-format`).
>
> Regeneration caveat: `git checkout -B` cannot recreate a branch that is checked
> out in another worktree — detach the prbuild worktree first
> (`git -C E:/Repository/GT_esmini/esmini_prbuild checkout --detach`).

## What the user must do to submit

The branches target upstream `dev`, but are based on the **local** `v3.4.0` tag.
To submit:

1. **Post the issue comment first.** Paste `GT_esmini/upstream_pr/issue-592-comment.md`
   as a comment on esmini issue #592 and wait for maintainer feedback on the two
   interpretation questions (main-road-span membership; `@elementDir` reverse
   composition).
2. **Retarget onto upstream and push to your fork.** For each branch, rebase it
   onto `upstream/dev` (`git rebase --onto upstream/dev v3.4.0 pr/vj-a-parse`,
   then the same for b/c/d onto the rebased parent), resolve any drift, and push
   each to your fork.
3. **Open the PRs in order A → B → C → D**, each targeting `dev`, each declaring
   its parent PR as the base (stacked). Use `PR-A.md` … `PR-D.md` as the bodies.
   PR-B carries the two open questions for maintainer decision.

Do not squash across slices — the stacking is the review unit.
