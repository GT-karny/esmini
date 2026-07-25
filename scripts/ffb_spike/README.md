# F7b Day-1 Spike — FFB Steering Actuation (research)

> **続編あり → [`CHARACTERIZATION.md`](CHARACTERIZATION.md)（Day-2）**
> 本書 (Day-1) の計測は**振幅 ±0.45 の周期波形のみ**で、実 AD 操舵が要求する ±0.035 の微小信号領域を
> 一度も測っていない。実機で「ホイールが動かない」不具合はその未計測領域で起きた。
> Day-2 では静止摩擦マップ・微小信号デッドバンド・力→速度・**実 AD 操舵時系列の再生**・
> 多成分合成の再現・stiction 対策 5 手法の比較を実測し、推奨設計を確定させている。
> **§1e の Kp/max_force 校正値は大振幅でのみ有効**であり、微小信号では別途摩擦補償が要る（Day-2 §4）。

**Date**: 2026-07-25 · **Wheel**: Logitech G29 · **Env**: Python 3.12 / PySDL2 0.9.17 / pysdl2-dll 2.32.10 (SDL 2.32.10) / Windows 11

Purpose: before committing C++ IFFBSink code for F7b (FFB target-angle tracking + position-error torque proxy for AD⇄manual override), verify feasibility and gather implementation-shaping numbers.

**No C++ / product code was touched. All work is under `scripts/ffb_spike/`.**

---

## 0. Reproducing

```
scripts/ffb_spike/.venv/Scripts/python.exe 01_probe_g29.py
scripts/ffb_spike/.venv/Scripts/python.exe 02_spring_follow.py --yes
scripts/ffb_spike/.venv/Scripts/python.exe 03_spring_damper_follow.py --yes --spring 0.55 --damper 0.55
scripts/ffb_spike/.venv/Scripts/python.exe 03_spring_damper_follow.py --yes --spring 0.95 --damper 0.30
scripts/ffb_spike/.venv/Scripts/python.exe 04_constant_pid_servo.py --yes --kp 4.0 --kd 0.35 --max_force 0.6
scripts/ffb_spike/.venv/Scripts/python.exe 05_torque_proxy.py --yes --mode spring   # NOT YET RUN
scripts/ffb_spike/.venv/Scripts/python.exe 05_torque_proxy.py --yes --mode pid      # DONE
```

All raw logs: `logs/*.csv` + `logs/*.json`. Amplitude cap in every follow test: ±45% of full lock (±202° on the G29 900° range).

---

## 1. Question 1: SPRING target-angle following — feasibility & numbers

### 1a. Probe (script 01)
G29 exposes: **CONSTANT, SPRING, DAMPER, FRICTION, GAIN**. `autocenter=False`. `SDL_HapticNumAxes = 1`. 128 effect slots.
Full result: `logs/01_probe_g29.json`.

### 1b. PITFALL — SPRING creation on G29 requires an explicit CARTESIAN direction

`SDL_HapticNewEffect(SPRING)` returns `-1 "Unable to create effect"` UNLESS `condition.direction.type = SDL_HAPTIC_CARTESIAN` and `direction.dir[0] = 1` are set — even though SDL docs treat condition-effect direction as ignored (see `_diag_spring.py` variants matrix).

Filling `right_coeff[1..2]` / `left_coeff[1..2]` on a 1-axis device also causes creation failure — only fill `[0]`.

**Implication for existing C++ code**: `GT_esmini/src/control/manualdrive/SDLFFBSink.cpp` (`SDLFFBSink::Init`) omits the direction for the SPRING and DAMPER effects. On G29 the spring/damper effect IDs therefore fail to create, `LOG_WARN "Spring effect unsupported"` fires, `has_spring_ = false`, and the sink **silently falls back to constant-force emulation** for the entire lifetime of the session. The visible behavior is fine because the emulation covers SAT/friction/damping via constant force — but this masks a real bug that F7b will hit head-on when it tries to use the SPRING for target tracking.

### 1c. SPRING alone — underdamped 2nd-order response

Script 02, `Kp=0.55, Sat=0.90`, 250 Hz update, `SDL_HapticSetGain` not called (default gain). Wheel following measured against the commanded SPRING center:

