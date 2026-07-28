"""feature:F7 investigation probe -- settles EXACTLY ONE question.

QUESTION: during the harness's lane-width lateral-offset maneuver
(scripts/ffb_spike/resume_ride_feel.run_network_arm_lane_shift, target 3.5m /
one lane width, speed 8 m/s, SHIPPED config, no overrides), does ego.lane
change as it moves laterally, and what is ego.offset at each moment?

WHY: ego.offset is roadmanager::Position::GetOffset(), re-referenced to
whichever lane the ego occupies -- it jumps at lane boundaries and is bounded
by ~half a lane width on driving lanes. Frozen real-wheel telemetry
(test_results/f7_realwheel_rerun_jerk0/f7_realwheel_basic.jsonl, t=7.56) shows
track 0->0, lane -3->-2, offset +1.749->-1.807 in one frame; max|offset|
across 6 runs is 1.81m. Yet the lane-shift harness reports offset_at_edge=
-3.230m -- reconcilable only if the ego left the outermost driving lane. This
probe watches lane+track+offset TOGETHER, frame by frame, to settle that.

STRICT SCOPE: no jerk sweeps, no ride-feel metrics, one speed only (8 m/s),
no other question. VERDICT at the bottom is decided strictly from recorded
numbers, no hedging.

ROUTE TAKEN: imports vd_resume_transient.py (`vrt`) for DLL/BASE_XOSC/
SHIPPED_CFG/_load_lib/ROOT, and resume_ride_feel.py (`rrf`) for
run_network_arm_lane_shift, _make_variant_speed_fixed, and
_route_departure_check (reused, not reimplemented). Checked BOTH slimmers
first: vrt._slim() DOES carry ego.lane/ego.offset (never dropped) but NOT
ego.track; rrf._slim_ext() adds ego.track(+ego.s), and
run_network_arm_lane_shift already calls _slim_ext() internally -- every
field this probe needs is therefore already threaded through the harness's
own frames, so the main measurement just calls the harness and reads its
output; no custom stepping loop was needed for it. A throwaway stepping loop
IS used, but only in _preflight_instrument_check(), because that must prove
the RAW (un-slimmed) telemetry carries these fields BEFORE the real maneuver
runs -- a slimming bug could otherwise mask a missing field. Native stdout is
deliberately NOT suppressed (no resume_ride_feel._quiet_native_stdout) so any
C++-side warning reaches the console.

Usage (DriverScript venv -- never bare python):
  DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\vd_lane_offset_semantics_probe.py

Output: printed to stdout AND written to test_results/f7_lane_offset_semantics_probe.txt
"""

from __future__ import annotations

import ctypes
import json
import os
import sys
import time
import xml.etree.ElementTree as ET

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import vd_resume_transient as vrt  # noqa: E402  (path insert must precede this)

ROOT = vrt.ROOT
sys.path.insert(0, os.path.join(ROOT, "scripts", "ffb_spike"))
import resume_ride_feel as rrf  # noqa: E402

XODR_PATH = os.path.join(ROOT, "resources", "xodr", "e6mini.xodr")
OUT_DIR = os.path.join(ROOT, "test_results")
OUT_TXT = os.path.join(OUT_DIR, "f7_lane_offset_semantics_probe.txt")

TARGET_OFFSET_M = 3.5  # harness default --lane-target-m (scripts/ffb_spike/resume_ride_feel.py main())
SPEED_MPS = 8.0  # the ONE speed this probe answers the question at -- see module docstring scope
SAMPLE_DT_S = 0.1  # per-frame table downsample interval (requirement: "downsampled to every 0.1s")
# A lane/track re-reference is a step discontinuity; continuous motion at this
# maneuver's steering caps (_LANE_SHIFT_* in resume_ride_feel.py) cannot move
# the ego more than a few cm within one dt=0.01s frame, so 0.3m in one frame
# safely flags a re-reference jump rather than ordinary continuous motion.
JUMP_THRESHOLD_M = 0.3


