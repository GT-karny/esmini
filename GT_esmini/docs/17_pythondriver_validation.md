# ControllerPythonDriver Validation Tests

## 概要 (Overview)

このドキュメントでは、`ControllerPythonDriver` (組み込みPythonコントローラー) の検証テストについて説明します。

`ControllerPythonDriver` は、esmini内でPythonスクリプトを直接実行し、車両制御を行うコントローラーです。C++側の埋め込みPythonエンジンとPython側のコントローラー実装がセットで動作するため、以下の4段階のテストレベルで検証を行います。

### テストレベル構成

| レベル | 名称 | 対象 | テスト数 |
|--------|------|------|----------|
| **L1** | ビルド・リンク検証 | PythonDriverBridge のインスタンス化 | 3 |
| **L2** | PythonDriverBridge 単体テスト | Python埋め込み、Initialize/Step/Shutdown | 12 |
| **L3** | Python側単体テスト | controller_base, frame_adapter, lights モジュール | 41 |
| **L4** | データフロー検証 | C++⇔Python間のデータマーシャリング | 10 |

**合計: 66テスト**

---

## 前提条件 (Prerequisites)

### C++テスト (L1, L2, L4)

1. **ビルド済みプロジェクト**
   ```powershell
   cmake --build "e:\Repository\GT_esmini\esmini\build" --config Release
   ```

2. **Python埋め込み環境**
   - 埋め込みPythonディレクトリ: `thirdparty/python-embed/python-3.12.10-embed-amd64/`
   - 必要なファイル (テスト実行ディレクトリにコピー):
     - `python312.dll`
     - `python3.dll`
     - `python312.zip` (標準ライブラリ)

   ```powershell
   # 自動コピー (初回のみ)
   $embedDir = "e:\Repository\GT_esmini\esmini\thirdparty\python-embed\python-3.12.10-embed-amd64"
   $testDir = "e:\Repository\GT_esmini\esmini\build\GT_esmini\test\Release"

   Copy-Item "$embedDir\python312.dll" $testDir
   Copy-Item "$embedDir\python3.dll" $testDir
   Copy-Item "$embedDir\python312.zip" $testDir
   ```

### Pythonテスト (L3)

1. **Python 3.12** (protobuf 6.x互換性のため必須)
   ```powershell
   python --version  # Python 3.12.x であることを確認
   ```

2. **仮想環境 (venv)**
   ```powershell
   cd e:\Repository\GT_esmini\esmini\DriverScript
   python -m venv venv
   .\venv\Scripts\activate
   ```

3. **依存関係インストール**
   ```powershell
   pip install -r requirements.txt
   ```

---

## テスト実行方法 (Execution)

### L1, L2, L4: C++テスト

```powershell
# テスト実行ディレクトリに移動
cd e:\Repository\GT_esmini\esmini\build\GT_esmini\test\Release

# テスト実行
.\test_PythonDriverBridge.exe
```

**期待される出力:**
```
[==========] Running 25 tests from 4 test suites.
[----------] Global test environment set-up.
[----------] 3 tests from PythonDriverBridgeLinkTest
[ RUN      ] PythonDriverBridgeLinkTest.CanInstantiate
[       OK ] PythonDriverBridgeLinkTest.CanInstantiate (0 ms)
...
[==========] 25 tests from 4 test suites ran. (XXX ms total)
[  PASSED  ] 25 tests.
```

### L3: Pythonテスト

```powershell
# DriverScript ディレクトリに移動
cd e:\Repository\GT_esmini\esmini\DriverScript

# venv有効化 (未実行の場合)
.\venv\Scripts\activate

# pytest実行
pytest tests/pythondriver/ -v
```

**期待される出力:**
```
======================== test session starts ========================
platform win32 -- Python 3.12.x, pytest-8.x.x, pluggy-1.x.x
collected 41 items

tests/pythondriver/test_controller_base.py::test_abstract_methods_enforced PASSED [ 2%]
tests/pythondriver/test_controller_base.py::test_init_receives_dict PASSED [ 4%]
...
tests/pythondriver/test_lights.py::test_clear_patch_resets_dirty PASSED [100%]

======================== 41 passed in X.XXs ========================
```

---

## テスト内容詳細 (Test Content)

### L1: ビルド・リンク検証

**ファイル:** `GT_esmini/test/unit/pythondriver/test_PythonDriverBridgeLink.cpp`

