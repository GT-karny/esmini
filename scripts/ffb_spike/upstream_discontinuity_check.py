#!/usr/bin/env python
"""feature:F7 — 躍度イベント前後で上流（経路追従側）の量に不連続が同時観測されるかを
記録するだけの観測用スクリプト（原因追い込みはしない。次課題の種の記録）。

## 背景

`jerk_window_and_ratelimiter_source.py` で特定した躍度216イベント
(tljunction t=18.21s, right_turn t=13.80s) は AD 生指令(`envelope.steer_in`)側で
既に大きい。もし上流（経路追従・レーン割当・プレビュー）が同時刻に不連続な入力を
出しているなら、躍度上限は対症療法に過ぎない可能性がある——という次の課題の
当たりを付けるためだけに、ログに見える範囲の不連続の有無を機械的に記録する。
**原因を特定する分析ではない。観測の記録のみ。**

## 見る量・判定しきい値（自分で決めた値。理由を明記）

  track / lane / fb.road_id / fb.lane        : カテゴリ値。変化そのものを記録。
  fb.valid / drv.valid / preview.valid       : 真偽値。反転を記録。
  indicator.left/right / override.*          : 真偽値。反転を記録。
  ego.s の飛び      : |実測Δs - speed*dt| > max(0.05, speed*dt*4) を記録
                      （加減速だけでは説明できない飛びの目安。緩め）
  ego.h の飛び      : |Δh| > 0.05 rad/frame を記録（envelope yaw_rate_max=1.0rad/s
                      の1frame分=0.01radの5倍。緩め）
  fb.s / fb.t の飛び: |Δ| > 0.10 を記録（緩め）
  drv.lookahead     : |Δ| > 0.01 を記録（通常は定数のはずなので僅かな変化も記録対象）
  preview 先頭点の飛び: 直前フレームの先頭点との距離が窓内の中央値の3倍を超えたら記録

## 使い方

    python upstream_discontinuity_check.py --jsonl run.jsonl --event-t 18.21
"""

from __future__ import annotations

import argparse
import io
import json
import math
import statistics
import sys
from pathlib import Path

WINDOW_HALF = 0.5
DS_ABS_MIN = 0.05
DS_REL_MULT = 4.0
DH_MAX = 0.05
FB_JUMP_MAX = 0.10
LOOKAHEAD_EPS = 0.01
PREVIEW_JUMP_MULT = 3.0


def load(path: Path) -> list[dict]:
    rows = []
    for line in io.open(path, encoding="utf-8"):
        line = line.strip()
        if line:
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return rows


def extract(r: dict) -> dict:
    ego, fb, drv = r["ego"], r["front_bumper"], r["driver"]
    prev = r.get("preview", {})
    pts = prev.get("points", [])
    p0 = pts[0] if pts else None
    ov = r.get("override", {})
    ind = r.get("indicator", {})
    return dict(
        t=float(r["sim_time"]), speed=float(ego["speed"]),
        track=ego["track"], lane=ego["lane"], s=float(ego["s"]), h=float(ego["h"]),
        fb_road=fb["road_id"], fb_lane=fb["lane"], fb_s=float(fb["s"]), fb_t=float(fb["t"]),
        fb_valid=bool(fb["valid"]),
        la=float(drv["lookahead"]), lat_err=float(drv["lateral_error"]),
        head_err=float(drv["heading_error"]), drv_valid=bool(drv["valid"]),
        prev_valid=bool(prev.get("valid", False)),
        p0=(p0["x"], p0["y"]) if p0 else None,
        ind_l=bool(ind.get("left", False)), ind_r=bool(ind.get("right", False)),
        ov_lat=bool(ov.get("lateral", False)), ov_lon=bool(ov.get("longitudinal", False)),
        ov_mt=bool(ov.get("manual_transition", False)), ov_at=bool(ov.get("auto_transition", False)),
        ov_rp=bool(ov.get("resume_pressed", False)),
    )


