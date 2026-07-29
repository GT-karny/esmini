#!/usr/bin/env python
"""feature:F7 — (1) 最悪窓の中で操舵躍度は実際に大きいのか、(2) 躍度スパイクの発生源は
AD 生指令かレート制限器か、を実測で決着させる（分析専用・製品コード不変更）。

## 背景

`worst_case_conditions.py` が特定した tljunction の最悪窓は 2 つ:
  (a) 残差ピークへの最終単調上昇区間
  (b) residual>0.04 の最大継続エピソード
打ち手C（操舵躍度の上限）はこれらの窓の中で実際に躍度が大きくなければ効き代がない。
また、躍度スパイクの発生源が AD の生指令なのか、安全包絡線のレート制限器なのかで
打ち手の設計（AD側を直すか、レート制限器の実装を直すか）が変わる。

## 使う実測値の出所（すべてコードから直接確認済み）

  MAX_STEER_ANGLE  = 0.61 rad   GT_esmini/config/virtual_driver.json "max_steer_angle"
                                （= VirtualDriverConfig::max_steer_angle のデフォルトと同値）
  STEER_RATE_MAX   = 1.5 rad/s  GT_esmini/include/gt_esmini/control/virtualdriver/
                                AdSteeringEnvelope.hpp kAdEnvelopeDefaultSteerRateMax
  RATE_LIMIT_NORM  = STEER_RATE_MAX / MAX_STEER_ANGLE （正規化 steer_norm/s の外側レート上限）
  wheel_base は実行時に object_->boundingbox_.dimensions_.length_ * 0.6 で決まる
  （ControllerVirtualDriver.cpp:312）。レート制限自体には無関係（AdSteeringEnvelope.cpp:64-68
  の steer_rate_max 分岐は wheel_base を使わない。curvature 由来の lateral_accel/yaw_rate
  クリップだけが wheel_base に依存する）ので、レート/躍度の本分析には使わない。

`envelope.steer_in` = ComputeAdSteeringEnvelope() へ渡す前の生 AD 指令
    (= telemetry_.driver.steer と同一値。ControllerVirtualDriver.cpp:550-554)
`envelope.steer_out` = 包絡線通過後の指令。ControllerVirtualDriver.cpp:379-381 で
    auto_cmd.steering をその場で上書きし、そのまま 431 行目で ffb->SetSteerTarget() に
    渡るので、**`ffb.target_norm` は `envelope.steer_out` と同一の値のはず**——本スクリプトが
    実測で検証する（別モデルではなく同じ変数の伝播）。

## 決め打ちしたしきい値（自分で決めた値。理由を明記）

  JERK_LIMIT_CANDIDATE = 25  /s^2   打ち手Cの候補上限（タスク指定）。
  JERK_HIGH_THRESHOLD  = 50  /s^2   引き継ぎ文書が使った "|jerk|>50" の再測定用（タスク指定）。
  SAT_TOL = 0.02  正規化/s   レート飽和判定の許容誤差。target_norm は JSON へ小数点4桁で
                            丸められて出力されるため、そこから差分計算したレート/躍度には
                            量子化ノイズが乗る。RATE_LIMIT_NORM(≈2.4590) 近傍で ±0.02 の
                            揺れは丸め起因であり実質「張り付いている」とみなす。
  BOUNDARY_TOL = 2 frames   躍度スパイクが飽和区間の開始/終了と「近接している」とみなす
                            フレーム距離（タスク指定の「±1〜2フレーム」の上限を採用）。
  JERK_CAP_CANDIDATES = 10/25/50 /s^2  audit H3: 打ち手Cの効果見積もり候補（値は暫定、
                            感度を見るため3点で出す）。

## audit H3 / H4（このスクリプトの数値を読む上での必須の注意）

  H3: 打ち手Cの効果見積もりは `steer_in`（制限器に入る前のAD生指令）に対して行う。
      `steer_out`（=`target_norm`）は既に制限器を通過済みなので、そこへ躍度上限を
      当てると制限器の減衰を二重計上して過大評価になる（前バージョンの pct_reduction
      指標はこの理由で破棄した）。本版は `reconstruct_steer_out()` で実装と同じ
      パイプライン順序（曲率クリップ→レート窓∩ジャーク窓）を再現し、開ループ推定として
      steer_out' を再構成する。
  H4: `jerk` の量子は 1.0/s^2（本ファイル冒頭の量子化ノート参照）。216 級の大イベントは
      相対誤差0.5%で十分な精度があるが、**basic 走行に現れる `1.00` はこの計器の
      測定下限であって観測値ではない**——真値が 0 〜 1.41 のどこにあるかは区別できない。
      通常運転側の小さい躍度分布（p99=2.0 等）も量子2個分でこの計器では意味を持たない
      （高精度ログでの再計測待ち。本スクリプトでは踏み込まない）。

## 使い方

    python jerk_window_and_ratelimiter_source.py --jsonl run1.jsonl [--jsonl run2.jsonl ...]
"""

