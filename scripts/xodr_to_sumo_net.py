#!/usr/bin/env python
"""Convert an OpenDRIVE file into a SUMO net.xml, working around what actually breaks.

SCOPE -- WHY THIS IS SHORT
--------------------------
The netconvert trap catalogue (.claude/skills/sumo-authoring/references/
netconvert_traps.md) lists nine traps, but it was built against SUMO 1.27.1 on
macOS with a left-hand-traffic OpenDRIVE 1.8 network. This repository runs SUMO
**1.6.0** on Windows over mostly right-hand-traffic roads, and the catalogue says
plainly that it is unverified here. So rather than porting all nine, every road in
resources/xodr/ was converted and measured, and preprocessing was written for the
one trap that actually reproduced.

Measured 2026-08-30 with the freshly built netconvert 1.6.0, scoring each net with
check_sumo_net_lane_alignment.py (fraction of SUMO lane-shape points that land on
a drivable OpenDRIVE lane):

    fabriksgatan            1.000
    curve_r100              1.000
    jolengatan              1.000
    multi_intersections     0.998
    highway_example...      0.976
    e6mini                  0.750   <-- the one worth fixing

All six converted without errors, so T1 (direct junctions) never fired: only
soderleden.xodr uses one, and it is not in the traffic set. T5 (left-hand traffic)
is handled in the netconvert binary itself, not here -- see
scripts/patch_sumo_lefthand.py.

WHAT e6mini ACTUALLY SUFFERS FROM (and why preprocessing is OFF by default)
--------------------------------------------------------------------------
It looked like the catalogue's T3 (a non-driving lane imported as drivable):
SUMO's built-in type map does give lane type ``stop`` a real speed
(``<type id="stop" speed="13.89" .../>``), so the hard shoulder becomes a running
lane. Rewriting ``stop`` to ``none`` removes it -- and made the score WORSE,
0.750 -> 0.667.

Measuring where each SUMO lane lands explains why. Lane WIDTHS are correct and in
the right order; the whole group is simply one lane too far inward:

    raw      _0 w2.85 -> xodr -4   _1 w3.90 -> -3   _2 w3.50 -> -2   _3 w3.65 -> BORDER
    stop->none  _0 w3.90 -> -3     _1 w3.50 -> -2   _2 w3.65 -> BORDER

e6mini's innermost lane is a 2.60 m ``border``. SUMO does not import border lanes,
and this netconvert does not reserve their width either, so the lane group starts
at the centreline instead of 2.60 m out. Removing MORE lanes cannot fix an offset
-- it just shifts the group further.

That is not any of T1-T9; it is a distinct defect found by measuring here. Fixing
it needs a lateral offset (or keeping the border lane importable purely to hold
its width), which is a bigger change than this script should make silently.

CONSEQUENCE: preprocessing is OPT-IN (--preprocess). The default path converts the
xodr untouched, which is the better-measuring behaviour on every road tested.
Roads whose alignment was measured good (>= 0.998): fabriksgatan, curve_r100,
jolengatan, multi_intersections, highway_example_with_merge_and_split.
e6mini is known-bad at 0.750 either way -- check before trusting traffic on it.

USAGE
    xodr_to_sumo_net.py --xodr R.xodr --out-dir DIR [--lefthand] [--preprocess]
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SUMO_TOOLS = REPO_ROOT / "thirdparty" / "sumo-tools"
NETCONVERT = SUMO_TOOLS / "bin" / "netconvert.exe"
DUAROUTER = SUMO_TOOLS / "bin" / "duarouter.exe"
RANDOM_TRIPS = SUMO_TOOLS / "tools" / "randomTrips.py"
VENV_PY = REPO_ROOT / "DriverScript" / ".venv" / "Scripts" / "python.exe"

# OpenDRIVE lane types that must never become SUMO running lanes. Kept
# deliberately tight: only types measured to cause a problem, plus the ones the
# trap catalogue names for the same reason. Widening this blindly would delete
# lanes that netconvert handles correctly.
# ONLY "stop". This started as {stop, parking, restricted, border, shoulder} --
# the full list the trap catalogue names -- and measuring showed that made e6mini
# WORSE (0.750 -> 0.667). Rewriting the 2.60 m centre `border` lane removes it
# from the import, and netconvert then lays the driving lanes out from the
# centreline instead of from the border's outer edge, shifting the whole group
# inward. The catalogue's claim that "width is preserved so other lanes' t
# offsets do not move" does not hold for this netconvert.
# So: only the type that was measured to cause a problem here.
NON_DRIVING_TYPES = {"stop"}
REPLACEMENT_TYPE = "none"


def preprocess(xodr_in: Path, xodr_out: Path) -> dict:
    """Rewrite non-driving lane types to `none`, preserving geometry.

    Only the ``type`` attribute changes; widths and offsets are untouched, so
    every other lane keeps its exact t-position. That matters -- deleting the
    lane instead would shift everything outboard of it.
    """
    tree = ET.parse(xodr_in)
    root = tree.getroot()
    counts: dict[str, int] = {}
    for lane in root.iter("lane"):
        t = lane.get("type")
        if t in NON_DRIVING_TYPES:
            lane.set("type", REPLACEMENT_TYPE)
            counts[t] = counts.get(t, 0) + 1
    xodr_out.parent.mkdir(parents=True, exist_ok=True)
    tree.write(xodr_out, encoding="utf-8", xml_declaration=True)
    return counts


def run_netconvert(
    xodr: Path, net_out: Path, lefthand: bool
) -> subprocess.CompletedProcess:
    if not NETCONVERT.is_file():
        raise SystemExit(
            f"netconvert not found at {NETCONVERT}\n"
            "Build it first: scripts/generate_sumo_netconvert.sh --lht"
        )
    cmd = [
        str(NETCONVERT),
        "--opendrive",
        str(xodr),
        "-o",
        str(net_out),
        # REQUIRED. Without it SUMO tries to fetch the schema named in the type
        # map's xsi:noNamespaceSchemaLocation over the network and dies with
        # "The types could not be loaded from 'built in type map'", which points
        # at the wrong thing entirely. See generate_sumo_netconvert.sh.
        "--xml-validation",
        "never",
        "--no-turnarounds.except-deadend",
    ]
    if lefthand:
        # Needs the patched netconvert; a stock 1.6.0 lays the lanes out wrong.
        cmd.append("--lefthand")
    net_out.parent.mkdir(parents=True, exist_ok=True)
    return subprocess.run(cmd, capture_output=True, text=True)


def generate_demand(
    net: Path, out_dir: str | Path, stem: str, count: int, seed: int
) -> Path | None:
    """randomTrips -> duarouter -> .rou.xml. Returns the route file, or None.

    Mirrors the hand-run recipe recorded in resources/sumo_inputs/readme.txt,
    which was the only written record of how the shipped demand files were made.
    """
    out_dir = Path(out_dir)
    trips = out_dir / f"{stem}.trips.xml"
    routes = out_dir / f"{stem}.rou.xml"

    env = {**os.environ, "PYTHONPATH": str(SUMO_TOOLS / "tools")}
    rt = subprocess.run(
        [
            str(VENV_PY),
            str(RANDOM_TRIPS),
            "-n",
            str(net),
            "-e",
            str(count),
            "-o",
            str(trips),
            "--seed",
            str(seed),
        ],
        capture_output=True,
        text=True,
        env=env,
    )
    if not trips.is_file():
        print(f"randomTrips failed: {(rt.stderr or rt.stdout)[:300]}", file=sys.stderr)
        return None

    dr = subprocess.run(
        [
            str(DUAROUTER),
            "-n",
            str(net),
            "--route-files",
            str(trips),
            "-o",
            str(routes),
            "--ignore-errors",
            # Same reason as netconvert: schema fetch would be attempted otherwise.
            "--xml-validation",
            "never",
            "--seed",
            str(seed),
        ],
        capture_output=True,
        text=True,
    )
    if not routes.is_file():
        print(f"duarouter failed: {(dr.stderr or dr.stdout)[:300]}", file=sys.stderr)
        return None
    return routes


def write_sumocfg(
    cfg: Path, net: Path, routes: Path, step_length: float = 0.05
) -> None:
    """Minimal .sumocfg referencing net+routes by RELATIVE name.

    Relative on purpose: GT_esmini's scenario sanitizer copies the xosc to a temp
    directory and absolutizes only the paths it knows about; a .sumocfg that names
    its inputs relative to its own location keeps working wherever the trio is
    copied, as long as they stay together.
    """
    cfg.write_text(
        "<?xml version='1.0' encoding='UTF-8'?>\n"
        "<configuration>\n"
        "    <input>\n"
        f'        <net-file value="{net.name}"/>\n'
        f'        <route-files value="{routes.name}"/>\n'
        "    </input>\n"
        "    <time>\n"
        '        <begin value="0"/>\n'
        f'        <step-length value="{step_length}"/>\n'
        "    </time>\n"
        "</configuration>\n",
        encoding="utf-8",
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--xodr", required=True, type=Path)
    ap.add_argument("--out-dir", required=True, type=Path)
    ap.add_argument("--lefthand", action="store_true")
    ap.add_argument(
        "--preprocess",
        action="store_true",
        help="rewrite non-driving lane types before converting. OFF by default: "
        "on the one road where it applies it measured WORSE (see module docstring)",
    )
    ap.add_argument(
        "--demand",
        type=int,
        default=0,
        help="also generate N random trips and a .sumocfg (0 = net only)",
    )
    ap.add_argument(
        "--seed",
        type=int,
        default=42,
        help="randomTrips/duarouter seed; fixed so runs are reproducible",
    )
    args = ap.parse_args()

    if not args.xodr.is_file():
        raise SystemExit(f"xodr not found: {args.xodr}")

    stem = args.xodr.stem
    args.out_dir.mkdir(parents=True, exist_ok=True)

    source = args.xodr
    if args.preprocess:
        prepped = args.out_dir / f"{stem}.prepped.xodr"
        counts = preprocess(args.xodr, prepped)
        if counts:
            detail = ", ".join(f"{k}x{v}" for k, v in sorted(counts.items()))
            print(f"preprocess: lane types -> {REPLACEMENT_TYPE} ({detail})")
        else:
            print("preprocess: no non-driving lane types found (no change)")
        source = prepped

    net_out = args.out_dir / f"{stem}.net.xml"
    proc = run_netconvert(source, net_out, args.lefthand)
    for line in (proc.stdout or "").splitlines():
        if line.strip() and "SUMO_HOME" not in line:
            print(f"  netconvert: {line}")
    if proc.returncode != 0 or not net_out.is_file():
        print((proc.stderr or "").strip()[:800], file=sys.stderr)
        print(f"FAILED: netconvert returned {proc.returncode}", file=sys.stderr)
        return 1

    print(f"wrote {net_out}")

    if args.demand > 0:
        routes = generate_demand(net_out, args.out_dir, stem, args.demand, args.seed)
        if routes is None:
            return 1
        print(f"wrote {routes}")
        cfg = args.out_dir / f"{stem}.sumocfg"
        write_sumocfg(cfg, net_out, routes)
        print(f"wrote {cfg}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