**目的:** `PythonDriverBridge` が正しくリンクされ、インスタンス化できることを確認。

| テストケース | 検証内容 |
|-------------|----------|
| `CanInstantiate` | `PythonDriverBridge` のデフォルトコンストラクタが動作 |
| `InitialStateNotInitialized` | 初期状態で `IsInitialized() == false` |
| `InitialStateNoFatalError` | 初期状態で `HasFatalError() == false` |

**重要性:** Python C APIとのリンクエラーを早期検出。

---

### L2: PythonDriverBridge 単体テスト

**ファイル:** `GT_esmini/test/unit/pythondriver/test_PythonDriverBridge.cpp`

**目的:** Python埋め込みエンジンの初期化、スクリプト読み込み、メソッド呼び出し、エラーハンドリングを検証。

#### テストパターン

各テストは以下のパターンで実行:
1. テストディレクトリに一時Pythonスクリプトを動的生成
2. `Initialize()` でスクリプトとクラスを読み込み
3. `CallStep()` でフレーム実行
4. `Shutdown()` でクリーンアップ

#### テストケース一覧

| ID | テストケース | 検証内容 |
|----|-------------|----------|
| L2-001 | `MinimalScriptInitializes` | 最小スクリプトで正常初期化 |
| L2-002 | `NonExistentScriptFails` | 存在しないスクリプトで `Initialize() == false` |
| L2-003 | `NonExistentClassFails` | 存在しないクラス名で `Initialize() == false` |
| L2-004 | `InitExceptionHandled` | `init()` で例外発生時に `Initialize() == false` |
| L2-005 | `StepReturnsValidData` | `step()` が有効なデータを返す (`valid == true`) |
| L2-006 | `StepExceptionHandled` | `step()` で例外発生時に `valid == false` |
| L2-007 | `MissingLightsKeyFails` | `lights` キー欠落時に `valid == false` |
| L2-008 | `MultipleStepCalls` | 連続 `CallStep()` が正常動作 |
| L2-009 | `ReinitializeAfterShutdown` | `Shutdown()` 後の再初期化が可能 |
| L2-010 | `StepBeforeInitializeFails` | 初期化前の `CallStep()` で `valid == false` |
| L2-011 | `DoubleShutdownSafe` | `Shutdown()` の多重呼び出しが安全 |
| L2-012 | `GetPythonHomeReturnsPath` | `GetPythonHome()` が正しいパスを返す |

**最小スクリプト例:**
```python
class MinimalController:
    def init(self, config):
        pass
    def step(self, frame_data):
        return {
            "throttle": 0.0, "brake": 0.0, "steering": 0.0,
            "gear": 1, "lights": {}, "engine_brake": 0.49,
            "adas_states": []
        }
```

---

### L3: Python側単体テスト

**ディレクトリ:** `DriverScript/tests/pythondriver/`

#### L3a: controller_base テスト

**ファイル:** `test_controller_base.py` (9テスト)

| テストケース | 検証内容 |
|-------------|----------|
| `test_abstract_methods_enforced` | 抽象メソッド未実装時に `TypeError` |
| `test_init_receives_dict` | `init()` が `dict` を受け取る |
| `test_step_receives_dict` | `step()` が `dict` を受け取る |
| `test_step_returns_dict` | `step()` が `dict` を返す |
| `test_close_is_optional` | `close()` はオプション (未実装でもOK) |
| `test_minimal_implementation_valid` | 最小実装が正常動作 |
| `test_controller_can_store_state` | コントローラーが状態を保持可能 |
| `test_init_config_not_mutated` | `init()` が config を変更しない |
| `test_step_frame_data_not_mutated` | `step()` が frame_data を変更しない |

**重要性:** Python側の基底クラス契約を保証。

#### L3b: frame_adapter テスト

**ファイル:** `test_frame_adapter.py` (11テスト)

| テストケース | 検証内容 |
|-------------|----------|
| `test_from_dict_empty` | 空dictからのパース |
| `test_from_dict_with_waypoints` | waypoints付きdictのパース |
| `test_from_dict_with_lon_profile` | lon_profile付きdictのパース |
| `test_from_dict_with_actions` | actions付きdictのパース |
| `test_to_result_minimal` | 最小出力生成 |
| `test_to_result_with_lights` | lights付き出力生成 |
| `test_to_result_with_adas_states` | adas_states付き出力生成 |
| `test_waypoint_dataclass_creation` | Waypoint dataclass生成 |
| `test_lon_profile_point_creation` | LonProfilePoint dataclass生成 |
| `test_frame_adapter_default_values` | デフォルト値の正当性 |
| `test_to_result_structure_validation` | 出力構造の妥当性検証 |

