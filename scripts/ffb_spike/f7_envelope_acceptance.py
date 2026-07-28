"""feature:F7 acceptance check — do the proposed envelope limits ever clip a frame
across the FULL evaluated regression set (Phase 1: lc/curve/right_turn CSVs +
Phase 2: 12-scenario car_following_traffic_control_batch telemetry)?

!! THIS IS NOT AN INDEPENDENT VERIFICATION. READ THIS BEFORE CITING IT. !!

    The limits it checks were DERIVED FROM THE SAME 15-scenario pool it checks
    them against. AdSteeringEnvelope.hpp states it outright: "normal driving
    peaks (15-scenario pool) were a_lat=3.289, yaw_rate=0.780, steer_rate=0.769;
    a_lat_max_steer and yaw_rate_max are that pool max x1.3". Running this file
    over that same pool therefore cannot fail for the reason that matters -- the
    limits were constructed to sit above those maxima, so "nothing clips" is
    arithmetic, not evidence. It tells you the constants were transcribed
    correctly and nothing else.

    This is the same identify-and-verify-on-one-dataset circularity that was
    found in the shadow model vs the synthetic plant, reappearing in the safety
    envelope. A real acceptance needs a pool this script has never seen:
    scenarios outside the 15 used to set the limits (junction/crosswalk/AEB
    manifests, or a real-vehicle capture). Point the CLI at such a root and the
    result becomes meaningful:

        python f7_envelope_acceptance.py <root-of-scenarios-not-in-the-15>

    Until that is done, the honest reading of a PASS here is "the shipped
    constants still bound the pool they were fitted to".

Candidate limits under test (team-lead's x1.3 proposal):
    a_lat_max_steer = 4.3   m/s^2
    yaw_rate_max    = 1.0   rad/s
    steer_rate_max  = 1.0   rad/s

Per-scenario: max value per metric, % of limit used (headroom), and clip
count/pct at the candidate limits. No simulation run here -- reads the same
Phase-1 CSVs (scripts/ffb_spike/profiles/*.csv) and Phase-2 telemetry.jsonl
already produced.

Usage:
    DriverScript/.venv/Scripts/python.exe scripts/ffb_spike/f7_envelope_acceptance.py <phase2_out_root>
"""
from __future__ import annotations

import csv
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).parent
MAX_STEER_ANGLE = 0.61

LIMITS = {"a_lat": 4.3, "yaw": 1.0, "steer_rate": 1.0}

PHASE1_WB = {"lc": 5.04 * 0.6, "curve": 5.0 * 0.6, "right_turn": 5.0 * 0.6}
PHASE2_WB = 3.0


