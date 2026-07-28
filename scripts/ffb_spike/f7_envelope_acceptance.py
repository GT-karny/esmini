"""feature:F7 acceptance check — did any frame apply a steering curvature above
the AD safety envelope's own cap, across the evaluated run set?

WHAT THIS FILE ASSERTS, AND WITH WHAT ASSUMPTIONS
-------------------------------------------------
The verdict comes from ONE comparison, per frame, on telemetry produced by the
envelope itself:

    |envelope.kappa_out|  <=  envelope.kappa_limit

Both sides are published by AdSteeringEnvelope.cpp, computed inside the same
call, from the same wheel_base / max_steer_angle / speed the clamp actually
used. NOTHING about the vehicle geometry, the speed sample, or the frame
timing enters this file's verdict. That is the entire point, and it took three
attempts to get there:

  1. The check read `driver.steer` — the AD's raw REQUEST, before the clamp —
     and reported a_lat = 42.6 m/s^2 (991% of limit) on a frame where the
     envelope had done its job perfectly (steer_in 0.9458 -> steer_out 0.0150).
  2. Corrected to read `envelope.steer_out`, it then re-derived the applied
     curvature from a hard-coded wheelbase (3.0) and the row's speed. The
     product derives wheel_base as boundingbox.length*0.6, and the clamp runs
     BEFORE the vehicle is integrated while telemetry records speed after.
     Two wrong assumptions, together manufacturing a 0.88% phantom overshoot
     that cost a full investigation. A "+/-1 frame interval" workaround was
     built on top, which narrowed the phantom without removing it.
  3. `kappa_limit` was then published, and the RIGHT-hand side was fixed — but
     the LEFT-hand side kept the hard-coded wheelbase. Half the double-mistake
     survived, and the 12 scenarios then in use happened to all be 5.0 m
     vehicles (-> 3.0), so the two numbers agreed by coincidence, not by
     construction. Worse, the direct comparison was assembled into a local
     `kappa_over` list that the function never returned, so the "direct check"
     could not fail at all: every PASS it printed came from the derived path
     it claimed to supersede.

`kappa_out` closes it. The derived-curvature path for run telemetry is GONE —
not superseded, deleted — so there is no longer a code path in which a
wheelbase or a speed sample can influence this file's answer.

min(|kappa_cmd|, kappa_limit) is NOT a valid substitute for kappa_out: the
curvature clamp is stage 1 of 3, and the steer_rate / steer_jerk stages move
the command again afterwards (pinned by
test_AdSteeringEnvelope.cpp's KappaOutIsNotMinOfCmdAndLimitWhen*StageBinds).

!! A PASS ON THE FITTING POOL IS NOT AN INDEPENDENT VERIFICATION. !!

    The envelope's limits were DERIVED from the 15-scenario pool
    (AdSteeringEnvelope.hpp: "normal driving peaks were a_lat=3.289,
    yaw_rate=0.780, steer_rate=0.769; a_lat_max_steer and yaw_rate_max are
    that pool max x1.3"). Running over that same pool cannot fail for the
    reason that matters. Point the CLI at scenarios outside those 15
    (junction / crosswalk / AEB manifests, or a real-vehicle capture) for the
    result to mean anything:

        python f7_envelope_acceptance.py <root-of-scenarios-not-in-the-15>

WHAT STILL CARRIES AN ASSUMPTION (reported, never part of the verdict)
---------------------------------------------------------------------
  * the steering-RATE column, which converts normalized steering to radians
    with MAX_STEER_ANGLE below. That is the product's configured
    max_steer_angle, not a published per-frame value, so it is a config
    assumption. It bounds a different limit (steer_rate_max) than the
    curvature comparison and is reported separately.
  * the Phase-1 CSV profiles, which predate the envelope entirely and carry no
    telemetry block. Their wheelbase is known per file (PHASE1_WB) and their
    numbers describe the pool the limits were fitted to. Context only.

Usage:
    DriverScript/.venv/Scripts/python.exe scripts/ffb_spike/f7_envelope_acceptance.py <run_out_root> [...]
"""
from __future__ import annotations

import csv
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).parent

# Config assumption, used ONLY for the steering-rate column and the Phase-1
# profiles — never for the curvature verdict (see module doc).
MAX_STEER_ANGLE = 0.61

# Mirrors the SHIPPED constants in AdSteeringEnvelope.hpp
# (kAdEnvelopeDefault{ALatMaxSteer,YawRateMax,SteerRateMax}), not the original
# "pool max x1.3" proposal. steer_rate was 1.0 here long after the product
# settled on 1.5 on separate grounds (the hpp explains why the x1.3 rule does
# not apply to it), which made this table report the rate limiter doing its job
# at exactly its cap as a 150% exceedance. Kept overridable with
# --steer-rate-max= for comparing candidate values.
LIMITS = {"a_lat": 4.3, "yaw": 1.0, "steer_rate": 1.5}

