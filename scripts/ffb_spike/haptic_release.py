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
    ... --require-device     # 無人実行用: ホイールが1台も見えないなら失敗にする
    ... --selftest           # 実機なしで3分岐が正しく返ることを確認する

pysdl2 が要る（`scripts/ffb_spike/.venv` に入っている）。

## 戻り値（3つの結果を区別する。ここが安全上の要点）

| exit | result | 意味 |
| ---: | :--- | :--- |
| 0 | `RELEASED` | **解放した**。1台以上を開いて停止・ゲイン0にした |
| 0 | `NOTHING_TO_RELEASE` | **解放する必要が無かった**。haptic デバイスが0台 |
| **1** | **`FAILED`** | **解放できなかった**。1台以上が open できない／停止できない |
| 2 | `CANNOT_RUN` | そもそも動かせない（pysdl2 が無い / SDL_Init 失敗） |

**2026-07-29 の是正（安全欠陥）**: 以前は open 失敗を `continue` で読み飛ばし、
**無条件に 0 を返していた**。open が失敗する状況とは「他プロセスがまだデバイスを
掴んでいる」＝**まさに力が残っている可能性がある状況**である。つまり
**力を解放する仕組みが、解放できなかったときに成功を報告していた。**
呼び出し側（`unattended_wheel_run.py`）は `rel != 0` で失敗を検知する作りだったが、
この経路では永遠に 0 が返るので検知できなかった。

## 実体のない列挙エントリ（phantom）— 2026-07-29 追加

G29 は **物理 1 台なのに haptic として 3 つ列挙され、そのうち 1 つは常に open できない**
（実測: joystick 1 台 / haptic [0]OK [1]FAILED [2]OK・全て同名。開く順序に依らず構造的）。
これを素直に失敗とすると **S5 が毎回赤**になり、緑固定と同じくらい役に立たない。

そこで **joystick がちょうど 1 台**で、**同名エントリを 1 つ以上開いて停止できている**
場合に限り、開けなかったエントリを phantom として `RELEASED` にする。
`StopAll`/`SetGain` はデバイス単位の操作なので、1 つでも開けていれば力は止まっている。

**joystick 1 台に限定するのは推測を避けるため**: haptic API に GUID は無く名前しか無いので、
**同型機 2 台**では「開けた方」と「開けなかった方」を区別できず、片方が本当に掴まれていても
phantom と誤判定しうる。2 台以上のときは推測せず**従来どおり失敗**にする。
1 つも開けていない場合も従来どおり失敗。**phantom は必ず stderr と `phantom=N` に出す**
（黙って握り潰さない）。

最終行は常に機械可読（`--quiet` でも抑制しない。これは診断ではなく**判定**）:

    HAPTIC_RELEASE result=<...> devices=N released=N failed=N phantom=N joysticks=N

