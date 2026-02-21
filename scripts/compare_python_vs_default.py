#!/usr/bin/env python3
"""PythonDriverController vs DefaultController 比較オーケストレーター

comparison_matrix.yamlで定義されたシナリオを両コントローラーで実行し、
結果を比較してHTML/JSONレポートを生成。
"""

import argparse
import json
import subprocess
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional

import sys
sys.path.insert(0, str(Path(__file__).parent))

from validate_realdriver_feature_results import load_yaml
from scenario_generator import generate_default_variant, generate_python_variant
from comparison_kpis import compare_all_metrics

try:
    from plot_comparison import generate_all_plots
    PLOT_AVAILABLE = True
except ImportError:
    PLOT_AVAILABLE = False
    print("警告: plot_comparisonモジュールが読み込めません。プロット生成をスキップします。")


def run_scenario(
    xosc_path: Path,
    output_dir: Path,
    gt_sim_exe: Path,
    timeout: int = 60,
    verbose: bool = False
) -> Dict[str, Any]:
    """GT_Simでシナリオを実行

    Args:
        xosc_path: XOSCファイルパス
        output_dir: 出力ディレクトリ
        gt_sim_exe: GT_Sim実行ファイルパス
        timeout: タイムアウト [s]
        verbose: 詳細ログ出力

    Returns:
        {
            "exit_code": int,
            "duration": float,
            "sim_csv": Path,
            "python_trace": Path | None,
            "stdout": str,
            "stderr": str,
        }
    """
    output_dir.mkdir(parents=True, exist_ok=True)

    sim_csv = output_dir / "sim.csv"
    python_trace = output_dir / "python_trace.jsonl"

    cmd = [
        str(gt_sim_exe),
        "--osc", str(xosc_path),
        "--headless",
        "--record", str(sim_csv),
    ]

    if verbose:
        print(f"  実行: {' '.join(cmd)}")
        print(f"  作業ディレクトリ: {output_dir}")

    start_time = time.time()

    try:
        # CWDをリポジトリルートに設定（相対パス解決のため）
        repo_root = Path(__file__).parent.parent
        result = subprocess.run(
            cmd,
            cwd=str(repo_root),
            capture_output=True,
            text=True,
            timeout=timeout
        )
        duration = time.time() - start_time
        exit_code = result.returncode

    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        exit_code = -1
        result = type('obj', (object,), {
            'stdout': f"タイムアウト: {timeout}秒",
            'stderr': f"タイムアウト: {timeout}秒"
        })

    # stdout/stderrを保存
    (output_dir / "stdout.txt").write_text(result.stdout, encoding='utf-8')
    (output_dir / "stderr.txt").write_text(result.stderr, encoding='utf-8')

    if verbose:
        print(f"  終了コード: {exit_code}")
        print(f"  実行時間: {duration:.2f}秒")

    return {
        "exit_code": exit_code,
        "duration": duration,
        "sim_csv": sim_csv if sim_csv.exists() else None,
        "python_trace": python_trace if python_trace.exists() else None,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }


def evaluate_against_thresholds(
    metrics: Dict[str, Dict[str, Any]],
    thresholds: Dict[str, Any],
    scenario_id: str
) -> Dict[str, Any]:
    """メトリクスを閾値と照合

    Args:
        metrics: 計算されたメトリクス
        thresholds: 閾値設定
        scenario_id: シナリオID

    Returns:
        {
            "pass": bool,
            "failures": List[str],
            "warnings": List[str],
        }
    """
    # デフォルト閾値を取得
    default_thresholds = thresholds.get("defaults", {})

    # シナリオ固有のオーバーライドを取得
    scenario_overrides = thresholds.get("scenario_overrides", {}).get(scenario_id, {})

    # 統合閾値（オーバーライドが優先）
    merged_thresholds = {}
    for category in ["trajectory", "speed", "lane_keeping", "route"]:
        merged_thresholds[category] = {
            **default_thresholds.get(category, {}),
            **scenario_overrides.get(category, {})
        }

    passed = True
    failures = []
    warnings = []

    # 軌跡メトリクス評価
    traj_metrics = metrics.get("trajectory", {})
    traj_thresholds = merged_thresholds.get("trajectory", {})

    if traj_metrics.get("xy_rmse", float('inf')) > traj_thresholds.get("xy_rmse_max", 0.5):
        passed = False
        failures.append(
            f"軌跡RMSE: {traj_metrics['xy_rmse']:.3f}m > {traj_thresholds['xy_rmse_max']}m"
        )

    if traj_metrics.get("endpoint_distance", float('inf')) > traj_thresholds.get("endpoint_distance_max", 2.0):
        passed = False
        failures.append(
            f"終点距離: {traj_metrics['endpoint_distance']:.3f}m > {traj_thresholds['endpoint_distance_max']}m"
        )

    # 速度メトリクス評価
    speed_metrics = metrics.get("speed", {})
    speed_thresholds = merged_thresholds.get("speed", {})

    if speed_metrics.get("speed_rmse", float('inf')) > speed_thresholds.get("speed_rmse_max", 0.3):
        passed = False
        failures.append(
            f"速度RMSE: {speed_metrics['speed_rmse']:.3f}m/s > {speed_thresholds['speed_rmse_max']}m/s"
        )

    # レーン遵守メトリクス評価
    lane_metrics = metrics.get("lane_keeping", {})
    lane_thresholds = merged_thresholds.get("lane_keeping", {})

    if lane_metrics.get("lane_id_match_ratio", 0.0) < lane_thresholds.get("lane_id_match_ratio_min", 0.95):
        passed = False
        failures.append(
            f"Lane ID一致率: {lane_metrics['lane_id_match_ratio']:.2%} < {lane_thresholds['lane_id_match_ratio_min']:.2%}"
        )

    # ルート進捗メトリクス評価
    route_metrics = metrics.get("route", {})
    route_thresholds = merged_thresholds.get("route", {})

    if route_metrics.get("s_end_delta", float('inf')) > route_thresholds.get("s_end_delta_max", 1.0):
        passed = False
        failures.append(
            f"終端s座標差分: {route_metrics['s_end_delta']:.3f}m > {route_thresholds['s_end_delta_max']}m"
        )

    # 警告チェック
    warn_thresholds = thresholds.get("warnings", {})
    if traj_metrics.get("xy_rmse", 0) > warn_thresholds.get("trajectory", {}).get("xy_rmse_warn", 0.3):
        warnings.append(f"軌跡RMSE警告: {traj_metrics['xy_rmse']:.3f}m")

    return {
        "pass": passed,
        "failures": failures,
        "warnings": warnings,
    }


