#!/usr/bin/env python3
"""比較テスト自動化スクリプト

PythonDriverController vs DefaultController の比較テストを実行し、
HTML レポートまで自動生成します。

使用方法:
    python run_comparison_test.py [--scenario SCENARIO_ID]

例:
    python run_comparison_test.py --scenario straight_500m
    python run_comparison_test.py  # 全シナリオ実行
"""

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

# スクリプトディレクトリをパスに追加
sys.path.insert(0, str(Path(__file__).parent / "scripts"))

from dat import DATFile
from validate_realdriver_feature_results import load_yaml
from scenario_generator import generate_python_variant
from comparison_kpis import compare_all_metrics


def run_gt_sim(xosc_path, output_dir, gt_sim_exe, timeout=60, verbose=False):
    """GT_Sim を実行してシミュレーション結果を取得"""
    output_dir.mkdir(parents=True, exist_ok=True)
    sim_dat = output_dir / "sim.dat"

    cmd = [
        str(gt_sim_exe),
        "--osc", str(xosc_path),
        "--headless",
        "--record", str(sim_dat),
    ]

    if verbose:
        print(f"  実行: {' '.join(cmd)}")

    start_time = time.time()
    try:
        result = subprocess.run(
            cmd,
            cwd=str(Path(__file__).parent),
            capture_output=True,
            text=True,
            timeout=timeout
        )
        duration = time.time() - start_time
        exit_code = result.returncode

        # stdout/stderr を保存
        (output_dir / "stdout.txt").write_text(result.stdout, encoding='utf-8', errors='ignore')
        (output_dir / "stderr.txt").write_text(result.stderr, encoding='utf-8', errors='ignore')

        if verbose:
            print(f"  終了コード: {exit_code}, 実行時間: {duration:.2f}秒")

        return {
            "exit_code": exit_code,
            "duration": duration,
            "sim_dat": sim_dat if sim_dat.exists() else None
        }
    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        print(f"  [WARN] タイムアウト: {timeout}秒")
        return {
            "exit_code": -1,
            "duration": duration,
            "sim_dat": None
        }


def convert_dat_to_csv(dat_file, verbose=False):
    """DAT ファイルを CSV に変換"""
    if not dat_file or not dat_file.exists():
        return None

    if verbose:
        print(f"  .dat → CSV 変換: {dat_file.name}")

    try:
        dat = DATFile(str(dat_file), extended=True)
        dat.save_csv(extended=True, include_file_refs=True)
        dat.close()

        csv_file = dat_file.with_suffix('.csv')
        if verbose:
            print(f"    → {csv_file.name} ({csv_file.stat().st_size // 1024}KB)")
        return csv_file
    except Exception as e:
        print(f"  [ERROR] 変換失敗: {e}")
        return None


def calculate_metrics(default_csv, python_csv, verbose=False):
    """メトリクスを計算"""
    if not default_csv or not python_csv:
        return None

    if verbose:
        print(f"  メトリクス計算中...")

    try:
        metrics = compare_all_metrics(default_csv, python_csv)
        if verbose:
            print(f"    軌跡RMSE: {metrics['trajectory']['xy_rmse']:.4f}m")
            print(f"    速度RMSE: {metrics['speed']['speed_rmse']:.4f}m/s")
            print(f"    Lane一致率: {metrics['lane_keeping']['lane_id_match_ratio']:.2%}")
        return metrics
    except Exception as e:
        print(f"  [ERROR] メトリクス計算失敗: {e}")
        import traceback
        traceback.print_exc()
        return None