def _parse_xodr_road0_lanes(path: str) -> dict:
    """Real lane geometry for road 0, parsed (not hardcoded). Only the first
    laneSection is reported (e6mini.xodr road 0 has exactly one, s=0)."""
    if not os.path.exists(path):
        raise RuntimeError(f"xodr not found: {path}")
    root = ET.parse(path).getroot()
    road = root.find(".//road[@id='0']")
    if road is None:
        raise RuntimeError(
            f"{path}: no <road id='0'> -- cannot report real lane geometry"
        )
    sections = road.findall(".//laneSection")
    if not sections:
        raise RuntimeError(f"{path}: road 0 has no <laneSection>")
    if len(sections) > 1:
        print(
            f"  NOTE: road 0 has {len(sections)} laneSections; reporting only the first "
            f"(s={sections[0].get('s')}) -- this maneuver stays within a short s-range."
        )
    lanes = []
    for lane in sections[0].findall(".//lane"):
        w_el = lane.find("width")
        lanes.append(
            {
                "id": int(lane.get("id")),
                "type": lane.get("type"),
                "width_m": float(w_el.get("a")) if w_el is not None else 0.0,
            }
        )
    lanes.sort(key=lambda x: -x["id"])
    return {"road_id": "0", "section_s": sections[0].get("s"), "lanes": lanes}


def _preflight_instrument_check() -> dict:
    """Prove the instrument sees what this probe measures BEFORE spending the
    full (up to 15s-capped) maneuver on it: checks the RAW telemetry JSON off
    GT_GetVirtualDriverTelemetry directly, not the _slim/_slim_ext dict (a
    slimming bug could otherwise mask a missing field). Throwaway stub-input
    init, a few steps, one raw sample, closed immediately."""
    import tempfile

    tmpdir = tempfile.mkdtemp(prefix="vd_lane_offset_preflight_")
    xosc = rrf._make_variant_speed_fixed(tmpdir, {"input_type": "stub"}, SPEED_MPS)
    lib = vrt._load_lib()
    argv_list = [
        b"vd_lane_offset_preflight",
        b"--osc",
        xosc.encode(),
        b"--headless",
        b"--fixed_timestep",
        b"0.01",
    ]
    argv = (ctypes.c_char_p * len(argv_list))(*argv_list)
    rc = lib.GT_InitWithArgs(len(argv_list), argv)
    if rc != 0:
        raise RuntimeError(f"preflight: GT_InitWithArgs rc={rc}")
    buf = ctypes.create_string_buffer(32768)
    raw = None
    try:
        for _ in range(5):
            lib.GT_Step(0.01)
            n = lib.GT_GetVirtualDriverTelemetry(0, buf, len(buf))
            if n > 0:
                raw = json.loads(buf.value.decode())
    finally:
        lib.GT_Close()
        try:
            os.remove(xosc)
        except OSError:
            pass
    if raw is None:
        raise RuntimeError(
            "preflight FAIL: GT_GetVirtualDriverTelemetry never returned a frame -- "
            "cannot prove the instrument sees anything"
        )
    ego = raw.get("ego", {})
    missing = [k for k in ("lane", "track", "offset") if ego.get(k) is None]
    if missing:
        raise RuntimeError(
            f"preflight FAIL: raw telemetry ego block missing/None for {missing} -- "
            f"ego keys present={sorted(ego.keys())}. Refusing to run the maneuver "
            f"against an instrument that cannot see what this probe measures."
        )
    return {"ego_keys": sorted(ego.keys()), "sample_ego": ego}


def _downsample(frames: list, dt: float) -> list:
    if not frames:
        return []
    next_t = frames[0]["sim_time"]
    out = []
    for f in frames:
        if f["sim_time"] >= next_t - 1e-9:
            out.append(f)
            next_t += dt
    if out[-1] is not frames[-1]:
        out.append(frames[-1])
    return out


