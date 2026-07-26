#!/usr/bin/env python
"""feature:F7 — 実機 G29 の無人走行スーパーバイザ。

## なぜ要るか

ハプティックホイールは**動力のあるアクチュエータ**である。人が見ている走行なら、
異常時に人が最後の砦になる（ホイールが暴れたら電源を抜く）。無人走行にはそれが無い。

製品側にも歯止めを入れた（`SDLFFBSink` の飽和継続 / 総実行時間ウォッチドッグと、
atexit / signal / console-ctrl による緊急解放）。本スクリプトはその外側の層で、
**製品が応答しなくなった場合**に備える:

  S1  実効力の飽和が継続        -> 終了（製品側watchdogと二重）
  S2  軸がハードストップに張り付き -> 終了
  S3  全体タイムアウト           -> 終了
  S4  テレメトリが更新されない（ハング）-> 終了

歯止めの判定には製品が毎フレーム吐く `GT_VD_TELEMETRY_JSONL` を追尾する。
記録専用の経路なので、監視のために製品の挙動を変えていない。

終了後は**プロセスが残っていないこと**を必ず確認する。プロセス消滅で OS が
DirectInput デバイスを解放し CONSTANT effect は止まるが、残存確認までやって
はじめて「ホイールに力がかかり続けない」と言える。

**Windows では terminate() は穏当な終了ではない。** CPython の Popen.kill は
Windows ブランチで terminate と同一関数（`kill = terminate`）で、どちらも
TerminateProcess を呼ぶ。子プロセスは一切コードを実行せずに死ぬので、製品側の
歯止め（Close / atexit / signal / console-ctrl / SEH）は**一つも走らない**。
よって**スーパーバイザが中断させた場合は必ず**、別プロセスによる haptic 解放
（S5）を走らせる。
"""

from __future__ import annotations

import argparse
import io
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def read_tail(path: Path, seen: int) -> tuple[list[dict], int]:
    """Return (new frames, new byte offset)."""
    if not path.exists():
        return [], seen
    out = []
    with io.open(path, encoding="utf-8") as fh:
        fh.seek(seen)
        for line in fh:
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    pass          # partially-written last line; picked up next poll
        new_seen = fh.tell()
    return out, new_seen


SPIKE_VENV_PY = (Path(__file__).resolve().parent / ".venv" / "Scripts" / "python.exe")
RELEASE_TOOL  = (Path(__file__).resolve().parent / "haptic_release.py")