| Profile | |err|·mean (frac) | |err|·max (frac) | Real loop Hz |
|---|---|---|---|
| step ±0.45 (period 4s) | 0.145 | 0.475 | 250 |
| sine 0.5 Hz amp 0.45 | **0.361** | 0.569 | 250 |
| chirp 0.1→3 Hz amp 0.45 | 0.295 | 0.675 | 250 |

`logs/02_spring_follow_*.csv` + `logs/02_spring_follow_summary.json`.

Reading the sine CSV point-by-point: the wheel is oscillating in *quadrature* with the target — at target crossings (~0) it is at ±0.40, at target extrema (±0.45) it is near 0. Classic no-damping mass-spring response.

### 1d. SPRING + DAMPER — no measurable damping benefit

Script 03, two runs (`Kp/Kd = 0.55/0.55` and `0.95/0.30`, `SetGain` not called). Sine 0.5 Hz stayed at ~34%–36% mean error — DAMPER effect had **no visible effect on the following error**. Numbers stable across both configs.

`logs/03_spring_damper_summary_*.json`.

Hypothesis (not conclusively verified in this spike): Logitech G29 driver's SPRING/DAMPER coefficient scale is compressed relative to CONSTANT level scale. This matches long-standing DirectInput-era G-series behavior notes in the community. `SDL_HapticSetGain(100)` was tested in the PID path (below) and worked; a controlled re-test of SPRING with `SetGain(100)` should be run before ruling SPRING out permanently — but the answer likely won't move the needle enough to matter.

### 1e. Constant-force PID servo — usable following

Script 04, PID against `SDL_HAPTIC_CONSTANT` at 250 Hz, `Kp=4.0 Kd=0.35 max_force=0.6`, `SetGain(100)`. Same amplitude cap.

| Profile | |err|·mean (frac) | |err|·max (frac) | |u|·max |
|---|---|---|---|---|
| step ±0.45 | 0.124 | 0.461 | 0.60 |
| sine 0.5 Hz amp 0.45 | **0.166** | 0.596 | 0.60 |
| chirp 0.1→3 Hz amp 0.45 | 0.231 | 0.460 | 0.60 |

`logs/04_pid_summary_kp400_kd35.json`.

Roughly **half the mean-error of SPRING+DAMPER** on the sine test, with commanded force saturating at 60% of the wheel's max. The step test settles to within a few percent by the end of each 1s hold (see CSV). The chirp is still noisy past ~1.5 Hz — expected, wheel inertia dominates at those rates.

### 1f. PITFALL — CONSTANT sign convention on G29

`SDL_HAPTIC_CONSTANT` with **positive** level pushes the wheel to **negative** axis (LEFT on G29). Axis positive = wheel turned RIGHT. So to servo toward `target > 0`, the commanded force must be `-Kp*(target - actual)`.

I discovered this the hard way — the initial PID run without the sign flip drove the wheel into the −1.0 hard stop and held it there. Fixed in script 04 with a `-(...)` on `u`. **The existing C++ SDLFFBSink uses `-lat_accel * sat_gain` — same convention, so the sign is already right there** — but any new code path (F7b target-tracking) needs this note.

Added-in-fix: script 04 also attenuates force near the hard stops (`if |actual| > 0.85 and force pushes further outward, taper to zero`).

### 1g. First-question answer

- Native SPRING target-following on G29 via SDL2 is **feasible in principle** (once the CARTESIAN direction pitfall is dealt with) but **not usable in practice** at default gain — coefficient scale is too weak to overcome wheel inertia at the target rates AD steering commands can reach.
- **Constant-force PID servo** (target angle → PID → constant force level, updated at 250 Hz) is the practical answer, and it fits the existing SDLFFBSink emulation infrastructure directly.
- 250 Hz update is fine; no evidence of the driver dropping updates. Real loop rate 250.05–250.09 Hz across every profile.

---

## 2. Question 2: Torque proxy — signal quality (USER-IN-THE-LOOP)

### 2a. Coverage