from __future__ import annotations

import argparse
import io
import json
import math
import sys
from pathlib import Path

MAX_STEER_ANGLE = 0.61     # rad, config/virtual_driver.json "max_steer_angle"
STEER_RATE_MAX = 1.5       # rad/s, AdSteeringEnvelope.hpp kAdEnvelopeDefaultSteerRateMax
RATE_LIMIT_NORM = STEER_RATE_MAX / MAX_STEER_ANGLE  # ≈ 2.4590 normalized/s

JERK_LIMIT_CANDIDATE = 25.0
JERK_HIGH_THRESHOLD = 50.0
# ⚠ STALE TOLERANCE — sized for an instrument that no longer exists.
# The 0.02 above is justified by "target_norm は JSON へ小数点4桁で丸められて
# 出力される". VirtualDriverTelemetryJson.cpp now writes NINE decimals, and its
# own comment records why the 4-decimal record was retired: at 1e-4 quantum the
# derived jerk quantized to 1.0 /s^2 and the whole normal-driving distribution
# was instrument floor rather than signal. For a 9-decimal capture the
# corresponding rate tolerance is ~1e-7, i.e. this constant is FIVE ORDERS too
# loose and will call anything within 0.02 /s of the rate cap "pinned".
#
# NOT changed here, deliberately: this script is a forensic tool pointed at
# historical captures, and I cannot verify which of its inputs predate the
# 9-decimal change — silently tightening it would reinterpret old data. Anyone
# re-running it on a fresh capture should pass the tighter value. Flagged
# rather than fixed, and flagged rather than left silent: a tolerance whose
# stated reason has expired is exactly how telemetry_golden.py hid a real
# engine nondeterminism from 2026-07-04 to 2026-07-28.
SAT_TOL = 0.02
BOUNDARY_TOL = 2
RES_EPISODE_THR_B = 0.04  # 窓(b)の定義: residual>0.04 の最大継続エピソード

# audit H3: 打ち手Cの効果見積もりは steer_in（制限器に入る前の指令）に対して行う。
# steer_out（=target_norm、制限器通過後）に躍度上限を当てるのは制限器の減衰を
# 二重計上するので無効（前バージョンの pct_reduction 指標はこの理由で破棄）。
JERK_CAP_CANDIDATES = (10.0, 25.0, 50.0)


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


def _rate_jerk(S: list[dict], key: str) -> tuple[list[float], list[float]]:
    n = len(S)
    rate = [0.0] * n
    for i in range(1, n):
        dt = S[i]["t"] - S[i - 1]["t"]
        if dt > 0:
            rate[i] = (S[i][key] - S[i - 1][key]) / dt
    jerk = [0.0] * n
    for i in range(1, n):
        dt = S[i]["t"] - S[i - 1]["t"]
        if dt > 0:
            jerk[i] = (rate[i] - rate[i - 1]) / dt
    return rate, jerk


