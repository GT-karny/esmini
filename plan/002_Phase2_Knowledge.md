# Phase 2 開発における技術的課題と対策 (ナレッジベース)

本ドキュメントは、Phase 2 (`LightStateAction` 実装) において発生したビルドエラーやテスト実装上の課題、およびそれらの解決策を記録したものです。今後の開発において同様の問題が発生した際の参照用として利用します。

## 1. ビルド関連: 依存関係とインクルードパス (CMake)

### 事象
ユニットテストのコンパイル時に `fatal error: fmt/format.h: No such file or directory` というエラーが発生した。`GT_esmini` 本体（DLL）のビルドは成功しているにもかかわらず、テストターゲットのみビルドに失敗した。

### 原因
`esmini` は独自のマクロやパス変数を多用してビルド環境を構築している。特にログ出力で使用される `logger.hpp` は `fmt` ライブラリに依存しており、この `fmt` へのパス (`EXTERNALS_FMT_INCLUDES`) がテスト用の `CMakeLists.txt` に正しく設定されていなかった。
また、Phase 1（スタブ実装）では依存関係が少なかったため問題が顕在化しなかったが、Phase 2 で `logger.hpp` や `pugixml` を本格的に使用し始めたことで依存関係が増加した。

### 対策
`esmini` プロジェクト標準の `unittest` マクロを使用する。
`support/cmake/common/unittest.cmake` で定義されているこのマクロは、`gtest` のセットアップだけでなく、`fmt`, `pugixml`, `CommonMini`, `ScenarioEngine` などの標準的な依存関係へのインクルードパスを自動的に解決してくれる。

**推奨される `test/CMakeLists.txt` の構成:**

```cmake
# esmini標準のunittestマクロをインクルード
include(${CMAKE_SOURCE_DIR}/support/cmake/common/unittest.cmake)

set(UNIT_TEST_SOURCES ...)
set(UNIT_TEST_TARGET test_TargetName)

# マクロを使用してターゲットを作成・設定
unittest(${UNIT_TEST_TARGET} "${UNIT_TEST_SOURCES}" 
    ${TARGET_STATIC}
    ScenarioEngine
    CommonMini
    # その他の依存ライブラリ
)

# 個別の追加インクルードパス
target_include_directories(${UNIT_TEST_TARGET} PRIVATE ...)
```

---

## 2. ユニットテスト関連: Protected メンバへのアクセス

### 事象
`GT_ScenarioReader::ParseLightStateAction` メソッドをテストしようとした際、コンパイルエラー（アクセス不可）が発生した。

### 原因
該当メソッドが `protected` として宣言されており、外部のテストクラスから直接呼び出すことができないため。C++ のユニットテストでは一般的な課題。

### 対策
プロダクションコードのカプセル化を壊す（`public` に変更する）ことなくテストするために、テストファイル内で「テスト用サブクラス」を作成する手法を採用する。

**実装パターン:**

```cpp
// テストファイル内でのみ定義
class TestableGT_ScenarioReader : public GT_ScenarioReader {
public:
    // 親クラスのコンストラクタを継承
    using GT_ScenarioReader::GT_ScenarioReader;
    
    // protectedメソッドをpublicとして公開
    using GT_ScenarioReader::ParseLightStateAction;
};

TEST_F(TestClass, TestCase) {
    // ラッパークラスをインスタンス化してテスト
    TestableGT_ScenarioReader reader(...);
    reader.ParseLightStateAction(...); // アクセス可能
}
```

---

## 3. Visual Studio / MSBuild 関連: 抽象クラスのエラー診断

### 事象
ビルド時に `error C2259: '...': cannot instantiate abstract class` が発生したが、エラーメッセージが日本語で文字化けしていたため、詳細な原因（どの純粋仮想関数が未実装か）の特定に難航した。

### 対策
1.  **英語出力の強制**: 環境変数 `VSLANG=1033` を設定して MSBuild を実行することで、エラーメッセージを英語化し、正確な関数名を特定しやすくする。
    *   PowerShell: `$env:VSLANG=1033; msbuild ...`
    *   CMD: `set VSLANG=1033 && msbuild ...`
2.  **段階的なテスト**: 最小限のテストファイル (`simple_test.cpp`) を作成し、機能を一つずつ追加（`include` 追加 → インスタンス化 → メソッド呼び出し）しながらビルドすることで、問題の切り分けを行う。

---

## まとめ
*   **CMake**: 自前でパスを書く前に、既存のヘルパーマクロ（`unittest`など）がないか確認する。
*   **C++テスト**: `protected` メンバのテストには継承と `using` 宣言を活用する。
*   **デバッグ**: ビルドエラーが複雑な場合、最小構成での再現とコンパイラ出力の英語化が有効。
