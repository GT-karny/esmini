#!/usr/bin/env python
"""feature:F7 — tljunction が最悪ケースである条件を実測から特定する（分析専用）。

## なぜ要るか

`decelerate_for_right_turn` と `traffic_lights_junction` は残差ピーク時点の指令躍度
（`target_norm` の2階微分）がほぼ同値なのに、しきい値に対するマージンが違う。
躍度だけでは tljunction を最悪ケースとして選ぶ根拠にならない。本スクリプトは
躍度以外に候補となる量（速度・目標のレート/反転回数/滞留、力の性質、残差の
立ち上がり方）を実測から横並びで数値化し、どれが tljunction を特異にしているかを
消去法で絞り込む。

**このスクリプトはモデルの再実装ではない。** `residual`/`shadow_norm`/`actual_norm`/
`effective_force` は製品 `OverrideManager` 自身が出した値をそのまま読む
（`shadow_margin_report.py` / `residual_decompose.py` と同じ方針）。シャドウ定数
（KINETIC/BRK_HI/BRK_LEFT/BRK_RIGHT/SLOPE/VMAX）はレジーム分類・帯滞留の境界値と
してのみ使い、計算には使わない。

## 決め打ちしたしきい値（自分で決めた値。以下に理由を明記する）

  RATE_SIGN_EPS   = 0.02  /s   `target_norm` レートの符号判定ノイズ床。
                              全フレームの nonzero レートのうち `0<|rate|<0.02` は
                              tljunction で 145/3500、right_turn で 118/2500 のみ
                              （実測。量子化ノイズとみなせる規模）。
  FORCE_SIGN_EPS  = 0.01       `effective_force` の符号判定ノイズ床。
                              `0<|f|<0.01` は tljunction 4/3502, right_turn 8/2502
                              （basic は 606/2002 — ほぼ無操舵区間が多いため別）。
  MOVING_RATE_EPS = 0.05  /s   タスク指定。「目標が動いている」判定。
  STOP_SPEED      = 0.5  m/s   タスク指定。停止判定。
  MIN_STOP_DURATION = 0.3 s    停止→発進イベントの最小停止継続時間（速度が0.5m/s
                              付近でチャタリングした瞬間を1イベントと誤カウント
                              しないための下限）。
  PEAK_WINDOW     = ±1.0 s     「残差ピーク近傍」の定義。ピーク時刻を中心に前後1秒。
  RES_EPISODE_THR = 0.02/0.03/0.04  タスク指定。残差エピソード検出のしきい値。

## 使い方

    python worst_case_conditions.py --jsonl run1.jsonl --jsonl run2.jsonl [...]
"""

from __future__ import annotations

import argparse
import io
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _shipped_config  # noqa: E402

# シャドウの出荷値（GT_esmini/config/virtual_driver.json から読む — 較正で
# configが変わっても追随するよう、ここでは再宣言しない。feature:F7 wheel
# autocal要件 sec 6-1/7-5）。レジーム境界としてのみ使う。
_sc = _shipped_config.load()
KINETIC = _sc.kinetic
BRK_HI = _sc.brk_hi
BRK_LEFT = _sc.brk_left    # force >= 0 側（ホイールを左へ押す）の breakaway 帯下端
BRK_RIGHT = _sc.brk_right  # force <  0 側（ホイールを右へ押す）の breakaway 帯下端
SLOPE = _sc.slope
VMAX = _sc.vmax

RATE_SIGN_EPS = 0.02
FORCE_SIGN_EPS = 0.01
MOVING_RATE_EPS = 0.05
STOP_SPEED = 0.5
MIN_STOP_DURATION = 0.3
PEAK_WINDOW = 1.0
RES_EPISODE_THRESHOLDS = (0.02, 0.03, 0.04)
TOPN = 20
PRECURSOR_WINDOW = 15  # ピーク手前何フレームまで「引き起こしたイベント」を探すか

