#!/usr/bin/env python3
"""コントローラー比較用メトリクス計算

PythonDriverControllerとDefaultControllerの動作を比較するための
差分メトリクスを計算。軌跡・速度・レーン遵守・ルート進捗を評価。
"""

import math
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# 既存の検証スクリプトから関数をインポート
import sys
sys.path.insert(0, str(Path(__file__).parent))

from validate_realdriver_feature_results import (
    parse_csv_rows,
    select_ego_rows,
    _parse_float,
    _parse_int,
)


def interpolate_value(
    t: float,
    times: List[float],
    values: List[float]
) -> Optional[float]:
    """線形補間で任意の時刻の値を取得"""
    if not times or not values or len(times) != len(values):
        return None

    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]

    # 二分探索で挿入位置を見つける
    for i in range(len(times) - 1):
        if times[i] <= t <= times[i + 1]:
            # 線形補間
            t0, t1 = times[i], times[i + 1]
            v0, v1 = values[i], values[i + 1]
            ratio = (t - t0) / (t1 - t0) if t1 != t0 else 0.0
            return v0 + ratio * (v1 - v0)

    return None


def align_time_series(
    rows1: List[Dict[str, str]],
    rows2: List[Dict[str, str]],
    dt: float = 0.01
) -> Tuple[List[Dict[str, float]], List[Dict[str, float]]]:
    """2つの時系列を共通時間グリッドにアライメント

    Args:
        rows1: 1つ目のCSV行リスト
        rows2: 2つ目のCSV行リスト
        dt: サンプリング間隔 [s]

    Returns:
        (aligned_rows1, aligned_rows2): アライメント後の行リスト（float値）
    """
    if not rows1 or not rows2:
        return [], []

    # 時刻を抽出
    times1 = [_parse_float(r.get("time", "0")) or 0.0 for r in rows1]
    times2 = [_parse_float(r.get("time", "0")) or 0.0 for r in rows2]

    # 共通時間範囲を計算
    t_start = max(times1[0], times2[0])
    t_end = min(times1[-1], times2[-1])

    if t_start >= t_end:
        return [], []

    # 共通時間グリッドを生成
    common_times = []
    t = t_start
    while t <= t_end:
        common_times.append(t)
        t += dt

    # 各フィールドを補間
    fields = ["x", "y", "z", "speed", "s", "t", "h", "roadId", "laneId"]

    aligned1 = []
    aligned2 = []

    for t_common in common_times:
        row1_interp = {"time": t_common}
        row2_interp = {"time": t_common}

        for field in fields:
            # rows1から値を抽出
            values1 = [_parse_float(r.get(field, "0")) or 0.0 for r in rows1]
            value1 = interpolate_value(t_common, times1, values1)
            if value1 is not None:
                row1_interp[field] = value1

            # rows2から値を抽出
            values2 = [_parse_float(r.get(field, "0")) or 0.0 for r in rows2]
            value2 = interpolate_value(t_common, times2, values2)
            if value2 is not None:
                row2_interp[field] = value2

        # roadId, laneIdは整数として最近傍値を使用
        for int_field in ["roadId", "laneId"]:
            val1 = interpolate_value(t_common, times1, [_parse_int(r.get(int_field, "0")) or 0 for r in rows1])
            val2 = interpolate_value(t_common, times2, [_parse_int(r.get(int_field, "0")) or 0 for r in rows2])
            if val1 is not None:
                row1_interp[int_field] = int(round(val1))
            if val2 is not None:
                row2_interp[int_field] = int(round(val2))

        aligned1.append(row1_interp)
        aligned2.append(row2_interp)

    return aligned1, aligned2


def compute_rmse(values1: List[float], values2: List[float]) -> float:
    """RMSEを計算"""
    if not values1 or not values2 or len(values1) != len(values2):
        return float('inf')

    squared_errors = [(v1 - v2) ** 2 for v1, v2 in zip(values1, values2)]
    return math.sqrt(sum(squared_errors) / len(squared_errors))


def compute_hausdorff_distance(
    points1: List[Tuple[float, float]],
    points2: List[Tuple[float, float]]
) -> float:
    """ハウスドルフ距離を計算"""
    if not points1 or not points2:
        return float('inf')

    def point_to_set_distance(point, point_set):
        return min(math.hypot(point[0] - p[0], point[1] - p[1]) for p in point_set)

    max_dist_1_to_2 = max(point_to_set_distance(p, points2) for p in points1)
    max_dist_2_to_1 = max(point_to_set_distance(p, points1) for p in points2)

    return max(max_dist_1_to_2, max_dist_2_to_1)