def build_series(rows: list[dict]) -> list[dict]:
    S = []
    for r in rows:
        ffb = r["ffb"]
        g = ffb["gates"]
        env = r.get("envelope", {})
        drv = r.get("driver", {})
        S.append(dict(
            t=float(r["sim_time"]),
            target=float(ffb.get("target_norm", 0.0)),
            residual=float(g.get("residual", 0.0)),
            steer_in=float(env.get("steer_in", 0.0)),
            steer_out=float(env.get("steer_out", 0.0)),
            lat_active=bool(env.get("lateral_accel_active", False)),
            yaw_active=bool(env.get("yaw_rate_active", False)),
            steer_rate_active=bool(env.get("steer_rate_active", False)),
            env_active=bool(env.get("active", False)),
            drv_steer=float(drv.get("steer", 0.0)),
        ))
    for key in ("target", "steer_in", "steer_out"):
        rate, jerk = _rate_jerk(S, key)
        for i, s in enumerate(S):
            s[f"rate_{key}"] = rate[i]
            s[f"jerk_{key}"] = jerk[i]
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


def dist4(xs: list[float]) -> tuple[float, float, float, float]:
    """median / p95 / p99 / max。空なら NaN 4つ。"""
    if not xs:
        return float("nan"), float("nan"), float("nan"), float("nan")
    s = sorted(xs)
    return percentile(s, 0.5), percentile(s, 0.95), percentile(s, 0.99), s[-1]


def fmt4(t: tuple[float, float, float, float], prec: int = 3) -> str:
    return " / ".join(f"{x:.{prec}f}" for x in t)


# --------------------------------------------------------------------------- #
# 窓の特定（worst_case_conditions.py と同じロジックを自己完結で再実装）
# --------------------------------------------------------------------------- #

def find_rise_window(S: list[dict]) -> tuple[int, int]:
    peak_idx = max(range(len(S)), key=lambda i: S[i]["residual"])
    j = peak_idx
    while j > 0 and S[j - 1]["residual"] <= S[j]["residual"]:
        j -= 1
    return j, peak_idx


def find_longest_episode(S: list[dict], thr: float) -> tuple[int, int] | None:
    episodes = []
    in_ep = False
    start = 0
    for i, s in enumerate(S):
        above = s["residual"] > thr
        if above and not in_ep:
            in_ep, start = True, i
        elif not above and in_ep:
            in_ep = False
            episodes.append((start, i - 1))
    if in_ep:
        episodes.append((start, len(S) - 1))
    if not episodes:
        return None
    return max(episodes, key=lambda se: S[se[1]]["t"] - S[se[0]]["t"])


# --------------------------------------------------------------------------- #
# audit H3: 打ち手Cの効果見積もり — steer_in を入力に、実装と同じパイプライン順序で
# steer_out' を再構成する（開ループ推定）。
# --------------------------------------------------------------------------- #

def reconstruct_steer_out(S: list[dict], jerk_cap_norm: float) -> list[float]:
    """AdSteeringEnvelope.cpp の順序: 曲率クリップ(lat/yaw) -> レート窓とジャーク窓の
    交差でクリップ。曲率クリップは本データで lateral_accel_active/yaw_rate_active が
    全区間 0.00%（source_report で確認済み）なので、steer_in がそのまま曲率クリップ後の
    値に等しいとみなして良い（このデータに限った近似。一般には成立しない）。
    レート窓 = prev ± RATE_LIMIT_NORM*dt（実装と同じ、既存の steer_rate_max）。
    ジャーク窓 = prev + (rate_prev ± jerk_cap*dt)*dt。rate_prev は前フレームの実現レートを
    ±RATE_LIMIT_NORM にクランプしたもの。前フレームの実現角(prev)は実測初期値2点で種付けし、
    以降を全区間通して再構成する（窓境界での近似誤差を避けるため）。"""
    n = len(S)
    out = [0.0] * n
    out[0] = S[0]["steer_out"]
    out[1] = S[1]["steer_out"] if n > 1 else out[0]
    for i in range(2, n):
        dt = S[i]["t"] - S[i - 1]["t"]
        dt_prev = S[i - 1]["t"] - S[i - 2]["t"]
        if dt <= 0:
            out[i] = out[i - 1]
            continue
        prev = out[i - 1]
        rate_prev = (out[i - 1] - out[i - 2]) / dt_prev if dt_prev > 0 else 0.0
        rate_prev = max(-RATE_LIMIT_NORM, min(RATE_LIMIT_NORM, rate_prev))
        lo_rate = prev - RATE_LIMIT_NORM * dt
        hi_rate = prev + RATE_LIMIT_NORM * dt
        lo_jerk = prev + (rate_prev - jerk_cap_norm * dt) * dt
        hi_jerk = prev + (rate_prev + jerk_cap_norm * dt) * dt
        lo, hi = max(lo_rate, lo_jerk), min(hi_rate, hi_jerk)
        if lo > hi:  # 交差が空になる異常ケースの保険（安全側でレート窓を優先。通常は起きない）
            lo, hi = lo_rate, hi_rate
        out[i] = max(lo, min(hi, S[i]["steer_in"]))
    return out