- **PID mode (`--mode pid`)**: run. `logs/05_torque_proxy_summary_pid.json` + 6 phase CSVs.
- **SPRING mode (`--mode spring`)**: **NOT RUN**. No `logs/05_torque_proxy_spring_*.csv` were produced. User selected "both modes" but only the PID mode result is on disk.

Given §1's finding that SPRING alone is too weak for target following on G29, PID-mode data is the more relevant of the two anyway — but the SPRING-mode measurement is worth doing to close the record.

### 2b. Per-phase result (PID mode, target = 0, `Kp=4.0 Kd=0.35 max_force=0.6 gain=100`)

Signal is `dev = actual_axis_fraction - 0`. |u| is the commanded FFB force (0..0.6 clipped). Stats drop the first 0.5s of each phase to avoid the countdown-tail transient.

| Phase | dev·mean | dev·std | dev·max | dev·p95 | |u|·mean | |u|·max |
|---|---|---|---|---|---|---|
| idle_1 (no touch)    | −0.0159 | 0.000  | 0.0159 | 0.0159 | 0.06 | 0.06 |
| rest (finger on)     | −0.0134 | 0.0002 | 0.0156 | 0.0133 | 0.05 | 0.06 |
| light (gentle push)  | −0.0221 | 0.0028 | 0.0268 | 0.0268 | 0.09 | 0.12 |
| medium (moderate)    | **−0.0563** | 0.0056 | 0.0669 | 0.0655 | **0.23** | 0.28 |
| firm (assertive)     | **−0.1189** | 0.0262 | 0.1691 | 0.1595 | **0.48** | 0.60 |
| idle_2 (release)     | −0.0072 | 0.0422 | 0.2261 | 0.1115 | 0.00 | 0.60 |

Note the mean is consistently negative — the user pushed one consistent direction (per instructions). Good.

`idle_1` shows the wheel resting at −0.0159 axis-frac with std=0. That's a **hardware center offset** (fixed axis bias with no torque applied). In production the "target=0" reference should be calibrated to the actual rest position, not raw axis 0. PID commanded −0.06 constant force to compensate, so the actual servo error at rest is ≈0.

### 2c. Signal separability

- **Noise floor** (idle_1, rest): dev p95 ≤ 0.016, |u| ≤ 0.06.
- **"Light" push** (finger held steady): dev p95 = 0.027, |u| = 0.12. **~1.7× above noise.** Barely distinguishable — sensitive to hand tremor.
- **"Medium" push** (assertive correction intent): dev p95 = 0.066, |u| = 0.28. **~4× above noise.** Cleanly separable.
- **"Firm" push** (clear override intent): dev p95 = 0.16, |u| = 0.60 (saturated). **~10× above noise.** Trivially separable.

### 2d. Release transient — a real false-positive risk

idle_2 dev_max = 0.226 and |u|_max = 0.60 — the PID saturates for ~0.4 s pulling the wheel back to center after the user releases from firm hold. From the CSV:

```
t=0.0..0.5s : wheel at −0.235 (residual firm hold), PID u = −0.6 (saturated)
t=0.5..0.9s : wheel returns to near zero
t=0.9s onward : |dev| < 0.005, |u| < 0.05
```

**Detection design implication**: a threshold-only rule on `|u|` or `|dev|` will re-trigger during the release swing. F7b needs one of:
1. Hysteresis + debounce ≥ 500 ms after crossing back below release threshold.
2. Latch: once fired, only clear on the AUTO_RESUME button (bit 7) — which is what the F7 override already does. In that model the release transient doesn't matter because we're already latched into manual.
3. Rate check: an "intervention" requires sustained `|u| > θ` for N ms.

Design (2) is the right one and it already exists in `OverrideManager`. The steering-torque path just needs to **feed the latch, not gate on release**.

### 2e. Threshold candidates (frac-based)

- **|u| > 0.20**: fires cleanly on medium+firm, ignores rest/light. Recommended primary. (In C++ with `max_force=1.0` this is |command| > ~1/3 of full torque.)
- **|dev| > 0.04**: dev-based fallback if `|u|` isn't available in the check path. Fires on medium+firm.
- **Combined**: `(|u| > 0.20 OR |dev| > 0.04) sustained ≥ 100 ms → latch to manual` (with 500 ms debounce on the latch-clear path).