**重要性:** C++⇔Python間のデータ変換ロジックを保証。

#### L3c: lights テスト

**ファイル:** `test_lights.py` (21テスト)

| テストケース | 検証内容 |
|-------------|----------|
| `test_initial_state_all_auto` | 初期状態は全て "auto" |
| `test_set_low_beam_on` | `set_low_beam(True)` → "on" |
| `test_set_low_beam_off` | `set_low_beam(False)` → "off" |
| `test_set_high_beam_on` | `set_high_beam(True)` → "on" |
| `test_set_warning_on` | `set_warning(True)` → 両インジケーター "on" |
| `test_set_warning_off` | `set_warning(False)` → 両インジケーター "off" |
| `test_set_left_indicator` | 左インジケーター単独操作 |
| `test_set_right_indicator` | 右インジケーター単独操作 |
| `test_to_patch_dict_only_dirty` | ダーティフラグ付きのみパッチに含む |
| `test_clear_patch_resets_dirty` | `clear_patch()` でダーティフラグクリア |
| ... (全21テスト) | ライト状態管理とパッチ生成の全パターン |

**重要性:** ライト制御の状態遷移とダーティトラッキングを保証。

---

### L4: データフロー検証

**ファイル:** `GT_esmini/test/unit/pythondriver/test_DataFlow.cpp`

**目的:** C++からPythonへのデータ受け渡し、PythonからC++への戻り値が正確であることを検証。

#### テスト手法: エコーパターン

各テストは以下のパターンで実行:
1. C++が特定のデータを含む `frame_data` を生成
2. Pythonスクリプトがデータを受け取り、加工して返す
3. C++が戻り値を検証し、正しく受け渡されたことを確認

#### テストケース一覧

| ID | テストケース | 検証データ | エコー方式 |
|----|-------------|----------|----------|
| L4-001 | `WaypointsPassedCorrectly` | waypoints配列 | `len(waypoints)` → steering, `sum(x)` → throttle |
| L4-002 | `LonProfilePassedCorrectly` | lon_profile配列 | 最後の `v_target` → throttle |
| L4-003 | `ActionsPassedCorrectly` | actions flags | フラグ値を throttle/brake/steering にエンコード |
| L4-004 | `LightsPatchParsed` | lights patch | 各ライト状態を設定、戻り値で確認 |
| L4-005 | `FrameIdPassedCorrectly` | frame_id | `frame_id / 100.0` → throttle |
| L4-006 | `DtPassedCorrectly` | dt (delta time) | `dt * 10.0` → throttle |
| L4-007 | `SpeedPassedCorrectly` | speed | `speed / 20.0` → throttle |
| L4-008 | `AllFieldsTogetherCorrectly` | 全フィールド複合 | 複数データの同時検証 |
| L4-009 | `EmptyWaypointsHandled` | 空waypoints | 空配列の正常処理 |
| L4-010 | `EmptyLonProfileHandled` | 空lon_profile | 空配列の正常処理 |

**エコースクリプト例 (waypoints):**
```python
class WaypointEcho:
    def step(self, frame_data):
        wps = frame_data.get("waypoints", [])
        count = len(wps)
        x_sum = sum(wp.get("x", 0) for wp in wps) if wps else 0
        return {
            "throttle": x_sum / 100.0,  # xの合計値を返す
            "steering": float(count),    # waypoint数を返す
            "brake": 0.0, "gear": 1, "lights": {},
            "engine_brake": 0.49, "adas_states": []
        }
```

C++側で検証:
```cpp
EXPECT_NEAR(result.throttle, expected_x_sum / 100.0, 0.001);
EXPECT_NEAR(result.steering, expected_count, 0.001);
```

---

## トラブルシューティング (Troubleshooting)

### C++テスト実行時のエラー

#### 1. `python312.dll not found` (Exit code 127)

**原因:** テスト実行ディレクトリにPython DLLがコピーされていない。