def generate_html_report(
    comparison_results: Dict[str, Any],
    output_path: Path,
    matrix_path: Path,
    thresholds_path: Path
) -> None:
    """HTMLレポートを生成

    Args:
        comparison_results: 比較結果
        output_path: 出力HTMLファイルパス
        matrix_path: マトリクスYAMLパス
        thresholds_path: 閾値YAMLパス
    """
    # テンプレートファイルを読み込み
    template_path = Path(__file__).parent / "comparison_report_template.html"

    if not template_path.exists():
        print(f"警告: HTMLテンプレートが見つかりません: {template_path}")
        return

    template = template_path.read_text(encoding='utf-8')

    # Jinja2が利用可能か確認
    try:
        from jinja2 import Template
        import math

        # inf値をフィルタリング
        def clean_inf(value):
            if isinstance(value, float) and (math.isinf(value) or math.isnan(value)):
                return "N/A"
            return value

        # メトリクスをクリーン化
        scenarios = comparison_results.get("scenarios", {})
        for scenario_id, result in scenarios.items():
            if result.get("metrics"):
                for category in result["metrics"].values():
                    if isinstance(category, dict):
                        for key in category:
                            category[key] = clean_inf(category[key])

        jinja_template = Template(template)
        html = jinja_template.render(
            timestamp=comparison_results.get("timestamp", "不明"),
            matrix_path=str(matrix_path),
            thresholds_path=str(thresholds_path),
            summary=comparison_results.get("summary", {}),
            scenarios=scenarios
        )
    except ImportError:
        # Jinja2がない場合は簡易置換
        print("警告: Jinja2がインストールされていません。簡易HTMLレポートを生成します。")
        print("pip install jinja2 でインストールすることを推奨します。")

        # 簡易版HTMLを生成
        scenarios_html = ""
        for scenario_id, result in comparison_results.get("scenarios", {}).items():
            if result.get("metrics"):
                pass_status = "[OK] 合格" if result["evaluation"]["pass"] else "[NG] 不合格"
                scenarios_html += f"""
                <h2>{scenario_id}</h2>
                <p><strong>説明:</strong> {result['description']}</p>
                <p><strong>結果:</strong> {pass_status}</p>
                <ul>
                    <li>XY RMSE: {result['metrics']['trajectory']['xy_rmse']:.3f} m</li>
                    <li>速度RMSE: {result['metrics']['speed']['speed_rmse']:.3f} m/s</li>
                    <li>Lane ID一致率: {result['metrics']['lane_keeping']['lane_id_match_ratio']:.1%}</li>
                </ul>
                """

        html = f"""<!DOCTYPE html>
<html lang="ja">
<head><meta charset="UTF-8"><title>比較レポート</title></head>
<body>
<h1>コントローラー比較レポート</h1>
<p>生成日時: {comparison_results.get('timestamp', '不明')}</p>
<h2>サマリー</h2>
<ul>
    <li>総数: {comparison_results['summary']['total']}</li>
    <li>合格: {comparison_results['summary']['passed']}</li>
    <li>不合格: {comparison_results['summary']['failed']}</li>
</ul>
{scenarios_html}
</body>
</html>
"""

    # HTMLを保存
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(html, encoding='utf-8')
    print(f"[OK] HTMLレポート生成: {output_path}")


