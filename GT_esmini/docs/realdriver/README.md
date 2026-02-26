# RealDriverController (非推奨)

> **注意**: RealDriverController は非推奨です。新規プロジェクトでは [PythonDriverController](../pythondriver/manual.md) を使用してください。

RealDriverController は UDP ベースの外部制御コントローラーでしたが、PythonDriverController（組み込み同期方式）に置き換えられました。このディレクトリのドキュメントは互換性維持のために残されています。

## 移行

PythonDriverController への移行手順は [移行ガイド](migration_guide.md) を参照してください。

## ドキュメント一覧

| ドキュメント | 内容 |
|:---|:---|
| [コントローラーロジック](controller_logic.md) | C++ 側のアーキテクチャと処理フロー |
| [プロトコル仕様](protocol_spec.md) | UDP パケット形式 |
| [API リファレンス](api_reference.md) | Python クライアント API |
| [モジュール構成](modules.md) | Python モジュール一覧 |
| [サンプルスクリプト](example_scripts.md) | 使用例 |
| [移行ガイド](migration_guide.md) | PythonDriverController への移行 |
| [LogiDrivePy](logidrivepy.md) | ステアリングコントローラー連携 |