def _lane_track_changes(frames: list) -> list:
    """Every consecutive-frame pair where ego.lane or ego.track differs --
    the exact event this question hinges on."""
    events = []
    prev = None
    for i, f in enumerate(frames):
        lane, track = f.get("ego_lane"), f.get("ego_track")
        if prev is not None:
            plane, ptrack = prev
            if (lane is not None and plane is not None and lane != plane) or (
                track is not None and ptrack is not None and track != ptrack
            ):
                events.append(
                    {
                        "idx": i,
                        "t": f["sim_time"],
                        "phase": f.get("phase"),
                        "lane_before": plane,
                        "lane_after": lane,
                        "track_before": ptrack,
                        "track_after": track,
                        "offset_before": frames[i - 1].get("ego_offset"),
                        "offset_after": f.get("ego_offset"),
                    }
                )
        prev = (lane, track)
    return events


def _cell(v, width: int = 0, prec: int | None = None) -> str:
    if v is None:
        s = "None"
    elif prec is not None and isinstance(v, float):
        s = f"{v:.{prec}f}"
    else:
        s = str(v)
    return s.rjust(width)


def _beyond_lane_edge(frame: dict, width_by_id: dict) -> dict | None:
    """THE decisive comparison: is |ego.offset| larger than the half-width of the
    lane ego.lane says it is in?

    Needed because ego.lane CLAMPS to the nearest *driving* lane -- drive past the
    outermost driving lane and lane stops changing while offset keeps growing. The
    harness's existing _route_departure_check only tests whether ego.lane left the
    {-2,-3,-4} band, so it cannot tell "inside lane -4" from "past lane -4's edge
    with lane clamped at -4"; both report route-departed=False. Only this
    magnitude-vs-geometry comparison separates them.
    """
    lane, off = frame.get("ego_lane"), frame.get("ego_offset")
    if lane is None or off is None:
        return None
    width = width_by_id.get(lane)
    if width is None:
        return {
            "lane": lane,
            "offset": off,
            "half_width": None,
            "verdict": f"lane {lane} not found in road 0's lane table -- cannot compare",
        }
    half = width / 2.0
    excess = abs(off) - half
    return {
        "lane": lane,
        "offset": off,
        "half_width": half,
        "excess_m": excess,
        "beyond_edge": excess > 0.0,
        "verdict": (
            f"BEYOND lane {lane}'s edge by {excess:.3f}m "
            f"(|offset|={abs(off):.3f} > half-width {half:.3f})"
            if excess > 0.0
            else f"inside lane {lane} (|offset|={abs(off):.3f} <= half-width {half:.3f})"
        ),
    }


def _classify(
    frames: list, events: list, xodr_lanes: list, maneuver_ok: bool, diag: dict
) -> list:
    """Decides (A)/(B)/(C) strictly from the recorded numbers -- no hedging,
    no assumption. See module docstring for what each option means.

    Deliberately split into two independent claims, because they need different
    evidence and conflating them would assert more than was measured:
      claim 1 (semantics)  -- is ego.offset lane-relative? decided by lane-change
                              + single-frame offset jump.
      claim 2 (excursion)  -- did the ego leave the driving lanes? decided ONLY by
                              _beyond_lane_edge() at the resume edge / peak.
    A lane change alone proves claim 1 and says NOTHING about claim 2: moving from
    lane -3 to lane -4 and stopping there is a lane change fully inside a driving
    lane.
    """
    if not maneuver_ok:
        return [
            f"NOT MEASURED -- the lane-shift maneuver did not converge (diag={diag}). "
            "Reporting a verdict from an unconverged run would misattribute ordinary "
            "in-progress motion to the question this probe asks. The per-frame table and "
            "lane-change list above are still raw, honest evidence -- just not a settled result."
        ]

    offsets = [f["ego_offset"] for f in frames if f.get("ego_offset") is not None]
    max_abs_offset = max(abs(o) for o in offsets) if offsets else None
    start_lane = frames[0].get("ego_lane")
    width_by_id = {l["id"]: l["width_m"] for l in xodr_lanes}
    half_width_start = width_by_id.get(start_lane)
    half_width_start = half_width_start / 2.0 if half_width_start is not None else None

    jump_events = [
        e
        for e in events
        if e["offset_before"] is not None
        and e["offset_after"] is not None
        and abs(e["offset_after"] - e["offset_before"]) > JUMP_THRESHOLD_M
    ]

    if events and jump_events:
        biggest = max(abs(e["offset_after"] - e["offset_before"]) for e in jump_events)
        return [
            f"(A) -- ego.lane/ego.track CHANGED during the maneuver ({len(events)} change "
            f"event(s)), and ego.offset jumped by {biggest:.3f}m in a single frame at a boundary "
            f"(> {JUMP_THRESHOLD_M}m threshold -- physically impossible for continuous lateral "
            "motion at this maneuver's speed/dt). ego.offset is LANE-RELATIVE: it is measured "
            "from whichever lane the ego currently occupies, not from the route lane.",
            "NOTE: this settles the SEMANTICS only. Whether the ego also ended up outside the "
            "driving lanes is a separate claim, decided by the 'beyond lane edge' lines above "
            "(|offset| vs that lane's half-width) -- NOT by the lane change itself.",
        ]
    if (
        not events
        and half_width_start is not None
        and max_abs_offset is not None
        and max_abs_offset > half_width_start + 0.1
    ):
        return [
            f"(B) -- ego.lane/ego.track NEVER changed, yet max|ego.offset|={max_abs_offset:.3f}m "
            f"exceeded the starting lane's half-width ({half_width_start:.3f}m, road 0 lane "
            f"{start_lane}). ego.offset behaved ROUTE-relative in this run; the frozen-telemetry "
            "premise needs rework."
        ]
    return [
        f"(C) unresolved from this run's numbers alone. Raw evidence: lane/track change "
        f"events={len(events)}, jump events(> {JUMP_THRESHOLD_M}m)={len(jump_events)}, "
        f"max|ego.offset|={max_abs_offset}, start_lane={start_lane}, "
        f"half_width_start={half_width_start}."
    ]