# Phase-1 CSVs are pre-envelope captures with a per-file known vehicle; they
# have no telemetry block to publish anything. There is deliberately no
# PHASE2_WB counterpart: run telemetry now publishes its own curvature, and
# re-introducing a wheelbase constant here would re-open the defect this file
# spent three revisions closing.
PHASE1_WB = {"lc": 5.04 * 0.6, "curve": 5.0 * 0.6, "right_turn": 5.0 * 0.6}

# One serialization quantum of VirtualDriverTelemetryJson.cpp's fixed 9
# decimals. kappa_out and kappa_limit are each rounded to 1e-9 absolute before
# they reach this file, so a difference below that is the instrument's own
# resolution floor and cannot be evidence of anything. A finer epsilon would
# manufacture breaches out of rounding; a coarser one would hide real ones
# (1e-9 of curvature is ~2e-7 m/s^2 of lateral accel at 14 m/s).
KAPPA_EPS_ABS = 1e-9

# Same idea, propagated to the steering-RATE column. That series is a finite
# difference of steer_out, so the 1e-9 steering quantum is amplified by
# MAX_STEER_ANGLE/dt: 1.2e-8 at dt=0.05, 6.1e-8 at dt=0.01. A rate limiter
# pinned exactly at its cap can serialize a hair either side of it, and a bare
# `>` would report the limiter working as the limiter failing.
#
# MEASURED, not guessed: over the 12-scenario car_following capture the largest
# applied steer_rate is 1.4999999862 against the 1.5 cap — i.e. 1.38e-8 BELOW
# it, one quantum, exactly what a limiter sitting on its cap looks like through
# this instrument. This constant was 1e-6 on first writing, which is 72x the
# distance it was meant to guard and would have hidden any real excess up to
# 1e-6. Tightened to 1e-7: a few quanta at the tightest dt in use, and an order
# below the only margin ever observed.
RATE_EPS_ABS = 1e-7


def series_from_phase1(name: str) -> dict:
    """Pre-envelope CSV profile. Derived metrics, known per-file wheelbase.

    Context only — this data has no envelope block, so it cannot answer the
    applied-curvature question and never contributes to the verdict.
    """
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
    return {"kind": "phase1", "a_lat": a_lat, "yaw": yaw, "steer_rate": steer_rate}


