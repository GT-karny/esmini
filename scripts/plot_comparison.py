#!/usr/bin/env python3
"""コントローラー比較可視化スクリプト

PythonDriverControllerとDefaultControllerの比較結果を可視化するプロットを生成。
"""

import argparse
from pathlib import Path
from typing import Dict, List, Tuple

try:
    import matplotlib
    matplotlib.use('Agg')  # GUI不要のバックエンドを使用
    import matplotlib.pyplot as plt
    MATPLOTLIB_AVAILABLE = True
except ImportError:
    MATPLOTLIB_AVAILABLE = False
    print("警告: matplotlibがインストールされていません。プロット生成をスキップします。")
    print("インストール: pip install matplotlib")

import sys
sys.path.insert(0, str(Path(__file__).parent))

from validate_realdriver_feature_results import parse_csv_rows, select_ego_rows, _parse_float


def plot_xy_trajectory_overlay(
    default_csv: Path,
    python_csv: Path,
    output: Path,
    title: str = "XY軌跡比較"
) -> None:
    """XY軌跡をオーバーレイ表示

    Args:
        default_csv: DefaultController結果CSV
        python_csv: PythonDriverController結果CSV
        output: 出力PNG画像パス
        title: プロットタイトル
    """
    if not MATPLOTLIB_AVAILABLE:
        return

    default_rows = parse_csv_rows(default_csv)
    python_rows = parse_csv_rows(python_csv)

    default_ego = select_ego_rows(default_rows)
    python_ego = select_ego_rows(python_rows)

    if not default_ego or not python_ego:
        print(f"警告: データが不足しているため、{output}をスキップします")
        return

    # X, Y座標を抽出
    default_x = [_parse_float(r.get("x", "0")) or 0.0 for r in default_ego]
    default_y = [_parse_float(r.get("y", "0")) or 0.0 for r in default_ego]
    python_x = [_parse_float(r.get("x", "0")) or 0.0 for r in python_ego]
    python_y = [_parse_float(r.get("y", "0")) or 0.0 for r in python_ego]

    # プロット作成
    fig, ax = plt.subplots(figsize=(12, 8))

    # 軌跡をプロット
    ax.plot(default_x, default_y, 'b-', linewidth=2, label='DefaultController', alpha=0.8)
    ax.plot(python_x, python_y, 'r--', linewidth=2, label='PythonDriverController', alpha=0.8)

    # 始点・終点マーカー
    ax.scatter(default_x[0], default_y[0], c='blue', s=150, marker='o', zorder=5, label='Default 始点')
    ax.scatter(default_x[-1], default_y[-1], c='blue', s=150, marker='s', zorder=5, label='Default 終点')
    ax.scatter(python_x[0], python_y[0], c='red', s=150, marker='o', zorder=5, label='Python 始点')
    ax.scatter(python_x[-1], python_y[-1], c='red', s=150, marker='s', zorder=5, label='Python 終点')

    ax.set_xlabel("X [m]", fontsize=12)
    ax.set_ylabel("Y [m]", fontsize=12)
    ax.set_title(title, fontsize=14, fontweight='bold')
    ax.legend(fontsize=10, loc='best')
    ax.grid(True, alpha=0.3)
    ax.axis('equal')

    plt.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output, dpi=150, bbox_inches='tight')
    plt.close(fig)

    print(f"  生成: {output}")


def plot_speed_comparison(
    default_csv: Path,
    python_csv: Path,
    output: Path,
    title: str = "速度プロファイル比較"
) -> None:
    """速度プロファイルを比較表示

    Args:
        default_csv: DefaultController結果CSV
        python_csv: PythonDriverController結果CSV
        output: 出力PNG画像パス
        title: プロットタイトル
    """
    if not MATPLOTLIB_AVAILABLE:
        return

    default_rows = parse_csv_rows(default_csv)
    python_rows = parse_csv_rows(python_csv)

    default_ego = select_ego_rows(default_rows)
    python_ego = select_ego_rows(python_rows)

    if not default_ego or not python_ego:
        print(f"警告: データが不足しているため、{output}をスキップします")
        return

    # 時刻と速度を抽出
    default_times = [_parse_float(r.get("time", "0")) or 0.0 for r in default_ego]
    default_speeds = [_parse_float(r.get("speed", "0")) or 0.0 for r in default_ego]
    python_times = [_parse_float(r.get("time", "0")) or 0.0 for r in python_ego]
    python_speeds = [_parse_float(r.get("speed", "0")) or 0.0 for r in python_ego]

    # プロット作成
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10), sharex=True)

    # 速度プロファイル
    ax1.plot(default_times, default_speeds, 'b-', linewidth=2, label='DefaultController', alpha=0.8)
    ax1.plot(python_times, python_speeds, 'r--', linewidth=2, label='PythonDriverController', alpha=0.8)
    ax1.set_ylabel("速度 [m/s]", fontsize=12)
    ax1.set_title(title, fontsize=14, fontweight='bold')
    ax1.legend(fontsize=10, loc='best')
    ax1.grid(True, alpha=0.3)

    # 速度差分
    # 共通時間範囲で補間（簡易版：最小サンプル数に合わせる）
    min_len = min(len(default_speeds), len(python_speeds))
    speed_diff = [abs(default_speeds[i] - python_speeds[i]) for i in range(min_len)]
    diff_times = default_times[:min_len]

    ax2.plot(diff_times, speed_diff, 'g-', linewidth=1.5, label='速度差分（絶対値）')
    ax2.fill_between(diff_times, 0, speed_diff, alpha=0.3, color='green')
    ax2.set_xlabel("時刻 [s]", fontsize=12)
    ax2.set_ylabel("速度差分 [m/s]", fontsize=12)
    ax2.set_title("速度差分", fontsize=12, fontweight='bold')
    ax2.legend(fontsize=10, loc='best')
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output, dpi=150, bbox_inches='tight')
    plt.close(fig)

    print(f"  生成: {output}")


