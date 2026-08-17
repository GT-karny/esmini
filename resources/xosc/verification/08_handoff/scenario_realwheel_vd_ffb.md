# 実機G29でのVD単独FFB確認（ケース1、フィールドテスト専用）

**このシナリオは実機のG29ステアリング/ペダルを接続したフィールドテスト専用であり、自動ゲートでは走らない。**
GUIから何気なく実行しても、意図した検証（力の発生・解除・再武装）にはならない点に注意すること。
VirtualDriverControllerが横・縦とも保持し、ManualDriveControllerは存在しない。
デバイス(G29)を開くのも力を出すのもVDひとつのため、二重オープンも力の取り合いも起こらない。

## 検証の狙い

override（押し返してMANUALにラッチ）もAUTO_RESUMEもVDのStep内だけで完結し、ドメインの再活性を必要としない。
xosc内コメントはこれを「今回の新規コードを通らない純粋なF7b経路」と呼んでいる。
同ディレクトリの`scenario_realwheel_handover_vd_md_vd.xosc`（ケース2）が移管まわりの新規コードを検証するのに対し、本シナリオはVD単独でのFFB基本動作（力の発生・解除・再武装）を検証する。
確認する3点は、xosc内コメントによれば(1)力の発生——起動直後からホイールがAIの指令舵角へ駆動される、(2)力の解除——押し返してMANUALにラッチした瞬間にサーボが解放される、(3)AUTO_RESUME——ボタンでAIへ戻りサーボが再武装する、である。

## シーン構成

| 項目 | 内容 |
| :-- | :-- |
| 道路 | `e6mini.xodr`（road 0、緩いカーブを含む道） |
| 自車 | road 0 / lane -3 / s=20→s=1400、目標速度8.333 m/s（Story Eventでlinear 3.0s掛けて到達） |
| ドメイン所有 | 横=VirtualDriverController／縦=VirtualDriverController（MDなし） |
| 他エンティティ | なし |
| 走行時間 | シミュレーション時間 180 s 超過 |

## 進行

1. Init：Ego位置teleport、開始速度8.333 m/sへstep。VirtualDriverController（ConfigFile `virtual_driver_realwheel.json`）を横・縦ともActivate。
2. t=0：CruiseActのCruiseイベントが発火し、目標速度8.333 m/sへ3.0秒かけて加速する。
3. 起動直後からホイールがAIの指令舵角へ駆動される（(1)の確認）。
4. 押し返すとMANUALにラッチしサーボが解放される（(2)の確認）。AUTO_RESUMEでAIへ戻りサーボが再武装する（(3)の確認）。奪う→返すを何往復か試す。
5. t>180s：StopTriggerでシミュレーション終了。

## 期待する挙動

自動matcherは無く、xosc内コメントに記載された実機での観測手順が判定手段になる。

- 起動した瞬間からホイールがAIの舵に追従して動く（サーボONが正常。順構成のsplitシナリオ、サーボOFFとは正常/異常の判定が逆になる点に注意）。
- 押し返してMANUALにラッチした瞬間に力が抜ける。
- AUTO_RESUMEで再びAIへ戻り力が戻る。
- ウォッチドッグは|force| >= 0.570が2.0秒継続すると走行を強制終了し力を落とす（`virtual_driver_realwheel.json`のffb_safety_*）。異常時はウィンドウを閉じれば止まる。
- e6mini road0 lane-3の緩いカーブをあえて選んでいる。xosc内コメントによれば、舵角が大きく振れる道だと「AD目標の移動速度」が残差に乗って誰も触っていないのにラッチする既知の機序（f7_override_detector_findings §4）と切り分けにくいため、まず素直な条件で(1)(2)(3)を通すことを意図している。

## 関連

- バッチ: 常設バッチには未所属。フィールドテスト専用の個別実行資産（手順はREALWHEEL_VD_FFB_TESTの節、handover_control_ownership_defects.md参照。起動オプション`window 60 60 1280 720`が必須）。
- 関連ID: `feature:F7`
