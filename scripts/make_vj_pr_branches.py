#!/usr/bin/env python3
"""make_vj_pr_branches.py -- generate the four upstream virtual-junction PR branches.

The GT_esmini fork implements native OpenDRIVE virtual-junction (VJ) support as
in-place edits to a handful of pristine core files, each hunk annotated with a
``[GT_ODR:vj-*]`` marker (design doc: GT_esmini/docs/odr_p6_virtual_junction_design.md
section 5 "PR packaging"). For upstream review the work is split into four
STACKED slices (each branch contains the ones before it):

    pr/vj-a-parse    parse-only (RoadLink/Junction/Connection model + parse)
    pr/vj-b-connect  connectivity + membership + OSI classification (+ registry)
    pr/vj-c-routing  position / path / route / router
    pr/vj-d-osi      OSIReporter.cpp lane-pairing mirror

Each branch is created FROM tag v3.4.0 (so no GT_esmini/ files exist on it at
all), the slice's hunks are applied on top of the pristine v3.4.0 sources, the
``[GT_ODR:*]`` markers are stripped (scripts/strip_gt_markers.py), the per-slice
upstream unittest fixture + RoadManager_test.cpp assertions are added, and the
touched files are clang-formatted if a .clang-format is present at the repo root.

MECHANICS -- the applied hunks are computed as a git diff between tag v3.4.0 and
the CURRENT HEAD (the pristine 2nd-class files carry the full implementation with
markers). Every git-diff hunk maps to exactly one slice (verified: zero mixed-id
hunks), routed by the vj marker id on its added lines. The RoadManager.hpp
data-model file is the one exception: it is single-id (vj-model) but its registry
declarations and PathNode.contact_s belong to later slices, so those specific
hunks are routed by their new-file offset (see HPP_HUNK_ROUTING).

This script does NOT touch any remote: no fetch, push, or PR creation. It only
produces local branches. Actual submission is the user's explicit action
(see GT_esmini/docs/odr_p6_s8_handoff.md).

Usage:
    make_vj_pr_branches.py [--base v3.4.0] [--from HEAD] [--dry-run]
"""
import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STRIP = os.path.join(REPO, "scripts", "strip_gt_markers.py")

# 2nd-class core files carrying the VJ implementation (pristine copies).
CORE_FILES = [
    "EnvironmentSimulator/Modules/RoadManager/RoadManager.hpp",
    "EnvironmentSimulator/Modules/RoadManager/RoadManager.cpp",
    "EnvironmentSimulator/Modules/RoadManager/LaneIndependentRouter.cpp",
    "EnvironmentSimulator/Modules/RoadManager/LaneIndependentRouter.hpp",
    "EnvironmentSimulator/Modules/Controllers/ControllerLooming.cpp",
]

# vj marker id -> slice letter. Prefix match (vj-parse-link / vj-parse-junction -> vj-parse).
ID_TO_SLICE = {
    "vj-parse": "A",
    "vj-model": "A",  # default for the data-model file; overridden per-hunk below
    "vj-synth": "B",
    "vj-membership": "B",
    "vj-osi": "B",  # vj-osi-class
    "vj-path": "C",
    "vj-route": "C",
    "vj-connect": "C",
    "vj-move": "C",
    "vj-enter": "C",
    "vj-lanes": "C",
    "vj-router": "C",
    "vj-looming": "C",
}

# RoadManager.hpp: route specific vj-model hunks (identified by their +new-start
# line in the v3.4.0->HEAD diff) to later slices. Everything else stays in A.
# 3660 = VirtualJunctionAnchor struct + GetVirtualJunctionAtRoadS/Anchors accessors
# 3821 = virtual_junction_anchors_ registry member + EstablishVirtualJunctionConnections decl
# 5293 = RoadPath::PathNode::contact_s
HPP_HUNK_ROUTING = {
    3660: "B",
    3821: "B",
    5293: "C",
}

SLICES = ["A", "B", "C", "D"]
BRANCH = {
    "A": "pr/vj-a-parse",
    "B": "pr/vj-b-connect",
    "C": "pr/vj-c-routing",
    "D": "pr/vj-d-osi",
}
# Cumulative slice membership (stacked): B contains A, C contains A+B, D contains A+B+C.
CUMULATIVE = {
    "A": {"A"},
    "B": {"A", "B"},
    "C": {"A", "B", "C"},
    "D": {"A", "B", "C"},  # D adds only the OSI mirror (a fresh file edit, not a slice hunk)
}

MARKER_RE = re.compile(r"\[GT_ODR:(vj-[a-z]+)")
HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@")


def run(cmd, **kw):
    kw.setdefault("cwd", REPO)
    kw.setdefault("check", True)
    kw.setdefault("text", True)
    kw.setdefault("encoding", "utf-8")
    kw.setdefault("errors", "replace")
    return subprocess.run(cmd, **kw)