# --------------------------------------------------------------------------- #
# Q1: 窓の中の躍度は実際に大きいのか
# --------------------------------------------------------------------------- #

def window_jerk_report(S: list[dict], lo: int, hi: int, label: str,
                        recon: dict[float, tuple[list[float], list[float]]]) -> dict:
    idxs_in = set(range(lo, hi + 1))
    inside = [S[i] for i in range(lo, hi + 1)]
    outside = [S[i] for i in range(len(S)) if i not in idxs_in]

    jerk_out_in = [abs(s["jerk_steer_out"]) for s in inside]     # 現行(制限器通過後)
    jerk_in_in = [abs(s["jerk_steer_in"]) for s in inside]        # AD生指令(制限器の前)
    jerk_out_ = [abs(s["jerk_steer_out"]) for s in outside]
    jerk_whole = [abs(s["jerk_steer_out"]) for s in S]
    rate_out_in = [abs(s["rate_steer_out"]) for s in inside]

    sat_in = sum(1 for s in inside if abs(abs(s["rate_steer_out"]) - RATE_LIMIT_NORM) < SAT_TOL)
    over25_in = [s for s in inside if abs(s["jerk_steer_out"]) > JERK_LIMIT_CANDIDATE]

    # audit H3: steer_in を入力に実装と同じ順序(曲率クリップ→レート窓∩ジャーク窓)で
    # steer_out' を再構成した結果(recon, 全区間分)を窓に切り出して、現行 steer_out と比較。
    # 開ループ推定であり、実機の軸応答(閉ループ)を変える証拠ではない。
    h3 = {}
    for cap, (rate_sim, jerk_sim) in recon.items():
        rs = [abs(x) for x in rate_sim[lo:hi + 1]]
        js = [abs(x) for x in jerk_sim[lo:hi + 1]]
        sat_sim = sum(1 for x in rs if abs(x - RATE_LIMIT_NORM) < SAT_TOL)
        h3[cap] = dict(jerk_max=max(js) if js else float("nan"),
                       jerk_p95=percentile(sorted(js), 0.95) if js else float("nan"),
                       rate_max=max(rs) if rs else float("nan"),
                       sat_frames=sat_sim)

    # 時間局在性: |jerk(steer_out)|>candidate のフレームが窓内のどこに来ているか
    dur = inside[-1]["t"] - inside[0]["t"]
    positions = [(s["t"] - inside[0]["t"]) / dur if dur > 0 else 0.0 for s in over25_in]
    thirds = [0, 0, 0]
    for p in positions:
        thirds[min(2, int(p * 3))] += 1

    return dict(
        label=label, lo_t=inside[0]["t"], hi_t=inside[-1]["t"], duration=dur, n=len(inside),
        jerk_out_in=dist4(jerk_out_in), jerk_in_in=dist4(jerk_in_in),
        jerk_out=dist4(jerk_out_), jerk_whole=dist4(jerk_whole),
        rate_out_in=dist4(rate_out_in), sat_in_frac=sat_in / len(inside), sat_in_n=sat_in,
        n_over25=len(over25_in), frac_over25=len(over25_in) / len(inside),
        h3=h3,
        positions_median=percentile(sorted(positions), 0.5) if positions else float("nan"),
        thirds=thirds,
    )


# --------------------------------------------------------------------------- #
# Q2: 躍度スパイクの発生源は AD 生指令かレート制限器か
# --------------------------------------------------------------------------- #

def saturation_runs(S: list[dict]) -> list[tuple[int, int]]:
    runs = []
    n = len(S)
    i = 0
    while i < n:
        if abs(abs(S[i]["rate_steer_out"]) - RATE_LIMIT_NORM) < SAT_TOL:
            j = i
            while j < n and abs(abs(S[j]["rate_steer_out"]) - RATE_LIMIT_NORM) < SAT_TOL:
                j += 1
            runs.append((i, j - 1))
            i = j
        else:
            i += 1
    return runs


