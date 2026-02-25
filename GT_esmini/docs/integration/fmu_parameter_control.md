# OSMP FMU パラメータ制御マニュアル

## 概要

`GT_OSMP_FMU` は、外部のFMIシミュレーション環境（Simulink, CarMaker, その他 FMI 2.0 対応ツール）から、実行中の OpenSCENARIO のパラメータを直接制御する機能を提供します。

FMUの入力変数（`Real_00` ～ `Real_49`）に値を書き込むことで、対応する OpenSCENARIO パラメータの値を動的に更新できます。

## 仕組み

FMU は標準的な `Real` 型の入力変数を 50個公開しています。これらの変数は汎用的であり、特定の意味を持ちません。
ユーザーは **マッピングファイル** を用意することで、どの入力変数がどのパラメータに対応するかを定義します。

1.  FMU 入力変数 `Real_xx` に値を設定 (`SetReal`)
2.  FMU がマッピングファイルを参照し、対応するパラメータ名を特定
3.  esmini のパラメータ設定 API (`SE_SetParameterDouble`) を呼び出し、値を反映

## マッピングファイルの設定

制御を行うには、シミュレーション実行ディレクトリ（通常は FMU の配置場所や実行カレントディレクトリ）に `fmu_parameters.txt` という名前のテキストファイルを作成してください。

### フォーマット

各行に `ValueReference, ParameterName` の形式で記述します。

*   **ValueReference**: FMU の変数ID。`Real_00` は `1`、`Real_01` は `2` ... `Real_49` は `50` です。 (`modelDescription.xml` 参照)
*   **ParameterName**: OpenSCENARIO ファイル（.xosc）内の `ParameterDeclarations` で定義されたパラメータ名。

### 記述例

```txt
1, TargetSpeed
2, FrictionCoefficient
3, EgoVehicleMass
```

この例では：
*   FMU の `Real_00` (VR=1) への入力が、シナリオパラメータ `TargetSpeed` に反映されます。
*   FMU の `Real_01` (VR=2) への入力が、`FrictionCoefficient` に反映されます。
*   FMU の `Real_02` (VR=3) への入力が、`EgoVehicleMass` に反映されます。

## 使用手順

1.  **シナリオの準備**: `.xosc` ファイルを開き、制御したい値を `ParameterDeclaration` で定義します。
    ```xml
    <ParameterDeclarations>
        <ParameterDeclaration name="TargetSpeed" parameterType="double" value="10.0"/>
    </ParameterDeclarations>
    ```
    シナリオ内では `$TargetSpeed` としてこの値を参照します。

2.  **マッピングの作成**: `fmu_parameters.txt` を作成し、割り当てを記述します。
    ```txt
    1, TargetSpeed
    ```

3.  **FMUの実行**: シミュレーション環境で `GT_OSMP_FMU.fmu` をロードします。
    `Real_00` ポートに入力信号を接続します。

4.  **動作確認**: シミュレーションを実行すると、`Real_00` の値に応じてシナリオ内の `TargetSpeed` が変化し、それに依存する挙動（例：車の速度）が変わることを確認します。

## 注意事項

*   `ValueReference` は `1` から始まります（`Real_00` = 1）。
*   パラメータ名は OpenSCENARIO 側と完全に一致させる必要があります（大文字小文字を区別）。
*   マッピングファイルが見つからない場合やフォーマットが不正な場合、その設定は無視されますがシミュレーション自体は続行します。詳細なログは FMU のログ出力（OSMPカテゴリ）で確認できます。