def plot_lateral_offset(
    default_csv: Path,
    python_csv: Path,
    output: Path,
    title: str = "横方向オフセット比較"
) -> None:
    """横方向オフセット（t座標）を比較表示

    Args:
        default_csv: DefaultController結果CSV
        python_csv: PythonDriverController結果CSV
        output: 出力PNG画像パス
        title: プロットタイトル
    """
    if not MATPLOTLIB_AVAILABLE:
        return

    default_rows = parse_csv_rows(default_csv)
    python_rows = parse_csv_rows(python_csv)

    default_ego = select_ego_rows(default_rows)
    python_ego = select_ego_rows(python_rows)

    if not default_ego or not python_ego:
        print(f"警告: データが不足しているため、{output}をスキップします")
        return

    # 時刻とt座標（横方向オフセット）を抽出
    # t座標がない場合はスキップ
    default_times = [_parse_float(r.get("time", "0")) or 0.0 for r in default_ego]
    python_times = [_parse_float(r.get("time", "0")) or 0.0 for r in python_ego]

    # t座標の存在確認
    if "t" not in default_ego[0] or "t" not in python_ego[0]:
        print(f"警告: t座標が見つからないため、{output}をスキップします")
        return

    default_t = [_parse_float(r.get("t", "0")) or 0.0 for r in default_ego]
    python_t = [_parse_float(r.get("t", "0")) or 0.0 for r in python_ego]

    # プロット作成
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10), sharex=True)

    # 横方向オフセット
    ax1.plot(default_times, default_t, 'b-', linewidth=2, label='DefaultController', alpha=0.8)
    ax1.plot(python_times, python_t, 'r--', linewidth=2, label='PythonDriverController', alpha=0.8)
    ax1.axhline(y=0, color='k', linestyle=':', linewidth=1, alpha=0.5, label='レーン中心')
    ax1.set_ylabel("横方向オフセット t [m]", fontsize=12)
    ax1.set_title(title, fontsize=14, fontweight='bold')
    ax1.legend(fontsize=10, loc='best')
    ax1.grid(True, alpha=0.3)

    # 横方向オフセット差分
    min_len = min(len(default_t), len(python_t))
    t_diff = [abs(default_t[i] - python_t[i]) for i in range(min_len)]
    diff_times = default_times[:min_len]

    ax2.plot(diff_times, t_diff, 'g-', linewidth=1.5, label='横方向オフセット差分（絶対値）')
    ax2.fill_between(diff_times, 0, t_diff, alpha=0.3, color='green')
    ax2.set_xlabel("時刻 [s]", fontsize=12)
    ax2.set_ylabel("横方向オフセット差分 [m]", fontsize=12)
    ax2.set_title("横方向オフセット差分", fontsize=12, fontweight='bold')
    ax2.legend(fontsize=10, loc='best')
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output, dpi=150, bbox_inches='tight')
    plt.close(fig)

    print(f"  生成: {output}")


def generate_all_plots(
    scenario_id: str,
    default_csv: Path,
    python_csv: Path,
    plots_dir: Path
) -> Dict[str, Path]:
    """全プロットを生成

    Args:
        scenario_id: シナリオID
        default_csv: DefaultController結果CSV
        python_csv: PythonDriverController結果CSV
        plots_dir: プロット出力ディレクトリ

    Returns:
        {plot_name: plot_path}
    """
    if not MATPLOTLIB_AVAILABLE:
        print("警告: matplotlibがないため、プロット生成をスキップします")
        return {}

    plots = {}

    # XY軌跡
    xy_plot = plots_dir / f"{scenario_id}_xy_trajectory.png"
    plot_xy_trajectory_overlay(
        default_csv,
        python_csv,
        xy_plot,
        title=f"XY軌跡比較: {scenario_id}"
    )
    plots["xy_trajectory"] = xy_plot

    # 速度プロファイル
    speed_plot = plots_dir / f"{scenario_id}_speed.png"
    plot_speed_comparison(
        default_csv,
        python_csv,
        speed_plot,
        title=f"速度プロファイル比較: {scenario_id}"
    )
    plots["speed"] = speed_plot

    # 横方向オフセット
    lateral_plot = plots_dir / f"{scenario_id}_lateral_offset.png"
    plot_lateral_offset(
        default_csv,
        python_csv,
        lateral_plot,
        title=f"横方向オフセット比較: {scenario_id}"
    )
    plots["lateral_offset"] = lateral_plot

    return plots


def main():
    parser = argparse.ArgumentParser(description="コントローラー比較可視化")
    parser.add_argument("default_csv", type=Path, help="DefaultController結果CSV")
    parser.add_argument("python_csv", type=Path, help="PythonDriverController結果CSV")
    parser.add_argument("--output-dir", type=Path, default=Path("plots"), help="出力ディレクトリ")
    parser.add_argument("--scenario", type=str, default="comparison", help="シナリオID")

    args = parser.parse_args()

    if not MATPLOTLIB_AVAILABLE:
        print("エラー: matplotlibが必要です")
        print("インストール: pip install matplotlib")
        return 1

    print(f"プロット生成: {args.scenario}")
    plots = generate_all_plots(
        args.scenario,
        args.default_csv,
        args.python_csv,
        args.output_dir
    )

    print(f"\n✓ {len(plots)}個のプロットを生成しました")
    for plot_name, plot_path in plots.items():
        print(f"  - {plot_name}: {plot_path}")

    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