### 2f. Second-question answer

- Position-error torque proxy is **usable as an intervention signal** in the medium-and-above regime with the PID-servo backend and the thresholds above.
- The "light touch" regime overlaps noise; if the product needs to fire on light corrections, additional filtering (high-pass on dev, or accumulate `|u|·dt` energy) will be needed. Recommend NOT firing on light — matches "assertive override" intent of F7.
- The release transient is real but is a non-issue once the F7 latch model is used (already the case).
- **SPRING-mode measurement is the missing piece** — its noise floor and separability may look different because SPRING doesn't have the servo's release-overshoot. Worth running when convenient (add to F7b Day-2).

---

## 3. Question 3: C++ IFFBSink implementation guidance

### 3a. Recommended architecture for F7b's target-tracking

Extend `IFFBSink` with one new call and one new state input:

```cpp
// IFFBSink additions
virtual void SetSteerTarget(double target_wheel_rad,  // AD's commanded wheel angle
                            bool active) = 0;         // false → disable target-tracking
```

`SDLFFBSink` (concrete) reuses its existing `emulate_via_constant_` combined-force path and adds a new component:

```cpp
// Inside UpdateCombinedConstantForce, after SAT/Friction/Damping/SoftStop:
double target_track = 0.0;
if (target_active_) {
    double err = target_wheel_rad_ - steering_pos;  // radians
    double derr = (err - prev_target_err_) / dt;
    prev_target_err_ = err;
    // Sign: positive err (target > actual) means "turn right further" → LEFT push
    // to axis-right sign, i.e. negative constant force. See spike §1f pitfall.
    target_track = -(target_kp_ * err + target_kd_ * derr) * manual_ratio;
    target_track = std::clamp(target_track, -target_max_force_, target_max_force_);
}
total = sat + friction + damping + soft_stop + target_track;
```

Concurrently expose the intervention signal:

```cpp
// Getter for OverrideManager to consume
struct FFBInterventionSample {
    double position_error_rad;  // target - actual
    double commanded_force;     // last |u|
    bool   active;              // target-tracking on
};
FFBInterventionSample SDLFFBSink::GetInterventionSample() const;
```

`OverrideManager` gets a new steering-threshold check (added alongside the existing pedal-threshold checks):
```cpp
if (ffb_sink_ && ffb_sink_->GetInterventionSample().active) {
    auto s = ffb_sink_->GetInterventionSample();
    if (std::abs(s.commanded_force) > config_.override_steer_force_threshold ||
        std::abs(s.position_error_rad) > config_.override_steer_dev_threshold_rad) {
        steer_intervention_ = true;  // fed into the existing latch
    }
}
```

### 3b. Fix the SPRING-direction bug at the same time

Whether or not F7b chooses to use SPRING, the current SDLFFBSink silently loses the SPRING/DAMPER paths on G29. Two lines in `SDLFFBSink::Init` fix it:

```cpp
effect.condition.direction.type   = SDL_HAPTIC_CARTESIAN;
effect.condition.direction.dir[0] = 1;
```
(applied to both SPRING and DAMPER effect setup). This bug fix should either land as part of F7b or as a standalone patch. It affects wheels with capable SPRING but no CONSTANT — G29 has CONSTANT so behavior is unchanged there, but the fix is cheap and correct.

### 3c. Config additions (`manual_drive.json → ffb`)

```json
"target_track": {
    "enabled": false,           // opt-in, default OFF
    "kp": 4.0,                  // servo P gain
    "kd": 0.35,                 // servo D gain
    "max_force": 0.6,           // per-tick cap (0..1)
    "override_steer_force_threshold": 0.20,   // |u| above this → intervention
    "override_steer_dev_threshold_rad": 0.18  // ~0.04 axis-frac × 450° × π/180
}
```

Numbers from this spike (§2e). Keep gain=100 on `SDL_HapticSetGain` (existing behavior is default, i.e. probably 100 already — verify in F7b unit test).

### 3d. What NOT to do