def compute_correlation(values1: List[float], values2: List[float]) -> float:
    """ピアソン相関係数を計算"""
    if not values1 or not values2 or len(values1) != len(values2):
        return 0.0

    n = len(values1)
    if n == 0:
        return 0.0

    mean1 = sum(values1) / n
    mean2 = sum(values2) / n

    numerator = sum((v1 - mean1) * (v2 - mean2) for v1, v2 in zip(values1, values2))
    denom1 = math.sqrt(sum((v1 - mean1) ** 2 for v1 in values1))
    denom2 = math.sqrt(sum((v2 - mean2) ** 2 for v2 in values2))

    if denom1 == 0.0 or denom2 == 0.0:
        return 0.0

    return numerator / (denom1 * denom2)


def compare_trajectories(
    default_csv: Path,
    python_csv: Path
) -> Dict[str, float]:
    """軌跡を比較

    Returns:
        {
            "xy_rmse": XY座標のRMSE [m],
            "xy_max_deviation": XY座標の最大偏差 [m],
            "endpoint_distance": 終点距離 [m],
            "path_length_delta": 経路長の差分 [m],
            "xy_correlation": XY軌跡の相関係数,
        }
    """
    default_rows = parse_csv_rows(default_csv)
    python_rows = parse_csv_rows(python_csv)

    default_ego = select_ego_rows(default_rows)
    python_ego = select_ego_rows(python_rows)

    if not default_ego or not python_ego:
        return {
            "xy_rmse": float('inf'),
            "xy_max_deviation": float('inf'),
            "endpoint_distance": float('inf'),
            "path_length_delta": float('inf'),
            "xy_correlation": 0.0,
        }

    # 時系列をアライメント
    aligned_default, aligned_python = align_time_series(default_ego, python_ego)

    if not aligned_default or not aligned_python:
        return {
            "xy_rmse": float('inf'),
            "xy_max_deviation": float('inf'),
            "endpoint_distance": float('inf'),
            "path_length_delta": float('inf'),
            "xy_correlation": 0.0,
        }

    # XY座標の差分を計算
    xy_errors = []
    for d_row, p_row in zip(aligned_default, aligned_python):
        dx = d_row.get("x", 0.0) - p_row.get("x", 0.0)
        dy = d_row.get("y", 0.0) - p_row.get("y", 0.0)
        xy_errors.append(math.hypot(dx, dy))

    xy_rmse = math.sqrt(sum(e ** 2 for e in xy_errors) / len(xy_errors))
    xy_max_deviation = max(xy_errors) if xy_errors else float('inf')

    # 終点距離
    endpoint_distance = math.hypot(
        aligned_default[-1].get("x", 0.0) - aligned_python[-1].get("x", 0.0),
        aligned_default[-1].get("y", 0.0) - aligned_python[-1].get("y", 0.0)
    )

    # 経路長
    def compute_path_length(rows):
        length = 0.0
        for i in range(1, len(rows)):
            dx = rows[i].get("x", 0.0) - rows[i - 1].get("x", 0.0)
            dy = rows[i].get("y", 0.0) - rows[i - 1].get("y", 0.0)
            length += math.hypot(dx, dy)
        return length

    default_path_length = compute_path_length(aligned_default)
    python_path_length = compute_path_length(aligned_python)
    path_length_delta = abs(default_path_length - python_path_length)

    # XY相関係数
    x_default = [r.get("x", 0.0) for r in aligned_default]
    y_default = [r.get("y", 0.0) for r in aligned_default]
    x_python = [r.get("x", 0.0) for r in aligned_python]
    y_python = [r.get("y", 0.0) for r in aligned_python]

    xy_correlation = (compute_correlation(x_default, x_python) + compute_correlation(y_default, y_python)) / 2.0

    return {
        "xy_rmse": xy_rmse,
        "xy_max_deviation": xy_max_deviation,
        "endpoint_distance": endpoint_distance,
        "path_length_delta": path_length_delta,
        "xy_correlation": xy_correlation,
    }


