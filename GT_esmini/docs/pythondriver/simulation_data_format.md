# シミュレーション記録データ形式

## 概要 (Overview)

GT_Simの`--record`オプションで記録されるファイル形式と、データ解析方法を説明します。

**重要**: 記録されるファイルは**esmini独自のバイナリ.dat形式**です。拡張子が`.csv`であってもテキストCSVではありません。

## ファイル形式 (File Format)

### バイナリ.dat形式

- **形式名**: esmini DAT format
- **バージョン**: 4.3 (VERSION_MAJOR=4, VERSION_MINOR=3)
- **構造**: Length-prefixed packet形式（バイナリ）
- **パケットタイプ**: 28種類（TIMESTAMP, OBJ_ID, POSE, SPEED, など）

### ファイル構造 (Structure)

#### 1. ヘッダー部（可変長、通常157バイト前後）

| オフセット | サイズ | フィールド |
|----------|--------|-----------|
| 0 | 4 | version_major (uint32) |
| 4 | 4 | version_minor (uint32) |
| 8 | 4 | header_size (uint32) |
| 12 | var | odr_filename (length-prefixed string) |
| var | var | model_filename (length-prefixed string) |
| var | var | git_rev (length-prefixed string) |

#### 2. データパケット部（可変長、繰り返し）

各パケットの構造：

| オフセット | サイズ | フィールド |
|----------|--------|-----------|
| 0 | 4 | packet_id (uint32, enum) |
| 4 | 4 | data_size (uint32) |
| 8 | var | data (size = data_size) |

#### パケットタイプ (Packet Types)

主要なパケットタイプ（`PacketHandler.hpp`で定義）：

| ID | 名前 | 説明 | データサイズ |
|----|------|------|-------------|
| 0 | OBJ_ID | オブジェクトID | 4 bytes (int32) |
| 1 | MODEL_ID | 3Dモデル ID | 4 bytes (int32) |
| 5 | TIMESTAMP | タイムスタンプ | 8 bytes (double) |
| 7 | SPEED | 速度 | 4 bytes (float) |
| 13 | POSE | 位置・姿勢 (x,y,z,h,p,r) | 24 bytes (6×float) |
| 14 | ROAD_ID | Road ID | 4 bytes (uint32) |
| 15 | LANE_ID | Lane ID | 4 bytes (int32) |
| 21 | DT | タイムステップ | 8 bytes (double) |
| 22 | END_OF_SCENARIO | シナリオ終了 | 8 bytes (double) |

## --recordオプションの使い方 (Recording Data)

### 基本的な使用

```bash
# 基本的な使用（自動的にsim.datが生成される）
GT_Sim.exe --osc scenario.xosc --record sim.dat

# ディレクトリ指定（シナリオ名ベースのファイル名で生成）
GT_Sim.exe --osc scenario.xosc --record output/

# カスタムファイル名
GT_Sim.exe --osc scenario.xosc --record output/my_simulation.dat
```

### 拡張子の扱い

```bash
# 拡張子を省略した場合 → 自動的に.datが追加される
GT_Sim.exe --osc scenario.xosc --record output/sim
# → output/sim.dat が生成される

# 拡張子を指定した場合 → そのまま使用される
GT_Sim.exe --osc scenario.xosc --record output/sim.csv
# → output/sim.csv が生成される（ただし中身はバイナリ.dat形式）
```

**注意**: 拡張子が`.csv`でも、中身は**バイナリ.dat形式**です。テキストCSVにはなりません。

## データ変換方法 (Converting Data)

### scripts/dat.pyの使用

`scripts/dat.py`は、バイナリ.datファイルをテキストCSVに変換するツールです。

#### 基本的な変換

```bash
# テキストCSVに変換（コンソール出力）
python scripts/dat.py sim.dat

# 拡張座標情報付き（roadId, laneId, offset, t, s を含む）
python scripts/dat.py sim.dat --extended

# ファイルリファレンス情報を含める
python scripts/dat.py sim.dat --file_refs

# ファイルに保存
python scripts/dat.py sim.dat --extended > output.csv
```

#### 出力形式

**標準ラベル**:
```
time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot
```

**拡張ラベル** (--extended):
```
time, id, name, x, y, z, h, p, r, roadId, laneId, offset, t, s, speed, wheel_angle, wheel_rot
```

**ヘッダー情報** (--file_refs):
```
Version: 4.3, OpenDRIVE: path/to/file.xodr, 3DModel: path/to/file.osgb, GIT REV: commit_hash
```

### Pythonスクリプトでの読み込み

`DATFile`クラスを使用してプログラムから読み込むことができます。

