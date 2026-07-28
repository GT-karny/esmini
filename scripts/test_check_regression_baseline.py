#!/usr/bin/env python3
"""check_regression_baseline.py の鮮度ゲート実証（feature:F7 監査(2)）。

check_regression_baseline.py は `gt_sim_test.py batch` とは別プロセスで、CI では
別ワークフローステップとして呼ばれる。gt_sim_test.py 側の _reset_batch_output_dir()
は「batch() が実際にもう一度走れば前回の緑は残らない」ことしか保証せず、
「batch ステップがそもそも走らなかった／失敗を continue-on-error で握り潰した」
場合に古い batch_verdict.json をそのまま読んでしまう穴には効かない。

このテストは main() を argv 付きで直接呼ぶ（sys.argv 経由ではない）。CI/
run_regression_gate.ps1 が実際に叩くのは CLI エントリポイントであり、
read_batch_output() を直接呼ぶだけのテストでは「CI が実際に叩く経路」を
示したことにならない。

実行: DriverScript/.venv/Scripts/python.exe scripts/test_check_regression_baseline.py
"""
import json
import shutil
import sys
import tempfile
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_regression_baseline as crb  # noqa: E402

FAILED = []


def expect(name, ok):
    print(f"  [{'OK ' if ok else 'MISS'}] {name}")
    if not ok:
        FAILED.append(name)


def _write_batch_verdict(out_dir: Path, generated_at) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    doc = {
        "manifest": "resources/xosc/verification/car_following_traffic_control_batch.yaml",
        "commit": "deadbeef",
        "overall": "needs-review",
        "summary": {"pass": 0, "fail": 0, "needs-review": 0, "error": 0},
        "scenarios": [],
    }
    if generated_at is not None:
        doc["generated_at"] = generated_at
    (out_dir / "batch_verdict.json").write_text(json.dumps(doc), encoding="utf-8")


def _minimal_baseline(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("manifest: x\nnote: test\nexpected_summary: {}\nscenarios: {}\n",
                     encoding="utf-8")


tmp = Path(tempfile.mkdtemp(prefix="crb_freshness_"))
try:
    print("== check_regression_baseline.py 鮮度ゲート (main() 経由、CIの実経路) ==")

    # 1. 十分新しい generated_at -> 鮮度ゲートを通過し、比較(0件)まで到達して PASS
    fresh_dir = tmp / "fresh"
    _write_batch_verdict(fresh_dir, datetime.now(timezone.utc).isoformat(timespec="seconds"))
    baseline_fresh = tmp / "baseline_fresh.yaml"
    _minimal_baseline(baseline_fresh)
    rc = crb.main(["--batch-out", str(fresh_dir), "--baseline", str(baseline_fresh)])
    expect("新鮮な generated_at は鮮度ゲートを通過して比較まで進む (rc=0)", rc == 0)

    # 2. 閾値(30分)より古い generated_at -> NOT MEASURED (exit 2) で落ちる
    stale_dir = tmp / "stale"
    old_ts = (datetime.now(timezone.utc) - timedelta(hours=2)).isoformat(timespec="seconds")
    _write_batch_verdict(stale_dir, old_ts)
    baseline_stale = tmp / "baseline_stale.yaml"
    _minimal_baseline(baseline_stale)
    rc = crb.main(["--batch-out", str(stale_dir), "--baseline", str(baseline_stale),
                   "--max-age-seconds", "1800"])
    expect("2時間前の generated_at (閾値30分超過) は exit 2 で落ちる", rc == 2)

    # 3. generated_at が無い成果物（旧形式/壊れたjson）も同様に exit 2 -- 「鮮度不明」を
    #    「鮮度OK」側に倒さない
    nogen_dir = tmp / "nogen"
    _write_batch_verdict(nogen_dir, None)
    baseline_nogen = tmp / "baseline_nogen.yaml"
    _minimal_baseline(baseline_nogen)
    rc = crb.main(["--batch-out", str(nogen_dir), "--baseline", str(baseline_nogen)])
    expect("generated_at が無い成果物も exit 2 で落ちる（鮮度不明を安全側に倒す）", rc == 2)

    # 4. --max-age-seconds を緩めれば同じ古い成果物でも鮮度ゲートは通過する。
    #    rc==2 が閾値と無関係な固定挙動ではなく、実際にパラメータへ反応することの証明。
    rc = crb.main(["--batch-out", str(stale_dir), "--baseline", str(baseline_stale),
                   "--max-age-seconds", str(3 * 3600)])
    expect("--max-age-seconds を緩めると同じ古い成果物でも鮮度ゲートは通過する (rc=0)",
           rc == 0)

finally:
    shutil.rmtree(tmp, ignore_errors=True)

print()
if FAILED:
    print(f"FAILED: {len(FAILED)} — {FAILED}")
    sys.exit(1)
print("鮮度ゲートは main() 経由（CI/run_regression_gate.ps1 の実経路）で意図通り発火した")