def compare_speed_profiles(
    default_csv: Path,
    python_csv: Path
) -> Dict[str, float]:
    """速度プロファイルを比較

    Returns:
        {
            "speed_rmse": 速度のRMSE [m/s],
            "speed_max_deviation": 速度の最大偏差 [m/s],
            "speed_end_delta": 終端速度の差分 [m/s],
            "acceleration_correlation": 加速度の相関係数,
        }
    """
    default_rows = parse_csv_rows(default_csv)
    python_rows = parse_csv_rows(python_csv)

    default_ego = select_ego_rows(default_rows)
    python_ego = select_ego_rows(python_rows)

    if not default_ego or not python_ego:
        return {
            "speed_rmse": float('inf'),
            "speed_max_deviation": float('inf'),
            "speed_end_delta": float('inf'),
            "acceleration_correlation": 0.0,
        }

    # 時系列をアライメント
    aligned_default, aligned_python = align_time_series(default_ego, python_ego)

    if not aligned_default or not aligned_python:
        return {
            "speed_rmse": float('inf'),
            "speed_max_deviation": float('inf'),
            "speed_end_delta": float('inf'),
            "acceleration_correlation": 0.0,
        }

    # 速度を抽出
    speeds_default = [r.get("speed", 0.0) for r in aligned_default]
    speeds_python = [r.get("speed", 0.0) for r in aligned_python]

    # 速度RMSE
    speed_rmse = compute_rmse(speeds_default, speeds_python)

    # 速度最大偏差
    speed_diffs = [abs(s1 - s2) for s1, s2 in zip(speeds_default, speeds_python)]
    speed_max_deviation = max(speed_diffs) if speed_diffs else float('inf')

    # 終端速度差分
    speed_end_delta = abs(speeds_default[-1] - speeds_python[-1])

    # 加速度を計算（数値微分）
    def compute_accelerations(speeds, dt=0.01):
        accels = []
        for i in range(1, len(speeds)):
            accel = (speeds[i] - speeds[i - 1]) / dt
            accels.append(accel)
        return accels

    accels_default = compute_accelerations(speeds_default)
    accels_python = compute_accelerations(speeds_python)

    # 加速度相関係数
    acceleration_correlation = compute_correlation(accels_default, accels_python)

    return {
        "speed_rmse": speed_rmse,
        "speed_max_deviation": speed_max_deviation,
        "speed_end_delta": speed_end_delta,
        "acceleration_correlation": acceleration_correlation,
    }


def compare_lane_keeping(
    default_csv: Path,
    python_csv: Path
) -> Dict[str, float]:
    """レーン遵守を比較

    Returns:
        {
            "t_offset_rmse": 横方向オフセットのRMSE [m],
            "t_offset_max": 横方向オフセットの最大値 [m],
            "lane_id_match_ratio": Lane IDの一致率,
            "lane_change_count_delta": 車線変更回数の差分,
        }
    """
    default_rows = parse_csv_rows(default_csv)
    python_rows = parse_csv_rows(python_csv)

    default_ego = select_ego_rows(default_rows)
    python_ego = select_ego_rows(python_rows)

    if not default_ego or not python_ego:
        return {
            "t_offset_rmse": float('inf'),
            "t_offset_max": float('inf'),
            "lane_id_match_ratio": 0.0,
            "lane_change_count_delta": float('inf'),
        }

    # 時系列をアライメント
    aligned_default, aligned_python = align_time_series(default_ego, python_ego)

    if not aligned_default or not aligned_python:
        return {
            "t_offset_rmse": float('inf'),
            "t_offset_max": float('inf'),
            "lane_id_match_ratio": 0.0,
            "lane_change_count_delta": float('inf'),
        }

    # 横方向オフセット（t座標）
    t_default = [r.get("t", 0.0) for r in aligned_default]
    t_python = [r.get("t", 0.0) for r in aligned_python]

    t_offset_rmse = compute_rmse(t_default, t_python)
    t_offset_diffs = [abs(t1 - t2) for t1, t2 in zip(t_default, t_python)]
    t_offset_max = max(t_offset_diffs) if t_offset_diffs else float('inf')

    # Lane ID一致率
    lane_ids_default = [int(r.get("laneId", 0)) for r in aligned_default]
    lane_ids_python = [int(r.get("laneId", 0)) for r in aligned_python]

    lane_id_matches = sum(1 for l1, l2 in zip(lane_ids_default, lane_ids_python) if l1 == l2)
    lane_id_match_ratio = lane_id_matches / len(lane_ids_default) if lane_ids_default else 0.0

    # 車線変更回数
    def count_lane_changes(lane_ids):
        count = 0
        for i in range(1, len(lane_ids)):
            if lane_ids[i] != lane_ids[i - 1]:
                count += 1
        return count

    lane_change_count_default = count_lane_changes(lane_ids_default)
    lane_change_count_python = count_lane_changes(lane_ids_python)
    lane_change_count_delta = abs(lane_change_count_default - lane_change_count_python)

    return {
        "t_offset_rmse": t_offset_rmse,
        "t_offset_max": t_offset_max,
        "lane_id_match_ratio": lane_id_match_ratio,
        "lane_change_count_delta": lane_change_count_delta,
    }


