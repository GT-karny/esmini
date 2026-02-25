# 外部制御 (External Control)

このドキュメントでは、FMI (Functional Mock-up Interface) や OSI (Open Simulation Interface) を介して、外部アプリケーションからGT_esmini内の車両を制御する方法について説明します。

## 概要

GT_esmini (および esmini) では、`ExternalController` を使用して車両の制御を外部委譲することができます。これにより、Simulinkや自作の制御プログラムなどが、UDPやFMI経由で車両の位置や状態を更新できるようになります。

## OpenSCENARIO設定

外部制御を行うには、OpenSCENARIOファイル (.xosc) で対象の車両に `ExternalController` を割り当てる必要があります。

### 1. コントローラーの定義と割り当て

`ScenarioObject` 内で `ObjectController` を定義し、プロパティで `ExternalController` を指定します。

```xml
<ScenarioObject name="Ego">
    <CatalogReference catalogName="VehicleCatalog" entryName="car_white"/>
    <ObjectController>
        <Controller name="ExternalController">
            <Properties>
                <!-- esminiにこのコントローラーがExternalControllerであることを伝える -->
                <Property name="esminiController" value="ExternalController" />
                <!-- ゴースト機能を使用する場合 (後述) -->
                <Property name="useGhost" value="true"/>
                <Property name="headStart" value="2.5"/>
            </Properties>
        </Controller>
    </ObjectController>
</ScenarioObject>
```

### 2. コントローラーの有効化 (重要)

コントローラーを割り当てただけでは、状態は「待機 (Inactive)」のままであり、物理制御は行われません。その結果、esminiのデフォルトの動作（`Init` で設定された `SpeedAction` など）が適用され、**外部入力がないにも関わらず車両が勝手に動く** という現象が発生します。

外部アプリが完全に制御権を持つには、`Init` フェーズで `ActivateControllerAction` を使用してコントローラーを明示的に有効化する必要があります。

```xml
<Init>
    <Actions>
        <Private entityRef="Ego">
            <!-- 既存の初期化アクション -->
            <PrivateAction>
                <TeleportAction>...</TeleportAction>
            </PrivateAction>
            
            <!-- コントローラーの有効化 -->
            <PrivateAction>
                <ActivateControllerAction longitudinal="true" lateral="true" />
            </PrivateAction>
            
            <!-- 初期速度の設定 (任意) -->
            <!-- ExternalController稼働中は無視されるか、初期状態として使われます -->
            <PrivateAction>
                <LongitudinalAction>
                    <SpeedAction>
                        <SpeedActionDynamics dynamicsShape="step" value="0" dynamicsDimension="time"/>
                        <SpeedActionTarget>
                            <AbsoluteTargetSpeed value="25"/>
                        </SpeedActionTarget>
                    </SpeedAction>
                </LongitudinalAction>
            </PrivateAction>
        </Private>
    </Actions>
</Init>
```

> [!IMPORTANT]
> `ActivateControllerAction` を忘れると、車両は `ExternalController` ではなくデフォルトの挙動（直進など）で動き続けてしまいます。FMI/OSI接続時は必ず有効化してください。

## ゴースト機能 (Ghost Trajectory)

`ExternalController` には、車両の「理想的な軌道（ゴースト）」を計算し、それを外部コントローラーへの参照入力として提供する機能があります。

### 設定

プロパティ `useGhost` を `true` に設定します。

```xml
<Property name="useGhost" value="true"/>
<Property name="headStart" value="2.0"/> <!-- 何秒先を走らせるか -->
```

### 動作

1.  **Ghost Object**: シミュレーション内部で、制御対象車両の「未来の姿」として不可視のゴースト車両が生成されます。
2.  **先行シミュレーション**: ゴースト車両は、実際の車両よりも `headStart` 秒だけ先の時間をシミュレーションします（シナリオの定義に従って走行します）。
3.  **OSI出力**: GT_esminiはこのゴーストの軌道を `future_trajectory` としてOSIメッセージに格納し、外部コントローラーに送信します。

外部コントローラーはこの `future_trajectory` を「目標パス」として追従制御を行うことができます。

## まとめ

1.  `<ObjectController>` で `esminiController="ExternalController"` を指定する。
2.  **必ず** `Init` で `ActivateControllerAction` を呼び出す。
3.  必要に応じて `useGhost` を有効にし、目標軌道を取得する。