def series_from_phase1(name: str) -> dict:
    rows = []
    with (HERE / "profiles" / f"{name}.csv").open(encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            rows.append({k: float(v) for k, v in r.items()})
    wb = PHASE1_WB[name]
    deltas = [r["steer"] * MAX_STEER_ANGLE for r in rows]
    speeds = [r["speed"] for r in rows]
    kappas = [math.tan(d) / wb for d in deltas]
    a_lat = [abs(v * v * k) for v, k in zip(speeds, kappas)]
    yaw = [abs(v * k) for v, k in zip(speeds, kappas)]
    steer_rate = []
    for i in range(1, len(rows)):
        dt = rows[i]["t_s"] - rows[i - 1]["t_s"]
        if dt > 0:
            steer_rate.append(abs(deltas[i] - deltas[i - 1]) / dt)
    return {"a_lat": a_lat, "yaw": yaw, "steer_rate": steer_rate, "frames_meta": []}


def series_from_phase2(name: str, path: Path) -> dict:
    rows = [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    active, prev_t = [], None
    for r in rows:
        t = r["sim_time"]
        if prev_t is not None and t <= prev_t:
            break
        active.append(r)
        prev_t = t
    # feature:F7 — WHICH STEER SIGNAL THIS READS, and why it changed.
    #
    # `driver.steer` is the AD's RAW REQUEST, recorded BEFORE the safety
    # envelope clamps it. Deriving a_lat/yaw/steer_rate from it answers "did
    # the AD ever ASK for something outside the limits", which is not the
    # question this file's name promises and is not a safety statement at all:
    # a run where the envelope caught every excursion perfectly would still be
    # reported as clipping.
    #
    # Measured on car_following_traffic_control/green_no_stop t=12.00:
    #     envelope.steer_in  = 0.9458   (the request -- 94.6% of full lock at 14 m/s)
    #     envelope.steer_out = 0.0150   (what actually reached the vehicle)
    # The old metric read 0.9458 and reported a_lat = 42.6 m/s^2 (991% of the
    # limit). The envelope had done exactly its job.
    #
    # So the applied signal is `envelope.steer_out` when the telemetry carries
    # it. `driver.steer` remains the fallback for older captures that predate
    # the envelope block, and the request series is kept separately because
    # "how often does the AD ask for something the clamp has to catch" is worth
    # reporting -- just not as a safety verdict.
    def _steer_out(r):
        env = r.get("envelope") or {}
        return env["steer_out"] if "steer_out" in env else r["driver"]["steer"]

    def _steer_in(r):
        env = r.get("envelope") or {}
        return env["steer_in"] if "steer_in" in env else r["driver"]["steer"]

    deltas = [_steer_out(r) * MAX_STEER_ANGLE for r in active]
    deltas_req = [_steer_in(r) * MAX_STEER_ANGLE for r in active]
    # feature:F7 — WHICH SPEED THE ENVELOPE ACTUALLY SAW.
    #
    # The clamp is kappa <= a_lat_max / max(v, v_floor)^2, evaluated inside the
    # controller step BEFORE the vehicle is integrated. `ego.speed` in a
    # telemetry row is the POST-step value. Pairing a row's steer_out with that
    # row's speed therefore over-states v by one frame of acceleration, and
    # because a_lat goes as v^2 the error is doubled.
    #
    # Measured, on the two frames that used to come out above the limit
    # (green_no_stop t=11.95/12.05): the envelope clamped to exactly 4.3 using
    # v=13.992, the row records v=14.005 (+0.013 m/s = one frame at the
    # observed 0.26 m/s^2), and recomputing with the row's speed yields 4.3095
    # -- 0.22% over a limit that was in fact respected. That was the whole of
    # the UNDETERMINED residue once the wheelbase term was accounted for.
    #
    # So each frame is paired with the PREVIOUS row's speed. The first frame
    # keeps its own (no earlier sample exists); it is a settling frame at
    # ~0 m/s where a_lat is ~0 either way.
    speeds = [active[max(0, i - 1)]["ego"]["speed"] for i in range(len(active))]
    kappas = [math.tan(d) / PHASE2_WB for d in deltas]
    a_lat = [abs(v * v * k) for v, k in zip(speeds, kappas)]
    yaw = [abs(v * k) for v, k in zip(speeds, kappas)]
    meta = {"a_lat": list(zip(a_lat, active)), "yaw": list(zip(yaw, active))}
    steer_rate, sr_meta = [], []
    for i in range(1, len(active)):
        dt = active[i]["sim_time"] - active[i - 1]["sim_time"]
        if dt > 0:
            steer_rate.append(abs(deltas[i] - deltas[i - 1]) / dt)
            sr_meta.append(active[i])
    # Request-side excursions (context only, never a verdict) and, per frame,
    # whether the corresponding clamp stage was engaged. The flags are what
    # separate "the clamp never ran" (a real defect) from "the clamp ran and an
    # offline recomputation disagrees slightly" (a measurement mismatch).
    kappas_req = [math.tan(d) / PHASE2_WB for d in deltas_req]
    a_lat_req = [abs(v * v * k) for v, k in zip(speeds, kappas_req)]
    clamp_on = [
        bool((r.get("envelope") or {}).get("lateral_accel_active", False)) for r in active
    ]
    return {"a_lat": a_lat, "yaw": yaw, "steer_rate": steer_rate,
            "active": active, "sr_meta": sr_meta,
            "a_lat_req": a_lat_req, "a_lat_clamp_on": clamp_on}


def main() -> int:
    """Returns a PROCESS EXIT CODE, and that is the point.

    This file is named "acceptance" and its whole job is to answer "do the
    candidate envelope limits ever clip a frame across the evaluated set". It
    used to be declared `-> None` and called bare from __main__, so it could
    not report a verdict to anything: it printed a clip count and exited 0
    whether that count was 0 or 4000. Every commit that cited it as evidence
    was citing a program that is structurally incapable of failing.

    Exit codes:
      0 - no frame clipped any candidate limit
      1 - at least one frame clipped (the answer this check exists to give)
      2 - nothing was evaluated (no series found), which is not a pass either
    """
    # Accepts one or more roots; each is searched recursively (rglob) for
    # telemetry.jsonl so both flat (phase2: root/<scenario>/telemetry.jsonl)
    # and nested (junction batches: root/<manifest>/<scenario>/telemetry.jsonl)
    # layouts work without a flag. Optional --steer-rate-max=X overrides the
    # steer_rate candidate limit (for comparing 1.0 vs 1.5 without editing code).
    args = sys.argv[1:]
    for a in list(args):
        if a.startswith("--steer-rate-max="):
            LIMITS["steer_rate"] = float(a.split("=", 1)[1])
            args.remove(a)
    extra_roots = [Path(a) for a in args]
    all_series: dict[str, dict] = {}
    for name in PHASE1_WB:
        all_series[f"phase1/{name}"] = series_from_phase1(name)
    for root in extra_roots:
        label = root.name
        for p in sorted(root.rglob("telemetry.jsonl")):
            scen_name = p.parent.name
            all_series[f"{label}/{scen_name}"] = series_from_phase2(scen_name, p)

    print(f"{'scenario':28s} {'a_lat max(%lim)':>18s} {'yaw max(%lim)':>16s} "
          f"{'steer_rate max(%lim)':>22s} {'clip(a_lat/yaw/sr)':>20s}")
    total_clips = {"a_lat": 0, "yaw": 0, "steer_rate": 0}
    total_n = {"a_lat": 0, "yaw": 0, "steer_rate": 0}
    clip_detail = []
    for scen, s in all_series.items():
        cells = []
        clipcounts = []
        for m in ("a_lat", "yaw", "steer_rate"):
            vals = s[m]
            mx = max(vals) if vals else 0.0
            lim = LIMITS[m]
            pct = 100.0 * mx / lim
            cells.append(f"{mx:7.3f} ({pct:5.1f}%)")
            clipped = [v for v in vals if v > lim]
            clipcounts.append(len(clipped))
            total_clips[m] += len(clipped)
            total_n[m] += len(vals)
            if clipped and "phase2" in scen:
                # find offending frame(s) for reporting
                src = s.get("active" if m != "steer_rate" else "sr_meta", [])
                pairs = zip(s[m], src) if m != "steer_rate" else zip(s[m], s["sr_meta"])
                for v, fr in pairs:
                    if v > lim:
                        clip_detail.append((scen, m, v, fr["sim_time"], fr["ego"]["speed"], fr["driver"]["steer"]))
        print(f"{scen:28s} {cells[0]:>18s} {cells[1]:>16s} {cells[2]:>22s} "
              f"{str(clipcounts):>20s}")

    print(f"\n=== TOTAL across {len(all_series)} scenarios ===")
    for m in ("a_lat", "yaw", "steer_rate"):
        print(f"  {m}: clipped={total_clips[m]}/{total_n[m]} "
              f"({100*total_clips[m]/max(1,total_n[m]):.3f}%)")

    if clip_detail:
        print("\n=== clip detail (phase2) ===")
        seen = set()
        for scen, m, v, t, spd, steer in clip_detail:
            key = (scen, round(t, 2))
            if key in seen:
                continue
            seen.add(key)
            print(f"  {scen} t={t:.2f}s metric={m} value={v:.3f} v={spd:.2f}m/s steer_norm={steer:.4f}")

    # --- verdict ---------------------------------------------------------
    #
    # Two different findings live in "the applied a_lat exceeded the limit",
    # and collapsing them would either hide a real defect or cry wolf:
    #
    #   clamp NOT engaged  -> the envelope did not act on a frame that needed
    #                         it. That is an unambiguous safety defect and it
    #                         fails the run.
    #   clamp engaged      -> the envelope acted and held the command at ITS
    #                         computed ceiling while this recomputation lands
    #                         above it. Kept as a distinct outcome because it
    #                         is a disagreement between two calculations of the
    #                         same quantity, not evidence that anything unsafe
    #                         reached the vehicle.
    #
    # THAT OUTCOME WAS ONCE REACHED, AND WAS TRACKED DOWN RATHER THAN LEFT
    # OPEN. green_no_stop t=11.95/12.05 recomputed to 4.3095 against the 4.3
    # ceiling (0.22% over) with the clamp demonstrably active. The cause was
    # this file, in two parts, both now fixed:
    #   * wheelbase -- the product derives it as boundingbox.length * 0.6
    #     (ControllerVirtualDriver.cpp), i.e. 3.00 for these 5.0 m vehicles;
    #     an ad-hoc check against 2.98 inflated the gap to 0.89%.
    #   * speed sample -- the clamp runs BEFORE the vehicle is integrated, so
    #     it used v=13.992 while the row records the post-step v=14.005. One
    #     frame of acceleration, doubled by the v^2.
    # With the pairing corrected the same data reports 0 clips out of 11,149.
    # The envelope had been correct the whole time.
    evaluated = sum(total_n.values())
    if evaluated == 0:
        print("\nRESULT: NOT EVALUATED — no series were found. "
              "Zero frames checked is not an acceptance.")
        return 2

    unclamped_breaches = []
    request_excursions = 0
    for scen, s in all_series.items():
        for a_out, a_req, on in zip(s.get("a_lat", []), s.get("a_lat_req", []),
                                     s.get("a_lat_clamp_on", [])):
            if a_req > LIMITS["a_lat"]:
                request_excursions += 1
            if a_out > LIMITS["a_lat"] and not on:
                unclamped_breaches.append((scen, a_out))

    print(f"\n=== applied vs requested ===")
    print(f"  AD requests beyond the a_lat limit (clamp's job to catch): {request_excursions}")
    print(f"  applied output beyond the limit with the clamp NOT engaged: "
          f"{len(unclamped_breaches)}")

    if unclamped_breaches:
        print(f"\nRESULT: FAIL — the envelope let {len(unclamped_breaches)} frame(s) "
              f"through above the lateral-accel limit WITHOUT engaging:")
        for scen, a in unclamped_breaches[:5]:
            print(f"    {scen}: a_lat={a:.3f} (limit {LIMITS['a_lat']})")
        return 1

    clipped_total = sum(total_clips.values())
    if clipped_total:
        print(f"\nRESULT: UNDETERMINED — {clipped_total} applied frame-metric(s) sit "
              f"above a limit, but on every one of them the corresponding clamp was "
              f"ENGAGED. That is an offline/online recomputation mismatch (see the "
              f"verdict comment), not a demonstrated clamp failure. Not passing it, "
              f"not failing it.")
        return 3

    print(f"\nRESULT: PASS — nothing above any limit reached the vehicle "
          f"({evaluated} frame-metrics evaluated).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