def compare_route_progress(
    default_csv: Path,
    python_csv: Path
) -> Dict[str, float]:
    """ルート進捗を比較

    Returns:
        {
            "s_end_delta": 終端s座標の差分 [m],
            "s_progress_delta": s進捗の差分 [m],
            "road_id_match_ratio": Road IDの一致率,
            "route_completion_match": ルート完走一致,
        }
    """
    default_rows = parse_csv_rows(default_csv)
    python_rows = parse_csv_rows(python_csv)

    default_ego = select_ego_rows(default_rows)
    python_ego = select_ego_rows(python_rows)

    if not default_ego or not python_ego:
        return {
            "s_end_delta": float('inf'),
            "s_progress_delta": float('inf'),
            "road_id_match_ratio": 0.0,
            "route_completion_match": False,
        }

    # 時系列をアライメント
    aligned_default, aligned_python = align_time_series(default_ego, python_ego)

    if not aligned_default or not aligned_python:
        return {
            "s_end_delta": float('inf'),
            "s_progress_delta": float('inf'),
            "road_id_match_ratio": 0.0,
            "route_completion_match": False,
        }

    # s座標
    s_default = [r.get("s", 0.0) for r in aligned_default]
    s_python = [r.get("s", 0.0) for r in aligned_python]

    # 終端s座標差分
    s_end_delta = abs(s_default[-1] - s_python[-1])

    # s進捗差分（総移動距離）
    s_progress_default = max(s_default) - min(s_default)
    s_progress_python = max(s_python) - min(s_python)
    s_progress_delta = abs(s_progress_default - s_progress_python)

    # Road ID一致率
    road_ids_default = [int(r.get("roadId", 0)) for r in aligned_default]
    road_ids_python = [int(r.get("roadId", 0)) for r in aligned_python]

    road_id_matches = sum(1 for r1, r2 in zip(road_ids_default, road_ids_python) if r1 == r2)
    road_id_match_ratio = road_id_matches / len(road_ids_default) if road_ids_default else 0.0

    # ルート完走一致（最終Road IDとLane IDが一致）
    route_completion_match = (
        road_ids_default[-1] == road_ids_python[-1] and
        aligned_default[-1].get("laneId", 0) == aligned_python[-1].get("laneId", 0)
    )

    return {
        "s_end_delta": s_end_delta,
        "s_progress_delta": s_progress_delta,
        "road_id_match_ratio": road_id_match_ratio,
        "route_completion_match": route_completion_match,
    }


def compare_all_metrics(
    default_csv: Path,
    python_csv: Path
) -> Dict[str, Dict[str, float]]:
    """全メトリクスを比較

    Returns:
        {
            "trajectory": {...},
            "speed": {...},
            "lane_keeping": {...},
            "route": {...},
        }
    """
    return {
        "trajectory": compare_trajectories(default_csv, python_csv),
        "speed": compare_speed_profiles(default_csv, python_csv),
        "lane_keeping": compare_lane_keeping(default_csv, python_csv),
        "route": compare_route_progress(default_csv, python_csv),
    }


if __name__ == "__main__":
    # テスト用
    import argparse

    parser = argparse.ArgumentParser(description="コントローラー比較メトリクス計算")
    parser.add_argument("default_csv", type=Path, help="DefaultController結果CSV")
    parser.add_argument("python_csv", type=Path, help="PythonDriverController結果CSV")
    args = parser.parse_args()

    metrics = compare_all_metrics(args.default_csv, args.python_csv)

    print("=== 軌跡メトリクス ===")
    for key, value in metrics["trajectory"].items():
        print(f"  {key}: {value:.4f}")

    print("\n=== 速度メトリクス ===")
    for key, value in metrics["speed"].items():
        print(f"  {key}: {value:.4f}")

    print("\n=== レーン遵守メトリクス ===")
    for key, value in metrics["lane_keeping"].items():
        print(f"  {key}: {value:.4f}")

    print("\n=== ルート進捗メトリクス ===")
    for key, value in metrics["route"].items():
        print(f"  {key}: {value}")