def main() -> int:
    if not os.path.exists(vrt.DLL):
        print(
            f"FAIL: DLL not found at {vrt.DLL} -- run /build first (not doing it automatically)"
        )
        return 1

    lines = []

    def w(s: str = ""):
        print(s)
        lines.append(s)

    w("=" * 78)
    w("feature:F7 lane-offset semantics probe -- ONE question: during the lane-width")
    w(
        "lateral offset maneuver, does ego.lane change, and what is ego.offset each moment?"
    )
    w("=" * 78)
    dll_mtime = time.strftime(
        "%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(vrt.DLL))
    )
    w(f"DLL under test: {vrt.DLL}  mtime={dll_mtime}")
    w(f"scenario base : {vrt.BASE_XOSC}")
    w(f"config (SHIPPED, no overrides applied by this probe): {vrt.SHIPPED_CFG}")

    w("\n--- road 0 lane geometry (parsed from resources/xodr/e6mini.xodr) ---")
    xodr_info = _parse_xodr_road0_lanes(XODR_PATH)
    for lane in xodr_info["lanes"]:
        w(
            f"  lane {lane['id']:>3}  type={lane['type']:<8}  width={lane['width_m']:.3f}m"
        )

    w(
        "\n--- preflight instrument self-check (RAW telemetry, run BEFORE the maneuver) ---"
    )
    pre = _preflight_instrument_check()
    w(
        f"  PASS: raw ego telemetry carries lane/track/offset. ego keys={pre['ego_keys']}"
    )
    w(f"  sample raw ego block: {pre['sample_ego']}")

    w(
        f"\n--- run_network_arm_lane_shift(target_offset_m={TARGET_OFFSET_M:g}, speed_mps={SPEED_MPS:g}, "
        "envelope_enabled=True, jerk_max=None, snap_max=None) ---"
    )
    w(
        "  jerk_max=None/snap_max=None => SHIPPED config values used untouched (currently "
        "ad_steering_envelope_enabled=true [same as the True passed here -- a no-op], "
        "ad_steering_envelope_steer_jerk_max=0.0/disabled). input_type is switched stub->network "
        "only so this probe can drive the synthetic manual steering -- that is harness plumbing, "
        "not an AD behavioral override. Native stdout is NOT suppressed (see module docstring)."
    )
    result = rrf.run_network_arm_lane_shift(TARGET_OFFSET_M, SPEED_MPS, True)
    frames = result["frames"]
    maneuver_ok, diag = result["maneuver_ok"], result["maneuver_diag"]
    w(f"  maneuver_ok(converged)={maneuver_ok}  diag={diag}")

    w(
        "\n--- route/lane-departure check (reused: resume_ride_feel._route_departure_check) ---"
    )
    route_check = rrf._route_departure_check(frames)
    w(f"  {route_check}")

    w(
        f"\n--- per-frame table (downsampled every {SAMPLE_DT_S}s; {len(frames)} raw frames total) ---"
    )
    w(f"  {'t[s]':>8}  {'phase':<14}  {'track':>6}  {'lane':>5}  {'offset[m]':>10}")
    for f in _downsample(frames, SAMPLE_DT_S):
        w(
            f"  {_cell(f['sim_time'], 8, 3)}  {_cell(f.get('phase')):<14}  "
            f"{_cell(f.get('ego_track'), 6)}  {_cell(f.get('ego_lane'), 5)}  "
            f"{_cell(f.get('ego_offset'), 10, 4)}"
        )

    w("\n--- every frame where ego.lane or ego.track CHANGES ---")
    events = _lane_track_changes(frames)
    if not events:
        w("  (none -- ego.lane and ego.track were constant for the entire run)")
    for e in events:
        w(
            f"  t={e['t']:.3f}s phase={e['phase']}  track {e['track_before']} -> {e['track_after']}  "
            f"lane {e['lane_before']} -> {e['lane_after']}  offset {_cell(e['offset_before'], 0, 4)} -> "
            f"{_cell(e['offset_after'], 0, 4)}"
        )

    width_by_id = {l["id"]: l["width_m"] for l in xodr_info["lanes"]}

    offsets = [f["ego_offset"] for f in frames if f.get("ego_offset") is not None]
    max_abs_offset = max(abs(o) for o in offsets) if offsets else None
    w(f"\nmax|ego.offset| over the whole run: {max_abs_offset}")

    # THE decisive comparison -- see _beyond_lane_edge(). Reported at both the
    # peak-|offset| frame and the AUTO_RESUME edge, because the harness's own
    # route-departure check cannot distinguish "inside the adjacent lane" from
    # "past the outermost driving lane with ego.lane clamped".
    w("\n--- beyond-lane-edge check (|ego.offset| vs that frame's lane half-width) ---")
    peak_frame = max(
        (f for f in frames if f.get("ego_offset") is not None),
        key=lambda f: abs(f["ego_offset"]),
        default=None,
    )
    if peak_frame is not None:
        b = _beyond_lane_edge(peak_frame, width_by_id)
        w(
            f"  at peak |offset|  (t={peak_frame['sim_time']:.3f}s, phase={peak_frame.get('phase')}): "
            f"{b['verdict'] if b else 'NOT MEASURED (missing lane/offset)'}"
        )

    resume_idx = next(
        (i for i, f in enumerate(frames) if f.get("auto_transition")), None
    )
    if resume_idx is not None:
        rf = frames[resume_idx]
        w(
            f"\nAUTO_RESUME edge (first override.auto_transition=true): idx={resume_idx} "
            f"t={rf['sim_time']:.3f}s  ego.track={rf.get('ego_track')}  ego.lane={rf.get('ego_lane')}  "
            f"ego.offset={rf.get('ego_offset')}"
        )
        b = _beyond_lane_edge(rf, width_by_id)
        w(
            f"  at AUTO_RESUME edge: {b['verdict'] if b else 'NOT MEASURED (missing lane/offset)'}"
        )
    else:
        w(
            "AUTO_RESUME edge: NOT FOUND -- no frame had override.auto_transition true. Treat as "
            "NOT MEASURED for this quantity (see route/lane-departure check above for why)."
        )

    w("\n" + "=" * 78)
    w("VERDICT (decided strictly from the numbers above):")
    for line in _classify(frames, events, xodr_info["lanes"], maneuver_ok, diag):
        w("  " + line)
    w("=" * 78)

    os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_TXT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"\nfull report written: {OUT_TXT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