def out(cmd, **kw):
    kw.setdefault("cwd", REPO)
    kw.setdefault("text", True)
    kw.setdefault("encoding", "utf-8")
    kw.setdefault("errors", "replace")
    return subprocess.run(cmd, capture_output=True, **kw).stdout


def slice_for_id(vjid):
    for k, v in ID_TO_SLICE.items():
        if vjid.startswith(k):
            return v
    raise SystemExit(f"unknown vj marker id: {vjid}")


def split_diff_by_slice(base, head, path):
    """Return {slice_letter: unified-diff-text} for one file's v3.4.0->HEAD diff.

    The diff header (--- / +++ / index) is prepended to every non-empty slice so
    each slice's text is an independently-applicable patch for that file.
    """
    raw = out(["git", "diff", "--no-color", base, head, "--", path])
    if not raw.strip():
        return {}
    lines = raw.splitlines(keepends=True)
    # locate the header (up to and including the +++ line)
    hdr_end = 0
    for i, ln in enumerate(lines):
        if ln.startswith("+++ "):
            hdr_end = i + 1
            break
    header = lines[:hdr_end]
    is_hpp = path.endswith("RoadManager.hpp")

    per_slice = {}
    cur_lines = []
    cur_slice = None

    def flush():
        if cur_slice and cur_lines:
            per_slice.setdefault(cur_slice, []).extend(cur_lines)

    for ln in lines[hdr_end:]:
        m = HUNK_RE.match(ln)
        if m:
            flush()
            cur_lines = [ln]
            new_start = int(m.group(1))
            # determine slice from the marker id on the hunk's added lines
            # (peek forward is easier: accumulate then classify at flush). We
            # classify lazily: read this hunk fully first.
            cur_slice = None
            cur_new_start = new_start
            continue
        if cur_lines:
            cur_lines.append(ln)
            if cur_slice is None:
                mm = MARKER_RE.search(ln)
                if mm and ln.startswith("+"):
                    sl = slice_for_id(mm.group(1))
                    if is_hpp and cur_new_start in HPP_HUNK_ROUTING:
                        sl = HPP_HUNK_ROUTING[cur_new_start]
                    cur_slice = sl
    flush()

    result = {}
    for sl, body in per_slice.items():
        result[sl] = "".join(header) + "".join(body)
    return result


def cumulative_patch(base, head, wanted_slices):
    """Concatenate per-file slice patches for all wanted slices into one patch."""
    chunks = []
    for path in CORE_FILES:
        by_slice = split_diff_by_slice(base, head, path)
        file_chunks = [by_slice[s] for s in ("A", "B", "C") if s in wanted_slices and s in by_slice]
        if not file_chunks:
            continue
        # A file may contribute hunks to several slices; git apply needs them in
        # ascending file order within one patch. Rebuild by merging the hunks of
        # all wanted slices for this file, sorted by +new-start.
        chunks.append(merge_file_slices(file_chunks))
    return "\n".join(c for c in chunks if c.strip())


def merge_file_slices(file_chunks):
    """Given several single-file diff texts (same header), merge their hunks in
    ascending +new-start order into one applicable patch."""
    header = []
    hunks = []  # (new_start, text)
    for ch in file_chunks:
        lines = ch.splitlines(keepends=True)
        hdr_end = 0
        for i, ln in enumerate(lines):
            if ln.startswith("+++ "):
                hdr_end = i + 1
                break
        if not header:
            header = lines[:hdr_end]
        cur = None
        start = None
        for ln in lines[hdr_end:]:
            m = HUNK_RE.match(ln)
            if m:
                if cur is not None:
                    hunks.append((start, "".join(cur)))
                cur = [ln]
                start = int(m.group(1))
            elif cur is not None:
                cur.append(ln)
        if cur is not None:
            hunks.append((start, "".join(cur)))
    hunks.sort(key=lambda t: t[0])
    return "".join(header) + "".join(h for _, h in hunks)