なお**本スクリプトの戻り値は最終確認手段ではない**。人が物理デバイスに触れる試験では
**目視と電源断が最終手段**である（条件A手順書）。戻り値は「機械が確認できた範囲」を
返すだけで、それが嘘をつかないようにするのがこの是正の目的。
"""

from __future__ import annotations

import argparse
import ctypes
import sys


def release_all(sdl2, say) -> dict:
    """全 haptic デバイスを停止・解放し、**何台成功し何台失敗したか**を返す。

    `sdl2` を引数で受けるのは selftest から差し替えるため（実機なしで
    open 失敗の分岐を通せるようにする。これが無いと、この関数の一番危険な
    経路だけが永久に未テストになる）。

    返り値: {"devices": N, "released": N, "failed": N, "failures": [str, ...]}
    """
    result = {"devices": 0, "released": 0, "failed": 0, "failures": [],
              "phantom": 0, "joysticks": 0, "released_names": set()}
    try:
        # 物理デバイス数の直接証拠。haptic API に GUID は無いが、
        # SDL_NumJoysticks() は「物理デバイスが何台か」を開かずに答える
        # （デバイスを掴まないので、実機試験と衝突しない）。
        result["joysticks"] = sdl2.SDL_NumJoysticks()
        n = sdl2.SDL_NumHaptics()
        result["devices"] = n
        say(f"haptic エントリ {n} 個 / joystick {result['joysticks']} 台")
        for i in range(n):
            name = sdl2.SDL_HapticName(i)
            name = name.decode(errors="replace") if name else f"<device {i}>"
            h = sdl2.SDL_HapticOpen(i)
            if not h:
                err = sdl2.SDL_GetError().decode(errors="replace")
                # ここを continue で読み飛ばして 0 を返していたのが是正前の欠陥。
                # open できない＝他プロセスが掴んでいる＝力が残っているかもしれない、
                # という**最も報告すべき状態**を握り潰していた。
                msg = f"[{i}] {name}: open 失敗 ({err})"
                say("  " + msg)
                result["failed"] += 1
                result["failures"].append((name, msg))
                continue
            try:
                # 停止 → 解放。開けている＝前の所有者は既に解放済みなので、
                # ここでの StopAll は確実に効く。
                sdl2.SDL_HapticStopAll(h)
                sdl2.SDL_HapticSetGain(h, 0)     # 念のため出力自体を 0 に
                result["released"] += 1
                result["released_names"].add(name)
                say(f"  [{i}] {name}: 全エフェクト停止・ゲイン 0")
            except Exception as e:               # noqa: BLE001
                # 開けたのに止められなかった。open 失敗より悪い（力が出ている
                # デバイスを掴んだまま止められていない）ので必ず失敗にする。
                msg = f"[{i}] {name}: 停止に失敗: {e}"
                say("  " + msg)
                result["failed"] += 1
                result["failures"].append((name, msg))
            finally:
                sdl2.SDL_HapticClose(h)
    finally:
        sdl2.SDL_Quit()

    # --- 「実体のない列挙エントリ」規則は撤回した（2026-07-29） -------------
    #
    # 一度は「joystick が 1 台なら、開けない haptic エントリは実体が無いので
    # 成功扱いにしてよい」という規則を入れた（78194323）。**実機で反証された。**
    #
    #   実行: HAPTIC_RELEASE result=FAILED devices=3 released=2 failed=1
    #         phantom=1 joysticks=1
    #         WARN「同一デバイスの別エントリを停止済みのため実体のない列挙として扱った」
    #   直後: **ユーザーが手で確認 → ホイールは重いまま＝力が残っていた**
    #
    # つまり「joystick が 1 台」は「開けないエントリが力を出していない」ことの
    # 証拠にならない。むしろ**開けない index 1 こそが実物の FFB インタフェースを
    # 握っている**可能性が高い（製品は SDL_HapticOpenFromJoystick で開くので
    # index を指定しておらず、開けた [0]/[2] が実体側だという保証がどこにも無い）。
    #
    # 規則は削除する。フラグで残すこともしない —— 「明示すれば使える」形で置くと、
    # いつか誰かが緑にするために使う。反証された規則は消すのが正しい。
    return result


def verdict(result: dict, require_device: bool) -> tuple[str, int]:
    """3分岐 → (result 名, exit code)。判定はここ1箇所に閉じる。"""
    if result["failed"] > 0:
        return "FAILED", 1
    if result["devices"] == 0:
        # 「止めるものが無い」は異常ではない（既定）。ただし無人実行では
        # 「あるはずのホイールが見えない」も異常なので --require-device で失敗にできる。
        return ("FAILED", 1) if require_device else ("NOTHING_TO_RELEASE", 0)
    return "RELEASED", 0


def _selftest() -> int:
    """実機なしで3分岐を通す。**open 失敗が非0を返すこと**が主眼。

    実機デバイスが無い環境では SDL_NumHaptics() が 0 になり、危険な分岐
    （open 失敗）に永久に到達しない。sdl2 モジュールを差し替えて
    「デバイスは見えるが掴まれている」状況を模す。
    """
    class _Stub:
        SDL_INIT_JOYSTICK = 1
        SDL_INIT_HAPTIC = 2

        def __init__(self, n, open_ok=True, stop_raises=False,
                     fail_idx=None, joysticks=1, names=None):
            self._n, self._open_ok, self._stop_raises = n, open_ok, stop_raises
            self._fail_idx = set() if fail_idx is None else set(fail_idx)
            self._joysticks = joysticks
            self._names = names
            self.closed = 0
            self.quit_called = False

        def SDL_NumJoysticks(self):          return self._joysticks
        def SDL_NumHaptics(self):            return self._n
        def SDL_HapticName(self, i):
            return (self._names[i] if self._names else b"StubWheel")
        def SDL_GetError(self):              return b"Haptic error Resetting device"
        def SDL_HapticOpen(self, i):
            if i in self._fail_idx:          return None
            return object() if self._open_ok else None
        def SDL_HapticStopAll(self, h):
            if self._stop_raises: raise RuntimeError("StopAll failed")
        def SDL_HapticSetGain(self, h, g):   return 0
        def SDL_HapticClose(self, h):        self.closed += 1
        def SDL_Quit(self):                  self.quit_called = True

    cases = [
        ("デバイス1台・open 成功 → RELEASED/0",        _Stub(1),                       False, "RELEASED", 0),
        ("デバイス0台 → NOTHING_TO_RELEASE/0",         _Stub(0),                       False, "NOTHING_TO_RELEASE", 0),
        ("デバイス0台 + --require-device → FAILED/1",  _Stub(0),                       True,  "FAILED", 1),
        ("**全エントリが open 失敗 → FAILED/1**",      _Stub(1, open_ok=False),        False, "FAILED", 1),
        ("2エントリ全滅 → FAILED/1",                   _Stub(2, open_ok=False),        False, "FAILED", 1),
        ("open できたが停止で例外 → FAILED/1",         _Stub(1, stop_raises=True),     False, "FAILED", 1),
        # --- 実機 G29 の形（joystick 1 台 / haptic 3 個 / index 1 が常に失敗）---
        ("**実機G29形: 3個中1個失敗+joy1 → FAILED/1（規則撤回後）**",
                                                       _Stub(3, fail_idx=[1], joysticks=1), False, "FAILED", 1),
        # --- ガード: 同型機2台では名前で区別できないので phantom 扱いしない ---
        ("同型2台(joy=2)で1個失敗 → FAILED/1",         _Stub(3, fail_idx=[1], joysticks=2), False, "FAILED", 1),
        # --- ガード: 1個も開けていないなら phantom 扱いしない ---
        ("joy1 だが全滅 → FAILED/1",                   _Stub(2, fail_idx=[0, 1], joysticks=1), False, "FAILED", 1),
        # --- ガード: 開けた側と名前が違うエントリは phantom 扱いしない ---
        ("名前が違うエントリが失敗 → FAILED/1",
             _Stub(2, fail_idx=[1], joysticks=1, names=[b"WheelA", b"WheelB"]), False, "FAILED", 1),
    ]
    bad = 0
    for desc, stub, req, want_name, want_code in cases:
        r = release_all(stub, lambda _m: None)
        name, code = verdict(r, req)
        ok = (name == want_name and code == want_code and stub.quit_called)
        print(f"  [{'OK ' if ok else 'NG!'}] {desc:52s} -> {name}/{code} "
              f"(dev={r['devices']} rel={r['released']} fail={r['failed']} "
              f"phantom={r['phantom']})")
        if not ok:
            bad += 1
    # 開けたデバイスは必ず閉じること（掴んだまま抜けたら本末転倒）
    s = _Stub(3)
    release_all(s, lambda _m: None)
    if s.closed != 3:
        print(f"  [NG!] 開いた3台のうち閉じたのは {s.closed} 台"); bad += 1
    else:
        print("  [OK ] 開いたデバイスは全て Close された")
    print(f"SELFTEST: {'PASS' if bad == 0 else f'FAIL ({bad} case)'}")
    return 0 if bad == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quiet", action="store_true",
                    help="診断行を抑制する（最終行の判定は抑制しない）")
    ap.add_argument("--require-device", action="store_true",
                    help="haptic デバイスが0台なら失敗にする（無人実行用）")
    ap.add_argument("--selftest", action="store_true",
                    help="実機なしで3分岐を検証する（open 失敗が非0を返すこと）")
    args = ap.parse_args()

    if args.selftest:
        return _selftest()

    def say(msg: str) -> None:
        if not args.quiet:
            print(f"[haptic_release] {msg}")

    try:
        import sdl2
    except Exception as e:   # noqa: BLE001 — 失敗そのものを報告したい
        print(f"[haptic_release] FATAL: pysdl2 を import できない: {e}", file=sys.stderr)
        print("[haptic_release] scripts/ffb_spike/.venv の python で実行すること",
              file=sys.stderr)
        print("HAPTIC_RELEASE result=CANNOT_RUN devices=0 released=0 failed=0")
        return 2

    if sdl2.SDL_Init(sdl2.SDL_INIT_JOYSTICK | sdl2.SDL_INIT_HAPTIC) != 0:
        err = sdl2.SDL_GetError().decode(errors="replace")
        print(f"[haptic_release] FATAL: SDL_Init 失敗: {err}", file=sys.stderr)
        print("HAPTIC_RELEASE result=CANNOT_RUN devices=0 released=0 failed=0")
        return 2

    r = release_all(sdl2, say)
    name, code = verdict(r, args.require_device)
    # phantom は成功扱いだが、**必ず見えるところに出す**（黙って握り潰さない）。
    for _nm, f in r["failures"]:
        print(f"[haptic_release] !! {f}", file=sys.stderr)
    if name == "FAILED":
        print("[haptic_release] !! 解放できていない。ホイールに力が残っている可能性がある — "
              "目視で確認し、必要なら電源を切ること", file=sys.stderr)
    # 判定行は --quiet でも必ず出す（これは診断ではなく結論）
    print(f"HAPTIC_RELEASE result={name} devices={r['devices']} "
          f"released={r['released']} failed={r['failed']} "
          f"phantom={r['phantom']} joysticks={r['joysticks']}")
    return code


if __name__ == "__main__":
    sys.exit(main())