def source_report(S: list[dict]) -> dict:
    n = len(S)
    max_diff = max(abs(s["target"] - s["steer_out"]) for s in S)

    jerk_in_dist = dist4([abs(s["jerk_steer_in"]) for s in S])
    jerk_out_dist = dist4([abs(s["jerk_steer_out"]) for s in S])
    rate_in_dist = dist4([abs(s["rate_steer_in"]) for s in S])
    rate_out_dist = dist4([abs(s["rate_steer_out"]) for s in S])

    runs = saturation_runs(S)
    sat_frames = sum((e - s + 1) for s, e in runs)
    run_lens_frames = [float(e - s + 1) for s, e in runs]
    run_lens_secs = [S[e]["t"] - S[s]["t"] for s, e in runs]
    boundary_idxs = set()
    for s, e in runs:
        boundary_idxs.add(s)
        if e + 1 < n:
            boundary_idxs.add(e + 1)

    high = [i for i in range(n) if abs(S[i]["jerk_steer_out"]) > JERK_HIGH_THRESHOLD]
    near_boundary = 0
    for i in high:
        if any(abs(i - b) <= BOUNDARY_TOL for b in boundary_idxs):
            near_boundary += 1
    concentration_pct = (near_boundary / len(high) * 100.0) if high else float("nan")

    # 決め手: 同一フレームで jerk(steer_in) はどうか。これが小さいのに jerk(steer_out) が
    # 大きいフレームだけが「レート制限器がその場で作った」と言える。
    paired = [(i, S[i]["jerk_steer_in"], S[i]["jerk_steer_out"],
               any(abs(i - b) <= BOUNDARY_TOL for b in boundary_idxs)) for i in high]
    n_input_also_high = sum(1 for _, ji, _, _ in paired if abs(ji) > JERK_LIMIT_CANDIDATE)
    n_input_smooth = sum(1 for _, ji, _, _ in paired if abs(ji) <= JERK_LIMIT_CANDIDATE)

    def flag_rate(pred) -> float:
        return sum(1 for s in S if pred(s)) / n

    def flag_rate_among(idxs, pred) -> float:
        return sum(1 for i in idxs if pred(S[i])) / len(idxs) if idxs else float("nan")

    return dict(
        max_diff_target_steerout=max_diff,
        jerk_in=jerk_in_dist, jerk_out=jerk_out_dist,
        rate_in=rate_in_dist, rate_out=rate_out_dist,
        n_sat_runs=len(runs), sat_frac=sat_frames / n,
        run_len_frames=dist4(run_lens_frames), run_len_secs=dist4(run_lens_secs),
        n_high=len(high), concentration_pct=concentration_pct,
        paired=paired, n_input_also_high=n_input_also_high, n_input_smooth=n_input_smooth,
        flag_rate_overall=dict(
            steer_rate_active=flag_rate(lambda s: s["steer_rate_active"]),
            lateral_accel_active=flag_rate(lambda s: s["lat_active"]),
            yaw_rate_active=flag_rate(lambda s: s["yaw_active"]),
        ),
        flag_rate_high=dict(
            steer_rate_active=flag_rate_among(high, lambda s: s["steer_rate_active"]),
            lateral_accel_active=flag_rate_among(high, lambda s: s["lat_active"]),
            yaw_rate_active=flag_rate_among(high, lambda s: s["yaw_active"]),
        ),
    )


# --------------------------------------------------------------------------- #
# 出力
# --------------------------------------------------------------------------- #

