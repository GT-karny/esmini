#!/usr/bin/env python
"""feature:F7 — 別プロセスから haptic デバイスの全エフェクトを停止・解放する。

## なぜ要るか

`TerminateProcess`（Python の `Popen.kill()` は Windows でこれ）は、対象プロセスに
**一切のコードを実行させずに**殺す。したがって製品側に入れた歯止め — destructor /
`atexit` / signal / console-ctrl / SEH フィルタ — は**どれも走らない**。

通常はプロセス消滅時に OS が DirectInput デバイスを解放し、CONSTANT effect も
止まる。しかしそれは OS とドライバの後始末に委ねているだけで、こちらから確認できる
保証ではない。**人がいない状態で物理デバイスに力をかける以上、「たぶん止まる」で
済ませない。**

そこで強制終了の後に本スクリプトを走らせ、**別プロセスとしてデバイスを開き直し、
全エフェクトを停止してから解放する**。開けた時点でデバイスは前の所有者から解放
されており、`SDL_HapticStopAll` が確実に効く。

スーパーバイザ（`unattended_wheel_run.py`）は `kill()` 経路を通ったとき必ずこれを
呼ぶ。単体でも実行でき、実機の様子がおかしいときの手動リセットにも使える。

## 使い方

    scripts/ffb_spike/.venv/Scripts/python.exe scripts/ffb_spike/haptic_release.py

pysdl2 が要る（`scripts/ffb_spike/.venv` に入っている）。
デバイスが無い場合も**正常終了**する（止めるものが無いのは異常ではない）。
"""

from __future__ import annotations

import argparse
import ctypes
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    def say(msg: str) -> None:
        if not args.quiet:
            print(f"[haptic_release] {msg}")

    try:
        import sdl2
    except Exception as e:   # noqa: BLE001 — 失敗そのものを報告したい
        print(f"[haptic_release] FATAL: pysdl2 を import できない: {e}", file=sys.stderr)
        print("[haptic_release] scripts/ffb_spike/.venv の python で実行すること",
              file=sys.stderr)
        return 2

    if sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC) != 0:
        err = sdl2.SDL_GetError().decode(errors="replace")
        print(f"[haptic_release] FATAL: SDL_Init 失敗: {err}", file=sys.stderr)
        return 2

    stopped = 0
    try:
        n = sdl2.SDL_NumHaptics()
        say(f"haptic デバイス {n} 台")
        for i in range(n):
            name = sdl2.SDL_HapticName(i)
            name = name.decode(errors="replace") if name else f"<device {i}>"
            h = sdl2.SDL_HapticOpen(i)
            if not h:
                err = sdl2.SDL_GetError().decode(errors="replace")
                say(f"  [{i}] {name}: open 失敗 ({err}) — 他プロセスが掴んだままの可能性")
                continue
            try:
                # 停止 → 解放。開けている＝前の所有者は既に解放済みなので、
                # ここでの StopAll は確実に効く。
                sdl2.SDL_HapticStopAll(h)
                sdl2.SDL_HapticSetGain(h, 0)     # 念のため出力自体を 0 に
                stopped += 1
                say(f"  [{i}] {name}: 全エフェクト停止・ゲイン 0")
            finally:
                sdl2.SDL_HapticClose(h)
    finally:
        sdl2.SDL_Quit()

    say(f"RESULT: {stopped} 台を停止・解放した")
    return 0


if __name__ == "__main__":
    sys.exit(main())