**解決策:**
```powershell
$embedDir = "e:\Repository\GT_esmini\esmini\thirdparty\python-embed\python-3.12.10-embed-amd64"
$testDir = "e:\Repository\GT_esmini\esmini\build\GT_esmini\test\Release"

Copy-Item "$embedDir\python312.dll" $testDir
Copy-Item "$embedDir\python3.dll" $testDir
```

#### 2. `ModuleNotFoundError: No module named 'encodings'`

**原因:** Python標準ライブラリ (`python312.zip`) がテスト実行ディレクトリにない。

**解決策:**
```powershell
Copy-Item "$embedDir\python312.zip" $testDir
```

#### 3. テストがすべてスキップされる

**原因:** `GT_ENABLE_EMBEDDED_PYTHON` または `GT_EMBEDDED_PYTHON_HOME` が未定義。

**解決策:** `GT_esmini/test/CMakeLists.txt` を確認:
```cmake
target_compile_definitions(test_PythonDriverBridge PRIVATE
    GT_ENABLE_EMBEDDED_PYTHON
    GT_EMBEDDED_PYTHON_HOME="${GT_EMBEDDED_PYTHON_HOME}")
```

---

### Pythonテスト実行時のエラー

#### 1. `ModuleNotFoundError: No module named 'pythondriver'`

**原因:** venvが有効化されていない、または依存関係が未インストール。

**解決策:**
```powershell
cd DriverScript
.\venv\Scripts\activate
pip install -r requirements.txt
```

#### 2. Protobuf version mismatch

**原因:** Python 3.8以下を使用 (protobuf 6.x未対応)。

**解決策:** Python 3.12を使用:
```powershell
python --version  # 3.12.x であることを確認
```

venvを再作成:
```powershell
Remove-Item -Recurse -Force venv
python -m venv venv
.\venv\Scripts\activate
pip install -r requirements.txt
```

#### 3. `RuntimeError: This protobuf was compiled with version X but Y is installed`

**原因:** OSI protobufファイルのコンパイル時のバージョンと実行時のprotobufバージョンが不一致。

**解決策:** `conftest.py` で自動的にバージョンチェックを無効化しているため、通常は発生しない。発生する場合は `DriverScript/conftest.py` と `DriverScript/tests/pythondriver/conftest.py` が正しく読み込まれているか確認。

---

## テスト結果 (Test Results)

### 検証環境

- **OS:** Windows 11
- **Compiler:** Visual Studio 2022 (MSVC)
- **Python:** 3.12.10 (embedded + venv)
- **CMake:** 3.26+
- **Protobuf:** 6.33.5 (Python側)

### 実行結果サマリー

| レベル | テスト数 | 成功 | 失敗 | スキップ |
|--------|---------|------|------|---------|
| L1 (Link) | 3 | 3 | 0 | 0 |
| L2 (Bridge) | 12 | 12 | 0 | 0 |
| L3 (Python) | 41 | 41 | 0 | 0 |
| L4 (DataFlow) | 10 | 10 | 0 | 0 |
| **合計** | **66** | **66** | **0** | **0** |

**成功率: 100%**

---

## まとめ (Summary)

`ControllerPythonDriver` の検証テストは、以下の4段階で実装されています:

1. **L1 (リンク検証):** Python C APIとの正しいリンクを確認
2. **L2 (Bridge単体):** Python埋め込みエンジンの初期化・実行・エラーハンドリングを検証
3. **L3 (Python単体):** Python側モジュールの単体動作を検証
4. **L4 (データフロー):** C++⇔Python間のデータマーシャリング精度を検証

これにより、C++とPythonの統合部分を含む全コンポーネントが正しく動作することを保証しています。

### 今後の拡張

現在のテストはモジュールレベル (L1-L4) のみです。今後、実シナリオ (F01-F15) との統合テストが必要な場合は、別途テストレベルL5を追加してください。ただし、既存シナリオは内容が破綻している可能性があるため、新規シナリオでの検証を推奨します。

---

## 関連ドキュメント (Related Documents)

- [16_realdriver_validation.md](16_realdriver_validation.md) - RealDriver (UDP版) の検証テスト
- [controller_real_driver_logic.md](controller_real_driver_logic.md) - RealDriverコントローラーのロジック
- [realdriver_protocol_spec.md](realdriver_protocol_spec.md) - RealDriver UDPプロトコル仕様

---

**最終更新:** 2026-02-21