# GT_esmini/config/virtual_driver.json: ffb_target_track_override_sustain_time。
# residual がしきい値を超えてもこの秒数だけ継続しないと MANUAL へは偽ラッチしない。
LATCH_SUSTAIN_TIME = 0.1


# --------------------------------------------------------------------------- #
# 読み込み・前処理
# --------------------------------------------------------------------------- #

def load(path: Path) -> list[dict]:
    rows = []
    for line in io.open(path, encoding="utf-8"):
        line = line.strip()
        if line:
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    if not rows:
        raise SystemExit(f"no frames in {path}")
    return rows


def band_bottom(f: float) -> float:
    """force の符号に応じた breakaway 帯下端（OverrideManager.cpp と同じ規則）。"""
    return BRK_LEFT if f >= 0.0 else BRK_RIGHT


def build_series(rows: list[dict]) -> list[dict]:
    """毎フレームをフラット化し、target_norm のレート/躍度と |f| の変化率を
    実測 dt（固定 0.01 を仮定しない）で計算して付加する。"""
    S = []
    for r in rows:
        ego = r["ego"]
        ffb = r["ffb"]
        g = ffb["gates"]
        env = r.get("envelope", {})
        drv = r.get("driver", {})
        ov = r.get("override", {})
        S.append(dict(
            t=float(r["sim_time"]),
            ov_lateral=bool(ov.get("lateral", False)),
            ov_manual_transition=bool(ov.get("manual_transition", False)),
            speed=float(ego["speed"]),
            h=float(ego.get("h", 0.0)),
            lane=ego.get("lane"),
            track=ego.get("track"),
            s=float(ego.get("s", 0.0)),
            target=float(ffb.get("target_norm", 0.0)),
            cmd_force=float(ffb.get("commanded_force", 0.0)),
            pos_err=float(ffb.get("position_error", 0.0)),
            actual=float(g.get("actual_norm", 0.0)),
            shadow=float(g.get("shadow_norm", 0.0)),
            residual=float(g.get("residual", 0.0)),
            eff_force=float(g.get("effective_force", 0.0)),
            shadow_moving=bool(g.get("shadow_moving", False)),
            sustain=float(g.get("sustain_accum", 0.0)),
            target_rate_gate=float(g.get("target_rate", 0.0)),
            derror_rate=float(g.get("derror_rate", 0.0)),
            block_reason=g.get("block_reason", ""),
            steer_in=float(env.get("steer_in", 0.0)),
            steer_out=float(env.get("steer_out", 0.0)),
            lat_active=bool(env.get("lateral_accel_active", False)),
            yaw_active=bool(env.get("yaw_rate_active", False)),
            steer_rate_active=bool(env.get("steer_rate_active", False)),
            env_active=bool(env.get("active", False)),
            drv_steer=float(drv.get("steer", 0.0)),
            lat_err=float(drv.get("lateral_error", 0.0)),
            head_err=float(drv.get("heading_error", 0.0)),
            lookahead=float(drv.get("lookahead", 0.0)),
        ))

    n = len(S)
    rate = [0.0] * n
    dfdt = [0.0] * n
    for i in range(1, n):
        dt = S[i]["t"] - S[i - 1]["t"]
        if dt <= 0:
            continue
        rate[i] = (S[i]["target"] - S[i - 1]["target"]) / dt
        dfdt[i] = (abs(S[i]["eff_force"]) - abs(S[i - 1]["eff_force"])) / dt
    jerk = [0.0] * n
    for i in range(1, n):
        dt = S[i]["t"] - S[i - 1]["t"]
        if dt <= 0:
            continue
        jerk[i] = (rate[i] - rate[i - 1]) / dt

    for i, s in enumerate(S):
        s["rate"] = rate[i]
        s["jerk"] = jerk[i]
        s["dfdt"] = dfdt[i]
    return S


# --------------------------------------------------------------------------- #
# 統計ヘルパ
# --------------------------------------------------------------------------- #

