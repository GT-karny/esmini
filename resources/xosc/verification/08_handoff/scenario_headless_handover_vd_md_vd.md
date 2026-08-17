# VD→MD→VD往復移管のヘッドレス確認（実機G29不要）

`scenario_realwheel_handover_vd_md_vd.xosc`と同一のストーリーボード構成を、ManualDriveの入力元だけ物理G29からUDPポート9100に差し替えて走らせる。
物理デバイスを一切開かないため力は出ず、実機テストの前にビルドと移管経路が生きていることを確認する用途に使う。

## 検証の狙い

xosc内コメントは、この構成が修正3点すべてを通ると述べる。
(1) 移管直後にMDが人の入力なしで運転を始めるか（従来はAUTOのまま早期returnし、upstreamのdefaultControllerが等速で滑らせていた）。
(2) AUTO_RESUMEでVDがドメインを取り戻すか（従来はVDが非活性のままStepされず、ボタンを見ている主体がいなかった）。
(3) HVD/テレメトリが実際に運転している側から出るか（従来は名前引きの優先順で常にMDが選ばれ、休眠中のMDの空データがOSI HostVehicleDataに載っていた）。
**UDPポート9100をbindするため、他のヘッドレス実行と並列に走らせてはならない**、とxosc内コメントは明記している。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `e6mini.xodr` |
| 自車 | road 0 / lane -3 / s=20→s=1400、目標速度8.333 m/s（Story Eventでlinear 3.0s掛けて到達） |
| ドメイン所有 | t<15.0s: 横=VD・縦=VD（MDはInitでActivateしない）／t>15.0s以降: 横=MD・縦=MD |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 180 s 超過 |

## 進行

1. Init：Ego位置teleport、速度8.333 m/sへstep。VirtualDriverController（ConfigFile既定=input_type stub/サーボOFF）を横・縦ともActivate。ManualDriveController（ConfigFile `manual_drive_headless_udp_override.json`、override.enabled=true）は宣言のみでActivateしない。
2. t=0：CruiseActのCruiseイベントが発火し、目標速度8.333 m/sへ3.0秒かけて加速する。
3. t=0..15s：VDが巡航する。MDにとってこの区間はまだINACTIVE。
4. t>15.0s（パラメータ`HandoverAt`）：ReleaseVDイベントが先に発火しVDを横・縦ともfalseでActivate、続いてTakeMDイベントが発火しMDを横・縦ともActivate。MDにとって最初のINACTIVE→ACTIVE遷移がここで起きる。
5. 移管後、誰も入力を送らなければthrottle=brake=0のまま。AUTO_RESUME（PSTCパケット、magic 'PSTC'、44バイト、buttonsのbit7=1<<7）をUDP 9100へ送るとVDへ復帰する。
6. t>180s：StopTriggerでシミュレーション終了。

## 期待する挙動

自動matcherは無く、xosc内コメントに記載された観測方法が判定手段になる。

- t=15s以降、誰も入力を送らずに速度が惰行で落ちていけば(1)合格（MDが積分している）。
- 完全に一定のままなら(1)不合格（upstreamのdefaultControllerが等速で滑らせている）。
- 復帰(2)まで見るには、AUTO_RESUMEビットを立てたPSTCパケットをポート9100へ送る必要がある。

## 関連

- バッチ: 常設バッチには未所属。ヘッドレスプローブ用の個別実行資産。UDP 9100へPSTCパケット（steering/throttle/brake/buttons、wire formatはmagic 0x50535443を含む44バイト）を送る参照実装は`scripts/manual_drive_client.py`（`--port 9100`が既定）。
- 関連ID: `feature:F7`