def evaluate_metrics(metrics, thresholds, scenario_id):
    """メトリクスを閾値と照合"""
    if not metrics:
        return {"pass": False, "failures": ["メトリクス計算失敗"], "warnings": []}

    # デフォルト閾値
    default_th = thresholds.get('defaults', {})
    # シナリオ固有の閾値
    scenario_th = thresholds.get('scenario_overrides', {}).get(scenario_id, {})

    # 統合
    traj_th = {**default_th.get('trajectory', {}), **scenario_th.get('trajectory', {})}
    speed_th = {**default_th.get('speed', {}), **scenario_th.get('speed', {})}
    lane_th = {**default_th.get('lane_keeping', {}), **scenario_th.get('lane_keeping', {})}
    route_th = {**default_th.get('route', {}), **scenario_th.get('route', {})}

    failures = []

    # 軌跡評価
    if metrics['trajectory']['xy_rmse'] > traj_th.get('xy_rmse_max', 0.5):
        failures.append(f"軌跡RMSE: {metrics['trajectory']['xy_rmse']:.4f}m > {traj_th.get('xy_rmse_max', 0.5)}m")
    if metrics['trajectory']['endpoint_distance'] > traj_th.get('endpoint_distance_max', 2.0):
        failures.append(f"終点距離: {metrics['trajectory']['endpoint_distance']:.4f}m > {traj_th.get('endpoint_distance_max', 2.0)}m")

    # 速度評価
    if metrics['speed']['speed_rmse'] > speed_th.get('speed_rmse_max', 0.3):
        failures.append(f"速度RMSE: {metrics['speed']['speed_rmse']:.4f}m/s > {speed_th.get('speed_rmse_max', 0.3)}m/s")

    # レーン遵守評価
    if metrics['lane_keeping']['lane_id_match_ratio'] < lane_th.get('lane_id_match_ratio_min', 0.95):
        failures.append(f"Lane ID一致率: {metrics['lane_keeping']['lane_id_match_ratio']:.2%} < {lane_th.get('lane_id_match_ratio_min', 0.95):.2%}")

    # ルート進捗評価
    if metrics['route']['s_end_delta'] > route_th.get('s_end_delta_max', 1.0):
        failures.append(f"終端s座標差分: {metrics['route']['s_end_delta']:.4f}m > {route_th.get('s_end_delta_max', 1.0)}m")

    return {
        "pass": len(failures) == 0,
        "failures": failures,
        "warnings": []
    }


def generate_html_report(summary, output_path, matrix_path, thresholds_path):
    """HTML レポートを生成"""
    try:
        from jinja2 import Environment
    except ImportError:
        print("[ERROR] Jinja2 not installed. Install with: pip install jinja2")
        return False

    # テンプレートをロード
    template_path = Path(__file__).parent / "scripts" / "comparison_report_template.html"
    if not template_path.exists():
        print(f"[ERROR] テンプレート not found: {template_path}")
        return False

    template_content = template_path.read_text(encoding='utf-8')

    # safe_format 関数を定義
    def safe_format(fmt_spec, value):
        if value is None or value == "N/A":
            return "N/A"
        if isinstance(value, str):
            try:
                value = float(value)
            except:
                return str(value)
        if isinstance(value, (int, float)):
            import math
            if math.isinf(value) or math.isnan(value):
                return "N/A"
            try:
                return fmt_spec % value
            except:
                return str(value)
        return str(value)

    # Jinja2 環境を作成
    env = Environment()
    env.filters['format'] = safe_format

    # レンダリング
    jinja_template = env.from_string(template_content)
    html = jinja_template.render(
        timestamp=summary.get("timestamp", "不明"),
        matrix_path=str(matrix_path),
        thresholds_path=str(thresholds_path),
        summary=summary.get("summary", {}),
        scenarios=summary.get("scenarios", {})
    )

    # HTML を保存
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(html, encoding='utf-8')
    return True