def percentile(sorted_xs: list[float], p: float) -> float:
    if not sorted_xs:
        return float("nan")
    n = len(sorted_xs)
    k = (n - 1) * p
    f, c = math.floor(k), math.ceil(k)
    if f == c:
        return sorted_xs[int(k)]
    return sorted_xs[f] + (sorted_xs[c] - sorted_xs[f]) * (k - f)


def dist3(xs: list[float]) -> tuple[float, float, float]:
    """median / p99 / max。空なら NaN 3つ。"""
    if not xs:
        return float("nan"), float("nan"), float("nan")
    s = sorted(xs)
    return percentile(s, 0.5), percentile(s, 0.99), s[-1]


def dist_median_p95_max(xs: list[float]) -> tuple[float, float, float]:
    if not xs:
        return float("nan"), float("nan"), float("nan")
    s = sorted(xs)
    return percentile(s, 0.5), percentile(s, 0.95), s[-1]


def pearson(ys: list[float], xs: list[float]) -> float:
    n = len(xs)
    if n < 3:
        return float("nan")
    mx, my = sum(xs) / n, sum(ys) / n
    sx = math.sqrt(sum((x - mx) ** 2 for x in xs))
    sy = math.sqrt(sum((y - my) ** 2 for y in ys))
    if sx == 0.0 or sy == 0.0:
        return float("nan")
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / (sx * sy)


def sign_reversals(values: list[float], eps: float) -> int:
    reversals = 0
    last_sign = 0
    for v in values:
        if abs(v) < eps:
            continue
        s = 1 if v > 0 else -1
        if last_sign != 0 and s != last_sign:
            reversals += 1
        last_sign = s
    return reversals


def moving_segments(sub: list[dict], eps: float) -> list[tuple[int, float]]:
    """|rate|>eps が連続するフレームを1区間とし、(frame数, 継続秒数) のリストを返す。"""
    segs: list[tuple[int, float]] = []
    cur_len = 0
    cur_dur = 0.0
    for i in range(len(sub)):
        moving = abs(sub[i]["rate"]) > eps
        if moving:
            dt = (sub[i]["t"] - sub[i - 1]["t"]) if i > 0 else 0.0
            cur_len += 1
            cur_dur += dt
        else:
            if cur_len > 0:
                segs.append((cur_len, cur_dur))
            cur_len, cur_dur = 0, 0.0
    if cur_len > 0:
        segs.append((cur_len, cur_dur))
    return segs


def count_stop_start_events(S: list[dict]) -> int:
    events = 0
    below_duration = 0.0
    prev_below = S[0]["speed"] < STOP_SPEED
    for i in range(1, len(S)):
        dt = S[i]["t"] - S[i - 1]["t"]
        cur_below = S[i]["speed"] < STOP_SPEED
        if prev_below:
            below_duration += dt
        if prev_below and not cur_below:
            if below_duration >= MIN_STOP_DURATION:
                events += 1
            below_duration = 0.0
        elif not cur_below:
            below_duration = 0.0
        prev_below = cur_below
    return events


def threshold_episodes(S: list[dict], thr: float) -> list[float]:
    """residual > thr が連続する区間ごとの継続秒数（近似: 開始フレームの t から
    しきい値を下回った最初のフレームの t まで）。"""
    episodes = []
    in_ep = False
    start_t = 0.0
    for s in S:
        above = s["residual"] > thr
        if above and not in_ep:
            in_ep = True
            start_t = s["t"]
        elif not above and in_ep:
            in_ep = False
            episodes.append(s["t"] - start_t)
    if in_ep:
        episodes.append(S[-1]["t"] - start_t)
    return episodes


def check_latch(S: list[dict]) -> dict:
    """MANUAL への偽ラッチの有無と時刻を `override.lateral`/`manual_transition` から判定する。
    ラッチが無い場合でも sustain_accum の最大値と LATCH_SUSTAIN_TIME との差（余裕）を報告する
    ——「しきい値には触れたが継続時間が足りず不発だった」かどうかが分かるようにするため。"""
    latch_idx = None
    for i, s in enumerate(S):
        if s["ov_lateral"] or s["ov_manual_transition"]:
            latch_idx = i
            break
    max_sustain = max(s["sustain"] for s in S)
    max_sustain_t = next(s["t"] for s in S if s["sustain"] == max_sustain)
    return dict(
        latched=latch_idx is not None,
        latch_idx=latch_idx,
        latch_t=S[latch_idx]["t"] if latch_idx is not None else None,
        max_sustain=max_sustain, max_sustain_t=max_sustain_t,
        sustain_margin=LATCH_SUSTAIN_TIME - max_sustain,
    )


