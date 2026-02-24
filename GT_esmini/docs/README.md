# GT_esmini ドキュメント

GT_esmini (Grand Touring esmini) は、[esmini](https://github.com/esmini/esmini) (Environment Simulator Minimalistic) にライト機能・車両制御・OSI出力などを追加する拡張モジュールです。

## 入門 (Getting Started)

| ドキュメント | 内容 |
|:---|:---|
| [概要](getting-started/overview.md) | GT_esmini の機能と設計思想 |
| [ビルド・インストール](getting-started/build_install.md) | ビルド手順（Windows / Linux / macOS） |
| [基本的な使い方](getting-started/basic_usage.md) | API の基本と最初のプログラム |
| [サンプルシナリオ](getting-started/examples.md) | 実用的な使用例とコードサンプル |

## 機能ガイド (Features)

| ドキュメント | 内容 |
|:---|:---|
| [LightStateAction](features/light_state_action.md) | OpenSCENARIO でのライト制御 |
| [AutoLight](features/auto_light.md) | 自動ライト制御（ブレーキ灯・ウインカー等） |

## PythonDriverController (推奨)

| ドキュメント | 内容 |
|:---|:---|
| [マニュアル](pythondriver/manual.md) | 包括的リファレンス（XOSC設定・Python API・物理モデル） |
| [システム構造](pythondriver/system_structure.md) | アーキテクチャとフレームシーケンス |
| [車両パラメータ](pythondriver/vehicle_params.md) | RealVehicle 物理パラメータ解説 |
| [検証テスト](pythondriver/validation_tests.md) | 4段階66テストの構造と実行方法 |
| [比較テスト](pythondriver/comparison_tests.md) | DefaultController との比較テスト |
| [データ形式](pythondriver/simulation_data_format.md) | .dat バイナリ形式仕様と CSV 変換 |
| [信号機修正 + FF制御](pythondriver/signal_fix_and_ff_control.md) | 信号機再発進修正とモデルベースFF制御 |

## RealDriverController (非推奨・互換用)

| ドキュメント | 内容 |
|:---|:---|
| [概要](realdriver/README.md) | 非推奨注記とナビゲーション |
| [コントローラーロジック](realdriver/controller_logic.md) | C++ 側のアーキテクチャ |
| [プロトコル仕様](realdriver/protocol_spec.md) | UDP パケット形式 |
| [API リファレンス](realdriver/api_reference.md) | Python クライアント API |
| [モジュール構成](realdriver/modules.md) | Python モジュール一覧 |
| [サンプルスクリプト](realdriver/example_scripts.md) | 使用例 |
| [移行ガイド](realdriver/migration_guide.md) | PythonDriverController への移行手順 |
| [LogiDrivePy](realdriver/logidrivepy.md) | ステアリングコントローラー連携 |

## 外部連携 (Integration)

| ドキュメント | 内容 |
|:---|:---|
| [OSI 連携](integration/osi_integration.md) | OSI 統合・デュアル軌跡出力 |
| [外部制御](integration/external_control.md) | ExternalController / ゴースト設定 |
| [FMI 調査](integration/fmi_investigation.md) | FMU 対応状況の調査結果 |
| [TrafficCommand](integration/traffic_command.md) | OSI TrafficCommand 調査 |
| [外部トリガー](integration/external_trigger.md) | シナリオイベントの外部発火 |
| [FMU パラメータ制御](integration/fmu_parameter_control.md) | FMI パラメータマッピング |

## リファレンス (Reference)

| ドキュメント | 内容 |
|:---|:---|
| [アーキテクチャ](reference/architecture.md) | 設計思想と内部構造 |
| [C API リファレンス](reference/api_reference.md) | GT_esminiLib 関数一覧 |
| [OpenSCENARIO アクション](reference/openscenario_actions.md) | v1.2 走行関連アクション詳細 |
| [RoadManager API](reference/rm_lib_reference.md) | Python RM ライブラリ |
| [Python ArgSpec](reference/python_argspec_grammar.md) | GT_Sim Frontend 引数仕様 |
| [配布ガイド](reference/distribution_guide.md) | リリースパッケージ構成 |

## トラブルシューティング

- [トラブルシューティング](troubleshooting.md) - よくある問題と解決方法

## 推奨される読む順序

### 初めて GT_esmini を使う場合

1. [概要](getting-started/overview.md) → 2. [ビルド](getting-started/build_install.md) → 3. [基本的な使い方](getting-started/basic_usage.md) → 4. [サンプル](getting-started/examples.md)

### PythonDriverController を使いたい場合

1. [マニュアル](pythondriver/manual.md) → 2. [システム構造](pythondriver/system_structure.md) → 3. [検証テスト](pythondriver/validation_tests.md)

### ライト機能を使いたい場合

1. [LightStateAction](features/light_state_action.md) → 2. [AutoLight](features/auto_light.md) → 3. [サンプル](getting-started/examples.md)

### 内部構造を理解したい場合

1. [アーキテクチャ](reference/architecture.md) → 2. [API リファレンス](reference/api_reference.md)

## 外部ドキュメント

- [GT_OSMP_FMU インターフェース仕様](../../GT_OSMP_FMU/FMU_Interface_Specification.md)
- [DriverScript README](../../DriverScript/README.md)
- [esmini 公式ドキュメント](https://esmini.github.io/)
- [OpenSCENARIO v1.2 仕様](https://www.asam.net/standards/detail/openscenario/)
- [Open Simulation Interface (OSI)](https://github.com/OpenSimulationInterface/open-simulation-interface)

## ライセンス

GT_esmini は、Mozilla Public License 2.0 の下でライセンスされています。
詳細は [LICENSE](../../LICENSE) ファイルを参照してください。
