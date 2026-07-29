"""feature:F7 — run the full per-domain-handover verification matrix and report it.

Every case is run in BOTH controller declaration orders. ScenarioEngine steps
controllers in declaration order, so an implementation that happens to work in
one order and not the other is order-dependent, not correct; a case only counts
as passing when both orders pass. That is why nothing here reports a single run.

The matrix:
  split-stub    both orders, no external input   — the committed CI-safe asset
  split-udp     both orders, steering 0.35 fed to ManualDrive over UDP
  handover      both orders, the working both-domain handover (control case)

Output is one JSON blob per case plus a verdict table. Run it with the venv
interpreter by absolute path:
  DriverScript/.venv/Scripts/python.exe GT_esmini/test/headless/vd_domain_split_matrix.py --outdir <dir>
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vd_domain_split_probe import (  # noqa: E402
    REPO_ROOT,
    assert_config_loaded,
    make_variant,
    read_csv,
    run_scenario,
    summarize,
)

HANDOFF_DIR = REPO_ROOT / "resources" / "xosc" / "verification" / "08_handoff"
SPLIT = HANDOFF_DIR / "scenario_split_domain_md_vd.xosc"
HANDOVER = HANDOFF_DIR / "scenario_full_handover_vd_to_md.xosc"

# Wheel-angle signatures measured on f7_curve_onset.xodr road 4 (R~49 m arc).
# Used to attribute lateral control, not as a pass/fail threshold on their own.
MD_STEER_SIGNATURE_DEG = -0.213
VD_STEER_SIGNATURE_DEG = -0.10


def case(outdir, name, source, swap, md_config=None, udp_steer=None):
    variant = make_variant(
        source,
        Path(outdir) / "scenarios" / f"{name}.xosc",
        swap_controller_order=swap,
        md_config=md_config,
    )
    csv_path = Path(outdir) / f"{name}.csv"
    rc, log_text = run_scenario(
        variant, csv_path, udp_steer=udp_steer, log_path=Path(outdir) / f"{name}.log"
    )
    if rc != 0:
        return {"label": name, "error": f"GT_Sim exit {rc}"}
    # Verify the run measured the controller it claims to. See
    # assert_config_loaded — a missing staged config degrades silently.
    assert_config_loaded(
        log_text,
        md_config or "manual_drive_headless_stub.json",
        "network" if md_config else "stub",
    )
    summary = summarize(read_csv(csv_path), name)
    summary["order"] = "VD_first" if swap else "MD_first"
    summary["udp_steer"] = udp_steer
    summary["md_config"] = md_config
    summary["csv"] = str(csv_path)
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument(
        "--udp-config",
        default="manual_drive_headless_udp.json",
        help="ManualDrive ConfigFile used for the UDP-signature cases",
    )
    ap.add_argument("--only", default=None, help="comma-separated case name filter")
    args = ap.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    plan = [
        ("split_stub_md_first", SPLIT, False, None, None),
        ("split_stub_vd_first", SPLIT, True, None, None),
        ("split_udp_md_first", SPLIT, False, args.udp_config, 0.35),
        ("split_udp_vd_first", SPLIT, True, args.udp_config, 0.35),
        ("handover_stub_md_first", HANDOVER, False, None, None),
        ("handover_stub_vd_first", HANDOVER, True, None, None),
    ]
    if args.only:
        wanted = {s.strip() for s in args.only.split(",")}
        plan = [p for p in plan if p[0] in wanted]

    results = {}
    for name, source, swap, md_config, udp in plan:
        print(f"--- {name} ---", flush=True)
        results[name] = case(
            outdir, name, source, swap, md_config=md_config, udp_steer=udp
        )
        print(json.dumps(results[name], indent=2), flush=True)

    (outdir / "matrix.json").write_text(json.dumps(results, indent=2), encoding="utf-8")

    print("\n=== verdict table ===")
    header = f"{'case':<26}{'ratio_min':>10}{'ratio_max':>10}{'wheel_tail':>12}{'speed_tail':>11}{'lane_off_tail':>14}{'ratio_ok':>10}"
    print(header)
    for name, r in results.items():
        if "error" in r:
            print(f"{name:<26}{r['error']}")
            continue
        print(
            f"{name:<26}{r['ratio_min']:>10}{r['ratio_max']:>10}"
            f"{r['wheel_angle_tail_mean']:>12}{r['speed_tail_mean']:>11}"
            f"{r['lane_offset_tail_mean']:>14}{str(r['ratio_within_tolerance']):>10}"
        )
    print(
        f"\nsignature reference: ManualDrive@0.35 = {MD_STEER_SIGNATURE_DEG} deg, "
        f"VirtualDriver = {VD_STEER_SIGNATURE_DEG} deg"
    )
    print(f"matrix written to {outdir / 'matrix.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