def find_precursor_jerk(S: list[dict], peak_idx: int, window: int = PRECURSOR_WINDOW) -> dict:
    """残差ピークを引き起こした指令イベントを実測で特定する。シャドウの無駄時間+一次遅れ
    (θ+τ)だけピーク時刻より手前に来るのが通常なので、ピーク直前の window フレームの中で
    |jerk| が最大のフレームを「原因イベント」とみなす。"""
    lo = max(0, peak_idx - window)
    idx = max(range(lo, peak_idx + 1), key=lambda i: abs(S[i]["jerk"]))
    return dict(idx=idx, t=S[idx]["t"], jerk=S[idx]["jerk"], rate=S[idx]["rate"],
                target=S[idx]["target"], f=S[idx]["eff_force"],
                offset_s=S[peak_idx]["t"] - S[idx]["t"])


# --------------------------------------------------------------------------- #
# 分析本体
# --------------------------------------------------------------------------- #

def analyse(path: Path) -> dict:
    rows = load(path)
    S_full = build_series(rows)
    latch = check_latch(S_full)
    if latch["latched"]:
        # ラッチ後は target_norm がもはや AD の通常指令ではない（手放し走行の前提が崩れる）ので、
        # 以降の全指標をラッチ前の区間だけで計算し直す。
        S = S_full[:latch["latch_idx"]]
        latch["note"] = (f"ラッチ検出: t={latch['latch_t']:.2f}s 以降を除外し、"
                          f"以降の全指標は pre-latch 区間（{len(S)}/{len(S_full)} frames）のみで計算")
    else:
        S = S_full
        latch["note"] = "ラッチなし（override.lateral/manual_transition は全区間 False）— 全区間を通常運転として集計"
    n = len(S)
    peak_idx = max(range(n), key=lambda i: S[i]["residual"])
    peak = S[peak_idx]
    precursor = find_precursor_jerk(S, peak_idx)
    t0, t1 = peak["t"] - PEAK_WINDOW, peak["t"] + PEAK_WINDOW
    near = [s for s in S if t0 <= s["t"] <= t1]

    # --- A. 速度 ---
    speeds_sorted = sorted(s["speed"] for s in S)
    near_speeds_sorted = sorted(s["speed"] for s in near)
    stopped_frac = sum(1 for s in S if s["speed"] < STOP_SPEED) / n
    A = dict(
        peak_speed=peak["speed"],
        whole=dist_median_p95_max(speeds_sorted),
        near=dist_median_p95_max(near_speeds_sorted),
        stopped_frac=stopped_frac,
        stop_start_events=count_stop_start_events(S),
    )

    # --- B. 操舵目標 ---
    def target_block(sub: list[dict]) -> dict:
        rates_abs = [abs(s["rate"]) for s in sub]
        jerks_abs = [abs(s["jerk"]) for s in sub]
        targets_abs = [abs(s["target"]) for s in sub]
        revs = sign_reversals([s["rate"] for s in sub], RATE_SIGN_EPS)
        moving_n = sum(1 for s in sub if abs(s["rate"]) > MOVING_RATE_EPS)
        segs = moving_segments(sub, MOVING_RATE_EPS)
        seg_frame_lens = [l for l, _ in segs]
        seg_durs = [d for _, d in segs]
        return dict(
            rate=dist3(rates_abs), jerk=dist3(jerks_abs), target_abs=dist_median_p95_max(targets_abs),
            reversals=revs, moving_frac=moving_n / max(len(sub), 1),
            n_segments=len(segs),
            seg_len_frames=dist_median_p95_max([float(x) for x in seg_frame_lens]),
            seg_dur=dist_median_p95_max(seg_durs),
        )
    B_whole = target_block(S)
    B_near = target_block(near)

    # --- C. 力 ---
    def force_block(sub: list[dict]) -> dict:
        f_abs = [abs(s["eff_force"]) for s in sub]
        dfdt_abs = [abs(s["dfdt"]) for s in sub]
        n_left = sum(1 for s in sub if s["eff_force"] >= 0.0 and KINETIC <= abs(s["eff_force"]) <= BRK_HI)
        n_right = sum(1 for s in sub if s["eff_force"] < 0.0 and KINETIC <= abs(s["eff_force"]) <= BRK_HI)
        n_band = n_left + n_right
        revs = sign_reversals([s["eff_force"] for s in sub], FORCE_SIGN_EPS)
        return dict(
            f_abs=dist_median_p95_max(f_abs), dfdt=dist3(dfdt_abs),
            band_frac=n_band / max(len(sub), 1),
            band_left_frac=n_left / max(len(sub), 1), band_right_frac=n_right / max(len(sub), 1),
            reversals=revs,
        )
    C_whole = force_block(S)
    C_near = force_block(near)

    # --- D. 残差の立ち上がり方 ---
    j = peak_idx
    while j > 0 and S[j - 1]["residual"] <= S[j]["residual"]:
        j -= 1
    rise_start = S[j]
    D = dict(
        rise_start_t=rise_start["t"], rise_duration=peak["t"] - rise_start["t"],
        rise_n_frames=peak_idx - j + 1,
        rise_start_speed=rise_start["speed"], rise_start_force=rise_start["eff_force"],
        rise_start_shadow_moving=rise_start["shadow_moving"], rise_start_rate=rise_start["rate"],
        episodes={thr: threshold_episodes(S, thr) for thr in RES_EPISODE_THRESHOLDS},
    )

    # --- E. 上位残差フレーム ---
    top = sorted(S, key=lambda s: -s["residual"])[:TOPN]

    # --- F. 相関（残差増分 vs 候補量、上昇分のみ） ---
    dres_list: list[float] = []
    cand: dict[str, list[float]] = {k: [] for k in
                                     ("abs_jerk", "abs_rate", "speed", "abs_dfdt", "bb_dist")}
    for i in range(1, n):
        a, b = S[i - 1], S[i]
        dt = b["t"] - a["t"]
        if dt <= 0:
            continue
        dres = b["residual"] - a["residual"]
        if dres <= 0:
            continue
        dres_list.append(dres)
        cand["abs_jerk"].append(abs(b["jerk"]))
        cand["abs_rate"].append(abs(b["rate"]))
        cand["speed"].append(b["speed"])
        cand["abs_dfdt"].append(abs(b["dfdt"]))
        cand["bb_dist"].append(abs(abs(b["eff_force"]) - band_bottom(b["eff_force"])))
    F = {k: pearson(dres_list, v) for k, v in cand.items()}

    return dict(path=path, n=n, n_total=len(S_full), peak=peak, near_n=len(near),
                latch=latch, precursor=precursor,
                A=A, B_whole=B_whole, B_near=B_near, C_whole=C_whole, C_near=C_near,
                D=D, E=top, F=F)


