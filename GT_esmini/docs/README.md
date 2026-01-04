# GT_esmini ドキュメント

GT_esmini (Grand Touring esmini) は、[esmini](https://github.com/esmini/esmini) (Environment Simulator Minimalistic) にライト機能を追加する拡張モジュールです。

## ドキュメント一覧

このディレクトリには、GT_esminiの使用方法、機能、アーキテクチャに関する包括的なドキュメントが含まれています。

### 入門ガイド

1. **[概要](01_overview.md)** - GT_esminiとは何か、何ができるか
2. **[ビルド・インストール](02_build_install.md)** - ビルド手順とインストール方法
3. **[基本的な使い方](03_basic_usage.md)** - 最初のステップとAPI使用方法

### 機能ガイド

4. **[LightStateAction機能](04_light_state_action.md)** - OpenSCENARIOでのライト制御
5. **[AutoLight機能](05_auto_light.md)** - 自動ライト制御の詳細
6. **[OSI連携](06_osi_integration.md)** - Open Simulation Interfaceとの統合

### 実践ガイド

7. **[サンプルシナリオ](07_examples.md)** - 実用的な使用例とコードサンプル

### リファレンス

8. **[アーキテクチャ](08_architecture.md)** - 設計思想と内部構造
9. **[トラブルシューティング](09_troubleshooting.md)** - よくある問題と解決方法
10. **[APIリファレンス](10_api_reference.md)** - 関数とデータ型の完全なリファレンス
14. **[外部制御](14_external_control.md)** - FMI/外部アプリによる制御設定とゴースト機能

## 推奨される読む順序

### 初めてGT_esminiを使う場合

1. [概要](01_overview.md) - GT_esminiの全体像を理解
2. [ビルド・インストール](02_build_install.md) - 環境構築
3. [基本的な使い方](03_basic_usage.md) - 最初のプログラムを作成
4. [サンプルシナリオ](07_examples.md) - 実例から学ぶ

### LightStateActionを使いたい場合

1. [LightStateAction機能](04_light_state_action.md) - 機能の詳細
2. [サンプルシナリオ](07_examples.md) - 実際のXOSCファイル例
3. [APIリファレンス](10_api_reference.md) - 詳細な仕様

### AutoLight機能を使いたい場合

1. [AutoLight機能](05_auto_light.md) - 自動制御の仕組み
2. [基本的な使い方](03_basic_usage.md) - 有効化方法
3. [サンプルシナリオ](07_examples.md) - 実装例

### OSI出力が必要な場合

1. [OSI連携](06_osi_integration.md) - OSI統合の詳細
2. [サンプルシナリオ](07_examples.md) - OSI使用例

### 内部構造を理解したい場合

1. [アーキテクチャ](08_architecture.md) - 設計思想とコンポーネント
2. [APIリファレンス](10_api_reference.md) - 詳細な実装仕様

## クイックリンク

- [GT_esmini GitHub リポジトリ](https://github.com/esmini/esmini) (GT_esminiディレクトリ)
- [esmini 公式ドキュメント](https://esmini.github.io/)
- [OpenSCENARIO v1.2 仕様](https://www.asam.net/standards/detail/openscenario/)
- [Open Simulation Interface (OSI)](https://github.com/OpenSimulationInterface/open-simulation-interface)

## 貢献

ドキュメントの改善提案やバグ報告は、GitHubのIssueまたはPull Requestでお願いします。

## ライセンス

GT_esminiは、Mozilla Public License 2.0の下でライセンスされています。
詳細は[LICENSE](../../LICENSE)ファイルを参照してください。
