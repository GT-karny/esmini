# 新機能提案 2026-06 — 全45提案 詳細付録

> [`feature_proposals_2026-06.md`](feature_proposals_2026-06.md) の付録。ワークフロー実行結果(19エージェント、foundation実在検証済み)からの復元ダイジェスト。ID順(P1〜P45)。各提案のスコアは審査3レンズ(アーキテクチャ適合/ワークフロー実需/工数・段階出荷性)の平均。
>
> 視点の対応: P1-P5=ADAS/AD制御(MiL/SiL)、P6-P10=知覚・センサー、P11-P15=安全V&V、P16-P20=HMI/DiL、P21-P25=データ/ML、P26-P30=基盤/DevOps、P31-P35=プロダクト戦略/デモ、P36-P40=フリートログ→シナリオ化(critic追加)、P41-P45=sim妥当性/実車相関(critic追加)

---

## P1 [8.3] SUTロックステップ接続ゲートウェイ(フレーム同期の外部制御コントローラ) — M / critical

- **対象**: 社内ADAS/AD制御スタック(ACC/AEB/LKA/ALKS)のSiL担当。自社スタックをegoの運転主体としてGT_esminiに接続する。
- **ストーリー**: SUTを別プロセスで起動し、GT_esminiがフレームkのOSI GroundTruth/HostVehicleDataをframe_id付きで送信→SUTが同じframe_idの制御コマンド(ペダル/ステア)を返すまでGT_Stepがブロック→物理1ステップ進行、という完全同期ループ。CIでは--no_realtime+ヘッドレスで実時間の数十倍速で回し、同一入力列なら同一結果を前提に回帰判定。
- **既存資産**: ControllerRealDriver(UDP 53995、[4B lightMask+OSI HostVehicleData protobuf]ワイヤ、ControllerRealDriver.cpp:486)とNetworkInputBridge(PSTC 44byteワイヤ、port 9100)の外部接続2系統、IPhysicsBackend::StepPedalSteer契約、ScenarioReader::RegisterController(GT_esminiLib.cpp L377-384、ユーザ型id 1003以降空き)、GT_OpenOSISocket(48198)、gt_lib.py/gt_sim_testの呼出側駆動GT_Stepパターン。死蔵中のGT_ENABLE_OSI_MOTION_REQUESTスキャフォールド(IPhysicsBackend.hpp:28 StepMotionRequest、監査TODO-2で削除判断保留中)をMotionRequest入力形式として正式活用できる。
- **欠落**: 既存の外部接続はすべて非同期latest-sample-wins方式で、フレーム同期保証がなくOSスケジューリング次第で結果が変わる=SiLの決定論要件を満たさない。frame_idハンドシェイク、コマンド到着までのブロッキング待ち(タイムアウト時のホールド/縮退方策付き)、起動時のcapabilityネゴシエーションが存在しない。
- **設計**: GT_esmini/src/control/ControllerSutBridge.cpp(型名"SutBridgeController"、型id 1003)+sut/LockstepChannel.{hpp,cpp}新設。ワイヤは[frame_id:uint32]を既存PSTC/HVD protobufの前置ヘッダとして追加した往復形式。GT_InitWithArgsのカスタム引数フィルタに--sut-lockstep <port>を追加し、GT_Step冒頭でコントローラの「フレームkコマンド待ち」を解決。参照クライアント(Python、sut_client_example.py)同梱。
- **Clean Core**: RegisterController+GT_esminiLib.cpp単一TU内で完結、EnvironmentSimulator/無改変。MotionRequest経路の有効化もGT_esmini/CMakeLists.txtのdefine追加のみ。
- **依存**: UDPポート慣例(9100/9200系/48198-48202)との衝突回避設計。MotionRequest対応は監査TODO-2の決着が前提。
- **リスク**: ブロッキング待ちとWeb UIライブ配信(OSIBridge/vd_bridgeのタイムアウト)の相互作用。SUT無応答時の安全フォールバック(前回値ホールドNフレーム→縮退停止)のセマンティクス設計が本質的難所。
- **審査**: foundation全て実在確認(StepMotionRequestスキャフォールド含む)。RegisterController+型id 1003でClean Core完全準拠、製品方向(検証工場のSiL化)に直結 / SiLの決定論・フレーム同期はSUT接続プラットフォームを名乗るための「ないと困る」中核 / 決定論保証の検証込みだとM工数はやや楽観的。単一SUT+ブロッキング待ちのMVPに絞れば段階出荷可能。

## P2 [7.3] OSI SensorView物標リスト配信(知覚制約付きSiL入力の開通) — M / high

- **対象**: 物標リスト入力を前提とするACC/AEB/ALKSのSiL担当。全知のGroundTruthではなくFOV/レンジ制限のある現実的な知覚入力で制御を評価する工程。
- **ストーリー**: --sensor-config sensor.json(FOV/レンジ/取付位置)でegoにObjectSensorを定義し、検知物標のみを含むosi3::SensorViewをUDP/gRPCでSUTへ毎フレーム配信。「先行車がセンサFOVに入る瞬間からACCが追従開始する」というセンサ起点の挙動がSiLで再現され、既存matcherで判定できる。
- **既存資産**: OSIReporter::CreateSensorViewFromSensorData(GT_OSIReporter_Sensor.cpp:16)、GetSensorView(GT_OSIReporter_Environment.cpp:22)、ReportSensors(GT_OSIReporter.cpp:181)のSensorView生成系一式が実装済み。センサ登録はupstream SE_AddObjectSensor(単一TUで内部アクセス可)。配信はGT_HostVehicleReporter(UDP 48199)のレポーターパターン、grpc_server.pyのservicer追加構造、osi_bridge.pyのマルチパケット再組立を複製。
- **欠落**: SensorView/SensorDataは生成済みだがUDP/gRPC外部配信が未接続(GetSensorView()のin-process取得のみ)。センサ諸元の設定ファイル定義も、LiveSceneViewの検知/非検知可視化も存在しない。R5-U4(OSIライト出力ポート)はライト限定で別物。
- **設計**: GT_esmini/src/osi/GT_SensorViewReporter(シングルトン、UDP 48203新設)+GT_OpenSensorViewSocket C API+GT_InitWithArgsの--sensor-config分岐でSE_AddObjectSensorを設定駆動。web側はStreamSensorView servicer追加、LiveSceneViewに「検知中=ハイライト」レイヤ(LayerKey追加)。
- **Clean Core**: GT_OSIReporterはCMake差し替えフォークのため追記はupstream追従コストを微増 → 新規ロジックはGT_SensorViewReporter側へ寄せフォークファイルへの追記は取得フック最小限に。
- **依存**: P1と併用時はframe_idスタンプの共通化。OSI UDPパケット契約([counter][size][data≤8192])のC++/Python両側実装を踏襲。
- **リスク**: upstream ObjectSensorの検知モデル(オクルージョン非対応等)の素朴さが「現実的知覚」として十分かの期待値調整。センサ座標vs世界座標の仕様確認。
- **審査**: 「素材が揃っている未接続パイプ」の開通でレバレッジ最大級、M妥当 / 死蔵資産の開通で実需(物標リスト前提SiL)に直結 / 遮蔽なしの理想センサのままでは価値はP6と組んでこそ。P8はこれに吸収。

## P3 [6.3] フォールトインジェクション基盤(知覚・操作系の遅延/欠落/固着の宣言的注入) — L / high

- **対象**: AEB/ALKSのロバスト性・フェールセーフ検証担当。HARA由来の故障条件をシミュレーションで網羅確認する工程。
- **ストーリー**: 「センサデータ100ms遅延+10%パケットドロップ下でAEBが要求減速度を達成するか」「ステア指令0.5秒固着でLKAが安全に縮退するか」を、fault_profile.jsonのパラメータスイープ+XOSC拡張アクションのトリガ連動(衝突3秒前に故障開始等)で機械実行。判定は既存/新規matcherで宣言的に行いバッチ回帰に組込。
- **既存資産**: 注入点(a)=IInputSource(4メソッド)を包むデコレータ+input_type 1行分岐(ControllerManualDrive.cpp:60-/ControllerVirtualDriver.cpp:89-で確認済み)。注入点(b)=P2のSensorView送出前フック。宣言的定義=GT_ScenarioReader::ParseExtensionActionsの拡張アクション注入パターン(GT_TrafficSignalControllerActionが完成形の雛形)。実行管理=GT_StepポストステップManager連鎖への1行追加パターン。バッチ組込=_prepare_policy_xoscと_eval_must()のmatcher追加。
- **欠落**: 故障注入機構は現状ゼロ。遅延リングバッファ/確率ドロップ/ノイズ/固着(stuck-at)という故障モデル群と、シナリオ条件連動の故障開始/解除(GT:FaultInjectionAction)が完全に新規。
- **設計**: GT_esmini/src/fault/FaultInjectionManager(シングルトン、GT_Stepチェーンに1行追加)+FaultProfile(config/fault_profile.json)+GT:FaultInjectionActionをParseExtensionActionsで注入。コマンド側はFaultingInputSourceデコレータ(任意の既存ソースを包む)、知覚側はGT_SensorViewReporterの送出直前フック。
- **依存**: 知覚側フォールトはP2が前提(コマンド側は単独先行可)。config追加時はin-processハーネスのexe相対パス解決バグ(F5で正規修正予定)と同じ罠に注意し絶対パス注入方式を踏襲。
- **リスク**: 故障モデルの妥当性(実ECU故障モードとの対応付け)は利用部門との擦り合わせ要。注入点が増えるほどテレメトリ契約への下流調整が発生。
- **審査**: デコレータ+拡張アクションは既存パターンに忠実で堅実 / 実需はあるが利用頻度は特定検証工程限定、L工数+P2前提のフルセットは小規模チームに重い / MVP=入力遅延/固着のみなら小さく出せる。着手順はP1完了後。

## P4 [9.0] 実行時刻制御: ポーズ/コマ送り/条件ブレークポイント — S / high

