# 配布用パッケージ構成ガイド

`GT_Sim.exe` をRelease版として配布する推奨フォルダ構成を以下に示します。

## 推奨ディレクトリ構成

```
GT_esmini_Release/
├── bin/
│   ├── GT_Sim.exe            # 実行ファイル
│   ├── GT_esminiLib.dll      # 推奨: 核となるDLL (OSI機能等を含む)
│   └── (esminiLib.dll)       # ※依存関係によっては必要だが、GT_esminiLibが静的リンクしている場合は不要
├── resources/                # 必須: esminiのリソースフォルダ
│   ├── fonts/
│   ├── models/
│   ├── objects/
│   └── ... (esmini/resourcesの中身を全てコピー)
├── scenarios/                # サンプルシナリオ
│   ├── demo.xosc
│   └── ...
├── LICENSE                   # ライセンスファイル
└── README.md                 # 説明書
```

## ポイント

1.  **DLLの依存関係**:
    - `GT_Sim.exe` は非常に軽量（約16KB）であり、実体は `GT_esminiLib.dll`（約16MB）にあります。必ず同じフォルダに配置してください。
    - OSG（OpenSceneGraph）はビルドログにより `OSG_LIBRARY_STATIC` が定義されているため、静的リンクされています。別途OSGのDLLやプラグインフォルダを配布する必要はありません。

2.  **Resourcesフォルダ**:
    - esminiは実行時に `../resources` または `./resources` を探索して3Dモデルやフォントを読み込みます。
    - `bin` フォルダと同階層（`../resources`）に `resources` フォルダを置くのが標準的です。

3.  **実行方法**:
    - ユーザーは `bin/GT_Sim.exe` を実行します。
    - 引数でシナリオパスを指定する場合: `GT_Sim.exe --osc ../scenarios/demo.xosc`

## 作成手順（例）

1. 任意の作業フォルダ（例: `package`）を作成
2. `e:\Repository\GT_esmini\esmini\build\GT_esmini\Release` から `GT_Sim.exe` と `GT_esminiLib.dll` を `package/bin` にコピー
3. `e:\Repository\GT_esmini\esmini\resources` フォルダを `package/resources` としてコピー
4. 配布したいXOSCファイルを `package/scenarios` にコピー
5. 全体をZIP圧縮


## ライセンス準拠に関する注意点

esmini (MPL 2.0) および依存ライブラリ (OSGPL等) を含むバイナリを配布する場合、以下の点に注意が必要です。

1.  **ソースコードの提供義務 (MPL 2.0)**:
    - esminiのソースコード（および `GT_OSIReporter.cpp` などMPL適用ファイルへの変更分）を提供可能にする必要があります。
    - **実用的な対応**: READMEに「本ソフトウェアはesmini (MPL 2.0) を使用しています。ソースコードは [リポジトリURL] から入手可能です」と明記するのが一般的です。

2.  **ライセンスファイルの同梱**:
    - `LICENSE` ファイル (MPL 2.0) を同梱してください。
    - OSG (OpenSceneGraph) は静的リンクされていますが、OSGPL (LGPL + 例外条項) によりバイナリ配布は許可されています。念のため `3rd_party_terms_and_licenses` フォルダも同梱するか、READMEで言及することを推奨します。

3.  **無保証の免責**:
    - ユーザーに対して「現状有姿 (As-Is)」での提供であり、保証がないことを明記してください。