def run_comparison_suite(
    matrix_path: Path,
    thresholds_path: Path,
    output_dir: Path,
    gt_sim_exe: Path,
    scenario_filter: Optional[str] = None,
    verbose: bool = False,
    generate_plots: bool = True,
    generate_report: bool = True
) -> Dict[str, Any]:
    """比較スイートを実行

    Args:
        matrix_path: comparison_matrix.yamlパス
        thresholds_path: comparison_thresholds.yamlパス
        output_dir: 出力ディレクトリ
        gt_sim_exe: GT_Sim実行ファイル
        scenario_filter: シナリオフィルタ（Noneで全実行）
        verbose: 詳細ログ出力

    Returns:
        {
            "scenarios": {scenario_id: result_dict, ...},
            "summary": {total, passed, failed},
        }
    """
    # 設定ファイルを読み込み
    matrix = load_yaml(matrix_path)
    thresholds = load_yaml(thresholds_path)

    scenarios = matrix.get("scenarios", [])
    defaults = matrix.get("defaults", {})

    results = {}
    total = 0
    passed = 0
    failed = 0

    for scenario in scenarios:
        scenario_id = scenario["id"]

        # フィルタ適用
        if scenario_filter and scenario_id != scenario_filter:
            continue

        total += 1
        print(f"\n{'=' * 60}")
        print(f"シナリオ: {scenario_id}")
        print(f"説明: {scenario['description']}")
        print(f"{'=' * 60}")

        baseline_xosc = Path(scenario["baseline_xosc"])
        if not baseline_xosc.exists():
            print(f"  エラー: ベースラインXOSCが見つかりません: {baseline_xosc}")
            results[scenario_id] = {"error": "baseline_xosc_not_found"}
            failed += 1
            continue

        # 絶対パスに変換
        baseline_xosc = baseline_xosc.resolve()

        # XOSCバリアントを生成
        # DefaultControllerバリアント: 元のXOSCをそのまま使用（コントローラー指定なし想定）
        default_xosc = baseline_xosc

        # PythonDriverControllerバリアント: ベースラインと同じディレクトリに生成（相対パス解決のため）
        python_xosc = baseline_xosc.parent / f"{scenario_id}_python.xosc"

        # トレース出力先ディレクトリを事前作成（絶対パス指定のため）
        python_result_dir = output_dir / "python" / scenario_id
        python_result_dir.mkdir(parents=True, exist_ok=True)

        print(f"\nXOSCバリアント生成...")
        print(f"  DefaultController: 元のXOSCを使用 ({baseline_xosc})")
        generate_python_variant(
            baseline_xosc,
            python_xosc,
            python_script=defaults.get("python_script", "DriverScript/pythondriver/scenario_drive_embedded.py"),
            python_class=defaults.get("python_class", "EmbeddedController"),
            python_trace=defaults.get("python_trace_enabled", True),
            python_trace_dir=str(python_result_dir.resolve()),
            verbose=verbose
        )

        # DefaultController実行
        print(f"\nDefaultController実行...")
        default_result_dir = output_dir / "default" / scenario_id
        default_result = run_scenario(
            default_xosc,
            default_result_dir,
            gt_sim_exe,
            timeout=defaults.get("timeout", 60),
            verbose=verbose
        )

        if default_result["exit_code"] != 0:
            print(f"  警告: DefaultController終了コード={default_result['exit_code']}")

        # PythonDriverController実行
        print(f"\nPythonDriverController実行...")
        python_result = run_scenario(
            python_xosc,
            python_result_dir,
            gt_sim_exe,
            timeout=defaults.get("timeout", 60),
            verbose=verbose
        )

        if python_result["exit_code"] != 0:
            print(f"  警告: PythonDriverController終了コード={python_result['exit_code']}")

        # トレースファイル検証
        if verbose:
            expected_traces = [
                python_result_dir / "python_trace.jsonl",
                python_result_dir / "cpp_to_py_trace.jsonl",
                python_result_dir / "py_to_cpp_trace.jsonl",
            ]
            for trace in expected_traces:
                if trace.exists():
                    line_count = sum(1 for _ in open(trace, 'r', encoding='utf-8'))
                    print(f"  [OK] {trace.name}: {line_count}行, {trace.stat().st_size}バイト")
                else:
                    print(f"  [WARN] {trace.name}: 未生成")

        # メトリクス計算
        print(f"\nメトリクス計算...")
        if default_result["sim_csv"] and python_result["sim_csv"]:
            metrics = compare_all_metrics(
                default_result["sim_csv"],
                python_result["sim_csv"]
            )

            # 閾値評価
            evaluation = evaluate_against_thresholds(metrics, thresholds, scenario_id)

            # 結果サマリー
            scenario_result = {
                "scenario_id": scenario_id,
                "description": scenario["description"],
                "default_result": {
                    "exit_code": default_result["exit_code"],
                    "duration": default_result["duration"],
                },
                "python_result": {
                    "exit_code": python_result["exit_code"],
                    "duration": python_result["duration"],
                },
                "metrics": metrics,
                "evaluation": evaluation,
            }

            results[scenario_id] = scenario_result

            # 合否判定
            if evaluation["pass"]:
                passed += 1
                print(f"\n[OK] 合格")
            else:
                failed += 1
                print(f"\n[NG] 不合格")
                for failure in evaluation["failures"]:
                    print(f"  - {failure}")

        else:
            results[scenario_id] = {
                "error": "sim_csv_not_found",
                "default_result": default_result,
                "python_result": python_result,
            }
            failed += 1
            print(f"\n[NG] エラー: sim.csvが見つかりません")

    # 総合サマリー
    summary = {
        "total": total,
        "passed": passed,
        "failed": failed,
        "pass_rate": passed / total if total > 0 else 0.0,
    }

    print(f"\n{'=' * 60}")
    print(f"総合結果: {passed}/{total} 合格 ({summary['pass_rate']:.1%})")
    print(f"{'=' * 60}")

    comparison_results = {
        "scenarios": results,
        "summary": summary,
        "timestamp": datetime.now().isoformat(),
    }

    # プロット生成
    if generate_plots and PLOT_AVAILABLE:
        print(f"\n[Plot] プロット生成中...")
        plots_dir = output_dir / "plots"
        for scenario_id, result in results.items():
            if result.get("metrics") and "default_result" in result and "python_result" in result:
                default_csv = result["default_result"].get("sim_csv")
                python_csv = result["python_result"].get("sim_csv")

                # pathオブジェクトではない場合はスキップ
                if isinstance(default_csv, Path) and isinstance(python_csv, Path):
                    if default_csv and default_csv.exists() and python_csv and python_csv.exists():
                        try:
                            from plot_comparison import generate_all_plots
                            generate_all_plots(scenario_id, default_csv, python_csv, plots_dir)
                        except Exception as e:
                            print(f"  警告: {scenario_id}のプロット生成に失敗: {e}")

    # HTMLレポート生成
    if generate_report:
        print(f"\n[HTML] HTMLレポート生成中...")
        html_report = output_dir / "comparison_report.html"
        generate_html_report(comparison_results, html_report, matrix_path, thresholds_path)

    return comparison_results