def print_window(w: dict) -> None:
    print(f"  --- 窓{w['label']}: t={w['lo_t']:.2f}s..{w['hi_t']:.2f}s "
          f"(継続{w['duration']:.3f}s, {w['n']} frames) ---")
    print(f"    |jerk(steer_out)| median/p95/p99/max  [現行]窓内={fmt4(w['jerk_out_in'], 2)}")
    print(f"    |jerk(steer_in)|  median/p95/p99/max  [AD生指令]窓内={fmt4(w['jerk_in_in'], 2)}")
    print(f"    |jerk(steer_out)| 窓外={fmt4(w['jerk_out'], 2)}  全体={fmt4(w['jerk_whole'], 2)}")
    print(f"    |rate(steer_out)| median/p95/p99/max  窓内={fmt4(w['rate_out_in'], 3)}")
    print(f"    レート上限({RATE_LIMIT_NORM:.4f})飽和(窓内) = {w['sat_in_n']}frames ({w['sat_in_frac']*100:.2f}%)")
    print(f"    |jerk(steer_out)|>{JERK_LIMIT_CANDIDATE:.0f} フレーム数/割合(窓内) = {w['n_over25']} / {w['frac_over25']*100:.2f}%")
    print(f"    |jerk(steer_out)|>{JERK_LIMIT_CANDIDATE:.0f} 時間局在性: 窓内正規化位置(0=開始,1=終了) "
          f"median={w['positions_median']:.2f}  前1/3={w['thirds'][0]} 中1/3={w['thirds'][1]} 後1/3={w['thirds'][2]}")
    print(f"    [audit H3 修正版] steer_in を入力に 曲率クリップ→レート窓∩ジャーク窓 で steer_out' を再構成"
          f"（開ループ推定・実機閉ループ効果の証拠ではない）:")
    print(f"      {'jerk_cap':>10} {'jerk_max':>10} {'jerk_p95':>10} {'rate_max':>10} {'sat_frames':>11}")
    print(f"      {'現行(実測)':>10} {w['jerk_out_in'][3]:>10.2f} {w['jerk_out_in'][1]:>10.2f} "
          f"{w['rate_out_in'][3]:>10.3f} {w['sat_in_n']:>11}")
    for cap in JERK_CAP_CANDIDATES:
        h = w["h3"][cap]
        print(f"      {cap:>10.0f} {h['jerk_max']:>10.2f} {h['jerk_p95']:>10.2f} "
              f"{h['rate_max']:>10.3f} {h['sat_frames']:>11}")
    print()