def series_from_run(path: Path) -> dict:
    """Run telemetry. The applied-curvature check reads PUBLISHED values only.

    A frame is DIRECTLY EVALUATED when its envelope block carries both
    kappa_limit > 0 and kappa_out. Anything else is counted as un-evaluated and
    reported as such — this function never falls back to reconstructing the
    applied curvature, because every reconstruction available to it needs a
    wheelbase and a speed sample it would have to invent.

    kappa_limit == 0 is the envelope's "no cap computed this frame" signal (the
    disabled pass-through), not a cap of zero. Those frames are un-evaluated
    too, and separately counted so a run made with the envelope switched off
    cannot be mistaken for a clean run.
    """
    rows = [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
    # Telemetry can wrap around when a run restarts within one file; keep the
    # first monotonically increasing stretch only.
    active, prev_t = [], None
    for r in rows:
        t = r["sim_time"]
        if prev_t is not None and t <= prev_t:
            break
        active.append(r)
        prev_t = t

    kappa_over = []       # direct breaches: (t, |kappa_out|, kappa_limit, clamp_engaged)
    ratios = []           # |kappa_out| / kappa_limit on directly evaluated frames
    excess = []           # |kappa_out| - kappa_limit [1/m], signed: the margin itself
    n_direct = 0
    n_no_kappa_out = 0    # has a cap but no applied curvature -> capture predates kappa_out
    n_no_cap = 0          # envelope did not run (disabled) or block absent entirely
    req_over = 0          # AD asked for more curvature than the cap: the clamp had work to do

    for r in active:
        env = r.get("envelope") or {}
        k_lim = env.get("kappa_limit")
        if not isinstance(k_lim, (int, float)) or k_lim <= 0.0:
            n_no_cap += 1
            continue
        k_out = env.get("kappa_out")
        if not isinstance(k_out, (int, float)):
            n_no_kappa_out += 1
            continue
        n_direct += 1
        ratios.append(abs(k_out) / k_lim)
        excess.append(abs(k_out) - k_lim)
        if abs(k_out) > k_lim + KAPPA_EPS_ABS:
            # Whether the clamp was ENGAGED separates the two findings that
            # live in "the output went over": clamp off means the envelope did
            # not act on a frame that needed it (unambiguous defect); clamp on
            # means it acted and its own output still exceeded its own cap
            # (also a defect, and a stranger one). Both fail; the message
            # distinguishes them.
            engaged = bool(env.get("lateral_accel_active") or env.get("yaw_rate_active"))
            kappa_over.append((r.get("sim_time"), abs(k_out), k_lim, engaged))
        k_cmd = env.get("kappa_cmd")
        if isinstance(k_cmd, (int, float)) and abs(k_cmd) > k_lim + KAPPA_EPS_ABS:
            req_over += 1

    # Steering RATE from the APPLIED command. No wheelbase and no speed enters
    # this; the one assumption is MAX_STEER_ANGLE (see module doc). Falls back
    # to driver.steer only for captures with no envelope block at all, where
    # the raw proposal is the only steering signal that exists.
    def _steer_out(r):
        env = r.get("envelope") or {}
        return env["steer_out"] if "steer_out" in env else r["driver"]["steer"]

    deltas = [_steer_out(r) * MAX_STEER_ANGLE for r in active]
    steer_rate, sr_meta = [], []
    for i in range(1, len(active)):
        dt = active[i]["sim_time"] - active[i - 1]["sim_time"]
        if dt > 0:
            steer_rate.append(abs(deltas[i] - deltas[i - 1]) / dt)
            sr_meta.append(active[i])

    return {"kind": "run", "steer_rate": steer_rate, "sr_meta": sr_meta,
            "kappa_over": kappa_over, "kappa_ratios": ratios, "kappa_excess": excess,
            "n_direct": n_direct, "n_no_kappa_out": n_no_kappa_out,
            "n_no_cap": n_no_cap, "req_over": req_over, "n_frames": len(active)}


def main() -> int:
    """Returns a PROCESS EXIT CODE, and that is the point.

    This file is named "acceptance" and its whole job is to answer "did any
    applied steering command exceed the safety envelope's own curvature cap".
    It was once declared `-> None` and called bare, so it printed a count and
    exited 0 whether that count was 0 or 4000; then, after it grew a verdict,
    the direct comparison it based that verdict on was built into a local that
    the producing function never returned, so it still could not fail. Both
    holes are closed, and the structure below is arranged so a future one is
    harder to open: the PASS branch requires a POSITIVE count of directly
    evaluated frames, never merely an empty list of breaches.

    Exit codes:
      0 - frames were directly evaluated and none exceeded the envelope's cap
      1 - at least one applied curvature exceeded the envelope's own cap
      2 - the question was not answered (no run telemetry, or none of it
          carries the published curvature) — not a pass
      3 - no curvature breach, but a steering-rate/Phase-1 candidate-limit
          exceedance was seen; those paths still carry assumptions, so they
          are reported without being called a product finding
    """
    args = sys.argv[1:]
    for a in list(args):
        if a.startswith("--steer-rate-max="):
            LIMITS["steer_rate"] = float(a.split("=", 1)[1])
            args.remove(a)
    # Each root is searched recursively for telemetry.jsonl, so both flat
    # (root/<scenario>/) and nested (root/<manifest>/<scenario>/) layouts work.
    extra_roots = [Path(a) for a in args]

    phase1: dict[str, dict] = {}
    runs: dict[str, dict] = {}
    for name in PHASE1_WB:
        phase1[f"phase1/{name}"] = series_from_phase1(name)
    for root in extra_roots:
        label = root.name
        for p in sorted(root.rglob("telemetry.jsonl")):
            runs[f"{label}/{p.parent.name}"] = series_from_run(p)

    # --- Table A: the verdict's own data -----------------------------------
    print("=== applied curvature vs the envelope's OWN cap (published both sides) ===")
    if not runs:
        print("  (no run telemetry found)")
    else:
        # max_excess is the same comparison in ABSOLUTE curvature [1/m], and it
        # is the number to read when the ratio prints as 1.000000: a rate
        # limiter parked exactly on its cap and a genuine hairline breach look
        # identical at 6 decimals of ratio, but not here.
        print(f"{'scenario':40s} {'frames':>8s} {'direct':>8s} {'|k_out|/k_lim max':>19s} "
              f"{'max excess [1/m]':>18s} {'AD over cap':>12s} {'breach':>7s}")
    total_direct = total_no_kappa_out = total_no_cap = total_req_over = 0
    worst_excess = float("-inf")
    breaches: list[tuple] = []
    for scen, s in sorted(runs.items()):
        ratio_max = max(s["kappa_ratios"], default=float("nan"))
        excess_max = max(s["kappa_excess"], default=float("nan"))
        if s["kappa_excess"]:
            worst_excess = max(worst_excess, excess_max)
        total_direct += s["n_direct"]
        total_no_kappa_out += s["n_no_kappa_out"]
        total_no_cap += s["n_no_cap"]
        total_req_over += s["req_over"]
        for b in s["kappa_over"]:
            breaches.append((scen, *b))
        print(f"{scen:40s} {s['n_frames']:8d} {s['n_direct']:8d} {ratio_max:19.6f} "
              f"{excess_max:18.3e} {s['req_over']:12d} {len(s['kappa_over']):7d}")

    if runs:
        print(f"  directly evaluated frames: {total_direct}")
        if worst_excess > float("-inf"):
            print(f"  worst |kappa_out| - kappa_limit over all frames: {worst_excess:.3e} 1/m "
                  f"(breach threshold: > +{KAPPA_EPS_ABS:.0e})")
        if total_no_kappa_out:
            print(f"  frames with a cap but NO kappa_out (capture predates it): "
                  f"{total_no_kappa_out} — NOT evaluated, NOT guessed")
        if total_no_cap:
            print(f"  frames with no cap (envelope disabled or block absent): {total_no_cap} "
                  f"— NOT evaluated")
        print(f"  AD requests above the cap (the clamp's job to catch): {total_req_over}")

    # --- Table B: context that still carries assumptions --------------------
    print("\n=== context (assumption-carrying; never the verdict) ===")
    print(f"{'series':40s} {'a_lat max(%lim)':>18s} {'yaw max(%lim)':>16s} "
          f"{'steer_rate max(%lim)':>22s} {'clips':>18s}")
    context_clips = 0
    clip_detail = []
    for scen, s in list(phase1.items()) + sorted(runs.items()):
        cells, counts = [], []
        for m in ("a_lat", "yaw", "steer_rate"):
            vals = s.get(m)
            if vals is None:
                cells.append(f"{'-':>15s}")
                counts.append(0)
                continue
            mx = max(vals) if vals else 0.0
            lim = LIMITS[m]
            cells.append(f"{mx:7.3f} ({100.0 * mx / lim:5.1f}%)")
            eps = RATE_EPS_ABS if m == "steer_rate" else 0.0
            clipped = [v for v in vals if v > lim + eps]
            counts.append(len(clipped))
            context_clips += len(clipped)
            if clipped and m == "steer_rate" and s.get("sr_meta"):
                for v, fr in zip(vals, s["sr_meta"]):
                    if v > lim + eps:
                        clip_detail.append((scen, m, v, fr["sim_time"], fr["ego"]["speed"]))
        print(f"{scen:40s} {cells[0]:>18s} {cells[1]:>16s} {cells[2]:>22s} {str(counts):>18s}")

    if clip_detail:
        print("\n--- steer_rate clip detail ---")
        seen = set()
        for scen, m, v, t, spd in clip_detail:
            key = (scen, round(t, 2))
            if key in seen:
                continue
            seen.add(key)
            print(f"  {scen} t={t:.2f}s {m}={v:.3f} v={spd:.2f}m/s")

    # --- verdict -----------------------------------------------------------
    if breaches:
        engaged_n = sum(1 for b in breaches if b[4])
        print(f"\nRESULT: FAIL — {len(breaches)} frame(s) applied a curvature above the "
              f"envelope's OWN cap ({engaged_n} of them with the curvature clamp reporting "
              f"ENGAGED). No wheelbase, speed sample, or timing assumption enters this "
              f"comparison:")
        for scen, t, k_out, k_lim, engaged in breaches[:5]:
            print(f"    {scen} t={t}: |kappa_out|={k_out:.9f} > kappa_limit={k_lim:.9f} "
                  f"(clamp engaged: {engaged})")
        return 1

    if total_direct == 0:
        print("\nRESULT: NOT EVALUATED — no frame carried both kappa_out and a positive "
              "kappa_limit, so the applied-curvature question was never asked. "
              f"(run telemetry frames seen: {sum(s['n_frames'] for s in runs.values())}; "
              f"missing kappa_out: {total_no_kappa_out}; no cap: {total_no_cap}) "
              "Zero frames checked is not an acceptance — rebuild and re-capture.")
        return 2

    if context_clips:
        print(f"\nRESULT: UNDETERMINED — the curvature check PASSED on {total_direct} frames, "
              f"but {context_clips} value(s) in the assumption-carrying context table sit above "
              f"a candidate limit. Those paths assume max_steer_angle (and, for Phase-1, a "
              f"wheelbase), so they are reported, not called a product finding.")
        return 3

    print(f"\nRESULT: PASS — {total_direct} frame(s) directly evaluated, none applied a "
          f"curvature above the envelope's own cap. Both sides of the comparison are "
          f"published by the envelope: no wheelbase, no speed sample, no timing assumption.")
    if total_req_over:
        print(f"  The clamp was not idle: {total_req_over} AD request(s) exceeded the cap "
              f"and were caught.")
    else:
        print("  NOTE: the clamp never had to act on this set, so this PASS shows the "
              "envelope did no harm, not that it works. Point the CLI at a set that "
              "provokes it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
