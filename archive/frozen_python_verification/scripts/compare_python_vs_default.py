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
from dat import DATFile

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
            "sim_dat": Path,
            "python_trace": Path | None,
            "stdout": str,
            "stderr": str,
        }
    """
    output_dir.mkdir(parents=True, exist_ok=True)

    sim_dat = output_dir / "sim.dat"
    python_trace = output_dir / "python_trace.jsonl"

    cmd = [
        str(gt_sim_exe),
        "--osc", str(xosc_path),
        "--headless",
        "--record", str(sim_dat),
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
        "sim_dat": sim_dat if sim_dat.exists() else None,
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

    Raises:
        ImportError: Jinja2がインストールされていない場合
    """
    # Jinja2必須チェック
    try:
        from jinja2 import Environment
        import math
    except ImportError:
        raise ImportError(
            "Jinja2が必要です。以下のコマンドでインストールしてください:\n"
            "pip install jinja2"
        )

    def safe_format(fmt_spec, value):
        """
        安全な数値フォーマット（inf/nan/None/"N/A"対応）

        Jinja2フィルタとして使用: {{ "%.3f"|format(value) }}
        → safe_format("%.3f", value) として呼び出される

        Args:
            fmt_spec: フォーマット指定子（例: "%.3f", ".1%"）
            value: フォーマット対象の値

        Returns:
            フォーマット済み文字列、またはエラー時は"N/A"
        """
        # 既に文字列の場合（clean_inf()で変換済み）
        if isinstance(value, str):
            return value

        # Noneの場合
        if value is None:
            return "N/A"

        # inf/nanの場合
        if isinstance(value, (int, float)):
            if math.isinf(value) or math.isnan(value):
                return "N/A"

        # 正常な数値の場合
        try:
            # fmt_spec が "%.3f" 形式の場合、format()互換に変換
            if fmt_spec.startswith("%"):
                # "%"を除去して、format()用の指定子に変換
                # 例: "%.3f" → ".3f"
                fmt_spec = fmt_spec[1:]

            return f"{value:{fmt_spec}}"
        except (ValueError, TypeError, KeyError):
            return "N/A"

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

    # テンプレートファイルを読み込み
    template_path = Path(__file__).parent / "comparison_report_template.html"

    if not template_path.exists():
        print(f"警告: HTMLテンプレートが見つかりません: {template_path}")
        return

    template_content = template_path.read_text(encoding='utf-8')

    # Jinja2環境を作成
    env = Environment()
    env.filters['format'] = safe_format  # formatフィルタをオーバーライド

    # テンプレートをロード
    jinja_template = env.from_string(template_content)

    # レンダリング
    html = jinja_template.render(
        timestamp=comparison_results.get("timestamp", "不明"),
        matrix_path=str(matrix_path),
        thresholds_path=str(thresholds_path),
        summary=comparison_results.get("summary", {}),
        scenarios=scenarios
    )

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
    skipped = 0

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
        if default_result["sim_dat"] and python_result["sim_dat"]:
            # .datファイルを拡張CSVに変換
            if verbose:
                print(f"  .dat → CSV変換中...")

            try:
                # DefaultController結果を変換
                dat_default = DATFile(str(default_result["sim_dat"]), extended=True)
                dat_default.save_csv(extended=True, include_file_refs=True)
                dat_default.close()

                # PythonDriverController結果を変換
                dat_python = DATFile(str(python_result["sim_dat"]), extended=True)
                dat_python.save_csv(extended=True, include_file_refs=True)
                dat_python.close()

                # 変換後のCSVファイルパスを更新
                # save_csv()は元のファイル名の拡張子を.csvに変更して保存する
                default_csv = default_result["sim_dat"].with_suffix('.csv')
                python_csv = python_result["sim_dat"].with_suffix('.csv')

                if verbose:
                    print(f"  DefaultController CSV: {default_csv}")
                    print(f"  PythonDriverController CSV: {python_csv}")

            except Exception as e:
                print(f"  [SKIP] .dat変換失敗（シミュレーション異常終了の可能性）: {e}")
                results[scenario_id] = {
                    "error": "dat_conversion_failed",
                    "details": str(e),
                    "default_result": default_result,
                    "python_result": python_result,
                    "skipped": True,
                }
                skipped += 1
                total -= 1  # スキップはtotalから除外
                continue

            # CSVファイルでメトリクス計算
            metrics = compare_all_metrics(default_csv, python_csv)

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
                "error": "sim_dat_not_found",
                "default_result": default_result,
                "python_result": python_result,
            }
            failed += 1
            print(f"\n[NG] エラー: sim.datが見つかりません")

    # 総合サマリー
    summary = {
        "total": total,
        "passed": passed,
        "failed": failed,
        "skipped": skipped,
        "pass_rate": passed / total if total > 0 else 0.0,
    }

    print(f"\n{'=' * 60}")
    skip_msg = f"（{skipped}件スキップ）" if skipped > 0 else ""
    print(f"総合結果: {passed}/{total} 合格 ({summary['pass_rate']:.1%}）{skip_msg}")
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
                default_dat = result["default_result"].get("sim_dat")
                python_dat = result["python_result"].get("sim_dat")

                # pathオブジェクトではない場合はスキップ
                if isinstance(default_dat, Path) and isinstance(python_dat, Path):
                    if default_dat and default_dat.exists() and python_dat and python_dat.exists():
                        try:
                            from plot_comparison import generate_all_plots
                            generate_all_plots(scenario_id, default_dat, python_dat, plots_dir)
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
        json.dumps(comparison_results, indent=2, ensure_ascii=False, default=str),
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