def print_run(path: Path, S: list[dict]) -> None:
    print(f"=== {path.name}  ({len(S)} frames) ===")
    rise = find_rise_window(S)
    ep = find_longest_episode(S, RES_EPISODE_THR_B)

    # audit H3: steer_in ベースの再構成を jerk_cap 候補ごとに全区間で1回だけ計算し、窓へ切り出す。
    recon = {}
    for cap in JERK_CAP_CANDIDATES:
        out = reconstruct_steer_out(S, cap)
        rate_sim, jerk_sim = _rate_jerk([dict(t=s["t"], v=v) for s, v in zip(S, out)], "v")
        recon[cap] = (rate_sim, jerk_sim)

    print("  [Q1: 最悪窓の中で躍度は実際に大きいのか]（曲率クリップは lat/yaw 発火率0.00%より "
          "steer_in パススルーと近似——このデータに限った近似）")
    wa = window_jerk_report(S, rise[0], rise[1], "(a) 最終単調上昇区間", recon)
    print_window(wa)
    if ep is not None:
        wb = window_jerk_report(S, ep[0], ep[1], "(b) residual>0.04 最大エピソード", recon)
        print_window(wb)
    else:
        print("  --- 窓(b): residual>0.04 のエピソードが無い（この走行では該当なし） ---")
        print()

    print("  [Q2: 躍度スパイクの発生源は AD 生指令かレート制限器か]（全区間）")
    sr = source_report(S)
    print(f"    sanity: max|target_norm - steer_out| = {sr['max_diff_target_steerout']:.6f}  "
          f"(0に近ければ ffb.target_norm==envelope.steer_out を確認)")
    print(f"    |jerk(steer_in)|  median/p95/p99/max = {fmt4(sr['jerk_in'], 2)}   ← AD生指令")
    print(f"    |jerk(steer_out)| median/p95/p99/max = {fmt4(sr['jerk_out'], 2)}   ← 包絡線通過後")
    print(f"    |rate(steer_in)|  median/p95/p99/max = {fmt4(sr['rate_in'], 3)}")
    print(f"    |rate(steer_out)| median/p95/p99/max = {fmt4(sr['rate_out'], 3)}")
    print(f"    steer_out レート飽和(|rate|≈{RATE_LIMIT_NORM:.4f}) フレーム割合 = {sr['sat_frac']*100:.2f}%  "
          f"({sr['n_sat_runs']} 区間)")
    print(f"    飽和区間長  median/p95/p99/max  frames={fmt4(sr['run_len_frames'], 1)}  "
          f"seconds={fmt4(sr['run_len_secs'], 3)}")
    print(f"    |jerk(steer_out)|>{JERK_HIGH_THRESHOLD:.0f} のフレーム数 = {sr['n_high']}")
    print(f"    → うち飽和区間境界(±{BOUNDARY_TOL}frame)に近接する割合 = "
          f"{sr['concentration_pct']:.1f}%" if sr['n_high'] else "    (該当フレームなし)")
    if sr["n_high"]:
        print(f"    [決め手] 同一フレームで |jerk(steer_in)|>{JERK_LIMIT_CANDIDATE:.0f}(=入力側も既にジャーキー) "
              f"= {sr['n_input_also_high']}/{sr['n_high']}件、"
              f"|jerk(steer_in)|<={JERK_LIMIT_CANDIDATE:.0f}(=入力は滑らか、出力だけ跳ねた) "
              f"= {sr['n_input_smooth']}/{sr['n_high']}件")
        print(f"      内訳(t, jerk_in, jerk_out, rate_active, near_sat_boundary):")
        for i, ji, jo, nb in sr["paired"]:
            print(f"        t={S[i]['t']:>7.2f}  jerk_in={ji:>+8.2f}  jerk_out={jo:>+8.2f}  "
                  f"steer_rate_active={str(S[i]['steer_rate_active']):<5}  near_boundary={nb}")
    fo, fh = sr["flag_rate_overall"], sr["flag_rate_high"]
    print(f"    envelope flag 発火率(全区間): steer_rate_active={fo['steer_rate_active']*100:.2f}% "
          f"lateral_accel_active={fo['lateral_accel_active']*100:.2f}% yaw_rate_active={fo['yaw_rate_active']*100:.2f}%")
    if sr["n_high"]:
        print(f"    envelope flag 真値割合(|jerk(steer_out)|>{JERK_HIGH_THRESHOLD:.0f} の{sr['n_high']}件中): "
              f"steer_rate_active={fh['steer_rate_active']*100:.1f}% "
              f"lateral_accel_active={fh['lateral_accel_active']*100:.1f}% "
              f"yaw_rate_active={fh['yaw_rate_active']*100:.1f}%")
    print()
    return dict(rise=rise, ep=ep, wa=wa, sr=sr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jsonl", type=Path, action="append", required=True)
    ap.add_argument("--config-label", default="変更前構成（θ=τ=0・打ち手Aのonset grace無効）",
                     help="入力ログがどの製品構成の記録かを明記する")
    args = ap.parse_args()

    print("=" * 100)
    print("feature:F7  最悪窓の躍度実態 と 躍度スパイクの発生源（AD生指令 vs レート制限器）")
    print("=" * 100)
    print(f"!!! 注記: 以下は {args.config_label} 時点の製品出力の記録である。現行出荷既定の数値では **ない**。")
    print()
    print(f"MAX_STEER_ANGLE={MAX_STEER_ANGLE} rad (config/virtual_driver.json)  "
          f"STEER_RATE_MAX={STEER_RATE_MAX} rad/s (AdSteeringEnvelope.hpp kAdEnvelopeDefaultSteerRateMax)")
    print(f"RATE_LIMIT_NORM = {STEER_RATE_MAX}/{MAX_STEER_ANGLE} = {RATE_LIMIT_NORM:.4f} normalized/s")
    print(f"JERK_LIMIT_CANDIDATE={JERK_LIMIT_CANDIDATE} (打ち手C候補上限)  "
          f"JERK_HIGH_THRESHOLD={JERK_HIGH_THRESHOLD} (引き継ぎ文書の再測定用)  "
          f"SAT_TOL={SAT_TOL}  BOUNDARY_TOL=±{BOUNDARY_TOL}frames")
    print()

    for path in args.jsonl:
        S = build_series(load(path))
        print_run(path, S)

    return 0


if __name__ == "__main__":
    sys.exit(main())