def clang_format(paths):
    if not os.path.exists(os.path.join(REPO, ".clang-format")):
        return
    exe = "clang-format"
    try:
        for p in paths:
            run([exe, "-i", p])
    except (FileNotFoundError, subprocess.CalledProcessError) as e:
        print(f"  [warn] clang-format skipped: {e}", file=sys.stderr)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default="v3.4.0", help="upstream base tag (default v3.4.0)")
    ap.add_argument("--from", dest="head", default="HEAD", help="branch/commit holding the full impl (default HEAD)")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, do not create branches")
    args = ap.parse_args(argv)

    base_sha = out(["git", "rev-parse", args.base]).strip()
    head_sha = out(["git", "rev-parse", args.head]).strip()
    print(f"base {args.base} = {base_sha[:10]}   from {args.head} = {head_sha[:10]}")

    # Precompute per-file, per-slice patch text at the current HEAD.
    plan = {}
    for path in CORE_FILES:
        plan[path] = split_diff_by_slice(base_sha, head_sha, path)
    print("\nslice map (added-hunk count per file):")
    for path in CORE_FILES:
        counts = {s: plan[path][s].count("\n@@ ") + (1 if plan[path].get(s, "").startswith("--- ") and "@@ " in plan[path][s] else 0) for s in plan[path]}
        print(f"  {os.path.basename(path):28s} { {s: plan[path][s].count('@@ -') for s in plan[path]} }")

    if args.dry_run:
        print("\n--dry-run: no branches created")
        return 0

    # asset payloads (fixture + test additions + OSI mirror) live in the co-located helper
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from vj_pr_assets import apply_assets  # noqa: E402

    for sl in SLICES:
        br = BRANCH[sl]
        print(f"\n=== {br} (slice {sl}) ===")
        run(["git", "checkout", "-B", br, base_sha])
        touched = []
        # apply cumulative code slices A..C
        wanted = CUMULATIVE[sl]
        for path in CORE_FILES:
            file_chunks = [plan[path][s] for s in ("A", "B", "C") if s in wanted and s in plan[path]]
            if not file_chunks:
                continue
            patch = merge_file_slices(file_chunks)
            proc = subprocess.run(["git", "apply", "--whitespace=nowarn", "-"], cwd=REPO, input=patch, text=True, encoding="utf-8", errors="replace")
            if proc.returncode != 0:
                raise SystemExit(f"git apply failed for {path} on {br}")
            touched.append(path)
        # strip markers from the touched files
        if touched:
            run([sys.executable, STRIP] + [os.path.join(REPO, p) for p in touched])
        # slice-D writes the OSI mirror fresh (asset step handles it)
        asset_files = apply_assets(REPO, sl)
        touched += asset_files
        clang_format([os.path.join(REPO, p) for p in touched])
        run(["git", "add"] + touched)
        msg = COMMIT_MSG[sl]
        run(["git", "commit", "--no-verify", "-m", msg])
        print(f"  committed {br}: {len(touched)} file(s)")

    print("\nDone. Branches: " + ", ".join(BRANCH[s] for s in SLICES))
    print("Nothing was pushed. See GT_esmini/docs/odr_p6_s8_handoff.md to submit.")
    return 0


COMMIT_MSG = {
    "A": """Add OpenDRIVE virtual junction parsing (ASAM 1.7+)

Parse <junction type="virtual"> with its @mainRoad/@sStart/@sEnd/@orientation
span and the virtual <connection> anchors (<predecessor>/<successor> with
@elementId/@elementS/@elementDir), and read the optional @elementS/@elementDir
mid-road contact on <road><link>. Virtual junctions and connection-less virtual
connections no longer abort the parse or emit "not supported" warnings.

Data model: RoadLink gains element_s_/element_dir_, Junction a virtual-attribute
span, Connection incoming/outgoing anchor s and an is-virtual flag. Registry and
runtime consumption follow in later commits; parsing alone is behaviour-neutral
for every non-virtual map.

Refs ASAM OpenDRIVE 10.4; issue #592.""",
    "B": """Resolve virtual junction connectivity and OSI classification

After CheckConnections(), bind each virtual junction's branch-road elementS links
to its connections, synthesize the missing branch->main counter-connections and
build a per-main-road anchor registry (empty for every map without virtual
junctions, so the cost is zero when the feature is absent). A virtual junction is
classified as a non-intersection (IsOsiIntersection == false), matching the direct
junction precedent, and the unsplit main-road span keeps reporting no junction id.

Refs ASAM OpenDRIVE 10.4; issue #592.""",
    "C": """Route and move across virtual junctions

Teach RoadPath, Route, the position stepping (MoveAlongS/MoveToConnectingRoad) and
LaneIndependentRouter to traverse a virtual junction: branch off the main road at a
mid-road anchor and merge back onto it, computing partial edge weights and landing
positions from the anchor s. Straight-through motion on the unsplit main road is
unchanged; the branch logic activates only when a route or path selects it.

Refs ASAM OpenDRIVE 10.4; issue #592.""",
    "D": """Pair virtual junction lanes in OSI output

Mirror the direct-junction per-laneLink lane pairing for virtual junctions in
UpdateOSIIntersection, resolving the main-road lane at the connection anchor s.
Branch lanes keep their driving type and pair across the anchor instead of being
emitted as intersection lanes.

Refs ASAM OpenDRIVE 10.4; issue #592.""",
}


if __name__ == "__main__":
    sys.exit(main())