- **対象**: 制御アルゴリズムのデバッグ担当(MiL/SiL)。判断ロジックの不具合解析や検証failの原因調査という日常デバッグ工程。
- **ストーリー**: Web UIのライブ監視中にAEB作動(あるいはVDのSTOP_AT_S制約発生)の瞬間で一時停止し、LiveSceneViewとVDテレメトリ/OSI状態を静止画面で精査、1フレームずつステップ実行して判断系列を追う。SUT接続時(P1)はSUT側デバッガのブレークとシミュレータ側ポーズが両立し「シミュレータが先に進んでしまう」問題が消える。
- **既存資産**: GT_Sim --control_pipeの行コマンド機構(GT_Sim/main.cpp:75,220-262にSPEED:/DRIVE_MODE:/QUIT実装確認済み)、固定Hzメインループ(main.cpp:588-613、speed_factorのatomic読み)、simulation_runner.pyのPUT /{job_id}/speed→named pipe書込の既存導線、LiveMonitorPanel/VdLivePage/useOsiStreamのライブUI。in-process側は元々gt_lib.pyがGT_Stepを呼出側駆動しており追加不要。
- **欠落**: サブプロセス実行(GT_Sim経由=Web UIの全実行)はフリーラン固定で、PAUSE/RESUME/STEP:\<n\>コマンドが存在せず一時停止もコマ送りも不可。条件ブレーク(テレメトリ値・制約種別・XOSC条件成立での自動停止)も皆無。F5のリアルタイムGUIパネルは「表示」であり「時刻制御」は含まない。
- **設計**: ControlPipeにPAUSE/RESUME/STEP:\<n\>を追加し、paused時はSE_Step/GT_Stepをスキップしつつ描画・WS/UDP配信は最終フレームを維持送信。backendにPUT /api/simulations/{job_id}/playback {action: pause|resume|step, frames: n}(speed変更APIの完全同型複製)、フロントはLiveMonitorPanelに再生制御ボタン3個。第2段でBREAK_ON:\<expr\>(constraints[].kind一致や速度閾値)をGT_Step後評価で実装。
- **Clean Core**: GT_Sim/main.cpp(GT_esmini配下)とweb backend/frontendのみ。コア無改変。
- **依存**: なし(単独完結)。P1と組むとSUTデバッグ体験が完成。
- **リスク**: pause中のOSI/HVD/VDテレメトリ配信周期の扱い(下流ブリッジのqueue満杯・タイムアウト挙動確認)。named pipeはWindows限定実装(現行製品の主戦場はWindowsなので許容)。
- **審査**: PAUSE/STEP追加は既存機構の最小増分でS工数が信頼できる、全ペルソナに波及する最高効率 / 商用シミュレータでは当然の日常デバッグ機能で「ないと困る」欠落筆頭、価値/工数比が全45件中最良級 / ポーズ→ステップ→条件ブレークの段階出荷が最も綺麗。

## P5 [7.3] 入力刺激の録画・リプレイと決定論回帰ハーネス(人間運転のテストケース化) — M / high

- **対象**: SiL回帰の運用担当と評価ドライバ。G29での手動運転やSUTコマンド列を再現可能な回帰資産に変換しリリース前回帰で流す工程。
- **ストーリー**: 評価ドライバがG29でテイクオーバー操作(AUTO→MANUAL遷移含む)を1回実演すると全入力フレームがjsonlで記録され、以後ReplayInputSourceで同一刺激を無人再生。CIでは同一runを2回実行してテレメトリ主要列のハッシュ一致を検証(決定論の常時監視)、SUT/VD/物理の更新時には同一入力刺激での挙動差分をcompareのRMSE+ゴースト軌跡で確認。
- **既存資産**: IInputSource(manualdrive/IInputSource.hpp、「リプレイ入力」追加が設計想定済みと明記された拡張点)+input_type文字列分岐(両コントローラ1行追加)、ManualDriveCoordinatorの毎フレームパイプライン(InputFrame取得点=記録点)、OverrideManagerのドメイン遷移、gt_sim_test compare+assert/batchのmanifestスキーマ、telemetry.jsonl契約、固定HzのGT_Step。
- **欠落**: 入力の記録器・再生器が存在しない。「同一入力→bit同一出力」を検証する手段(決定論matcher)も無い。シナリオベース(XOSC)では表現できない「人間の連続操作」を回帰資産化できる点が本質的に新しい。
- **設計**: InputRecorder(全InputFrameを{t,steering,throttle,brake,buttons,override遷移}のjsonlでrun出力ディレクトリへ、--record-inputフラグ)+ReplayInputSource(input_type="replay"、フレーム番号同期再生、終端でホールド)。manifestにinput_replay:キー、新matcher deterministic_replay(2回実行→x/y/speed列ハッシュ比較)。
- **Clean Core**: GT_esmini配下のみ。IInputSourceの契約(4メソッド)内で完結。
- **依存**: RealVehicleのEngineModelアイドルジッタ等のRNGシード固定化が決定論の前提。FFB(SDL_Haptic)はリプレイ時NullFFBSinkへ自動退避。
- **リスク**: 浮動小数の非決定要因(初期化順序・タイマ由来dt)発見時の切り分け工数が読みにくい。録画フォーマットはボタンマップ依存のためマップのスナップショット同梱が必要。
- **審査**: IInputSourceは「リプレイ入力」を明示想定した拡張点で土台は本物 / XOSCで表現不能な人間連続操作の回帰資産化は本質的に新しい / **P40と機構が実質同一なので統合設計が前提**。MVPは記録+再生に絞る。

## P6 [7.0] 幾何遮蔽つき検知リスト生成(GT_SensorSimManager) — M / high

