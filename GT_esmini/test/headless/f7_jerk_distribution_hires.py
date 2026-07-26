"""feature:F7 — 通常運転の指令躍度分布を9桁ヘッドレスログで取り直す（audit 対応）。

## 背景

`AdSteeringEnvelope.hpp` の `kAdEnvelopeDefaultSteerJerkMax=25.0` は実機ログ（4桁固定小数、
dt=0.01）から出した「p99=2.0 /s^2、単発max=290、二峰の谷」を根拠に置かれた PROVISIONAL 値だが、
その計器は躍度を 1.0/s^2 の量子でしか解像できない（p99=2.0 は量子2個分）。
`VirtualDriverTelemetryJson.cpp` の出力精度が 4桁→9桁 に上がった（躍度量子 1.0→1e-5 /s^2）ので、
同じ3走行に対応するシナリオをヘッドレス・閉ループで再実行し、`envelope.steer_in`
（包絡線に入る前の生 AD 指令。エンベロープの設定に一切左右されない）の躍度分布を実測し直す。

## 循環を避ける設定

`ad_steering_envelope_steer_jerk_max` と `ad_steering_envelope_steer_snap_max` を両方 0
（無効）にして走らせる。理由:
  1. steer_in 自体はそもそも包絡線通過前の値なので上限の影響を受けない。
  2. だが AD (PIDPurePursuitDriver) はステートレスでも、車両の物理位置は前フレームの
     steer_out の結果を引きずる。jerk/snap 上限が有効なまま走らせると、上限が軌道を
     変え、その変わった軌道に対して次フレームの AD が新しい steer_in を計算する
     ——上限が指令分布を作るという循環になる。
  3. 両方 0 にすると AdSteeringEnvelope.hpp のコード注記通り「ジャーク段への
     bit-identical no-op」になり、レート制限器より前の状態（この躍度上限が実装される
     前の実機ログと同じ条件）に一致する。

`GT_esmini/config/virtual_driver.json` は一切書き換えない
（gt_sim_test.py の `_prepare_policy_xosc` と同じ、per-run 一時ConfigFile注入方式）。

## 使い方

    DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\f7_jerk_distribution_hires.py
"""
from __future__ import annotations

import json
import math
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "GT_esmini" / "scripts" / "verification"))

from gt_sim_test import run, _prepare_policy_xosc, BASE_VD_CONFIG  # noqa: E402

DT = 0.01
MAX_TIME = 45.0

SCENARIOS = {
    "basic": ROOT / "resources" / "xosc" / "virtual_driver_basic.xosc",
    "right_turn": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                  / "decelerate_for_right_turn.xosc",
    "tljunction": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                  / "traffic_lights_junction.xosc",
}

OUT_ROOT = ROOT / "test_results" / "f7_jerk_distribution_hires"


def _make_config(tmpdir: Path) -> Path:
    """base(shipped) config + jerk/snap 上限を両方無効化した一時 config。
    シップ済み GT_esmini/config/virtual_driver.json は読むだけで一切書き換えない。"""
    base = json.loads(BASE_VD_CONFIG.read_text(encoding="utf-8"))
    base["ad_steering_envelope_steer_jerk_max"] = 0.0
    base["ad_steering_envelope_steer_snap_max"] = 0.0
    cfg_path = tmpdir / "virtual_driver.json"
    cfg_path.write_text(json.dumps(base, indent=2), encoding="utf-8")
    return cfg_path


def run_scenario(name: str, xosc_path: Path) -> Path:
    run_dir = OUT_ROOT / name
    tmpdir = Path(tempfile.mkdtemp(prefix=f"f7_hires_{name}_"))
    cfg_path = _make_config(tmpdir)
    variant = _prepare_policy_xosc(xosc_path, tmpdir, cfg_path)
    meta = run(variant, run_dir, DT, MAX_TIME, 0, None)
    print(f"[{name}] {meta}", file=sys.stderr)
    return run_dir / "telemetry.jsonl"


def main() -> int:
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    paths = {}
    for name, xosc_path in SCENARIOS.items():
        if not xosc_path.exists():
            print(f"SKIP {name}: {xosc_path} not found", file=sys.stderr)
            continue
        paths[name] = run_scenario(name, xosc_path)
    print(json.dumps({k: str(v) for k, v in paths.items()}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
