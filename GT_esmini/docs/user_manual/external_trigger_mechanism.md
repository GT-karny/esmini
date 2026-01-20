# 外部アプリケーションからのシナリオトリガー実行マニュアル

本マニュアルでは、`esmini` で実行中の OpenSCENARIO シナリオに対して、外部アプリケーション（C++ など）から任意のタイミングでイベントをトリガーする方法について解説します。

## 概要

OpenSCENARIO の `VariableCondition`（または `ParameterCondition`）と、`esminiLib` が提供する `SE_SetVariable`（または `SE_SetParameter`）API を組み合わせることで、外部プログラムからシナリオ内のイベント発火を制御することが可能です。

これにより、シミュレーション中にユーザー入力や外部のロジックに基づいて、交通信号の切り替えや車両のアクションなどを動的に実行できます。

## 手順 1: OpenSCENARIO (.xosc) の作成

シナリオ側では、外部からの信号を受け取るための「変数」を定義し、その変数の値を監視する「トリガー」を設定します。

### 1-1. 変数の宣言

`ParameterDeclarations`（または OpenSCENARIO 1.1 以降の `VariableDeclarations`）を使用して、トリガー用の変数を定義します。初期値は `0`（false 相当）としておきます。

```xml
<ParameterDeclarations>
    <!-- 外部トリガー用の変数。初期値 0 -->
    <ParameterDeclaration name="ExternalEventTrigger" parameterType="integer" value="0"/>
</ParameterDeclarations>
```

### 1-2. イベントとトリガーの設定

`Maneuver` 内の `Event` に `StartTrigger` を設定し、変数の値が変化（例: `1` になる）したときにアクションが実行されるようにします。

```xml
<Story name="ExternalControlStory">
    <Act name="ExternalControlAct">
        <ManeuverGroup name="ExternalControlMG" maximumExecutionCount="1">
            <Actors selectTriggeringEntities="false"/>
            <Maneuver name="SignalControlManeuver">
                <!-- このイベントは ExternalEventTrigger が 1 になると発動します -->
                <Event name="ChangeSignalToRed" priority="overwrite">
                    <Action name="SetSignalRedAction">
                        <GlobalAction>
                            <TrafficSignalStateAction name="Signal_ID_123" state="red"/>
                        </GlobalAction>
                    </Action>
                    <StartTrigger>
                        <ConditionGroup>
                            <Condition name="CheckExternalTrigger" delay="0" conditionEdge="rising">
                                <!-- 変数 ExternalEventTrigger が 1 と等しくなったかを判定 -->
                                <ByValueCondition>
                                    <ParameterCondition parameterRef="ExternalEventTrigger" value="1" rule="equalTo"/>
                                </ByValueCondition>
                            </Condition>
                        </ConditionGroup>
                    </StartTrigger>
                </Event>
            </Maneuver>
        </ManeuverGroup>
    </Act>
</Story>
```

*   **ポイント**: `conditionEdge="rising"` を指定することで、値が `0` から `1` に変わった瞬間だけイベントが発動するようにできます。
*   **注意**: `Property` ではなく `Parameter` または `Variable` として定義された値を参照する場合は `ParameterCondition` (OSC 1.0) または `VariableCondition` (OSC 1.1) を使用します。esmini では `ParameterCondition` でも動的な値変更に対応しています。

## 手順 2: 外部アプリケーション (C++) の実装

`esminiLib` をリンクした外部アプリケーションから、API を使用して変数の値を書き換えます。

### 2-1. esminiLib のインクルード

```cpp
#include "esminiLib.hpp"
```

### 2-2. トリガーの発火

シナリオがロードされ、シミュレーションが初期化された後（`SE_Init` 後）、任意のタイミングで以下の関数を呼び出します。

```cpp
// トリガー変数 "ExternalEventTrigger" を 1 に設定してイベントを発火
SE_SetParameterInt("ExternalEventTrigger", 1);

// 必要であれば、次のイベントのために 0 に戻す処理を入れることも可能です
// (ConditionEdge="rising" を使用している場合は自動的にエッジ検出されるため、
//  次に 0 に戻してから再度 1 にすれば再発火可能です)
```

もし OpenSCENARIO 1.1 の `VariableDeclarations` を使用し、厳密に区別したい場合は `SE_SetVariableInt` を使用することもできますが、`esmini` の実装上 `SE_SetParameterInt` でも多くの場合動作します（変数はパラメータ空間で管理されるため）。

## 動作の仕組み

1.  外部アプリが `SE_SetParameterInt` を呼ぶと、esmini 内部の変数管理テーブル（`ScenarioReader::variables`）の値が即座に更新されます。
2.  esmini のシミュレーションステップ（`SE_Step`）が進むと、`StoryBoard` の評価フェーズで `Condition` がチェックされます。
3.  `ParameterCondition` が参照している変数の値が `1` になっていることが検出され、条件が `true` になります。
4.  対応する `Event` が開始され、定義された `Action`（この例では信号機の変更）が実行されます。

## 制限事項

*   OpenSCENARIO 1.0 の標準仕様では `Parameter` は定数ですが、esmini では動的な変更を許容する拡張的な実装となっています。
*   頻繁に値を変更する場合や、複数のトリガーを管理する場合は、変数名の衝突に注意してください。
