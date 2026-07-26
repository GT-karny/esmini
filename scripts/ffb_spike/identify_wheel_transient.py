#!/usr/bin/env python
"""feature:F7 — ホイールの過渡特性（無駄時間 θ と時定数 τ）を専用入力で同定する。

## なぜ専用計測が要るのか

AD 走行のログから θ と τ を当てはめようとしたが失敗した。同定走行で最良だった
θ=0.04s は、保留走行では τ 単独に負けた（0.0692 対 0.0679）。理由は明快で、
**閉ループの駆動データでは力と運動が相関している**ため、無駄時間と一次遅れが
区別できない。設計された入力（ステップ・反転・スイープ）でしか分離できない。

既存の CHARACTERIZATION.md は「一定力を印加して終端速度を測る」**定常**計測なので
v(f) は取れているが過渡は取れていない。ここで取りに行くのはその欠落分だけである。

## 何を測るか

  step      静止から一定力 F を印加。**θ** = 指令から最初の運動までの時間、
            **τ** = 終端速度の 63% に達するまでの時間。
  reversal  +F で動かしてから -F へ反転。残差ピークが実機で出たのは反転区間なので、
            ここが本命。反転後に運動方向が入れ替わるまでの時間を測る。
  sweep     周波数を上げながら方形波を与え、追従が崩れる周波数を見る。
            τ の独立した推定値になる（step の当てはめと突き合わせる）。

各条件を左右・複数回で繰り返す。**ばらつきも成果物**である: 合成プラントの
振り幅（PM 条件(1)）をこの実測ばらつきから決めるため。

## 安全（無人運転が前提）

力を出しているのは本スクリプト自身なので、歯止めは**インライン**に置く。
外から監視するより強い。

  S1 飽和継続   1 区間の印加時間を上限で切る（設計上そもそも短い）
  S2 逸脱       Rig.guard() が可動域を超えたら即座に力を切って中断
  S3 総時間     --max-runtime を超えたら中断
  S4 ハング     区間ごとに進捗ファイルへ書き出す（外側の watchdog 用）
  S5 解放       finally / atexit / SIGINT / SIGTERM で必ずゼロへランプして解放。
                Rig.close() は 10 段のゼロランプ後に StopAll する。

## 使い方

    scripts/ffb_spike/.venv/Scripts/python.exe scripts/ffb_spike/identify_wheel_transient.py \\
        --out test_results/f7_transient_ident.json --yes
"""

from __future__ import annotations

import argparse
import atexit
import io
import json
import math
import signal
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import g29lib  # noqa: E402

SAMPLE_DT = 0.004          # 250 Hz。theta が 10ms 級なら 4ms 刻みで解像できる
MOVE_EPS = 0.004           # 「動いた」とみなす軸変位（SDL2 量子化 ~0.001 の 4 倍）


class Aborted(RuntimeError):
    pass


def record(rig, force: float, seconds: float, guard_ref: float) -> list[tuple[float, float]]:
    """force を印加しながら (t, axis) を SAMPLE_DT 刻みで記録する。

    可動域を超えたら即座に力を切って Aborted を送出する（S2）。
    """
    out = []
    rig.set_reference(guard_ref)
    t0 = time.perf_counter()
    rig.set_force(force)
    while True:
        t = time.perf_counter() - t0
        if t >= seconds:
            break
        a = rig.axis()
        out.append((t, a))
        if not rig.guard(a):
            rig.set_force(0.0)
            raise Aborted(f"S2 可動域逸脱: axis={a:+.3f} ref={guard_ref:+.3f}")
        time.sleep(SAMPLE_DT)
    rig.set_force(0.0)
    return out