# --------------------------------------------------------------------------- #
# 出力
# --------------------------------------------------------------------------- #

def fmt3(t: tuple[float, float, float]) -> str:
    return f"{t[0]:.4f} / {t[1]:.4f} / {t[2]:.4f}"


def print_run(a: dict) -> None:
    p = a["path"]
    peak = a["peak"]
    print(f"--- {p.name}  ({a['n']}/{a['n_total']} frames after latch filter, "
          f"near-peak window n={a['near_n']}) ---")
    print(f"  [peak] t={peak['t']:.2f}s residual={peak['residual']:.4f} "
          f"speed={peak['speed']:.3f} target={peak['target']:+.4f} "
          f"rate={peak['rate']:+.3f}/s jerk={peak['jerk']:+.2f}/s^2 "
          f"f={peak['eff_force']:+.4f} shadow_moving={peak['shadow_moving']}")
    print()

    L = a["latch"]
    print("  [ラッチ確認: override.lateral / manual_transition / sustain_accum]")
    print(f"    {L['note']}")
    print(f"    override True 初出       = {'t=%.2fs' % L['latch_t'] if L['latched'] else 'なし'}")
    print(f"    sustain_accum 最大        = {L['max_sustain']:.4f}s (t={L['max_sustain_t']:.2f}s)  "
          f"latch閾値={LATCH_SUSTAIN_TIME}s  余裕={L['sustain_margin']:.4f}s"
          + ("" if L["latched"] else "  ※閾値未達=不発（この意味で偽ラッチではない）"))
    print()

    pc = a["precursor"]
    print(f"  [残差ピークを引き起こした指令イベントの実測]（ピーク前 {PRECURSOR_WINDOW} frame 以内で |jerk| 最大）")
    print(f"    t={pc['t']:.2f}s（ピークの{pc['offset_s']:.2f}s前） jerk={pc['jerk']:+.2f}/s^2 "
          f"rate={pc['rate']:+.3f}/s target={pc['target']:+.4f} f={pc['f']:+.4f}")
    print()

    A = a["A"]
    print("  [A: 速度]")
    print(f"    ピーク瞬間速度              = {A['peak_speed']:.3f} m/s")
    print(f"    走行全体 median/p95/max     = {fmt3(A['whole'])} m/s")
    print(f"    ピーク近傍(±{PEAK_WINDOW:.1f}s) median/p95/max = {fmt3(A['near'])} m/s")
    print(f"    停止(<{STOP_SPEED}m/s)フレーム割合    = {A['stopped_frac']*100:.2f}%")
    print(f"    停止→発進イベント数(継続>={MIN_STOP_DURATION}s) = {A['stop_start_events']}")
    print()

    print("  [B: 操舵目標の性質]  (whole / near-peak)")
    Bw, Bn = a["B_whole"], a["B_near"]
    print(f"    |rate|  median/p99/max        whole={fmt3(Bw['rate'])}  near={fmt3(Bn['rate'])}  /s")
    print(f"    |jerk|  median/p99/max        whole={fmt3(Bw['jerk'])}  near={fmt3(Bn['jerk'])}  /s^2")
    print(f"    |target| median/p95/max       whole={fmt3(Bw['target_abs'])}  near={fmt3(Bn['target_abs'])}")
    print(f"    符号反転回数(|rate|>={RATE_SIGN_EPS})     whole={Bw['reversals']}  near={Bn['reversals']}")
    print(f"    |rate|>{MOVING_RATE_EPS} フレーム割合        whole={Bw['moving_frac']*100:.2f}%  near={Bn['moving_frac']*100:.2f}%")
    print(f"    連続区間数(|rate|>{MOVING_RATE_EPS})        whole={Bw['n_segments']}  near={Bn['n_segments']}")
    print(f"    区間長(frames) median/p95/max whole={fmt3(Bw['seg_len_frames'])}")
    print(f"    区間長(秒)     median/p95/max whole={fmt3(Bw['seg_dur'])}")
    print()

    print("  [C: 力の性質]  (whole / near-peak)")
    Cw, Cn = a["C_whole"], a["C_near"]
    print(f"    |f|     median/p95/max        whole={fmt3(Cw['f_abs'])}  near={fmt3(Cn['f_abs'])}")
    print(f"    |d|f|/dt| median/p99/max      whole={fmt3(Cw['dfdt'])}  near={fmt3(Cn['dfdt'])}")
    print(f"    breakaway帯({KINETIC}-{BRK_HI})滞在割合  whole={Cw['band_frac']*100:.2f}%  near={Cn['band_frac']*100:.2f}%")
    print(f"      内訳 f>=0(左)              whole={Cw['band_left_frac']*100:.2f}%  near={Cn['band_left_frac']*100:.2f}%")
    print(f"      内訳 f<0(右)               whole={Cw['band_right_frac']*100:.2f}%  near={Cn['band_right_frac']*100:.2f}%")
    print(f"    力の符号反転回数(|f|>={FORCE_SIGN_EPS})  whole={Cw['reversals']}  near={Cn['reversals']}")
    print()

    D = a["D"]
    print("  [D: 残差の立ち上がり方]")
    print(f"    ピークへの最終単調区間: 開始t={D['rise_start_t']:.2f}s 継続={D['rise_duration']:.3f}s "
          f"({D['rise_n_frames']} frames)")
    print(f"      区間開始時: speed={D['rise_start_speed']:.3f} f={D['rise_start_force']:+.4f} "
          f"shadow_moving={D['rise_start_shadow_moving']} rate={D['rise_start_rate']:+.3f}/s")
    for thr in RES_EPISODE_THRESHOLDS:
        eps = D["episodes"][thr]
        if eps:
            print(f"    residual>{thr}: エピソード数={len(eps)}  "
                  f"継続 max={max(eps):.3f}s 合計={sum(eps):.3f}s  個別={['%.3f'%e for e in eps]}")
        else:
            print(f"    residual>{thr}: エピソード数=0")
    print()

    print(f"  [E: 上位{TOPN}残差フレーム]")
    print(f"    {'t':>7} {'speed':>7} {'target':>8} {'rate':>8} {'jerk':>9} "
          f"{'f':>8} {'shdw_mv':>7} {'residual':>9}")
    for s in a["E"]:
        print(f"    {s['t']:>7.2f} {s['speed']:>7.3f} {s['target']:>+8.4f} {s['rate']:>+8.3f} "
              f"{s['jerk']:>+9.2f} {s['eff_force']:>+8.4f} {str(s['shadow_moving']):>7} "
              f"{s['residual']:>9.4f}")
    print()

    F = a["F"]
    print("  [F: 相関（残差増分 vs 候補量、上昇分のみ）]")
    for k in ("abs_jerk", "abs_rate", "speed", "abs_dfdt", "bb_dist"):
        v = F[k]
        print(f"    {k:<10} r = {v:+.3f}" if v == v else f"    {k:<10} r = NaN")
    print()


