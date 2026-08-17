# シナリオ主導のVD解放（Activate-OFF経路）

自車をVirtualDriverController（横・縦とも）で巡航させ、シナリオイベントで`ActivateControllerAction lateral="false" longitudinal="false"`を発火して制御を手放させる。
以後、制御が再び自動側へ戻らないことだけを確認する。

## 検証の狙い

esmini上流の`OSCPrivateAction.cpp`は、`lateral="false" longitudinal="false"`のActivateControllerActionを`ControllerVirtualDriver::Activate()`にOFFモードで渡すルートを通り、`Controller::Deactivate()`は呼ばない。
本シナリオはこの「Activate-OFF」経路だけを固定的に踏む。
同ディレクトリの`scenario_domain_takeover_vd.xosc`は別コントローラが片ドメインを奪う`DeactivateDomains()`経路を踏むため、両者は制御を手放す手段が異なる別の検証対象になる。
先行車など他エンティティは介在させず、判定はVirtualDriverテレメトリの`vd_active`のみで行う。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_plain.xodr`（直線道路） |
| 自車 | road 1 / lane -1 / s=30、目標速度13.889 m/s（Story Eventでlinear 3.0s掛けて到達） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 14 s 超過（rising edge） |

## 進行

1. Init：VirtualDriverControllerを横・縦ともActivate、road 1 s=30→s=480のルートを割り当てる。
2. t=0：CruiseActのCruiseイベントが発火し、目標速度13.889 m/sへ3.0秒かけて加速する。
3. t>8.0s（パラメータ`DeactivateAt`）：HandoffActのDeactivateイベントが発火し、`ActivateControllerAction lateral="false" longitudinal="false"`を実行する。
4. t>14s：StopTriggerでシミュレーション終了。

## 期待する挙動

- t=8.0sの解放指示直後にVD制御が手放される。
- 手放した後、制御が再度アクティブに戻ることはない。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `vd_control_relinquished` | sim_time > 7.5 s | 8.0sの解放トリガに対し余裕を持たせた後、VDが制御を手放し以後再アクティブ化しないこと |

## 関連

- バッチ: `scenario_handoff_batch.yaml`
- 期待値: `scenario_deactivate_vd.expectations.yaml`
- 関連ID: `feature:F7`