def main():
    parser = argparse.ArgumentParser(description="PythonDriverController vs DefaultController 比較テスト")
    parser.add_argument("--scenario", help="実行するシナリオID (省略時は全シナリオ)")
    parser.add_argument("--matrix", default="GT_esmini/test/comparison_matrix.yaml", help="テストマトリクスYAML")
    parser.add_argument("--thresholds", default="GT_esmini/test/comparison_thresholds.yaml", help="閾値YAML")
    parser.add_argument("--output", default="test_results/comparison_auto", help="出力ディレクトリ")
    parser.add_argument("--gt-sim", default="build/GT_esmini/Release/GT_Sim.exe", help="GT_Sim実行ファイル")
    parser.add_argument("--verbose", action="store_true", help="詳細ログ出力")

    args = parser.parse_args()

    # パスを設定
    repo_root = Path(__file__).parent
    matrix_path = repo_root / args.matrix
    thresholds_path = repo_root / args.thresholds
    output_dir = repo_root / args.output
    gt_sim_exe = repo_root / args.gt_sim

    # 設定をロード
    print(f"比較テスト開始")
    print(f"  マトリクス: {matrix_path}")
    print(f"  閾値: {thresholds_path}")
    print(f"  出力: {output_dir}")
    print(f"  GT_Sim: {gt_sim_exe}")

    if not gt_sim_exe.exists():
        print(f"[ERROR] GT_Sim not found: {gt_sim_exe}")
        return 1

    matrix = load_yaml(matrix_path)
    thresholds = load_yaml(thresholds_path)
    defaults = matrix.get("defaults", {})
    scenarios = matrix.get("scenarios", [])

    # シナリオフィルタ
    if args.scenario:
        scenarios = [s for s in scenarios if s['id'] == args.scenario]
        if not scenarios:
            print(f"[ERROR] Scenario not found: {args.scenario}")
            return 1

    # 結果を格納
    results = {}
    total = 0
    passed = 0
    failed = 0

    for scenario in scenarios:
        scenario_id = scenario['id']
        total += 1

        print(f"\n{'=' * 60}")
        print(f"シナリオ: {scenario_id}")
        print(f"説明: {scenario['description']}")
        print(f"{'=' * 60}")

        baseline_xosc = repo_root / scenario["baseline_xosc"]
        if not baseline_xosc.exists():
            print(f"[ERROR] Baseline XOSC not found: {baseline_xosc}")
            failed += 1
            continue

        # Python バリアントを生成
        python_xosc = baseline_xosc.parent / f"{scenario_id}_python.xosc"
        python_result_dir = output_dir / "python" / scenario_id
        python_result_dir.mkdir(parents=True, exist_ok=True)

        print(f"\n1. XOSC バリアント生成...")
        generate_python_variant(
            baseline_xosc,
            python_xosc,
            python_script=defaults.get("python_script", "DriverScript/pythondriver/scenario_drive_embedded.py"),
            python_class=defaults.get("python_class", "EmbeddedController"),
            python_trace=defaults.get("python_trace_enabled", True),
            python_trace_dir=str(python_result_dir.resolve()),
            verbose=args.verbose
        )

        # DefaultController 実行
        print(f"\n2. DefaultController 実行...")
        default_result_dir = output_dir / "default" / scenario_id
        default_result = run_gt_sim(baseline_xosc, default_result_dir, gt_sim_exe,
                                     timeout=defaults.get("timeout", 60), verbose=args.verbose)

        # PythonDriverController 実行
        print(f"\n3. PythonDriverController 実行...")
        python_result = run_gt_sim(python_xosc, python_result_dir, gt_sim_exe,
                                    timeout=defaults.get("timeout", 60), verbose=args.verbose)

        # .dat → CSV 変換
        print(f"\n4. DAT → CSV 変換...")
        default_csv = convert_dat_to_csv(default_result.get("sim_dat"), verbose=args.verbose)
        python_csv = convert_dat_to_csv(python_result.get("sim_dat"), verbose=args.verbose)

        # メトリクス計算
        print(f"\n5. メトリクス計算...")
        metrics = calculate_metrics(default_csv, python_csv, verbose=args.verbose)

        # 閾値評価
        print(f"\n6. 閾値評価...")
        evaluation = evaluate_metrics(metrics, thresholds, scenario_id)

        # 結果を格納
        results[scenario_id] = {
            "scenario_id": scenario_id,
            "description": scenario['description'],
            "default_result": {
                "exit_code": default_result.get("exit_code", -1),
                "duration": default_result.get("duration", 0.0)
            },
            "python_result": {
                "exit_code": python_result.get("exit_code", -1),
                "duration": python_result.get("duration", 0.0)
            },
            "metrics": metrics or {},
            "evaluation": evaluation
        }

        if evaluation["pass"]:
            passed += 1
            print(f"\n[OK] 合格")
        else:
            failed += 1
            print(f"\n[NG] 不合格")
            for failure in evaluation["failures"]:
                print(f"  - {failure}")

    # サマリーを生成
    summary = {
        "scenarios": results,
        "summary": {
            "total": total,
            "passed": passed,
            "failed": failed,
            "pass_rate": passed / total if total > 0 else 0.0
        },
        "timestamp": datetime.now().isoformat()
    }

    # JSON サマリーを保存
    print(f"\n{'=' * 60}")
    print(f"7. JSON サマリー生成...")
    summary_path = output_dir / "comparison_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f"  → {summary_path}")

    # HTML レポートを生成
    print(f"\n8. HTML レポート生成...")
    report_path = output_dir / "comparison_report.html"
    if generate_html_report(summary, report_path, matrix_path, thresholds_path):
        print(f"  → {report_path}")
    else:
        print(f"  [ERROR] HTML レポート生成失敗")

    # 総合結果
    print(f"\n{'=' * 60}")
    print(f"総合結果")
    print(f"{'=' * 60}")
    print(f"  合格: {passed}/{total}")
    print(f"  不合格: {failed}/{total}")
    print(f"  合格率: {summary['summary']['pass_rate']:.1%}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
