#!/usr/bin/env python3
"""Verify that the VD's partner-id diagnostics actually join against OSI.

WHY THIS EXISTS (and why a unit test cannot replace it)
-------------------------------------------------------
The policies publish `gt.<policy>.*_osi_id` diagnostics (and `overtake.lead_osi_id`)
claiming to name the vehicle / pedestrian / crosswalk the VD is reacting to, in
the id space an OSI consumer can join on. Both sides of that claim are assigned
at RUNTIME from one global counter (CommonMini GetNewGlobalId), so a unit test
comparing two constants proves nothing about it: it would pass just as happily if
the emitter shipped the scenario entity index instead -- which is exactly the bug
this whole change set exists to fix.

The only evidence that settles it is a real run where the SAME frame carries both
the VD's claim and the OSI GroundTruth, checked against each other. That is what
this script does. It consumes gt_sim_test.py's `--osi` capture, which pulls the
GroundTruth IN-PROCESS via SE_GetOSIGroundTruth (no socket, so no dropped-packet
failure mode to misread as a clean pass).

WHAT IS CHECKED, per frame that carries a scene
-----------------------------------------------
  existence   every claimed id >= 0 resolves to an object in the right half of
              the GroundTruth (moving for vehicles/pedestrians, stationary for
              the crosswalk).
  identity    gt.crosswalk.object_osi_id's stationary object must carry
              source_reference `object_id:<n>` equal to gt.crosswalk.object_id.
              Two independently-emitted fields agreeing is a real identity
              check, not a plausibility one.
  geometry    gt.lead_vehicle / gt.aeb: the distance from the ego to the named
              object must agree with the policy's own gap_m plus the two half
              lengths. Naming the wrong vehicle moves this by whole car lengths.
  proximity   gt.conflict_point / gt.crosswalk.ped: the named object must be
              within `--proximity-m` of the ego. Deliberately WEAK -- it rejects
              a wrong id space (whose ids resolve to arbitrary objects) but does
              not pin the exact partner. Reported as such.
  non-empty   every key must have been claimed on at least one frame. A run
              where a key never appears proves nothing about it, so it is
              reported as UNEXERCISED rather than being counted as a pass.

Usage
-----
    verify_osi_id_join.py <run_dir | telemetry.jsonl> [--report out.json]
    verify_osi_id_join.py <scenario.xosc> --run-out <dir> [--max-time 30]

Exit code 0 only when every claim checked out AND at least one key was
exercised; 1 on any mismatch; 2 on a usage/IO error.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

THIS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(THIS_DIR))

# Every partner-id key the VD can emit. Listed explicitly so that a key which
# NEVER appears in a run is still reported: a stats table built only from what
# was observed cannot tell "verified" from "never happened", which is the same
# false-PASS shape the `considered` flag exists to prevent elsewhere.
KNOWN_KEYS = (
    "gt.conflict_point.other_osi_id",
    "gt.crosswalk.object_osi_id",
    "gt.crosswalk.ped_osi_id",
    "gt.lead_vehicle.lead_osi_id",
    "gt.aeb.lead_osi_id",
    "overtake.lead_osi_id",
)

# Keys that name a STATIONARY OSI object; everything else *_osi_id names a
# moving one. Kept as an explicit set: a new key defaulting to "moving" is a
# loud failure here, which is the right way round.
STATIONARY_KEYS = {"gt.crosswalk.object_osi_id"}

# Keys whose distance to the ego is pinned by a gap the same policy publishes.
# key -> (gap detail key, tolerance [m])
GEOMETRY_KEYS = {
    "gt.lead_vehicle.lead_osi_id": ("gt.lead_vehicle.gap_m", 1.5),
    "gt.aeb.lead_osi_id": ("gt.aeb.gap_m", 1.5),
}


def _iter_frames(jsonl: Path):
    with open(jsonl, "r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield line_no, json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"{jsonl}:{line_no}: malformed telemetry line: {exc}")


def _claims(frame: dict) -> list[tuple[str, int]]:
    """Every partner-id claim in one frame, as (key, value)."""
    out: list[tuple[str, int]] = []
    detail = (frame.get("policy") or {}).get("detail") or {}
    for key, raw in detail.items():
        if not key.endswith("_osi_id"):
            continue
        try:
            out.append((key, int(raw)))
        except (TypeError, ValueError):
            out.append((key, -2))  # -2 == unparseable, reported as a mismatch
    overtake = frame.get("overtake") or {}
    if "lead_osi_id" in overtake:
        out.append(("overtake.lead_osi_id", int(overtake["lead_osi_id"])))
    return out


def _ego(scene: dict) -> dict | None:
    for obj in scene.get("objects", []):
        if obj.get("is_host"):
            return obj
    return None


def verify(jsonl: Path, proximity_m: float) -> dict:
    stats: dict[str, dict] = {
        k: {"claimed": 0, "checked": 0, "mismatched": 0, "no_partner": 0}
        for k in KNOWN_KEYS
    }
    findings: list[dict] = []
    warnings: list[dict] = []
    frames_total = 0
    frames_with_scene = 0
    # Stationary objects ride on ONE frame only (gt_sim_test attaches the static
    # catalogue to the first scene that carries it and never repeats it), so the
    # table has to accumulate across the run. Looking it up per-frame finds an
    # empty list on every later frame and reports a perfectly good crosswalk id
    # as "not present" -- a checker bug that reads exactly like the code bug it
    # is supposed to detect.
    stationary: dict[int, dict] = {}

    for line_no, frame in _iter_frames(jsonl):
        frames_total += 1
        scene = frame.get("scene")
        claims = _claims(frame)
        for key, _ in claims:
            stats.setdefault(
                key, {"claimed": 0, "checked": 0, "mismatched": 0, "no_partner": 0}
            )
        if not scene:
            # No GroundTruth on this frame -> nothing to join against. Not a
            # failure by itself; the summary reports how many frames were
            # actually checkable so a run captured without --osi cannot pass
            # by having nothing to disagree with.
            continue
        frames_with_scene += 1

        moving = {o["id"]: o for o in scene.get("objects", [])}
        for so in scene.get("stationary_objects", []):
            stationary[so["id"]] = so
        ego = _ego(scene)
        detail = (frame.get("policy") or {}).get("detail") or {}

        for key, value in claims:
            st = stats[key]
            if value == -1:
                st["no_partner"] += 1
                continue
            st["claimed"] += 1

            def fail(reason: str, **extra):
                st["mismatched"] += 1
                findings.append(
                    {
                        "line": line_no,
                        "sim_time": frame.get("sim_time"),
                        "key": key,
                        "value": value,
                        "reason": reason,
                        **extra,
                    }
                )

            if value < -1:
                fail("unparseable value in telemetry")
                continue

            table = stationary if key in STATIONARY_KEYS else moving
            half = "stationary_objects" if key in STATIONARY_KEYS else "objects"
            target = table.get(value)
            if target is None:
                fail(
                    f"id not present in scene.{half}",
                    available=sorted(table.keys()),
                )
                continue
            st["checked"] += 1

            # --- identity: the crosswalk's ODR id must agree with the policy's
            #     own object_id field (two independent emissions of the same
            #     object).
            if key == "gt.crosswalk.object_osi_id":
                odr_claimed = detail.get("gt.crosswalk.object_id")
                odr_osi = target.get("odr_object_id")
                if odr_claimed is not None and odr_osi is not None:
                    if int(odr_claimed) != int(odr_osi):
                        fail(
                            "OSI stationary object's odr_object_id disagrees with "
                            "gt.crosswalk.object_id",
                            odr_claimed=int(odr_claimed),
                            odr_from_osi=int(odr_osi),
                        )
                        continue
                # NOT a failure when false: gt_sim_test's `is_crosswalk` means
                # "provably a crosswalk", and it goes false for a telemetry
                # capture predating the odr_type identifier. Treating it as a
                # mismatch would fail runs for a missing label rather than a
                # wrong id.
                if target.get("is_crosswalk") is False:
                    warnings.append(
                        {
                            "line": line_no,
                            "key": key,
                            "value": value,
                            "note": "stationary object is not provably a crosswalk",
                            "odr_type": target.get("odr_type"),
                        }
                    )

            if ego is None:
                continue  # no host in the GroundTruth -> skip the metric checks

            dist = math.hypot(target["x"] - ego["x"], target["y"] - ego["y"])

            # --- geometry: distance must match the policy's own gap.
            if key in GEOMETRY_KEYS:
                gap_key, tol = GEOMETRY_KEYS[key]
                gap = detail.get(gap_key)
                if gap is not None:
                    bodies = 0.5 * float(ego.get("length", 4.0)) + 0.5 * float(
                        target.get("length", 4.0)
                    )
                    expected = float(gap) + bodies
                    # A gap of exactly 0 is CLAMPED, not measured: both policies
                    # do `if (gap < 0) gap = 0`, so from there on the number says
                    # "no freespace left" and carries no distance any more.
                    # Demanding equality there fails every interpenetrating frame
                    # of a collision scenario while the id is perfectly correct --
                    # so the check weakens to its still-falsifiable half (the
                    # named body must actually be that close) instead of having
                    # its tolerance widened until the real cases stop failing.
                    if float(gap) > 0.0:
                        if abs(dist - expected) > tol:
                            fail(
                                f"distance to the named object disagrees with {gap_key}",
                                distance_m=round(dist, 3),
                                expected_m=round(expected, 3),
                                tolerance_m=tol,
                            )
                            continue
                    elif dist > expected + tol:
                        fail(
                            f"{gap_key} is clamped to 0 (no freespace) but the named "
                            "object is not within the two half-lengths",
                            distance_m=round(dist, 3),
                            bodies_m=round(bodies, 3),
                            tolerance_m=tol,
                        )
                        continue

            # --- proximity (weak): a partner must at least be near the ego.
            elif dist > proximity_m:
                fail(
                    "named object is not near the ego (weak check)",
                    distance_m=round(dist, 3),
                    proximity_m=proximity_m,
                )
                continue

    unexercised = sorted(k for k, v in stats.items() if v["claimed"] == 0)
    return {
        "telemetry": str(jsonl),
        "frames_total": frames_total,
        "frames_with_scene": frames_with_scene,
        "keys": stats,
        "unexercised_keys": unexercised,
        "findings": findings,
        "warnings": warnings,
    }


def _print_report(report: dict) -> None:
    print(f"[osi-id-join] {report['telemetry']}")
    print(
        f"  frames: {report['frames_total']} total, "
        f"{report['frames_with_scene']} with an OSI scene"
    )
    if not report["keys"]:
        print("  no *_osi_id claims found at all")
    for key, st in sorted(report["keys"].items()):
        verdict = (
            "MISMATCH"
            if st["mismatched"]
            else ("unexercised" if st["claimed"] == 0 else "ok")
        )
        print(
            f"  {key:38s} {verdict:12s} "
            f"claimed={st['claimed']} joined={st['checked']} "
            f"mismatched={st['mismatched']} no_partner={st['no_partner']}"
        )
    for f in report["findings"][:20]:
        extra = {
            k: v
            for k, v in f.items()
            if k not in ("line", "sim_time", "key", "value", "reason")
        }
        print(
            f"    ! t={f['sim_time']} {f['key']}={f['value']}: {f['reason']}"
            + (f" {extra}" if extra else "")
        )
    if len(report["findings"]) > 20:
        print(f"    ... and {len(report['findings']) - 20} more")
    if report["warnings"]:
        print(f"  {len(report['warnings'])} warning(s), first: {report['warnings'][0]}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("target", help="run dir, telemetry.jsonl, or a .xosc to run first")
    ap.add_argument("--run-out", help="output dir when TARGET is a .xosc")
    ap.add_argument(
        "--policies",
        default="",
        help="comma-separated VD policies to enable for the run (same names as a "
        "batch manifest's `policies:` list, e.g. lead,aeb,crosswalk). They "
        "default OFF in config, so without this most partner-id keys are never "
        "emitted at all.",
    )
    ap.add_argument("--dt", type=float, default=0.05)
    ap.add_argument("--max-time", type=float, default=30.0)
    ap.add_argument(
        "--proximity-m",
        type=float,
        default=400.0,
        help="weak-check radius for partners without a published gap. Default "
        "derived, not guessed: ConflictPointResolver predicts BOTH corridors "
        "over its 120 m lookahead and yields on their overlap, so two vehicles "
        "sharing a conflict region are legitimately up to ~240 m apart; the "
        "overtake scan reaches 400 m for oncoming traffic. A tighter radius "
        "fails the opening frames of a normal oncoming-yield approach.",
    )
    ap.add_argument("--report", help="write the full JSON report here")
    ap.add_argument(
        "--expect",
        default="",
        help="comma-separated keys this run MUST exercise (e.g. "
        "gt.crosswalk.ped_osi_id). No scenario exercises all of them, so the "
        "caller states which ones this one is evidence for; a listed key that "
        "never appears fails the run.",
    )
    args = ap.parse_args(argv)

    target = Path(args.target)
    if target.suffix == ".xosc":
        if not args.run_out:
            ap.error("--run-out is required when TARGET is a scenario")
        import gt_sim_test  # local import: only needed in run mode

        out_dir = Path(args.run_out)
        scenario = target
        policies = [p.strip() for p in args.policies.split(",") if p.strip()]
        if policies:
            # Reuse gt_sim_test's own config-injection path rather than a second
            # one: the policies are opt-in via an ABSOLUTE ConfigFile property on
            # the controller, and having two implementations of that is how the
            # two drift apart.
            out_dir.mkdir(parents=True, exist_ok=True)
            # ABSOLUTE on purpose: ControllerVirtualDriver resolves a relative
            # ConfigFile against the DLL directory, so a relative path here loads
            # nothing and the run silently proceeds on built-in defaults (i.e.
            # every policy OFF) -- a "no claims" result that looks like a code
            # bug rather than a harness one.
            cfg = gt_sim_test._write_policy_config(
                policies, (out_dir / "virtual_driver.run.json").resolve()
            )
            scenario = gt_sim_test._prepare_policy_xosc(target, out_dir, cfg)

        gt_sim_test.run(
            scenario=scenario,
            out_dir=out_dir,
            dt=args.dt,
            max_time=args.max_time,
            snapshots=1,
            dll=None,
            capture_osi=True,
        )
        jsonl = out_dir / "telemetry.jsonl"
    elif target.is_dir():
        jsonl = target / "telemetry.jsonl"
    else:
        jsonl = target

    if not jsonl.is_file():
        print(f"no telemetry at {jsonl}", file=sys.stderr)
        return 2

    report = verify(jsonl, args.proximity_m)
    _print_report(report)
    if args.report:
        Path(args.report).write_text(json.dumps(report, indent=2), encoding="utf-8")

    rc = 0
    if report["findings"]:
        rc = 1
    if report["frames_with_scene"] == 0:
        print(
            "  FAIL: no frame carried an OSI scene (was the run captured with --osi?)",
            file=sys.stderr,
        )
        rc = 1

    expected = [k.strip() for k in args.expect.split(",") if k.strip()]
    unknown = [k for k in expected if k not in KNOWN_KEYS]
    if unknown:
        print(f"  unknown key(s) in --expect: {unknown}", file=sys.stderr)
        return 2
    missing = [k for k in expected if k in report["unexercised_keys"]]
    if missing:
        print(
            "  FAIL: no frame ever claimed "
            + ", ".join(missing)
            + " -- an unexercised key is not a verified one",
            file=sys.stderr,
        )
        rc = 1
    if not expected and all(v["claimed"] == 0 for v in report["keys"].values()):
        print(
            "  FAIL: not a single partner-id claim in the whole run -- this run "
            "is evidence for nothing",
            file=sys.stderr,
        )
        rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
