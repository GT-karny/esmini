# 他コントローラによる片ドメイン奪取（v1.3 DeactivateDomains経路）

自車にExternalController（TakeoverController）とVirtualDriverControllerの2つを割り当て、初期はVirtualDriverControllerが横・縦を保持する。
走行中にTakeoverControllerが横ドメインだけを奪い、VirtualDriverController側のサーボが解放されることを確認する。

## 検証の狙い

OpenSCENARIO v1.3以降、別コントローラが片ドメインをActivateすると、現職コントローラは`Controller::DeactivateDomains(mask)`で該当ドメインだけを剥奪される。
この経路は`scenario_deactivate_vd.xosc`が踏む「lateral/longitudinal=falseでActivate」する経路とは別物で、`Controller::Deactivate()`にも到達しない。
xosc内コメントによれば、commit 7678bb99以前はこの経路で`ControllerVirtualDriver`の`TearDownControlOutputs()`が呼ばれず、FFBサーボが停止せずに回り続ける不具合があった。
本シナリオはその回帰防止として、片ドメインだけを奪われてもVDがサーボを解放することを固定する。
esminiは`ActivateControllerAction`の`controllerRef`属性を実際には読まず常に直近に割り当てたコントローラ（`controllers_.back()`）を選ぶため、xosc内では意図的にVirtualDriverControllerを2番目（後）に宣言している。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `straight_500m_plain.xodr`（直線道路） |
| 自車 | road 1 / lane -1 / s=20、速度13.889 m/sへstep（即時）到達 |
| ドメイン所有 | Init: 横=VD・縦=VD／t>8.0s以降: 横=TakeoverController（ExternalController）・縦=VD（維持） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 14 s 超過 |

## 進行

1. Init：Ego位置teleport後、速度13.889 m/sへstep。VirtualDriverControllerを横・縦ともActivate。
2. t>8.0s（パラメータ`TakeoverAt`）：TakeoverActのTakeoverEventが発火し、TakeoverController（ExternalController、useGhost=false）が横ドメインだけをActivateする。縦ドメインはVirtualDriverControllerが保持したまま。
3. t>14s：StopTriggerでシミュレーション終了。

## 期待する挙動

- t=8.0sの奪取後、VirtualDriverControllerのサーボが解放され`vd_active`がfalseになる。
- 縦ドメインは奪われていないが、期待値yamlのコメントによれば`DeactivateDomains(LAT)`後の後続Activateは「ACTIVE→ACTIVE」と判定され`SetUpControlOutputs()`が再実行されないため、`vd_active`はfalseのまま維持される。
- 期待値yamlの実測注記: `vd_active`はt=8.100までTrue、以後は残り全区間false。

## 判定基準

| matcher | 条件 | 意味 |
| :-- | :-- | :-- |
| `vd_control_relinquished` | sim_time > 7.5 s | 8.0sの奪取トリガに対し余裕を持たせた後、片ドメインのみの奪取でもVDがサーボを止めて制御を手放すこと |

## 関連

- バッチ: `scenario_handoff_batch.yaml`
- 期待値: `scenario_domain_takeover_vd.expectations.yaml`
- 関連ID: `feature:F7`