- **対象**: 検知リスト入力前提のフュージョン/プランナをSiLで回す知覚・センサーフュージョン開発者、ADAS機能開発者。
- **ストーリー**: 現状GT_esminiから取れるのは完全なGroundTruthのみで「駐車車両の陰の歩行者は見えない」という実機の本質的制約を再現できない。egoにマウント定義した仮想センサ(位置/FOV/レンジ)ごとに、2D遮蔽計算でフィルタされたosi3::SensorData(DetectedMovingObject+existence_probability=可視面積率)を毎フレーム取得。
- **既存資産**: upstream ObjectSensor(IdealSensor.hpp — FOV+距離判定の参照実装)/OSIReporter::ReportSensors・CreateSensorViewFromSensorData/GT_Stepのポストステップマネージャ連鎖/GT_SetCurrentOSIReporterグローバル登録口/ConfigLoader::ResolveConfigPathのconfig/*.json方式。
- **欠落**: ObjectSensorは水平FOV+near/farのみで遮蔽なし・existence_probabilityなし・OSI DetectedMovingObject形式でない。ReportSensorsの出力はglobal_ground_truth詰替えで「検知リスト」の体をなしていない。
- **設計**: GT_esmini/src/osi/にGT_SensorSimManager(シングルトン、Manager連鎖に1行追加)新設。全MovingObject BBoxを2Dポリゴン化しセンサ原点からのシャドウキャスティングで可視角度区間を計算、可視率をexistence_probabilityに格納したSensorDataを生成。config/sensor_sim.jsonで複数センサ定義、C API GT_GetSensorData追加、--sensor-simフラグで有効化。
- **依存**: なし(単独着手可)。R5-U4と無関係なポート/フィールドのため衝突しない。
- **リスク**: 遮蔽計算のO(N²)は100台規模で要プロファイル(角度区間ソートでO(N log N)に)。建物・植生の遮蔽はstationary_object BBox近似に留まる点を仕様明記。
- **審査**: Manager連鎖・ConfigLoader・C API追加は全て確立パターン / 「駐車車両の陰の歩行者」の安価な再現は身の丈設計として妥当、P10の評価語彙の土台 / 幾何エッジケースのテスト工数を過小評価気味。P2(配信)と組で初めて外部価値。

## P7 [6.0] 統計的センサ誤差モデル+VD知覚イン・ザ・ループ — M / high

- **対象**: VirtualDriverポリシー開発者と検証エンジニア(交通ポリシーのロバスト性回帰)。
- **ストーリー**: VDのLeadVehicleAware/TrafficLightAwareは現状Entities生参照の完全知覚で動くため「検知が0.2秒遅れたら」「先行車を3フレーム見失ったら」の挙動が検証できない。誤差プロファイル(遅延/ガウスノイズ/未検知率/ゴースト率、シード固定)を指定してbatchを回し「知覚劣化時も車間と減速度プロファイルが規定内」を回帰ゲート化。
- **既存資産**: ITrafficPolicy::Evaluate(TrafficPolicyContext)の確定契約/TrafficPolicyManager/_POLICY_FLAG辞書+per-run virtual_driver.json生成/既存マッチャ/VirtualDriverTelemetryJson::ToJson。
- **欠落**: F2/F3は完全知覚前提の判断ロジック追加であり、本提案は判断の上流に「知覚の劣化」を注入する直交軸。誤差・遅延・ドロップアウトの概念はコードベースのどこにも無い。
- **設計**: PerceivedSceneBuilder(遅延リングバッファ+ノイズ+未検知/ゴースト抽選、std::mt19937シードconfig指定)新設、TrafficPolicyContextにconst PerceivedScene*追加。既存3ポリシーはperceived_scene非nullならEntitiesの代わりに読むopt-in切替。P6があれば遮蔽フィルタ合成可能だが無くても単独動作。
- **依存**: 単独着手可。F2/F3の新ポリシーも同じPerceivedScene契約に乗せると実装当初からロバスト性検証可能。
- **リスク**: 誤差注入で既存passがflaky化 → デフォルトOFF+シード固定+誤差有効バッチは別マニフェスト分離。PIDPurePursuit凍結特性と誤差の切り分け。
- **審査**: opt-in切替は所有権ルールを壊さない慎重な設計 / VDは人間ドライバーのモデルでありSUTではないため実務需要は研究寄り / **TrafficPolicyContext改変はF2/F3が活発開発されるhard-won領域と並走するためマージ摩擦が高く、F2/F3完了後に着手すべき**。

## P8 [5.3] SensorData/SensorView外部配信チャネル(UDP+gRPC+OSMPセンサーFMU接続) — M / medium

- **対象**: 外部知覚スタック(ROS/FMI環境)を持つセンサーモデル担当、OSMP準拠ツールチェーン連携の検証基盤担当。
- **要旨**: UDP 48203へのSensorDataストリーム+gRPC StreamSensorData+修理後GT_OSMP_FMUのsensor_view出力接続で「GT_esmini→センサーFMU→知覚FMU」のOSMPチェーンを成立させる。
- **見送り理由(審査)**: UDP/gRPC配信部分はP2とほぼ同一提案でポート(48203)まで衝突。FMU接続は既知破損(BLD-1)の修理が先行条件と自認しており、独自価値はFMUレグのみ=優先度低。**P2へ吸収し、FMUレグはBLD-1修理後に再評価**。

## P9 [3.3] GTラベル付きカメラ画像データセット出力(自動アノテーションキャプチャ) — L / medium

- **対象**: カメラ知覚チーム(検知モデルのスモークテスト/希少シーンの合成データ)。
- **要旨**: シナリオカタログを運転席カメラでオフスクリーン実行し、画像+フレーム同期の2D bbox/車種/灯火/遮蔽率ラベル(COCO風JSON)を一括出力。OSGカメラのView×Projection行列からBBox 8頂点をスクリーン投影。
- **見送り理由(審査)**: OSGカメラ行列取得とSE_SaveImagesToFile(非同期書出し)のフレーム同期が非自明で実装スケッチが楽観的。**esminiの非フォトリアル描画では知覚モデルの評価・学習に耐えず、この用途はCARLAや実車データで代替されるのが実務の通例**。45件中最低評価。

## P10 [6.7] 可視性グラウンドトゥルース検証(オクルージョン認識マッチャ+FOVレイヤー+遮蔽シナリオファミリ) — M / high

- **対象**: 「対象が見えた時点」を基準にADAS/VDの反応性を評価する検証エンジニアとシナリオ作成者。
- **ストーリー**: 飛び出しや見通しの悪い交差点の評価では「egoから対象がいつ可視になったか」が合否基準の起点(視認後反応時間、視認時TTC)。P6の可視率計算をVDテレメトリに載せ、reacts_within_after_visible / first_visible_ttc_above を宣言的に判定、LiveSceneViewにFOV扇形+遮蔽シャドウのレイヤーを追加。
- **既存資産**: _eval_must(マッチャ追加=ifブロック1個)/VirtualDriverTelemetryJson::ToJson(全経路同一形状 — perception節1箇所追加で全経路に乗る)/LayerKey機構/gen_NN_*.pyの3点セット規約+build_manifest.py/annotation_match.extract_features。
- **欠落**: 可視性・遮蔽という概念がマッチャ約13種・テレメトリ・UIレイヤーのどこにも無い。F2は知覚完全前提の判断側で、本提案は「見える/見えない」の評価基盤側。
- **設計**: (1) C++: GT_SensorSimManager(P6)の対象別可視率をテレメトリperception節としてemit (2) Python: マッチャ2種追加+extract_featuresにfirst_visible系特徴 (3) UI: LayerKey "fov"追加 (4) シナリオ: gen_09_occlusion.py(駐車車両列の陰からの交差車/歩行者)を3点セット規約で追加。
- **依存**: P6(可視率の計算源)が前提。
- **リスク**: 歩行者のVD側知覚が未整備のため当初は交差「車両」遮蔽シナリオ限定。first_visible_tの可視率閾値はper-scenario指定可能に。
- **審査**: 4面すべて確立パターン、可視性起点評価は検証工場の語彙拡張として製品適合が高い / LHT遮蔽シナリオは日本市場重視と噛み合う / P6完了必須の縦積みでC++/Python/UI/シナリオ4面同時のM申告は過小。

## P11 [9.0] サロゲート安全指標エンジン(TTC/THW/PET/必要減速度の自動算出・記録・宣言的判定) — M / critical

- **対象**: 安全性評価/V&V担当。VDポリシー(F2/F3含む)の検証と回帰ゲート運用で「ぶつからなかった」だけでなく「どれだけ危なかったか」を定量化。
- **ストーリー**: batchを回すと各runにcriticality.jsonl(毎フレームのTTC/THW/PET/a_req時系列)が自動併産。expectations.yamlに『min_ttc_above: 1.5』『required_decel_below: 4.0』と書くだけで合否化、注釈UIのリプレイではTTCが閾値を切った瞬間へ失敗フレームジャンプ。F2ではギャップ受容判断の安全マージンをTTC/PETで直接評価。
- **既存資産**: gt_sim_test.pyの_OsiCapture+_gt_to_scene(scene.jsonlのobjectsが既にx/y/h/speed/length/widthを毎フレーム保持。L122-175確認)、vd_recorder.pyのscene.jsonl(GUI実行も同形式)、_eval_must(L530)のマッチャ機構と既存maintained_following_distance(THWパーセンタイル、L758)の実装雛形、verdict.jsonの失敗フレームidx/t→UIジャンプ機構、LiveSceneView.tsxのLayerKey機構。
- **欠落**: 既存マッチャは速度・車線・THWパーセンタイル止まりで、ISO 34502/SOTIFの議論に必須のサロゲート指標(TTC、PET、必要減速度a_req)の算出・時系列記録・閾値判定が存在しない。F4は実行の自動化であって指標の新設ではない。
- **設計**: GT_esmini/scripts/verification/criticality.py新設 — scene.jsonl+telemetry.jsonlの後処理でego対全moving_objectのペアワイズTTC(縦+交差軌道の等速外挿)、THW、PET(交差領域占有時刻差)、a_reqを算出しcriticality.jsonl出力。_eval_mustへ min_ttc_above / pet_above / required_decel_below / thw_above の4マッチャ追加(scene系同様osi:true必須、無ければskip)。Webは/runs/{id}/telemetryへの併載とLiveSceneViewのTTC色分けレイヤー(LayerKey 1個でライブ/リプレイ両対応)。
- **Clean Core**: C++改変ゼロ。OSI出力の純粋な後処理。検証ツール層Pythonは凍結対象外。
- **依存**: なし(単独着手可)。F2/F3が進むほど評価対象が増えて価値が上がる。
- **リスク**: PETの交差領域定義が道路形状依存 → 第1段はOBB軌跡投影の保守的近似、junction系はmeta.yamlにconflict領域ヒントの逃げ道。OSI 25HzスロットルはTTC算出には十分だがPET精度に注記。
- **審査**: foundationの行番号まで全て正確、C++改変ゼロ=Clean Core完全準拠かつ最小リスク。P12/P15等の目的関数にもなる基盤価値 / 検証工場の評価語彙の土台で毎バッチ効く最高頻度の欠落 / TTC/THW 2マッチャだけのMVPから段階出荷できる最高の費用対効果。

## P12 [8.0] 安全エンベロープ不変条件モニタ(全run常時適用のグローバルKPI層+衝突検出) — M / high

- **対象**: V&V責任者/回帰ゲート運用者。合否基準の形式化と、シナリオ作者が書き忘れても落ちない安全網。
- **ストーリー**: シナリオ個別のexpectations.yamlとは独立に『衝突ゼロ・最大減速度≤X・横加速度≤Y・道路逸脱なし』のグローバル不変条件が全runへ自動適用。F2開発中、個別シナリオは『曲がれた』でpassでも対向車と接触していればinvariant違反で即fail。verdict.jsonには通常mustとinvariantsが分離記録され、監査時に「常時条件の適用範囲=全run」を示せる。
- **既存資産**: gt_sim_test.pyのassert/batch評価系(manifestのdefaultsマージ機構)、scene.jsonlのOBB情報(length/width/h)、telemetry.jsonlからの加速度差分算出、run_regression_gate.ps1(-FailOnBehavioral切替の前例)。
- **欠落**: must[]はシナリオ個別記述のみで全run常時適用の安全要件の概念が無い。**衝突検出そのものが検証系に存在しない**(esmini本体の衝突オプションはgt_sim_testの判定経路に乗っていない)。
- **設計**: config/global_invariants.yaml(must[]と同語彙+新規collision_free/road_departure_free)を定義しbatchが全エントリへ自動マージ、verdict.jsonにinvariantsセクション分離出力。collision_freeはscene.jsonlのego対他車OBB交差判定(分離軸テスト)。run_regression_gate.ps1に-FailOnInvariant追加(behavioral WARN運用のままinvariant違反だけ硬いfailに格上げ)。
- **Clean Core**: C++改変ゼロ。既存YAML評価系の拡張のみ。
- **依存**: P11とa_req/加速度算出コードを共有(同時開発が効率的だがcollision_free単体なら独立着手可)。
- **リスク**: phase3_batchは『ポリシー未実装段階で意図的にFAILする』設計のため、invariant既定セットを厳しくしすぎると開発中バッチが常時赤に。invariantは物理的絶対条件(衝突等)に限定し快適性系閾値は通常mustへ — 線引きを文書化。
- **審査**: 「衝突検出が検証経路に存在しない」という欠落指摘は正確でF4運用開始前に入れる価値大 / 検証工場として恥ずかしい部類の欠落、P11と並ぶ評価基盤の二本柱 / OBB SATも後処理で完結しM妥当(S寄り)、P11と独立に出せる。

## P13 [6.3] ODDカバレッジ台帳(パラメータ空間×実行結果のカバレッジ計測と未検証ギャップ可視化) — M / high

- **対象**: セーフティケース作成者・検証計画担当。「どこまで検証したか/どこが空白か」を示す。
- **ストーリー**: odd_taxonomy.yamlに『接近速度/ギャップ時間/道路形状/LHT-RHT/信号有無』の軸定義を書くと、coverage_reportが既存variantのmeta.yaml paramsとレジストリのverdict/人手ラベルを突合し、軸別ヒートマップと未カバーセル一覧を生成。『LHT×短ギャップ×多車線が未実行』が一目で分かり次のgen_*.pyスイープ範囲の根拠になる。
- **既存資産**: .meta.yamlのgenerator.params/road_ref(07_oncoming_yield__p001.meta.yamlで実在確認)、annotation_storeのSQLite+run_id規約、build_manifest.pyの決定論的生成、RunListPanelフィルタ。
- **欠落**: F1は『量産』、F4は『CI回帰』であり、実行したものをODD軸に射影してカバレッジ率と空白を計測する装置はどちらにも無い。リンクは揃っているのに横断集計が一切存在しない。
- **設計**: odd_taxonomy.yaml(meta params→ODD軸名+ビン境界の宣言マッピング)新設、coverage_report.pyがmeta.yamlとDBを結合して軸ペア別ヒートマップ+セル状態のHTML+CSV出力。Webは/api/verification/coverage+Coverageタブ(セルクリック→該当runフィルタ遷移)。
- **依存**: F1のmeta.yaml規約に全面依存。**P39のODD台帳部分とここへ統合**。
- **リスク**: 軸定義の粒度設計が本体。手書き正典(01〜06系)はmeta.yamlが無いため第1段は生成カタログのみ対象。
- **審査**: 実在リンクの横断集計でF1/F4のどちらも持たない層という整理は正しい / **カタログ36本の現規模では集計対象が薄く先行投資の色が濃い — 層2/層3拡張後が適時** / P39と提案間重複あり統合要。

## P14 [5.3] 要件トレーサビリティ+監査エビデンスパッケージ生成(RTM自動生成) — M / high

- **対象**: 監査/アセスメント対応者。要件↔シナリオ↔結果↔構成情報の追跡可能性を示す工程。
- **要旨**: requirements.yaml(要件カタログ)+expectations.yamlのmust[]へのrequirement: REQ-IDタグ(未知キー無視の前方互換で無改修安全)→trace_report.pyがRTM(要件×シナリオ×verdict×コミット)と監査ZIPを生成。
- **見送り理由(審査)**: 技術実装は安いが、本体コストはrequirements.yamlの整備・維持という組織プロセスで1〜2名体制では棚晒しリスク。RTM/監査ZIPは監査時のみの低頻度機能でP44と監査パッケージ領域が相互重複。**監査需要が顕在化してからの着手で遅くない**。

## P15 [7.3] Falsificationループ(適応的パラメータ探索によるクリティカルシナリオ自動発見とカタログ固化) — L / critical

- **対象**: 安全性評価エンジニア(SOTIF unknown-unsafe領域の探索)。F2/F3ポリシーの弱点境界を人手グリッド設計の隙間から掘り出す。
- **ストーリー**: 『gen_07_oncoming_yieldのfirst_gap_s∈[1.0,4.0]×oncoming_speed∈[5,20]でmin TTCを最小化せよ』とfalsify.pyに指示すると、生成→in-process実行→指標算出→次候補選択の閉ループが無人で回り、数百評価の末に『gap 2.1s×速度17m/sでTTC 0.4s』のような境界ケースを発見。発見variantは3点セット規約でgenerated/へ固化され回帰バッチと注釈UIに自動接続、永続的な回帰資産になる。固定グリッド36本では決して見つからない境界がSOTIF成果物として蓄積。
- **既存資産**: gen_07/gen_08のパラメータ化生成+authoring_common.pyファクトリ、gt_sim_test runのin-process実行(GtLib、headless、osi_port引数)、catalog_id 3点セット規約+build_manifest.py+registry/scan自動登録、目的関数=P11のcriticality指標。
- **欠落**: F1は固定グリッドの量産であり、結果を観測して次のパラメータを適応的に選ぶ探索ループは存在しない。『生成→実行→指標→次候補』の最適化閉ループと発見ケースの自動固化が本質的新規。
- **設計**: falsify.py新設。gen_*.pyの生成関数をimportしてパラメータ辞書→一時xosc生成、run+criticality.pyで目的関数評価。サンプラはISamplerで抽象化し、第1段はラテンハイパーキューブ+最良点近傍の局所絞り込み(依存追加なし)、第2段でoptuna等へ差替え可能。閾値超の発見variantは3点セットで固化(meta.generatorにsampler種別と探索履歴を記録し再現性担保)。
- **Clean Core**: C++改変ゼロ。
- **依存**: P11(目的関数)が前提。F2/F3のポリシー実装が入っているほど価値が高い(現状でもlead/traffic_light/stop_yieldで成立)。
- **リスク**: 同時1シミュレーション制約のため逐次実行=1 run数十秒×数百評価で数時間級 → 第1段は夜間逐次バッチと割り切る(P26並列化で解消)。発見境界が「ドライバーモデル既知特性」由来である切り分けレポートが必要。
- **審査**: 配管は全て実在し、依存追加なしの段階設計も現実的。固定グリッド(F1)を本質的に超える探索ループ / 発見ケースの3点セット固化→回帰資産化という配管も正しい / **P24とほぼ同一提案でこちらの設計が精緻 — P15で一本化**。

## P16 [5.7] クラスタ/HUDプロトタイプ画面(マルチディスプレイ配信) — M / high

- **対象**: HMI開発者(メータクラスタ/HUDのプロトタイパー)、DiL実験の実験者。
- **要旨**: クラスタ案をJSONテーマとして定義し、GT_Sim運転席視点をディスプレイ1、クラスタ画面(/cluster/:jobId、VdLivePage同形のレイアウトレス登録)をディスプレイ2に全画面表示。既存/ws/osiのHVD JSONを購読しspeedometer/タコ/ギア/ウインカー/ADASテルテールをCanvas描画、テーマ差替えでA/B比較。
- **判定(審査)**: HVDは既にWSへ流れておりほぼフロントのみで技術リスク低。ただし単体では「画面」であり、P17/P18が無いと実験装置として完結しない。DiL/HMI実験の社内需要確認後にP17/P18とセットで。

## P17 [6.7] シナリオ駆動HMIイベント/TORトリガアクション — M / critical

- **対象**: 警報・TOR(テイクオーバー要求)実験を設計するHMI/ヒューマンファクター研究者。
- **ストーリー**: XOSCに「先行車急減速の2秒前にFCW警報」「トンネル進入5秒後にTOR発出」をGT拡張アクションとして記述。警報がクラスタ画面に表示され、OSI HVDのADAS状態として外部HMI実機へも配信。発火時刻はイベントマーカーとしてテレメトリに記録され反応時間計測の基準点になる。
- **既存資産**: GT_ScenarioReader::ParseExtensionActionsの確立済み注入パターン、ExtraAction.cppのGT_TrafficSignalControllerAction実装雛形、GT_StepポストステップManager連鎖、GT_HostVehicleReporter::AddADASFunction(custom_name+stateの汎用ADAS出力口)、JSON-over-UDPレポーター雛形(GT_ScenarioVariablesReporter 48200)。
- **欠落**: シナリオ語彙に「HMIイベント(警報/TOR)」という概念が存在しない。OSI HVDのADAS状態はコントローラ読み出し専用でシナリオ側から駆動する経路が無い。F6(AutoLight)は灯火の話で別物。
- **設計**: ExtraAction.cppにGT_HMIEventAction(event_type=TOR/FCW/LDW/カスタム、severity、duration)追加。発火イベントは新HMIEventBusシングルトンに積み、GT_Stepポストステップで (a)AddADASFunction経由OSI HVD (b)新GT_HMIEventReporter(JSON-over-UDP) (c)C API GT_GetHMIEvents で出力。
- **依存**: なし(P16/P18と束でDiL実験系が完結)。R5-U3とは独立(灯火に触らない)。
- **リスク**: UDPポート追加は慣例固定群との衝突回避(48203はP2/P35と取り合い — ポート台帳調停要)。外部HMI実機との名前規約(例: gt.hmi.tor=1)の文書化。
- **審査**: 3面とも完成形の前例がありクリーンな拡張 / DiL実験成立の要石 / 'critical'評価はDiL実験需要前提でP16/P18と束でないと単独MVPの価値が出ない。

## P18 [6.7] 被験者応答テレメトリ+反応時間の宣言的判定 — L / critical

- **対象**: DiL実験の実験者・解析担当。
- **ストーリー**: TOR発出後、被験者がステアを切るかブレーキを踏むとOverrideManagerがAUTO→MANUAL遷移を検出。その遷移時刻、生入力波形、ウインカー操作がテレメトリとして毎フレーム記録。実験後『takeover_within: {event: TOR, max_t: 3.0}』と書くだけで全トライアルの反応時間が自動判定され、失敗フレームからリプレイUIへ直接ジャンプ。
- **既存資産**: OverrideManager::JustTransitionedToManual()(遷移フレーム検出API既存、OverrideManager.hpp:30)+ドメイン別状態、SDL2WheelInputのInputFrame(生入力)、IndicatorFSM状態、VirtualDriverTelemetryJsonのJSON化前例とtelemetry.jsonl同形契約、vd_recorder.py、_eval_mustマッチャ前方互換設計。
- **欠落**: テレメトリチャネルはVirtualDriver専用でManualDrive(被験者運転)には存在しない。被験者の生入力・オーバーライド遷移時刻が一切記録されず、反応時間という実験の主要従属変数が測れない。既存マッチャ約13種は全て車両挙動系で人間応答系が無い。
- **設計**: ManualDriveCoordinatorにDriverResponseTelemetry(生入力+ドメイン状態+IndicatorFSM+直近HMIイベントid)追加、VDと同形のpull C API GT_GetManualDriveTelemetry+UDP push。Webはvd_bridge複製の常駐ブリッジ+/ws/md/{job_id}、記録はvd_recorder同形。gt_sim_testへreaction_time_below/takeover_within/no_input_before_event(早すぎる予期反応の検出)マッチャ追加。
- **依存**: P17(HMIイベントマーカー)が反応時間の基準点。単体でも入力ログ・オーバーライド記録として価値あり。
- **リスク**: telemetry.jsonlフレーム形状はクロスセッション契約 → manual_driveセクション追加で既存フィールド不変に。100Hz生入力のファイルサイズ。反応時間精度は固定Hzのフレーム粒度(既定100Hz=10ms)が下限。
- **審査**: JustTransitionedToManualの実在確認、確立パターンの複製で実装確度高 / 反応時間はDiLの主要従属変数でManualDriveテレメトリ皆無の指摘は正確 / **ManualDriveテレメトリ経路はP5/P22/P33とも共通する汎用的な穴でDiL以外にも波及価値 — 配管を統一設計し、MVPはテレメトリ配管のみに絞り反応時間マッチャは後段**。

## P19 [4.7] 被験者実験プロトコルランナー(セッション/トライアル管理) — M / high

- **対象**: DiL実験の実験者(実験計画担当)。
- **要旨**: protocol.yaml(被験者ID、練習/本試行のシナリオ列=ラテン方格、休憩)→experiment_runner.pyがGT_Sim(運転席視点+実ハンドル)をトライアル毎に順次起動、experiment/<被験者>/<トライアル>名前空間で自動登録。
- **見送り理由(審査)**: セッション状態管理・実験者確認画面・トライアル順序制御は実質新規のWebサブシステムでM工数は楽観的。P17/P18の上の縦積み3段目で、実験規模が小さいうちは手順書+手動で代替可。ラテン方格までの製品化はニッチ化リスク。

## P20 [4.0] 視線・ドライバーモニタリングデータ取込み+視線オーバーレイ — M / medium

- **対象**: ヒューマンファクター研究者・DMS連携検証担当。
- **要旨**: アイトラッカーのホストPCから視線・頭部姿勢をUDP JSONで受信(gaze_bridge.py、vd_bridge複製)、sim_timeと較正してgaze.jsonl記録、LiveSceneViewに視線レイ+AOI交差ハイライトのレイヤー。
- **見送り理由(審査)**: 価値の核心であるwall clock↔sim_time時刻同期較正が脆く、UDPジッタ下で視線解析に耐える精度の根拠が薄い。アイトラッカー実機がないと開発も検証もできない外部ハード依存。**ワイヤ仕様だけ規定して延期が妥当**。

## P21 [6.7] 学習データセット・エクスポーター(バッチ実行結果→自動ラベル付きML形式) — M / high

- **対象**: 行動モデル/知覚モデルを学習する社内MLエンジニア。
- **ストーリー**: batch実行後にエクスポーターを1コマンド叩くと、全runのフレーム列(ego状態+周辺移動体+信号色+ポリシー制約)・シナリオパラメータ(meta.yaml)・自動ラベル(verdict.jsonのマッチャ結果)・人手ラベル(注釈DB)が結合されたParquet/JSONLデータセットが出る。catalog_id単位でtrain/val分割。
- **既存資産**: batch()/_OsiCapture/_gt_to_scene、annotation_storeのSQLite+JSONサイドカー、*.meta.yaml、extract_featuresの前例、全フレーム返却API。
- **欠落**: 素材は全run dirに揃っているが「学習形式への結合・スキーマ固定・分割」が存在しない。毎回アドホックなスクリプトを書くことになる。
- **設計**: gt_dataset_export.py新設。RESULTS_DIR/batch配下をスキャンし、scene.jsonl(観測)+telemetry.jsonl(行動)+verdict.json+ラベルをsim_timeでjoinしてフレームテーブル化、meta.yamlのparamsを列展開。スキーマはバージョンタグ付き固定、telemetry.jsonl形状契約には触れない(読むだけ)。
- **依存**: F1の3点セット規約と注釈レジストリ。
- **リスク**: scene.jsonlは約25Hzスロットルでtelemetryと周波数が違う → 時刻整合(最近傍/補間)の仕様を最初に固める。
- **審査**: 読み取り専用のPythonツールでClean Core・凍結制約と無縁 / 本質はグルーコードで価値は社内ML工程の実在性に依存 / スキーマ固定はF4レポートやP25/P27の下地にも転用可。

## P22 [6.3] ManualDrive人間運転デモ・レコーダー(模倣学習用の状態-行動ログ収集) — M / high

- **対象**: 模倣学習・運転スタイルモデルの教師データを集めるMLエンジニアとテストドライバー。
- **ストーリー**: controller_type=manualでカタログシナリオを起動しG29で走ると、観測(OSI GTの周辺車・信号)と行動(HVDのthrottle/brake/steering/gear)が同期記録され、run_id='demo/<セッション>'として注釈レジストリに自動登録。同一catalog_idでVD走行とデモ走行が並ぶため教師データと評価基準が揃う。
- **既存資産**: GetInputsForOSI/GetPowertrainForOSI(HVD出力契約)、GT_HostVehicleReporter(48199)、osi_bridge.pyのsubscribe_gt/subscribe_hvd(受信実装済)、vd_recorder.pyの_scene_loop、annotation_storeのrun登録。
- **欠落**: vd_recorder.pyはVDテレメトリ(48202)前提でVirtualDriver実行しか記録せず、ManualDriveセッションは何のデータ資産も残らない。
- **設計**: demo_recorder.py新設(simulation_runnerのcontroller_type=manual分岐で起動)。subscribe_hvdでhvd.jsonl(行動)、subscribe_gtでscene.jsonl(観測)を書き、meta.jsonにドライバー名/入力デバイス/catalog_id記録。annotation_storeに'demo/'名前空間を1行追加。リプレイはscene.jsonl互換なのでLiveSceneViewがそのまま使える。
- **依存**: P21(エクスポーター)と組むと模倣学習パイプラインが完成。単体でもリプレイ/注釈は成立。
- **リスク**: 同時1ジョブ制約は継承。ドライバー個人情報の扱いは運用ルール要。
- **審査**: Python層に閉じ実装は素直 / 模倣学習と人間ベースライン(P25/P33)の共通基盤 / **P18と記録系が重複するため統合前提 — ManualDriveテレメトリ経路の統一設計が先決**。

## P23 [6.3] 同期エージェント制御C API(gym風 step/observe/act の決定論ループ) — L / high

- **対象**: RL/行動クローンのポリシーをegoに載せて学習・評価したいMLエンジニア。
- **ストーリー**: 任意言語(ctypes/FFI)からGT_esminiLib.dllをin-processロードし、GT_InitWithArgs→ループ{GT_GetAgentObservation(ego+近傍N台+信号+制約)→自モデル推論→GT_SetAgentCommand(throttle,brake,steer)→GT_Step(dt)}→GT_Closeを完全同期・決定論で回す。フル車両物理を介すため出力がそのまま実車相当のペダル/ステア空間になる。
- **既存資産**: GT_esminiLib.cppのextern "C"追加パターン、IInputSource(input_type分岐1行で両コントローラ対応)、StubInputSource/NetworkInputBridgeの前例、RealVehicleBackend+OverrideManager::SyncState、gt_lib.py GtLib(in-process参照実装)、GT_InjectCachedRoadModel(リセット高速化素材)。
- **欠落**: 外部制御はUDP 9100の非同期パスのみでフレーム同期が保証されず学習ループに使えない。in-process側は観測は取れるが行動注入のC APIが存在しない。エピソードreset・終端判定のセマンティクス未定義。
- **設計**: AgentInputSource(IInputSource実装、C APIから書かれた最新コマンドをPollで返す)新設+input_type="agent"。GT_SetAgentCommand/GT_GetAgentObservation(JSON、VirtualDriverTelemetryJsonのego/scene節流用)追加。resetはGT_Close→再Init(model_cache+headlessで1秒未満目標)。終端/報酬は同梱せずexpectations.yamlマッチャをオフライン評価として再利用。
- **依存**: なし(独立)。Python凍結はランタイム機能(PythonDriver)が対象でC API提供は抵触しない。
- **リスク**: reset(再Init)レイテンシが学習スループットのボトルネックになり得る(まず計測)。ManualDriveのドメイン所有権流用のため二重制御回避バリデーション必須。
- **審査**: 報酬/終端を同梱しない割り切りは誠実 / 観測は取れるのに行動注入APIが無い非対称の指摘は正確 / **P1と「決定論的外部制御」の同一ニッチ — チャネル層を共有設計しないと二重資産**。RL学習の社内実需は研究寄り。

## P24 [5.7] メトリクス駆動シナリオ探索(失敗事例マイニング・敵対的パラメータ最適化) — M / high

- **判定**: **P15と本質的に同一の閉ループ(生成→実行→指標→次候補)で社内競合 — P15(設計が精緻: criticality指標接続+サンプラ抽象化+再現性の作り込み)へ一本化**。本提案の進化戦略サンプラ案はP15のISamplerの一実装として吸収可。

## P25 [6.0] 行動ベンチマーク・ハーネス(人間デモ/VD/外部モデルの統一行動指標比較) — M / medium

- **対象**: 行動モデルの良し悪しを定量で議論したいMLエンジニアとVD開発者。
- **要旨**: 同一catalog_idの複数run(人間デモ、VDポリシー構成A/B、外部モデル)をbenchサブコマンドで比較 — 速度プロファイルRMSE・jerk分布・THWパーセンタイル・停止位置誤差の比較表+カタログ横断リーダーボード。「新ポリシーは人間デモにどれだけ近づいたか」を改版ごとに追跡。
- **判定(審査)**: マッチャ内部のjerk/THW計算をmetricsモジュールへ抽出して共用する部分は技術負債返済を兼ねて筋が良い(単独でも価値)。リーダーボードの本領は人間デモ(P22)や外部モデル接続が揃ってから。「仮想人間ドライバー」という製品の本懐に直結する軸ではある。

## P26 [8.3] 並列ヘッドレスバッチ実行基盤(ポートブロック分離ワーカープール) — M / high

- **対象**: 検証担当エンジニアとF4 CI運用者。コミット前ゲートと夜間カタログ回帰の実行工程。
- **ストーリー**: カタログ36本+phase3/anticipationバッチは現在逐次forループで数十分、層2/層3拡張で数百本になると回帰ごとに回せなくなる。--workers 8で同じマニフェストを投げ数分でbatch_verdict.jsonを得る。CI(F4)も同フラグで夜間全カタログを1ジョブ内並列実行。
- **既存資産**: gt_sim_test.py batch()(L1003、逐次ループ)/gt_lib.py GtLib(DLLパス引数付きin-process実行、プロセス分離実証済み)/GT_InitWithArgsのカスタム引数フィルタに--sv-port・--vd-portの前例(GT_esminiLib.cpp:574-615)/host_vehicle_config.jsonのudp_port(per-run config注入は_prepare_policy_xoscで確立済み)/batch_verdict.json集約とscan_registryの取込規約。
- **欠落**: OSI GroundTruthだけ#define OSI_OUT_PORT 48198(GT_OSIReporter.cpp:37)でハードコードされ、gt_sim_testは48198をループバックbindして自己受信するため複数ワーカーが衝突し並列化できない。batch()のマルチプロセス化・ポート払い出し・verdictマージの仕組みが存在しない。
- **設計**: (1) C++: GT_InitWithArgsに--osi-portを追加しGT_OSIReporter::OpenSocket(GT_OSIReporter.cpp:174)のポートを引数化(GT管理フォークファイル内で完結) (2) Python: batch()に--workers N、ProcessPoolExecutorで1run=1サブプロセス、ワーカーindexからポートブロック(例: 48198+idx*16でOSI/HVD/SV/VDを束で割当) (3) 親が各verdict.jsonをマージし現行形状のbatch_verdict.json/batch_summary.md出力 → 注釈レジストリ・Web UIは無改修で連結。
- **Clean Core**: GT_OSIReporter.cppはCMake差し替えのGT管理ファイルなのでポート引数化は非抵触。EnvironmentSimulator/無改変。
- **依存**: F4の前段加速器。**R5-U4(OSIライト出力ポート)とポート設計の整合を着手時に確認**。
- **リスク**: ポート慣例48198-48202はC++とPython両側ハードコードのクロスセッション契約のため、既定値は不変とし並列時のみオフセット適用に限定。Windowsのプロセス起動コストでワーカー数の損益分岐 → 8前後を既定に。
- **審査**: ボトルネック特定が正確、C++変更はGT管理ファイル内の引数化1点 / カタログ数百本+F4 CI化を目前にした「確実に踏む」ボトルネックで「時間の問題」型の必須投資 / verdictマージで下流無改修の段階出荷設計も良い。

## P27 [7.3] KPIトレンドDBとバッチ間回帰差分API — M / high

- **対象**: VD開発者と検証リード。週次品質確認とF4 CIレポートの消費工程。
- **ストーリー**: 現在バッチ結果はrunディレクトリとbatch_summary.md(1回限りのテキスト)に散在し「どのコミットからjerkが悪化したか」「前回バッチ比でどのシナリオが新規fail化したか」を人がディレクトリを掘って探している。本機能後はバッチ完了→registry/scanの延長でKPI行が自動蓄積され、Verifyページでscenario_stem×commitのトレンド曲線と2バッチdiff(新規fail/新規pass/KPI悪化Top N)を確認、悪化runへワンクリックでリプレイジャンプ。
- **既存資産**: verification_runsテーブルの既設commit_hash/verdict_overall/verdict_summary列(db/database.py:41-59)/scan_registry(pull型冪等upsert)/annotation_match.py extract_features(max_decel・jerk・duration抽出実装済み)/gt_sim_testの_git_commit()によるmeta.jsonコミット記録/RunListPanelとVTargetProfileChart.tsx(チャート部品の前例)。
- **欠落**: F4は『1回のバッチをCIで実行』の自動化。run単位KPIの正規化テーブル、コミット軸トレンド、バッチ間diffのAPI/画面はどこにも存在しない(**commit_hash列は記録されるだけで一切集計されていない**)。
- **設計**: kpi_metrics(run_id, metric, value)テーブル追加、scan_registryの延長でtelemetry.jsonl/verdict.json/compare.jsonからKPI抽出upsert(抽出関数はextract_featuresを昇格・共通化)。/api/verification/trends と /batches/{a}/diff/{b} 追加。VerifyページにTrendsタブ。
- **依存**: F4と相互補完(CIバッチが主データ源)。P30(bisect)のgood/bad境界推定の入力にもなる。
- **リスク**: KPI定義がマッチャ進化で変わるとトレンド連続性が切れる → metric名にバージョンサフィックス。スキーマ追加は既存PRAGMAマイグレーションパターンで後方互換。
- **審査**: 「記録されるだけで一切集計されていない」指摘が正確 / 「どのコミットから悪化したか」はVD開発の週次実痛 / F4の出力を時系列資産化する自然な次層でF4本体と重複しない。

## P28 [5.3] シナリオカタログ資産インデックス(パラメータ検索・タグ・世代ハッシュ差分) — M / medium

- **対象**: シナリオオーサリング担当と検証担当。カタログ数百本時代の資産管理。
- **要旨**: meta.yamlのparamsをDBへ取り込みファセット検索(「交差角60度台・2車線・信号なしのT字でfailしたrun」)、gen_*.py再生成時のxosc内容ハッシュでneeds_rebaselineタグ自動付与。
- **見送り理由(審査)**: 36本の現在はmeta.yamlのgrepで足りる。needs_rebaselineハッシュ検出は良案だが、**層2/層3拡張で数百本に達した時点での着手が適切**。実装自体はscan_registry同形のpullパターンで安い。

## P29 [5.7] 検証成果物ライフサイクル管理(容量可視化・保持ポリシー・ピン留め・透過圧縮) — S / medium

- **対象**: 開発機を使う全員とCI管理者。
- **要旨**: run別サイズ内訳の可視化、注釈済みrun/ベースラインのピン留め保持、未注釈の古いrunのjsonl圧縮→期限後アーカイブ削除(dry-run付き)、保持ポリシーYAMLでCI自動GC。
- **判定(審査)**: 実在する運用痛でS工数も妥当だが、差別化に寄与しない純保守機能で手動削除+注意で当面代替可能。**P26並列化+F4 CI化で蓄積が加速した後が適時**。import_sidecars(DB再構築可)という削除耐性の既存設計を正しく活用する設計は良い。

## P30 [4.3] 回帰自動bisect(コミット二分探索+DLLアーティファクトキャッシュ) — L / medium

- **対象**: VD開発者。F4夜間回帰が新規failを検出した直後の原因特定工程。
- **要旨**: bisect_regression.pyにシナリオ+expectations+good/bad commitを渡すと、コミット別DLLキャッシュを使って該当シナリオ1本だけを二分探索実行し原因コミットを報告。
- **見送り理由(審査)**: 各中点コミットのフルビルド(数分〜十数分)が支配項でDLLキャッシュのヒット率は楽観的。git bisect+手動再ビルド(あるいはAIエージェント)で代替可能な頻度の低い工程にL工数は見合わない。**F4安定稼働後の贅沢品**。gt_sim_testのdll引数という下地の実在確認は正確。

## P31 [5.3] wasmシングルファイル共有デモ(インストール不要ブラウザデモのエクスポート) — L / high

- **対象**: 営業・展示会担当、社外/他部署へシナリオを説明するエンジニア、教育担当。
- **要旨**: シナリオ詳細画面から xosc/xodr+wasmモジュール+俯瞰Canvasビューアを単一HTMLに固めた自己完結デモを書き出し。受け手はインストール不要でブラウザ再生+ParameterDeclarationsスライダーで再実行。「メール添付できるシミュレータ」はCARLA/CarMakerに構造的に不可能な差別化。
- **既存資産**: GT_esmini/web/wasm/のesminiJS GT版(embind.cpp — step/getCurrentState/getTrafficSignalStates/getVehicleLightStates、MODULARIZE単一ファイル)、GTRouteJS、LiveSceneViewの俯瞰描画、road_geometry_service.pyのポリライン抽出、/scenarios/{file}/params。
- **見送り理由(審査)**: wasmはv3.0.2 APIピン留めでv3.3.0追従後の再ビルド・API修正リスクをL申告が吸収しきれない。emsdk別系統ビルドの保守が恒常負担。想定ユーザーが社内開発・検証である以上営業価値は副次的。**standaloneビューアはまずP32(wasm不要)で切り出すのが正順**。R4でのwasm再ビルド方針決定後に再評価。

## P32 [7.0] 検証Run共有パッケージ(スタンドアロンHTMLリプレイ+import/export) — M / high

- **対象**: 検証担当→レビュー担当(上長・設計部門・顧客報告)、教育担当(失敗事例の教材化)。
- **ストーリー**: 注釈UIで気になるrun(例: needs-discussionにした対向車ギャップの怪しい挙動)を「エクスポート」すると、telemetry.jsonl+scene.jsonl+verdict.json+metaを内包した自己完結HTMLが出力される。受け手はGT_Sim未インストールでもブラウザで開くだけでLiveSceneView相当のリプレイをスクラブでき、失敗フレームへワンクリックジャンプして議論できる。別マシンの注釈UIにはimportしてラベル付けの続きが可能 — 検証結果が初めて『個人のRESULTS_DIR』から出て組織の資産になる。
- **既存資産**: VerificationReplayPage/VerificationAnnotatePageのReplayTransport(実在確認済)、GET /runs/{id}/telemetry(全フレーム返却)、vd_recorder.pyのscene.jsonl、annotation_store.pyのimport_sidecars(JSONサイドカーからのDB冪等再構築=**import機構の半分は実装済**)、verdict.jsonの失敗フレームジャンプ機構。
- **欠落**: run成果物はRESULTS_DIRローカル限定でマシン間・人間間の共有導線が皆無。needs-discussionラベルを付けても『議論する場』へ持ち出せない。
- **設計**: GET /runs/{run_id}/export(zip+ビューア入りHTML)とPOST /import(zip受領→RESULTS_DIR展開→registry/scan)。ReplayTransport+LiveSceneViewをstandalone Viteエントリに切り出し、telemetry/sceneデータをHTML末尾にJSONインライン埋め込み。meta.jsonにスキーマバージョン。
- **依存**: なし(F4と隣接するが独立出荷可。CIレポートにエクスポートリンクを載せると相乗)。
- **リスク**: telemetry.jsonl形状はクロスセッション契約のためビューアとデータのバージョン不整合に注意(スキーマ版で防御)。長尺runのHTML肥大はPNG除外とフレーム間引きで対処。
- **審査**: wasm不要のデータリプレイ専用でP31より大幅に低リスク / needs-discussionランを組織で議論可能にする価値は検証工場の運用に直接効く / import_sidecarsで輸入機構の半分が既存という見立ても正確。

## P33 [6.0] ManualDrive自動採点モード(教習・展示会ドライビングスコアカード) — M / high

- **対象**: 教育担当(新人ADASエンジニア研修)、展示会ブース運営、採用イベント。
- **ストーリー**: 受講者がG29+FFBでカタログシナリオを運転すると、終了と同時にexpectations.yamlマッチャで自動採点され『一時停止: PASS / 減速の滑らかさ: jerk超過3回 / 車間維持: THW 1.2s』のスコアカードと、VirtualDriverのお手本走行ゴースト重ね比較が表示される。実ハンドル+フル物理+即時採点の三点セットは競合の展示デモ(映像再生かキーボード)と決定的に差別化。検証工場の判定器を人間の運転評価に転用する「一粒で二度おいしい」構成。
- **既存資産**: ControllerManualDrive(SDL2WheelInput+FFB+RealVehicleBackend)、_eval_must()の13種マッチャ、vd_recorder.pyのOSIBridge購読録画、baseline_track.jsonゴースト機構とPOST /runs/{id}/assert、検証カタログ36本(課題ライブラリとして即流用)。
- **欠落**: 採点パイプラインはVDテレメトリ前提で『egoにVDController未割当=エラー』。人間運転セッションをtelemetry.jsonl形状で記録する経路が無い。
- **設計**: ControllerManualDriveにVDテレメトリのego状態サブセットをemitする軽量レポータを追加し既存UDP 48202と同一フレーム形状で送信(**vd_bridge/vd_recorder/リプレイUIが無改修で全部乗る**)。TrainingPage(課題タイル+スコアカード+ゴースト比較)新設、終了時に人間評価用mustセット(.human_expectations.yaml)でassert。
- **依存**: ウインカー採点はR5-U3完了が前提。F5のFFB仕上げと相乗。
- **リスク**: VD固有マッチャは人間運転ではskip → 人間評価用mustセットの設計要。テレメトリ形状互換を崩すとWebオーバーレイ契約に波及(サブセット+null許容)。
- **審査**: 同形契約を正しく逆手に取った巧い構成、マッチャの人間評価転用は検証工場資産の二次利用として筋が良い / 本流のADAS開発・検証ワークフロー上の頻度は低い / **配管はP18と共通化必須**。human_expectations整備が隠れ工数。

## P34 [6.7] デモプロファイル+キオスクモード(ワンクリック起動・自動巡回) — S / high

- **対象**: 営業・展示会担当、マネジメント報告会の発表者、新人の初日オンボーディング担当。
- **ストーリー**: 『シナリオ+コントローラ種別+ExecutionConfig(ウィンドウ配置/hz/autolight等)+説明テキスト』を1つのデモプロファイルとして名前付き保存し、専用デモページの大タイルをワンクリックで起動。展示会ではプレイリストで自動巡回(ジョブ終了検知→3秒後に次を起動)、Electronをフルスクリーンキオスクで常設展示。現状の『Projects→シナリオ選択→ExecutionPanelで7個のノブを正しく設定』という属人的手順を、営業や役員でも失敗しないワンクリックに置き換える。
- **既存資産**: simulation_runner.pyのcontroller_type分岐とper-run config自動生成、ExecutionConfig(window配置/extra_args/drive_mode/headless)、--control_pipe(QUIT/SPEED:)、manual_drive_api.pyのユーザープリセットCRUD前例、Electron main/index.tsのIPCパターン。
- **欠落**: 実行構成の『名前付き保存・ワンクリック再生』が存在しない(プリセットはmanual_drive.json設定のみ、controller_config.pyの/presetsはDefault 1件)。プレイリスト/自動巡回/キオスク表示も無くデモのたびに手順書が要る。
- **設計**: demo_profilesテーブル+/api/demos CRUD+POST /api/demos/{id}/run(既存simulation_runnerへ委譲するだけ)。DemoPage(大タイル+説明+サムネイル)。プレイリストは既存ジョブ状態ポーリングの終了イベントで次を起動。Electronに--kioskフラグ。
- **依存**: なし。SimulationConflictError(同時1ジョブ)制約とは直列巡回のため整合。
- **リスク**: GT_Sim実機ウィンドウとElectronウィンドウの前面制御がOS依存。デモ中クラッシュからの自動復帰(リトライ1回追加)。
- **審査**: CRUD+委譲+終了検知巡回はS工数で確実、リスクほぼゼロ / 「7個のノブ」の属人性解消はデモに限らず日常の実行構成保存としても効く / キオスク/プレイリストは後段に切れる段階出荷構成。

## P35 [5.7] ストーリーボード・インスペクタ(OpenSCENARIOトリガー連鎖のライブ可視化) — M / medium

- **対象**: 新人シナリオ作成エンジニア、シナリオレビュー担当、『イベントが発火しない』をデバッグする全員。
- **要旨**: Story/Act/Event/Conditionツリーをシミュレーション進行に同期して『どの条件がいま評価中か・いつ発火したか』をタイムライン付きハイライト。wasmの死蔵API(popStoryBoardEvents/popConditionEvents — 実在確認済・UIから完全未使用)を活用。
- **判定(審査)**: 「イベントが発火しない」はOpenSCENARIOを書く全員が高頻度で踏む実痛で着眼は良い。ただし**第1段をwasm経路(v3.0.2ピン)に置く段階順が逆 — ネイティブJSON-over-UDPレポーター(GT_ScenarioVariablesReporter 48200の複製)から始めるべき**。再設計すれば検討価値あり。

## P36 [7.7] 実走行ログ→FollowTrajectoryシナリオ変換器(log2xosc) — M(実態L) / critical

- **対象**: フリートログ管理エンジニア/シナリオ再構成担当。実車部隊からニアミス報告を受け検証チームへ再現シナリオを供給する工程。
- **ストーリー**: テストコース走行のMDF4(CAN由来のego速度/舵角)+GNSS軌跡、周辺車軌跡(ドラレコ解析やRTK計測のCSV)を投入すると、走行場所のOpenDRIVEへマップマッチングした上でego+周辺車を時刻付きFollowTrajectory(Polyline+TimeReference)で再生するxoscが生成される。生成xoscをそのままGT_Sim/注釈UIで再生して報告事象と目視照合し、egoをVirtualDriverに差し替えた『同状況でVDはどう振る舞うか』バリアントも同時に得る。
- **既存資産**: FollowTrajectory正典形=GT_esmini/test/scenarios/realdriver_f09_follow_trajectory.xosc(Polyline+WorldPosition+TimeReference)。xosc生成=authoring_common.py(make_npc_vehicle/make_virtual_driver_controller/assemble_scenario、scenariogeneration 0.16.5)。マップマッチング=GT_esmini/scripts/rm_lib.py EsminiRMLib(SetWorldXYHPosition/GetPositionData/GetLaneInfoで世界XY→road/lane/s/t解決)。3点セット規約=scenario_authoring_foundation.md。
- **欠落**: **実車ログを読む機構がリポジトリ全体に存在しない(rosbag/MDF4/CAN/GNSSヒットゼロ)**。F1量産基盤はパラメトリック生成専用で、実測時系列からのxosc合成・座標系変換(緯度経度→xodr平面)・マップマッチング・多アクター時刻同期は全て新規。
- **設計**: resources/scenario_authoring/log_import/新設 — (a)入力アダプタ層log_readers.py(CSV、MDF4=asammdf。rosbagは契約だけ定義)→正規化中間形式track.jsonl(actor_id, t, x, y, h, v) (b)map_match.pyがrm_libでGNSS/平面座標をxodrへ吸着 (c)log2xosc.pyがauthoring_commonファクトリでego(VD/Default切替)+NPC(FollowTrajectory)のxoscと.meta.yaml(source_log/期間/座標変換パラメータ記録)を生成。依存はrequirements-authoring.txtへ。
- **Clean Core**: コア改変なし。純Pythonオーサリングツール+esminiRMLib.dllのctypes利用のみ。
- **依存**: F1の3点セット規約とvalidate_catalog.py。走行場所に対応するxodrの存在が運用前提。
- **リスク**: 最大リスクは『実走行した道路のOpenDRIVEが無い』こと — 初期スコープをxodr整備済みテストコース/自社計測路に限定。GNSSドリフトによるマップマッチ失敗はlane所属チェックで検出しWARN。
- **審査**: 実→sim輸入路が完全空白という欠落認識は正確で戦略価値は45件中最上位級 / 素材(rm_lib/f09正典/authoringファクトリ)は全て実在確認済み / **座標系変換・マップマッチング・多アクター時刻同期を含むM申告は過小で実態はL。MVPは平面座標CSV+ego単独に絞るべき**。

## P37 [6.3] 危険イベント自動切り出し(イベントマイナー)と前後N秒シナリオ化 — M / high

- **対象**: フリートデータ管理者/検証リード。数時間〜数十時間の長尺ログから検証価値のある区間だけを抽出する選別工程。
- **ストーリー**: 正規化済みtrack.jsonl(+CAN由来ペダル/減速度)を一括スキャンし、急減速(|ax|閾値+持続)・近接(THW/TTC下限割れ)・カットイン(隣接車の横位置遷移+前方ギャップ縮小)の3検知器がイベントを自動列挙。採択イベントの前後N秒(既定: 前10s/後5s)がlog2xoscへ一括投入され再現シナリオ群になる。UN-R157のクリティカルシナリオ収集要求に対し『ログ→候補→ケース』の系譜が機械的に残る。
- **既存資産**: extract_featuresの前例、既存マッチャの語彙対応(deceleration_profile_smooth/maintained_following_distance/speed_reduction_before_landmark)、build_manifest.pyのmanifest生成パターン。
- **欠落**: イベント検知ロジック自体がリポジトリに皆無。現状の検証シナリオ選定は人が想像で書くかグリッド網羅であり『現実で実際に起きた危険事象』起点の選別機構が無い。
- **設計**: log_import/event_miner.py。検知器はdetectorレジストリ(名前→関数)でプラガブル、閾値はevent_miner.yamlで外部化。出力events.yaml(ID/種別/t0/t1/関与アクター/シビリティ)、log2xoscが--events指定で一括クリップ変換。**イベント種別ごとにexpectations.yamlの雛形を自動付与し変換直後からassertが回る状態にする**。
- **依存**: P36の中間形式track.jsonl(完全依存の2段目)。
- **リスク**: 閾値チューニングの誤検知/見逃し — 初期は再現率優先(過検知を人がレビューで落とす)、注釈UIの採択/棄却ラベルを閾値改善のフィードバックに。カットイン検知はマップマッチ品質依存。
- **審査**: detector レジストリ+expectations雛形自動付与は手堅い / P36パイプラインに必須の前処理工程 / P36成立が完全前提で単独では立たない。

## P38 [7.7] 実測ベースライン比較: gt_sim_test compareの実測トラック対応+再現一致度マッチャ — S / high

- **対象**: 検証エンジニア/シナリオ再構成担当。再構成シナリオが『現実を再現できている』ことを定量証明する受入工程、VD挙動を実人間ドライバーと突き合わせる分析工程。
- **ストーリー**: 再構成xoscを実行後 `gt_sim_test compare --baseline measured:track.jsonl` が実測軌跡を直接ベースラインとしてXY RMSE/速度RMSE/最大乖離/イベント時刻ずれを算出し、新マッチャ reconstruction_fidelity(xy_rmse_below/speed_rmse_below/event_timing_tolerance)でpass/failを宣言的判定。リプレイUIでは実測軌跡がゴースト表示され乖離の大きいフレームへジャンプ。**規制対応の『実走行データとの一致』エビデンスの生成器になる**。
- **既存資産**: gt_sim_test.py compare()(L378-424で実在確認: _interp時刻格子補間+xy_rmse_m/speed_rmse_mps/endpoint_dist_m算出+baseline_track.json出力)。_eval_must()のifブロック1個でマッチャ追加できる前方互換設計。ゴースト描画=LiveSceneView.tsxのghostPath既存機構。API露出=POST /runs/{id}/baseline-compareの同形拡張。
- **欠落**: 現状のベースラインは『Defaultコントローラで同シナリオを再実行した.osi』のみ(_ego_track_from_osi)で、シミュレータ外部の実測データを比較対象にできない。sim-vs-simの自己比較しか存在せず「実→simの一致度」という概念自体が無い。
- **設計**: compare()のベースライン解決部(_resolve_baseline_osi)に measured: プレフィックス分岐を足し、track.jsonl→(t,x,y,speed)列を既存_interp格子にそのまま流す(中間形式がP36と共通なので変換不要)。reconstruction_fidelityマッチャを1ブロック追加。baseline_track.jsonは既存形式のまま吐くのでリプレイUI/注釈UIは無改修でゴースト表示が効く。
- **Clean Core**: コア改変なし。gt_sim_test.pyとweb backendの局所変更のみ。C-API追加不要。
- **依存**: P36のtrack.jsonl形式。**単体でも手作成の実測CSVに対して動作可能でパイプライン中で最初に着手できる**。
- **リスク**: 実測とsimの時刻原点・座標原点の整合規約を中間形式仕様で固定(マップマッチ時の変換パラメータを.meta.yamlへ記録して再適用)。FollowTrajectory追従誤差(esmini側の再生精度)とVD自由走行乖離(評価対象)を混同しない — 判定はベースライン種別をverdictに明記。
- **審査**: measured:プレフィックス分岐+マッチャ1個という外科的な最小変更でS工数が完全に信頼できる / P36の「現実を再現できている」証明を閉じる高レバレッジ / **S申告が正直に成立する稀有な提案。P36完成を待たず任意CSVで先行出荷できる**。

## P39 [5.3] 再構成ケースの回帰カタログ固化+ODDカバレッジ台帳 — M / high

- **対象**: 検証リード/規制対応(UN-R157/NATM)担当。
- **要旨**: log2xoscの生成物をreplay用バッチmanifestへ固化(catalog_id=replay_<log_id>__e<NNN>規約)、meta.yamlにsource_log/event/oddセクション追加、実走行由来カバレッジマトリクスをWeb UIで一覧。
- **見送り理由(審査)**: P36/P37依存のパイプライン最後段で単独着手不能。**ODD台帳部分はP13と提案間重複しており統合が前提。系譜メタ規約(source_log)の定義はP36側に吸収すべき内容**。manifest/レジストリ複製の実装自体は安い。

## P40 [6.3] CAN操作量リプレイ入力ソース(LogReplayInputSource)による車両物理モデル妥当性検証 — M / medium

- **対象**: 車両モデル担当/規制対応担当。RealVehicle物理が実車挙動を再現できているかを定量確認するモデルバリデーション工程(UN-R157のシミュレーション信頼性評価)。
- **ストーリー**: 実車テストのCAN操作列(t, 舵角, アクセル, ブレーキ, ギア)を新入力ソース input_type="log_replay" で時刻同期再生しRealVehicle物理に通して走らせる。出力軌跡をP38のmeasuredベースライン比較で実測GNSS軌跡と突合、RMSEが閾値内なら『この速度域・操作域で物理モデルは実車と一致』というエビデンスに。乖離が大きい領域はパラメータ同定(P42)対象として特定。
- **既存資産**: IInputSource(「リプレイ入力」追加が明示想定された拡張点、input_type分岐1行)、RealVehicleBackend+real_vehicle_params.json(Civic FL1級実測諸元)、gt_sim_test runのin-process実行、P38の比較器、StubInputSource(最小実装の雛形)。
- **欠落**: 入力ソースは実デバイス(SDL2)/ネットワーク(UDP 9100)/スタブのみで、記録済み操作列の決定論的な時刻同期再生が存在しない。NetworkInputBridge経由のPython外部送信はUDPジッタで再現性が壊れ回帰やパラメータ同定に使えない。
- **設計**: manualdrive/にLogReplayInputSource新設(jsonl/csvのt→操作量を線形補間してPoll応答、終端でIsConnected=false)。ファイルパスはmanual_drive.jsonまたはxoscコントローラプロパティで指定。Python側はlog_import/に操作列抽出(MDF4→input.jsonl)とレポート生成の小スクリプト。
- **依存**: P36の入力アダプタ(MDF4→操作列抽出を共用)、P38の比較器。
- **リスク**: 物理モデルの次数限界(タイヤ非線形域・路面μ)で一致しない領域が必ず出る — 妥当性『範囲』をレポートに明記する運用が前提。急峻なブレーキ入力での補間誤差。
- **審査**: UDPジッタ問題を正しく回避、StubInputSource雛形からM申告はむしろ安全側 / 物理モデル妥当性検証(P41)の入口 / **P5のReplayInputSourceと機構が実質同一で別個に作るべきではない — 統合実装(in-process再生のこちらが筋)。P41の再生経路もこれに統一すべき**。

## P41 [6.7] 実測マニューバ相関ハーネス(定常円・DLC・制動の実測CSV開ループ再生+KPI相関レポート) — L / critical

- **対象**: シミュレーション妥当性確認エンジニア/車両運動性能の実験部門。物理モデルの初回妥当性確認時と車種パラメータセット追加時。
- **ストーリー**: テストコースで取得した定常円旋回(ISO 4138)・ダブルレーンチェンジ(ISO 3888-2)・制動距離試験のCANログをCSVで持ち込む。実測の操舵・ペダル入力をsimに開ループ再生し、sim応答と実測応答をマニューバ別KPI(定常円=アンダーステア勾配・定常ヨーレートゲイン、DLC=最大ヨーレート・経路逸脱、制動=100-0km/h距離・平均減速度・ピッチ角)で突合、誤差率・Pearson相関係数・RMSEを自動算出したcorrelation_report.md/jsonを得る。**これが以降の全credibility論証の一次データになる**。
- **既存資産**: NetworkInputBridge(PSTC 44byteワイヤ、vd_input.pyにPythonパッキング前例)/ControllerManualDrive(絶対パスConfigFileプロパティ対応をControllerManualDrive.cpp:38-51で確認済み)+RealVehicleBackend/GtLibのin-processヘッドレス実行/compare()の_interp+RMSE/GT_HostVehicleReporter(48199、ステア・ペダル・ギア・RPM)/scenario_authoring規約で平坦広場xodr+試験シナリオを量産(gen_90_validation_maneuvers.py)。
- **欠落**: compareはsim-vs-simのXY/速度RMSEのみで、実測データとの比較・実測入力の再生・車両運動KPI(アンダーステア勾配/ヨーレートゲイン/制動距離)は一切存在しない。**車両物理モデルと現実の一致を測る機能はロードマップ上どこにもない**。
- **設計**: GT_esmini/scripts/validation/(新設、verification/と並置)にreplay_runner.py(in-process実行+OSI 48198/HVD 48199自己受信記録。telemetry源はOSI/HVDなのでgt_sim_testのVD必須制約を回避)+maneuver_kpi.py(マニューバ種別ごとのKPI抽出器)+correlate.py(時間整列+誤差率/相関/RMSE+Markdownレポート)。マニューバ定義はmaneuvers/*.yaml、実測CSVスキーマはmeasured_schema.mdで規定。
- **依存**: なし(独立着手可)。ただし**テストコース実測データの入手が外部前提**。
- **リスク**: RealVehicleの横モデルは線形アンダーステア(understeer_factor/critical_speed)で高横G域(>4m/s²目安)は原理的に合わない — これは欠陥ではなくP44のvalidity domain切り出しの根拠データになる。実測CSVの座標系・フィルタ前処理の規約化に往復が要る。
- **審査**: sim自体の妥当性確認という軸の戦略価値は高い(フル物理を売りにする製品の信頼性の根) / **入力再生にUDP 9100(latest-wins)を使う原案は決定論性で劣る — P40のin-process再生(LogReplayInputSource)へ統一すべき** / L申告+実測データ入手という外部前提でソフト側だけで完結しない。MVP=制動試験1種から。

## P42 [6.0] vehicle_paramsパラメータ同定支援(実測誤差最小化の自動キャリブレーションループ) — M / high

- **対象**: 車両モデル担当エンジニア。新規車種のパラメータセット作成時、P41で相関不足が判明したときの是正工程。
- **ストーリー**: 現状のreal_vehicle_params.jsonはCivic FL1のカタログ諸元からの手付け値(ファイル内コメントに明記)。同定対象と探索範囲をcalibration.yamlで宣言(mass_kg±10%, aero_drag_cd 0.25-0.35, understeer_factor, steer_gain, pitch_stiffness/damping)し、P41の相関KPI(複数マニューバの重み付き誤差和)を目的関数にin-processヘッドレスの速さを利かせてscipy(差分進化/Nelder-Mead)で自動探索。同定済みjson+感度ランキング+before/after KPI表を出力、人間は物理的妥当性をレビューして採用。
- **既存資産**: PhysicsInitParams.vehicle_params_file(ManualDrive/VirtualDriver共用)/manual_drive.jsonのreal_vehicle.vehicle_params_fileキー(ManualDriveConfig.cpp:147でパース確認済み)/RealVehicleBackend.cpp:17のConfigLoader解決/per-run config生成パターンと絶対パス注入パターン。
- **欠落**: パラメータの外部化(土台)は完了しているが、実測との誤差から値を逆算する系統的同定機能はゼロ。「パラメータがいじれる」と「パラメータが現実に合っている証拠がある」の間の橋が欠けている。
- **設計**: GT_esmini/scripts/validation/calibrate.py。イテレーション毎にper-run vehicle_params JSONとそれを指すper-run manual_drive.jsonを一時dirへ書き絶対パスConfigFileを一時xoscに注入して起動。**vehicle_params_fileだけはResolveConfigPath(exe相対固定)を通すため、ConfigLoader::ResolveConfigPathに絶対パスパススルー1分岐を追加 — GT_esmini層なのでClean Core非抵触、policy configパス解決バグ(F5)と同類の既知の罠の正規修正を兼ねる**。
- **依存**: P41(目的関数の供給元)。scipyはrequirements-authoring系に追加(凍結対象外)。
- **リスク**: 単一マニューバへの過適合(制動に合わせると旋回が悪化)→ マルチマニューバ重み付き目的関数を必須要件に。構造的限界はパラメータでは埋まらない — 同定残差をP44のドメイン外判定根拠として記録。
- **審査**: 配管を実コードで確認、絶対パスパススルーは既知の罠の正規修正を兼ねる良設計(単独でも切り出す価値がある副産物) / P41の自然な後続 / 新車種追加時のみの低頻度工程。1評価=1シミュレーションの壁時間と収束チューニングが隠れコスト。

## P43 [6.3] モデル回帰スイート(基準マニューバKPIの許容帯ゲート化) — M / high

- **対象**: GT_esmini開発者(RealVehicle/IDriverModel/IPhysicsBackendに触れる全員)とCI。物理・制御コード変更のPR時に毎回走る。
- **ストーリー**: RealVehicleのトルコンモデル等に手を入れたとき、P41で確立した相関が黙って劣化する事故を防ぐ。run_regression_gate.ps1のStep 3として基準マニューバ群(定常円3半径×3速度・DLC・制動5初速・惰行)を自動実行し、凍結済みreference_kpis.yaml(golden KPI+許容帯。実測相関済みには実測由来の帯、未相関には前回リリース値由来の帯)と照合。re-baselineは明示コマンドでのみ可能でgit履歴に残る=「モデルが変わったのに証拠が古い」状態を構造的に検出。**ISO 26262-8のツール認定が要求する『ツール変更時の認定根拠再確認プロセス』の実装になる**。
- **既存資産**: run_regression_gate.ps1(Step1 unit/Step2 behavioralの2段構造に第3段追加)/batch manifestスキーマ/_eval_must()の前方互換マッチャ追加/resources/xosc/verification/の.xosc+.expectations.yamlペア規約/F4のCI組込に相乗り。
- **欠落**: 既存回帰ゲートはVDのシナリオ挙動検証であり、車両物理モデル自体のKPI(制動距離・アンダーステア勾配・ピッチ応答)を数値帯でゲートする仕組みは存在しない。「実測相関の非劣化保証」という品質軸が本質的に新しい。
- **設計**: resources/xosc/model_regression/(新設)に基準マニューバxosc+model_regression_batch.yaml。_eval_mustへkpi_in_band系マッチャ3種(braking_distance_in_band/steady_state_gain_in_band/pitch_response_in_band、KPI計算はmaneuver_kpi.pyをimport)。reference_kpis.yamlはコミット対象、rebaseline.pyのみが更新可能。-IncludeModelRegressionスイッチ→安定後デフォルト化。
- **依存**: P41のKPI抽出器を流用。F4と直交(相乗り関係)。
- **リスク**: 許容帯が緩すぎれば無意味・きつすぎればflaky。**real_vehicle_params.jsonのidle_jitter_seed=0は非決定と明記されているため回帰用は固定シード版paramsを用意する必要**。
- **審査**: 回帰工学として正攻法で実装も確実 / 獲得した実測相関が黙って劣化する事故の防止はISO 26262-8的にも筋が良い / **実測未相関でも『前回リリース値由来の帯』で先行出荷できる段階設計が良い — KPI抽出器は簡易版で先行可能**。

## P44 [6.0] Validity Domain台帳とcredibilityエビデンスパッケージ — M / critical

- **対象**: 安全論証担当(セーフティケース作成者)・NATM仮想試験credibility評価/ISO 26262-8 TCL論証の対応者。
- **ストーリー**: P41〜43の相関結果から「このsimは速度0-80km/h・横加速度≤4m/s²・乾燥路μ相当でKPI誤差X%以内」というvalidity_domain.yamlを定義。以後、全検証runは終了時に自走エンベロープ(最大速度・推定横加速度v²κ・最大減速度)を自動抽出され、ドメイン内/外/部分外がverdict.jsonと注釈UIにバッジ表示 — **『相関検証していない領域を踏んだ検証結果を、気付かずに安全論証の証拠にする』事故を構造的に防ぐ**。evidence_pack.pyが相関レポート+ドメイン定義+再現メタデータ(git commit・DLLハッシュ・vehicle_paramsハッシュ・verdict群)を1つのZIPに固化。
- **既存資産**: meta.yaml規約(generated_at_commit/params)/verdict.json+batch_summary.md/telemetry.jsonlのego(x,y,h,speed,s)+_speed_accel_jerkの微分推定パターン/annotation_store(registry/scan)/RunListPanelフィルタ/GT_InjectCachedRoadModelのFNV-1aハッシュ(成果物ハッシュの前例)。
- **欠落**: 現状の検証資産は「VDが正しく振る舞ったか」を記録するだけで「その結果はどの条件範囲でなら証拠能力があるか」を表すメタデータ層が皆無。高横Gの急旋回や未相関速度域を含んでいても誰も検出できない。
- **設計**: validity_domain.py — ドメイン軸(speed/lat_accel/decel/curvature)をYAML定義、run後にtelemetry.jsonlから走行エンベロープ抽出してin/out/partial判定、verdict.jsonにdomain_statusフィールド追加(前方互換)。レジストリにdomain_statusカラム+RunListPanelにバッジ+フィルタ。evidence_pack.pyはbatch_id指定でZIP化。
- **依存**: 枠組み自体は単独で動く(ドメイン定義が暫定値でもバッジは機能)が、ドメイン境界の数値的根拠はP41の相関結果が供給。
- **リスク**: ドメイン軸の恣意性 → 第1版は速度・横加速度・減速度の3軸に限定しNATM/ASAM CMCの語彙に合わせる。横加速度は曲率×速度²の推定である旨をエビデンスに明記。
- **審査**: 全てPython層で前方互換に収まり実装現実性が高い / 「未相関領域を踏んだ結果を証拠にする事故の構造的防止」は独自価値 / **P41の相関結果がないと台帳の中身が空箱 — バッジ機構のみの先行MVPは成立**。監査ZIP部分はP14と相互重複。

## P45 [4.7] NetworkPhysicsBridge back-to-backクロスモデル検証 — M / medium

- **対象**: シミュレーション妥当性確認エンジニア。実測が入手できない領域(限界域・低μ・高横G)のRealVehicle誤差傾向の見積もり、外部物理(CarSim等)導入判断の根拠作り。
- **要旨**: 同一のペダル・操舵入力系列をRealVehicleBackendとNetworkPhysicsBridge(UDP cmd 9200/state 9201、実装済み・ManualDriveで実証済み)接続の外部高忠実物理の両方へ流し、応答差分をマニューバKPIで自動比較するmodel-to-modelレポート。ISO 26262-8でいう『別ツールによる冗長確認』に相当。
- **見送り理由(審査)**: 外部高忠実物理(CarSim等)へのアクセスが社内に実在するか不明で、同梱Pythonスタブとの比較は自己言及的にしか検証できない。用途がニッチで1〜2名体制の優先度は低い。**外部物理資産が実在する場合のみP41完了後に再評価**。

---

*本付録はワークフロー実行結果(セッション一時ファイル)からの復元。原文の judge_comments はレンズ別([arch]/[value]/[feas])3件だが、要点のみ「審査」欄に統合した。*