- **Do not** rely on SDL SPRING+DAMPER conditions alone for target tracking on G29 (§1c, §1d).
- **Do not** measure the intervention signal from `|dev|` alone — it overlaps noise until `|dev| > ~0.04 frac`. Use `|u|` (commanded force) as the primary channel.
- **Do not** gate the latch-clear path on `|dev|` — the release transient will re-fire it. Latch-clear stays on AUTO_RESUME (bit 7), already implemented.
- **Do not** issue full torque near hard stops — port the near-lock attenuation from script 04.

### 3e. F7b effort estimate

- IFFBSink API extension + config + coordinator wiring: 0.5 day
- SDLFFBSink target-tracking (PID) + intervention sample: 0.5 day
- Fix SPRING direction bug: 30 min
- OverrideManager steering-threshold path + unit tests: 0.5 day
- Web GUI "AD wheel following" toggle + telemetry surface: 0.5 day
- Manual bench verification with G29 (bench setup, kickoff-style spike shortcuts do not substitute): 0.5 day

**Total: ~2.5 dev-days**, plus one calibration session with the real G29 to lock in `override_steer_force_threshold` (default 0.20 is a good starting point per this spike).

Pre-conditions:
- The existing OverrideManager latch model stays as-is (F7 acceptance).
- `SDL_HapticSetGain(100)` should be verified as the actual runtime gain in a unit-adjacent test (this spike showed it works but did not compare gain=100 vs gain=50 explicitly).

---

## 4. Environment traps (raw text — memory-integration candidates for PM)

1. **SPRING requires CARTESIAN direction on G29 / SDL2 / DirectInput** — silent creation failure otherwise. Not documented in SDL_haptic guide. Existing SDLFFBSink.cpp is affected.
2. **`SDL_HapticNumAxes = 1` on G29**; filling `right_coeff[1..2]` / `left_coeff[1..2]` also causes creation failure.
3. **CONSTANT sign convention on G29**: positive level → LEFT wheel motion → NEGATIVE axis. C++ SAT computation with `-lat_accel * sat_gain` matches; any new servo code needs the flip.
4. **G29 hardware center offset**: axis rest position ≠ 0 (measured −0.016 axis-frac idle). Production code must calibrate at boot.
5. **PySDL2 module surface**: `sdl2.ctypes` does not exist — use `import ctypes` directly. `sdl2.SDL_HAPTIC_INFINITY` IS exposed (= 0xFFFFFFFF).
6. **G29 exposes 3 standalone haptic devices** (via `SDL_NumHaptics()`) alongside the 1 joystick — the wheel is at joystick index 0 with `SDL_JoystickIsHaptic == true`; ignore the standalone entries for the wheel path.
7. **PID release transient on constant-force servo**: ~0.4 s of dev>0.10 after user releases from firm hold. Threshold-only detection will re-fire; the F7 latch model already handles this correctly.
8. **PowerShell `*>` redirects to UTF-16** — spike used stdout-only, no grep completion checks needed.

---

## 5. What was NOT done

- **SPRING-mode torque proxy measurement** (script 05 `--mode spring`): the user selected "SPRING + PID both" but only PID output landed on disk. Suggest running it in a follow-up session (kickoff-style, one 40s user-in-loop session).
- **SPRING with `SDL_HapticSetGain(100)`**: was set in the PID path but not re-tested in the SPRING follow paths (02, 03). Might improve SPRING numbers, unlikely to change the "PID is better" verdict.
- **Longer-duration stability test** (30-60 min continuous FFB): out of Day-1 scope. Recommended before F7b ships.
- **Alternate wheels** (Thrustmaster, Fanatec): only G29 tested. All findings above are conditional on Logitech's DirectInput driver behavior.

---

## Files

- `01_probe_g29.py` — device probe (no FFB emitted)
- `02_spring_follow.py` — SPRING center-tracking, unmanned
- `03_spring_damper_follow.py` — SPRING+DAMPER combined, unmanned
- `04_constant_pid_servo.py` — constant-force PID servo, unmanned
- `05_torque_proxy.py` — user-in-loop deviation measurement
- `_diag_spring.py` — SPRING effect-creation variant matrix (diagnostic; keep for future porting evidence)
- `logs/*.csv` `logs/*.json` — raw measurements