def main():
    parser = argparse.ArgumentParser(
        description="PythonDriverController vs DefaultController 比較オーケストレーター"
    )
    parser.add_argument(
        "--matrix",
        type=Path,
        default=Path("GT_esmini/test/comparison_matrix.yaml"),
        help="comparison_matrix.yamlパス"
    )
    parser.add_argument(
        "--thresholds",
        type=Path,
        default=Path("GT_esmini/test/comparison_thresholds.yaml"),
        help="comparison_thresholds.yamlパス"
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="出力ディレクトリ（デフォルト: test_results/comparison_YYYYMMDD_HHMMSS）"
    )
    parser.add_argument(
        "--gt-sim",
        type=Path,
        default=Path("DriverScript/bin/GT_Sim.exe"),
        help="GT_Sim実行ファイルパス"
    )
    parser.add_argument(
        "--scenario",
        type=str,
        default=None,
        help="特定シナリオのみ実行（scenario_id）"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="詳細ログを出力"
    )
    parser.add_argument(
        "--no-plots",
        action="store_true",
        help="プロット生成をスキップ"
    )
    parser.add_argument(
        "--no-report",
        action="store_true",
        help="HTMLレポート生成をスキップ"
    )

    args = parser.parse_args()

    # 出力ディレクトリ設定
    if args.output is None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output = Path(f"test_results/comparison_{timestamp}")

    print(f"比較テスト開始")
    print(f"  マトリクス: {args.matrix}")
    print(f"  閾値: {args.thresholds}")
    print(f"  出力: {args.output}")
    print(f"  GT_Sim: {args.gt_sim}")

    if not args.gt_sim.exists():
        print(f"\nエラー: GT_Simが見つかりません: {args.gt_sim}")
        return 1

    # 比較スイート実行
    comparison_results = run_comparison_suite(
        matrix_path=args.matrix,
        thresholds_path=args.thresholds,
        output_dir=args.output,
        gt_sim_exe=args.gt_sim,
        scenario_filter=args.scenario,
        verbose=args.verbose,
        generate_plots=not args.no_plots,
        generate_report=not args.no_report
    )

    # 結果をJSONに保存
    summary_json = args.output / "comparison_summary.json"
    summary_json.parent.mkdir(parents=True, exist_ok=True)
    summary_json.write_text(
        json.dumps(comparison_results, indent=2, ensure_ascii=False),
        encoding='utf-8'
    )

    print(f"\n[OK] 結果保存: {summary_json}")

    # 不合格がある場合は終了コード1
    if comparison_results["summary"]["failed"] > 0:
        return 1

    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