def estimate_step(samples: list[tuple[float, float]]) -> dict:
    """ステップ応答から theta と tau を推定する。

    theta: 指令(t=0)から軸変位が MOVE_EPS を超える最初の時刻。
           静止摩擦を破るまでの時間も含むので「見かけの無駄時間」であり、
           純粋な輸送遅れの上界になる。
    tau  : 速度が終端速度の 63.2% に達するまでの時間（theta 起点）。
    """
    if len(samples) < 20:
        return {}
    t0_axis = samples[0][1]
    theta = None
    for t, a in samples:
        if abs(a - t0_axis) > MOVE_EPS:
            theta = t
            break
    if theta is None:
        return {"moved": False}

    # 速度系列（中央差分・軽い平滑化）
    vs = []
    for i in range(2, len(samples) - 2):
        dt = samples[i + 2][0] - samples[i - 2][0]
        if dt > 0:
            vs.append((samples[i][0], (samples[i + 2][1] - samples[i - 2][1]) / dt))
    if not vs:
        return {"moved": True, "theta": theta}
    tail = [v for t, v in vs if t > vs[-1][0] * 0.6]
    v_term = statistics.median(tail) if tail else 0.0
    tau = None
    if abs(v_term) > 0.02:
        for t, v in vs:
            if t >= theta and abs(v) >= 0.632 * abs(v_term):
                tau = max(t - theta, 0.0)
                break
    return {"moved": True, "theta": theta, "tau": tau, "v_terminal": v_term}