def check_run(path: Path, event_t: float) -> None:
    rows = load(path)
    win = [extract(r) for r in rows if abs(float(r["sim_time"]) - event_t) <= WINDOW_HALF]
    if not win:
        print(f"  {path.name}: t={event_t} 付近にフレームが無い")
        return

    # preview 先頭点の飛び幅の「通常値」を窓内中央値から出す（イベント自体を含む前提で
    # 大きめに出るが、それでも通常値の3倍という基準は十分保守的）。
    jumps = []
    for i in range(1, len(win)):
        a, b = win[i - 1]["p0"], win[i]["p0"]
        if a and b:
            jumps.append(math.hypot(b[0] - a[0], b[1] - a[1]))
    typical_jump = statistics.median(jumps) if jumps else 0.0

    findings: list[str] = []
    print(f"--- {path.name}  event t={event_t}s  (窓 ±{WINDOW_HALF}s, {len(win)} frames, "
          f"preview先頭点の通常飛び幅(中央値)={typical_jump:.4f}) ---")
    for i in range(1, len(win)):
        a, b = win[i - 1], win[i]
        tags = []
        if b["track"] != a["track"]:
            tags.append(f"track {a['track']}->{b['track']}")
        if b["lane"] != a["lane"]:
            tags.append(f"lane {a['lane']}->{b['lane']}")
        if b["fb_road"] != a["fb_road"]:
            tags.append(f"fb.road_id {a['fb_road']}->{b['fb_road']}")
        if b["fb_lane"] != a["fb_lane"]:
            tags.append(f"fb.lane {a['fb_lane']}->{b['fb_lane']}")
        if b["fb_valid"] != a["fb_valid"]:
            tags.append(f"fb.valid {a['fb_valid']}->{b['fb_valid']}")
        if b["drv_valid"] != a["drv_valid"]:
            tags.append(f"driver.valid {a['drv_valid']}->{b['drv_valid']}")
        if b["prev_valid"] != a["prev_valid"]:
            tags.append(f"preview.valid {a['prev_valid']}->{b['prev_valid']}")
        if b["ind_l"] != a["ind_l"] or b["ind_r"] != a["ind_r"]:
            tags.append(f"indicator {(a['ind_l'],a['ind_r'])}->{(b['ind_l'],b['ind_r'])}")
        for k in ("ov_lat", "ov_lon", "ov_mt", "ov_at", "ov_rp"):
            if a[k] != b[k]:
                tags.append(f"override.{k} {a[k]}->{b[k]}")
        ds = b["s"] - a["s"]
        ds_exp = a["speed"] * (b["t"] - a["t"])
        if abs(ds - ds_exp) > max(DS_ABS_MIN, abs(ds_exp) * DS_REL_MULT):
            tags.append(f"ego.s jump Δs={ds:+.3f} (expected≈{ds_exp:+.3f})")
        dh = b["h"] - a["h"]
        if abs(dh) > DH_MAX:
            tags.append(f"ego.h jump Δh={dh:+.4f}rad")
        if abs(b["fb_s"] - a["fb_s"]) > FB_JUMP_MAX:
            tags.append(f"fb.s jump Δ={b['fb_s']-a['fb_s']:+.3f}")
        if abs(b["fb_t"] - a["fb_t"]) > FB_JUMP_MAX:
            tags.append(f"fb.t jump Δ={b['fb_t']-a['fb_t']:+.3f}")
        if abs(b["la"] - a["la"]) > LOOKAHEAD_EPS:
            tags.append(f"driver.lookahead {a['la']:.3f}->{b['la']:.3f}")
        if a["p0"] and b["p0"]:
            d = math.hypot(b["p0"][0] - a["p0"][0], b["p0"][1] - a["p0"][1])
            if typical_jump > 0 and d > typical_jump * PREVIEW_JUMP_MULT:
                tags.append(f"preview先頭点 jump={d:.4f} (通常{typical_jump:.4f}の{d/typical_jump:.1f}倍)")

        marker = " <== EVENT FRAME" if abs(b["t"] - event_t) < 0.005 else ""
        if tags or marker:
            print(f"    t={b['t']:.2f}{marker}" + (": " + "; ".join(tags) if tags else ""))
            findings.extend(tags)

    if findings:
        print(f"  => 観測: 窓内で{len(findings)}件の不連続タグ（上記参照）")
    else:
        print("  => 見える範囲では同時刻の不連続なし")
    print()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jsonl", type=Path, required=True)
    ap.add_argument("--event-t", type=float, required=True)
    args = ap.parse_args()
    check_run(args.jsonl, args.event_t)
    return 0


if __name__ == "__main__":
    sys.exit(main())