def print_comparison(analyses: list[dict]) -> None:
    print("=" * 100)
    print("横並び比較（要約）")
    print("=" * 100)
    names = [a["path"].stem.replace("f7_realwheel_", "") for a in analyses]
    print(f"{'metric':<38}" + "".join(f"{nm:>20}" for nm in names))

    def row(label: str, vals: list) -> None:
        print(f"{label:<38}" + "".join(f"{v:>20}" for v in vals))

    row("residual peak", [f"{a['peak']['residual']:.4f}" for a in analyses])
    row("latched (MANUAL)", ["YES" if a["latch"]["latched"] else "no" for a in analyses])
    row("sustain_accum max (s)", [f"{a['latch']['max_sustain']:.4f}" for a in analyses])
    row(f"sustain margin to {LATCH_SUSTAIN_TIME}s (s)", [f"{a['latch']['sustain_margin']:.4f}" for a in analyses])
    row("precursor jerk (/s^2)", [f"{a['precursor']['jerk']:+.2f}" for a in analyses])
    row("precursor offset before peak (s)", [f"{a['precursor']['offset_s']:.2f}" for a in analyses])
    row("peak speed (m/s)", [f"{a['A']['peak_speed']:.3f}" for a in analyses])
    row("speed whole max (m/s)", [f"{a['A']['whole'][2]:.3f}" for a in analyses])
    row("speed near-peak max (m/s)", [f"{a['A']['near'][2]:.3f}" for a in analyses])
    row("stopped frac (%)", [f"{a['A']['stopped_frac']*100:.2f}" for a in analyses])
    row("stop->start events", [a["A"]["stop_start_events"] for a in analyses])
    row("|rate| whole max (/s)", [f"{a['B_whole']['rate'][2]:.3f}" for a in analyses])
    row("|rate| near max (/s)", [f"{a['B_near']['rate'][2]:.3f}" for a in analyses])
    row("|jerk| whole max (/s^2)", [f"{a['B_whole']['jerk'][2]:.2f}" for a in analyses])
    row("|jerk| near max (/s^2)", [f"{a['B_near']['jerk'][2]:.2f}" for a in analyses])
    row("rate reversals whole", [a["B_whole"]["reversals"] for a in analyses])
    row("rate reversals near", [a["B_near"]["reversals"] for a in analyses])
    row("moving(|rate|>0.05) frac whole(%)", [f"{a['B_whole']['moving_frac']*100:.2f}" for a in analyses])
    row("moving frac near(%)", [f"{a['B_near']['moving_frac']*100:.2f}" for a in analyses])
    row("|f| whole max", [f"{a['C_whole']['f_abs'][2]:.4f}" for a in analyses])
    row("|f| near max", [f"{a['C_near']['f_abs'][2]:.4f}" for a in analyses])
    row("|d|f|/dt| whole max", [f"{a['C_whole']['dfdt'][2]:.3f}" for a in analyses])
    row("|d|f|/dt| near max", [f"{a['C_near']['dfdt'][2]:.3f}" for a in analyses])
    row("breakaway band frac whole(%)", [f"{a['C_whole']['band_frac']*100:.2f}" for a in analyses])
    row("breakaway band frac near(%)", [f"{a['C_near']['band_frac']*100:.2f}" for a in analyses])
    row("force reversals whole", [a["C_whole"]["reversals"] for a in analyses])
    row("force reversals near", [a["C_near"]["reversals"] for a in analyses])
    row("rise duration to peak (s)", [f"{a['D']['rise_duration']:.3f}" for a in analyses])
    row("rise n_frames", [a["D"]["rise_n_frames"] for a in analyses])
    for thr in RES_EPISODE_THRESHOLDS:
        row(f"res>{thr} episodes", [len(a["D"]["episodes"][thr]) for a in analyses])
    print()
    print(f"{'corr(dres, candidate)':<38}" + "".join(f"{nm:>20}" for nm in names))
    for k in ("abs_jerk", "abs_rate", "speed", "abs_dfdt", "bb_dist"):
        row(k, [f"{a['F'][k]:+.3f}" if a['F'][k] == a['F'][k] else "NaN" for a in analyses])
    print()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jsonl", type=Path, action="append", required=True)
    ap.add_argument("--config-label", default="変更前構成（θ=τ=0・打ち手Aのonset grace無効）",
                     help="入力ログがどの製品構成の記録かを明記する（再利用時は必ず書き換えること）")
    args = ap.parse_args()

    print("=" * 100)
    print("feature:F7  tljunction 最悪ケース条件の特定（躍度だけでは説明できない差分を探す）")
    print("=" * 100)
    print(f"!!! 注記: 以下の residual/shadow_norm/sustain_accum/マージン/合否判定は"
          f" {args.config_label} 時点の製品出力の記録である。現行出荷既定の数値では **ない**。")
    print()
    print(f"シャドウ定数: kinetic={KINETIC} breakaway={BRK_HI}(uncond)/{BRK_LEFT}L/{BRK_RIGHT}R "
          f"slope={SLOPE} v_max={VMAX}")
    print(f"しきい値: RATE_SIGN_EPS={RATE_SIGN_EPS} FORCE_SIGN_EPS={FORCE_SIGN_EPS} "
          f"MOVING_RATE_EPS={MOVING_RATE_EPS} STOP_SPEED={STOP_SPEED} "
          f"MIN_STOP_DURATION={MIN_STOP_DURATION} PEAK_WINDOW=±{PEAK_WINDOW} "
          f"LATCH_SUSTAIN_TIME={LATCH_SUSTAIN_TIME}")
    print()

    analyses = [analyse(p) for p in args.jsonl]
    for a in analyses:
        print_run(a)
    print_comparison(analyses)
    return 0


if __name__ == "__main__":
    sys.exit(main())