def estimate_reversal(samples: list[tuple[float, float]], t_flip: float) -> dict:
    """反転入力から、運動方向が入れ替わるまでの遅れを測る。"""
    vs = []
    for i in range(2, len(samples) - 2):
        dt = samples[i + 2][0] - samples[i - 2][0]
        if dt > 0:
            vs.append((samples[i][0], (samples[i + 2][1] - samples[i - 2][1]) / dt))
    pre = [v for t, v in vs if t < t_flip]
    if not pre:
        return {}
    v_before = statistics.median(pre[-15:]) if len(pre) >= 15 else statistics.median(pre)
    if abs(v_before) < 0.02:
        return {"moved": False}
    t_zero = t_rev = None
    for t, v in vs:
        if t < t_flip:
            continue
        if t_zero is None and v * v_before <= 0:
            t_zero = t - t_flip                       # 停止するまで
        if t_rev is None and v * v_before < 0 and abs(v) > 0.02:
            t_rev = t - t_flip                        # 逆向きに動き出すまで
            break
    return {"moved": True, "v_before": v_before,
            "t_to_stop": t_zero, "t_to_reverse": t_rev}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--progress", type=Path, help="S4 用の進捗ファイル")
    ap.add_argument("--forces", default="0.25,0.30,0.35,0.40")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--step-seconds", type=float, default=0.60)
    ap.add_argument("--max-runtime", type=float, default=300.0, help="S3")
    ap.add_argument("--force-cap", type=float, default=0.45)
    ap.add_argument("--excursion", type=float, default=0.40, help="S2 可動域")
    ap.add_argument("--yes", action="store_true", help="実機が動く。確認を省略する")
    args = ap.parse_args()

    if not args.yes:
        print("実機ホイールが動きます。--yes を付けて実行してください。")
        return 2

    forces = [float(x) for x in args.forces.split(",")]
    rig = g29lib.Rig(force_cap=args.force_cap, excursion_limit=args.excursion)

    def release(*_):
        try:
            rig.close()
        except Exception:
            pass
    atexit.register(release)
    for s in (signal.SIGTERM,):
        try:
            signal.signal(s, lambda *_: (release(), sys.exit(143)))
        except Exception:
            pass

    results = {"sample_dt": SAMPLE_DT, "move_eps": MOVE_EPS,
               "force_cap": args.force_cap, "excursion": args.excursion,
               "steps": [], "reversals": [], "aborted": None}
    t_start = time.perf_counter()

    def progress(msg: str) -> None:
        print(f"[ident] {msg}", flush=True)
        if args.progress:
            args.progress.parent.mkdir(parents=True, exist_ok=True)
            io.open(args.progress, "a", encoding="utf-8").write(
                json.dumps({"t": time.time(), "msg": msg}) + "\n")

    try:
        with rig:
            progress("rig opened")
            for rep in range(args.repeats):
                for f in forces:
                    for sign in (+1.0, -1.0):
                        if time.perf_counter() - t_start > args.max_runtime:
                            raise Aborted(f"S3 総時間 {args.max_runtime}s 超過")
                        # 毎試行の前に中央へ戻す。戻さないと変位が累積し、
                        # 実測では 6/24 試行で S2（可動域逸脱）に達して中断した。
                        rig.recenter(0.0)
                        ref = rig.settle(0.4)
                        # --- step ---
                        s = record(rig, sign * f, args.step_seconds, ref)
                        est = estimate_step(s)
                        est.update(force=sign * f, rep=rep,
                                   direction="left" if sign > 0 else "right")
                        results["steps"].append(est)
                        progress(f"step f={sign*f:+.2f} rep={rep} "
                                 f"theta={est.get('theta')} tau={est.get('tau')}")
                        rig.settle(0.4)
            # --- reversal ---
            for rep in range(args.repeats):
                for f in (0.30, 0.40):
                    for sign in (+1.0, -1.0):
                        if time.perf_counter() - t_start > args.max_runtime:
                            raise Aborted(f"S3 総時間 {args.max_runtime}s 超過")
                        rig.recenter(0.0)
                        ref = rig.settle(0.4)
                        rig.set_reference(ref)
                        out = []
                        t0 = time.perf_counter()
                        t_flip = args.step_seconds
                        rig.set_force(sign * f)
                        flipped = False
                        while True:
                            t = time.perf_counter() - t0
                            if t >= t_flip * 2:
                                break
                            if not flipped and t >= t_flip:
                                rig.set_force(-sign * f)
                                flipped = True
                            a = rig.axis()
                            out.append((t, a))
                            if not rig.guard(a):
                                rig.set_force(0.0)
                                raise Aborted(f"S2 可動域逸脱: axis={a:+.3f}")
                            time.sleep(SAMPLE_DT)
                        rig.set_force(0.0)
                        est = estimate_reversal(out, t_flip)
                        est.update(force=sign * f, rep=rep)
                        results["reversals"].append(est)
                        progress(f"reversal f={sign*f:+.2f} rep={rep} "
                                 f"t_to_stop={est.get('t_to_stop')} "
                                 f"t_to_reverse={est.get('t_to_reverse')}")
                        rig.settle(0.4)
    except Aborted as e:
        results["aborted"] = str(e)
        progress(f"ABORTED: {e}")
    except Exception as e:   # noqa: BLE001 — 何であれ力を切って記録する
        results["aborted"] = f"{type(e).__name__}: {e}"
        progress(f"ERROR: {e}")
    finally:
        release()

    # --- 集計 ---
    th = [s["theta"] for s in results["steps"] if s.get("theta") is not None]
    ta = [s["tau"] for s in results["steps"] if s.get("tau") is not None]
    tr = [r["t_to_reverse"] for r in results["reversals"] if r.get("t_to_reverse") is not None]
    def stat(v):
        if not v:
            return None
        return {"n": len(v), "median": statistics.median(v), "min": min(v), "max": max(v),
                "stdev": statistics.pstdev(v) if len(v) > 1 else 0.0}
    results["summary"] = {"theta_s": stat(th), "tau_s": stat(ta), "t_to_reverse_s": stat(tr)}

    args.out.parent.mkdir(parents=True, exist_ok=True)
    io.open(args.out, "w", encoding="utf-8").write(json.dumps(results, indent=2))
    print(json.dumps(results["summary"], indent=2))
    print(f"[ident] wrote {args.out}")
    return 1 if results["aborted"] else 0


if __name__ == "__main__":
    sys.exit(main())
