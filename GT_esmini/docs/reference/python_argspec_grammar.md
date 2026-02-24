# Python Argspec Grammar for GT_Sim Frontend

`DriverScript/gt_sim_frontend_pyside6.py` は、各 Python サンプルが `--dump-argspec` で返す JSON を読み込み、引数入力UIを自動生成します。  
このドキュメントは、その出力フォーマット（文法）を定義します。

## 1. 必須仕様

各サンプルスクリプトは次を満たしてください。

1. `argparse` で通常引数を定義する
2. `--dump-argspec`（`store_true`）を追加する
3. `--dump-argspec` が指定された場合、**引数メタ情報の JSON 配列のみ**を標準出力へ出して終了する

## 2. JSON Grammar

トップレベルは `list[ArgSpec]`。

```json
[
  {
    "name": "--target_speed",
    "type": "float",
    "default": 10.0,
    "required": false,
    "help": "Default target speed in m/s",
    "description": "巡航目標速度[m/s]です。"
  }
]
```

### `ArgSpec` フィールド

- `name` (string, required)
  - `--` で始まる long option 名（例: `--target_speed`）
- `type` (string, required)
  - `str` / `int` / `float` / `bool`
- `default` (any, optional)
  - デフォルト値。未設定時は `null`
- `required` (bool, required)
  - 必須引数かどうか
- `help` (string, optional)
  - 引数の短い説明
- `description` (string, optional)
  - UIで入力欄の下に表示する説明文
- `choices` (array, optional)
  - 選択肢（指定時はコンボボックスとして描画）
- `ui` (string, optional)
  - UIヒント。現状は `path` を使用
- `path_kind` (string, optional)
  - `ui=path` の補助情報。`file` / `dir`

## 3. Frontend 側の扱い

- `description` があれば説明文として表示
- `description` が無ければ `help` を説明文として表示
- `choices` があれば `QComboBox`
- `type` に応じて `QLineEdit / QSpinBox / QDoubleSpinBox / QCheckBox` を生成

## 4. 共通実装

推奨は `DriverScript/argspec_utils.py` の利用です。

- `add_dump_argspec_option(parser)`
- `maybe_dump_argspec(args, parser, descriptions=..., ui_hints=...)`

`ui_hints` 例:

```python
ui_hints={
    "--xodr_path": {"ui": "path", "path_kind": "file"},
    "--lib_path": {"ui": "path", "path_kind": "file"},
}
```

## 5. 運用ルール

- JSONは `ensure_ascii=True` で出力（Windows環境の文字コード差対策）
- `--dump-argspec` 実行時は副作用を持つ初期化（UDP接続、DLLロード、GUI起動）を行わない
- 引数名を変更した場合は、既存GUI設定値との互換性に注意する

## 6. 共通引数名ルール（統一）

複数サンプルで同じ意味の値は、引数名を統一してください。

- 通信系
  - `--ip`
  - `--port`
  - `--osi_port`
  - `--id`
  - `--target_speed_port`
- ライブラリ/マップ系
  - `--xodr_path`
  - `--lib_path` (`esminiRMLib.dll`)
  - `--gt_lib_path` (`GT_esminiLib.dll`)

`gt_sim_frontend_pyside6.py` 側では上記の一部を「自動入力 + 非表示」で扱います。  
同じ意味で別名（例: `--speed_port`, `--map_path`）を導入しないでください。