def release_haptics_out_of_process() -> int:
    """強制終了の後始末: 別プロセスでデバイスを開き直し全エフェクトを止める。

    TerminateProcess は対象にコードを実行させないので、製品側の歯止めは一つも
    走らない。ここだけが最後の砦になる。pysdl2 が要るので ffb_spike の venv で
    起動する（無ければ失敗として報告する — 黙って諦めない）。
    """
    if not SPIKE_VENV_PY.exists():
        print(f"[supervisor] ERROR: {SPIKE_VENV_PY} が無い。haptic を解放できない")
        return 2
    try:
        r = subprocess.run([str(SPIKE_VENV_PY), str(RELEASE_TOOL)],
                           capture_output=True, text=True, timeout=60)
        for ln in (r.stdout or "").splitlines():
            print("   " + ln)
        for ln in (r.stderr or "").splitlines():
            print("   ! " + ln)
        return r.returncode
    except Exception as e:   # noqa: BLE001
        print(f"[supervisor] ERROR: haptic 解放ツールの実行に失敗: {e}")
        return 2


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="GT_Sim.exe のパス")
    ap.add_argument("--osc", required=True, help="シナリオ xosc")
    ap.add_argument("--jsonl", required=True, type=Path, help="毎フレーム記録の出力先")
    ap.add_argument("--logfile", default="unattended_run.log", help="stdout/stderr の保存先")
    ap.add_argument("--max-runtime", type=float, default=120.0, help="S3 全体タイムアウト [s]")
    ap.add_argument("--max-saturation", type=float, default=2.0, help="S1 飽和継続の上限 [s]")
    ap.add_argument("--saturation-level", type=float, default=0.57,
                    help="S1 飽和とみなす |実効力|（既定 0.95 x max_force 0.6）")
    ap.add_argument("--max-hardstop", type=float, default=2.0, help="S2 端張り付きの上限 [s]")
    ap.add_argument("--hardstop-level", type=float, default=0.95, help="S2 とみなす |軸|")
    ap.add_argument("--max-stall", type=float, default=10.0, help="S4 無更新の上限 [s]")
    ap.add_argument("--extra", nargs=argparse.REMAINDER, default=[],
                    help="GT_Sim へ渡す追加引数")
    args = ap.parse_args()

    if args.jsonl.exists():
        args.jsonl.unlink()
    args.jsonl.parent.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env["GT_VD_TELEMETRY_JSONL"] = str(args.jsonl)

    cmd = [args.exe, "--osc", args.osc] + list(args.extra)
    print(f"[supervisor] launching: {' '.join(cmd)}")
    print(f"[supervisor] telemetry -> {args.jsonl}")
    logfh = io.open(args.logfile, "w", encoding="utf-8", errors="replace")
    proc = subprocess.Popen(cmd, stdout=logfh, stderr=subprocess.STDOUT, env=env)

    t0 = time.monotonic()
    seen = 0
    sat_since = None
    stop_since = None
    last_frame_wall = t0
    abort_reason = None
    hard_killed = False

    try:
        while proc.poll() is None:
            time.sleep(0.20)
            now = time.monotonic()
            frames, seen = read_tail(args.jsonl, seen)
            if frames:
                last_frame_wall = now
            for f in frames:
                g = f.get("ffb", {}).get("gates", {})
                force = abs(float(g.get("effective_force", 0.0)))
                axis = abs(float(g.get("actual_norm", 0.0)))
                t = float(f.get("sim_time", 0.0))

                sat_since = t if (force >= args.saturation_level and sat_since is None) else \
                            (None if force < args.saturation_level else sat_since)
                if sat_since is not None and (t - sat_since) >= args.max_saturation:
                    abort_reason = (f"S1 実効力 {force:.3f} が {args.saturation_level:.3f} 以上で "
                                    f"{t - sat_since:.1f}s 継続（上限 {args.max_saturation:.1f}s）")

                stop_since = t if (axis >= args.hardstop_level and stop_since is None) else \
                             (None if axis < args.hardstop_level else stop_since)
                if stop_since is not None and (t - stop_since) >= args.max_hardstop:
                    abort_reason = (f"S2 軸 {axis:.3f} がハードストップ域で "
                                    f"{t - stop_since:.1f}s 継続（上限 {args.max_hardstop:.1f}s）")
                if abort_reason:
                    break

            if not abort_reason and (now - t0) >= args.max_runtime:
                abort_reason = f"S3 全体タイムアウト {args.max_runtime:.0f}s"
            if not abort_reason and (now - last_frame_wall) >= args.max_stall:
                abort_reason = (f"S4 テレメトリが {now - last_frame_wall:.1f}s 更新されない"
                                f"（上限 {args.max_stall:.0f}s）")

            if abort_reason:
                print(f"[supervisor] ABORT: {abort_reason}")
                # WINDOWS では terminate() は穏当ではない。CPython の
                # Popen.kill は Windows ブランチで terminate と同一関数
                # （subprocess.py: `kill = terminate`）であり、どちらも
                # TerminateProcess を呼ぶ。対象プロセスは **一切コードを実行せずに**
                # 死ぬので、Close() も atexit も signal も console-ctrl も SEH
                # フィルタも走らない。
                #
                # したがって hard_killed はここで立てる。kill() 側だけで立てていた
                # 以前の実装は、S1-S4 で中断して terminate が 10 秒以内に成功した
                # 経路 — つまり最も普通に起きる経路 — で haptic 解放を丸ごと飛ばし、
                # 安全条件が実際にトリップした場面でこそ後始末が抜けていた。
                proc.terminate()
                hard_killed = True
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    print("[supervisor] terminate に応答なし — kill する")
                    proc.kill()
                    proc.wait(timeout=10)
                break
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=10)
            hard_killed = True
        logfh.close()

    # --- 強制終了経路の後始末（C6）---------------------------------------
    # Windows では terminate() も kill() も TerminateProcess であり、対象プロセスに
    # 一切コードを実行させない（CPython subprocess.py の Windows ブランチは
    # `kill = terminate`）。製品側の destructor / atexit / signal / console-ctrl /
    # SEH フィルタは **どれも走らない**。OS がデバイスを解放するはずだが、それは
    # 確認できる保証ではない。人がいない以上「たぶん止まる」で済ませないので、
    # 別プロセスでデバイスを開き直して全エフェクトを停止・解放する。
    #
    # つまり「スーパーバイザが中断させた」＝必ずこの経路を通る。正常完走のときだけ
    # 製品側の Close() が走るので、そのときは不要。
    if hard_killed:
        print("[supervisor] 強制終了経路を通った — 別プロセスで haptic を解放する")
        rel = release_haptics_out_of_process()
        if rel != 0:
            print("[supervisor] !! haptic の外部解放に失敗した。"
                  "ホイールに力が残っている可能性がある — 目視/電源で確認すること")
            return 3

    rc = proc.returncode
    print(f"[supervisor] process exited rc={rc} after {time.monotonic() - t0:.1f}s wall")

    # --- 後始末の確認: プロセスが残っていないこと -------------------------
    exe_name = Path(args.exe).name
    leftovers = []
    try:
        out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {exe_name}"],
                             capture_output=True, text=True, timeout=30).stdout
        leftovers = [ln for ln in out.splitlines() if exe_name.lower() in ln.lower()]
    except Exception as e:      # noqa: BLE001 — 確認できないこと自体を報告したい
        print(f"[supervisor] WARN: プロセス確認に失敗: {e}")
        leftovers = ["<確認不能>"]

    if leftovers:
        print(f"[supervisor] !! {exe_name} が残存している。ホイールに力がかかり続ける恐れ:")
        for ln in leftovers:
            print("   " + ln)
        return 2
    print(f"[supervisor] 後始末 OK: {exe_name} の残存プロセスなし")

    frames, _ = read_tail(args.jsonl, 0)
    print(f"[supervisor] captured {len(frames)} telemetry frames")
    if abort_reason:
        print(f"[supervisor] RESULT: ABORTED ({abort_reason})")
        return 1
    print("[supervisor] RESULT: completed normally")
    return 0


if __name__ == "__main__":
    sys.exit(main())