```python
import sys
sys.path.insert(0, 'scripts')
from dat import DATFile

# .datファイルを読み込む
dat = DATFile('sim.dat', extended=True)

# タイムスタンプとオブジェクト情報
print(f'Timestamps: {len(dat.timestamps)}')
print(f'Objects: {len(dat.objects_timeline)}')
print(f'Final time: {dat.timestamps[-1]:.3f}s')

# 特定時刻のオブジェクト状態を取得
obj_state = dat.get_object_state_struct_at_time(obj_id=0, t=1.0)
print(f'Position at t=1.0: x={obj_state["x"]:.2f}, y={obj_state["y"]:.2f}')

# CSVファイルとして保存
dat.save_csv(extended=True)  # sim.csvが生成される
dat.close()
```

## 比較テストでの使用 (Comparison Tests)

### compare_python_vs_default.py

比較テストでは自動的に.datファイルが生成され、解析されます。

```bash
python scripts/compare_python_vs_default.py \
    --matrix GT_esmini/test/comparison_matrix.yaml \
    --scenario straight_500m \
    --output test_results/comparison_test \
    --gt-sim build/GT_esmini/Release/GT_Sim.exe
```

**生成されるファイル**:
- `test_results/comparison_test/default/straight_500m/sim.dat`
- `test_results/comparison_test/python/straight_500m/sim.dat`
- `test_results/comparison_test/default/straight_500m/python_trace.jsonl`（Python版のみ）

### データ解析の例

```bash
# Defaultのデータを解析
python -c "
import sys
sys.path.insert(0, 'scripts')
from dat import DATFile

dat = DATFile('test_results/comparison_test/default/straight_500m/sim.dat')
print(f'Frames: {len(dat.timestamps)}')
print(f'Duration: {dat.timestamps[-1]:.3f}s')
print(f'Objects: {list(dat.objects_timeline.keys())}')
dat.close()
"
```

**期待される出力**:
```
Frames: 3002
Duration: 30.010s
Objects: [0, 1]
```

## トラブルシューティング (Troubleshooting)

### wc -lの結果が意味不明

**症状**: `wc -l sim.dat`の結果が実際のフレーム数と一致しない。

```bash
$ wc -l sim.dat
218 sim.dat  # 実際には3000フレーム前後あるはず
```

**原因**: `wc -l`はテキストファイルの行数をカウントするツールです。バイナリファイルに対して実行すると、**バイナリデータ内の改行文字（0x0A）の出現回数**をカウントするため、無意味な値になります。

**解決策**: `scripts/dat.py`で正しくパースしてください。

```bash
# 正しいフレーム数の確認
python -c "
import sys
sys.path.insert(0, 'scripts')
from dat import DATFile
dat = DATFile('sim.dat')
print(f'Actual frames: {len(dat.timestamps)}')
dat.close()
"
```

### DATFileでパースエラーが発生

**症状**: `DATFile()`でファイルを開こうとすると、以下のようなエラーが発生する。

```python
struct.error: unpack requires a buffer of 4 bytes
```

**原因**: シミュレーションがクラッシュで途中終了し、最後のパケットが不完全な状態でファイルが切断されている可能性があります。

**確認方法**:

```python
from pathlib import Path

file_path = Path('sim.dat')
file_size = file_path.stat().st_size
print(f'File size: {file_size} bytes')

# 期待されるファイルサイズと比較
# 例: 30秒, 100Hz, 2オブジェクトの場合、約190KB前後
```

**対処法**:
1. シミュレーションを再実行して正常に終了させる
2. 不完全なファイルでも読める範囲でデータを抽出する（部分的なパース）

### ファイル拡張子が.csvだが、テキストCSVとして読めない

**症状**: `pandas.read_csv()`や`csv.reader()`でエラーが発生する。

**原因**: ファイル拡張子が`.csv`でも、中身は**バイナリ.dat形式**です。

**解決策**: `scripts/dat.py`でテキストCSVに変換してから読み込んでください。

```bash
# 1. バイナリ.datファイルをテキストCSVに変換
python scripts/dat.py sim.csv --extended > sim_text.csv

# 2. テキストCSVとして読み込む
python -c "
import pandas as pd
df = pd.read_csv('sim_text.csv')
print(df.head())
"
```

### 過去のsim.csvファイルは使えなくなるのか？

**回答**: いいえ、過去の`sim.csv`ファイルも`scripts/dat.py`で読めます。

ファイル形式は拡張子に依存せず、常にesmini独自のバイナリ.dat形式です。拡張子が`.csv`でも`.dat`でも、`scripts/dat.py`は同じように処理できます。

```bash
# 拡張子が.csvでも.datでも同じように読める
python scripts/dat.py old_sim.csv --extended
python scripts/dat.py new_sim.dat --extended
```

## 関連ドキュメント (Related Documents)

- [validation_tests.md](validation_tests.md) - PythonDriver検証テスト
- [PacketHandler.hpp](../../../EnvironmentSimulator/Modules/ScenarioEngine/SourceFiles/PacketHandler.hpp) - パケット定義
- [dat.py](../../../scripts/dat.py) - DATファイル解析スクリプト
- [comparison_kpis.py](../../../scripts/comparison_kpis.py) - メトリクス計算スクリプト

---

**最終更新**: 2026-02-22
