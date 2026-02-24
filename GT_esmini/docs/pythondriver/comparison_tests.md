# PythonDriverController vs DefaultController 比較テスト

## 概要

このドキュメントでは、PythonDriverControllerとDefaultControllerの動作を比較するテストの実行方法を説明します。

## 自動化スクリプト

### 基本的な使い方

```bash
# venv環境のPythonを使用
cd e:\Repository\GT_esmini\esmini
DriverScript\.venv\Scripts\python.exe run_comparison_test.py
```

### オプション

```bash
# 特定のシナリオのみ実行
DriverScript\.venv\Scripts\python.exe run_comparison_test.py --scenario straight_500m

# 詳細ログ出力
DriverScript\.venv\Scripts\python.exe run_comparison_test.py --verbose

# 出力先を指定
DriverScript\.venv\Scripts\python.exe run_comparison_test.py --output test_results/my_test

# ヘルプを表示
DriverScript\.venv\Scripts\python.exe run_comparison_test.py --help
```

### 実行フロー

スクリプトは以下のステップを自動的に実行します：

1. **XOSCバリアント生成**: Python用のシナリオファイルを生成
2. **DefaultController実行**: 標準コントローラーでシミュレーション実行
3. **PythonDriverController実行**: Pythonコントローラーでシミュレーション実行
4. **DAT→CSV変換**: バイナリ.datファイルをCSV形式に変換
5. **メトリクス計算**: 軌跡、速度、レーン遵守、ルート進捗を比較
6. **閾値評価**: 合格/不合格を判定
7. **JSONサマリー生成**: `comparison_summary.json`を生成
8. **HTMLレポート生成**: `comparison_report.html`を生成

### 出力ファイル

```
test_results/comparison_auto/
├── comparison_report.html      # HTMLレポート
├── comparison_summary.json     # JSONサマリー
├── default/
│   └── {scenario}/
│       ├── sim.dat             # バイナリ記録
│       ├── sim.csv             # CSV形式（変換後）
│       ├── stdout.txt          # 標準出力
│       └── stderr.txt          # 標準エラー
└── python/
    └── {scenario}/
        ├── sim.dat
        ├── sim.csv
        ├── python_trace.jsonl  # Pythonトレース
        ├── cpp_to_py_trace.jsonl
        ├── py_to_cpp_trace.jsonl
        ├── stdout.txt
        └── stderr.txt
```

## 手動実行（高度な使用法）

### ステップ1: シミュレーション実行

```bash
# DefaultController
build\GT_esmini\Release\GT_Sim.exe --osc resources/xosc/straight_500m.xosc --headless --record default_sim.dat

# PythonDriverController
build\GT_esmini\Release\GT_Sim.exe --osc resources/xosc/straight_500m_python.xosc --headless --record python_sim.dat
```

### ステップ2: DAT→CSV変換

```python
from scripts.dat import DATFile

# DefaultController結果を変換
dat = DATFile('default_sim.dat', extended=True)
dat.save_csv(extended=True, include_file_refs=True)
dat.close()

# PythonDriverController結果を変換
dat = DATFile('python_sim.dat', extended=True)
dat.save_csv(extended=True, include_file_refs=True)
dat.close()
```

### ステップ3: メトリクス計算

```python
from pathlib import Path
from scripts.comparison_kpis import compare_all_metrics

default_csv = Path('default_sim.csv')
python_csv = Path('python_sim.csv')

metrics = compare_all_metrics(default_csv, python_csv)
print(metrics)
```

## テストシナリオ

テストシナリオは`GT_esmini/test/comparison_matrix.yaml`で定義されています：

| シナリオID | 説明 | 検証項目 |
|-----------|------|---------|
| `straight_500m` | 直進路での縦方向制御 | 軌跡精度、速度プロファイル |
| `lane_change_simple` | 車線変更中の横方向制御 | レーン遵守、車線変更スムーズさ |
| `speed_profile` | 速度プロファイル追従 | 加減速精度 |
| `lane_change` | 標準車線変更 | 車線変更タイミング |

## 合格基準

合格基準は`GT_esmini/test/comparison_thresholds.yaml`で定義されています。

### straight_500mシナリオの閾値

| メトリクス | 閾値 | 説明 |
|-----------|------|------|
| 軌跡RMSE | < 0.3m | XY座標のRMSE |
| 終点距離 | < 1.0m | 最終位置の差 |
| 速度RMSE | < 0.2m/s | 速度のRMSE |
| Lane ID一致率 | > 99% | レーンIDの一致率 |
| 終端s座標差分 | < 1.0m | ルート進捗の差 |

## トラブルシューティング

### 問題: メトリクスが「N/A」または「inf」になる

**原因**: .datファイルがCSVに変換されていない

**解決策**:
- `run_comparison_test.py`スクリプトを使用（自動変換機能付き）
- または手動でDAT→CSV変換を実行

### 問題: PythonDriverControllerの軌跡が大きくずれる

**症状**: xy_rmse > 30m, endpoint_distance > 100m

**確認事項**:
1. CSVファイルの最後の行を確認
   ```bash
   tail -5 test_results/.../python/.../sim.csv
   ```
2. `h`（ヘディング）の値を確認
   - `h ≈ 6.283` (2π): 360度回転している
   - `x座標 >> s座標`: 実際の位置が期待値を大幅に超過

**対処**:
- XOSCシナリオファイルの設定を確認
- シミュレーション期間が適切か確認

### 問題: GT_Simが見つからない

**エラー**: `GT_Sim not found`

**解決策**:
```bash
# ビルドを実行
cmake --build "e:\Repository\GT_esmini\esmini\build" --config Release

# または--gt-simオプションでパスを指定
DriverScript\.venv\Scripts\python.exe run_comparison_test.py --gt-sim path/to/GT_Sim.exe
```

## 参考情報

### Python環境

**重要**: 必ず`DriverScript/.venv`の仮想環境を使用してください。

```bash
# 仮想環境のPythonを確認
DriverScript\.venv\Scripts\python.exe --version
# Python 3.12.10
```

### 関連ファイル

- `scripts/compare_python_vs_default.py`: 既存の比較スクリプト（.dat変換機能追加済み）
- `scripts/comparison_kpis.py`: メトリクス計算ロジック
- `scripts/dat.py`: .datファイルパーサー
- `scripts/comparison_report_template.html`: HTMLテンプレート
- `GT_esmini/test/comparison_matrix.yaml`: テストシナリオ定義
- `GT_esmini/test/comparison_thresholds.yaml`: 合格基準定義

### 既知の問題

#### PythonDriverControllerの360度回転

**現象**: PythonDriverControllerが道路を1周（360度）回転し、x座標が約134m超過

**影響**: 軌跡メトリクスと速度メトリクスが大幅に不合格

**状況**: 調査中

## 更新履歴

- 2026-02-22: .dat→CSV自動変換機能を追加
- 2026-02-22: `run_comparison_test.py`自動化スクリプトを作成
- 2026-02-22: HTMLレポート生成機能を実装
