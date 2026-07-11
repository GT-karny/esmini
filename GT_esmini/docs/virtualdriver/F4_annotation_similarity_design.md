# F4 ストレッチ: 注釈データセット類似度の自動判定（設計メモ）

| 項目 | 内容 |
| --- | --- |
| ドキュメント種別 | 設計メモ（構想のみ・**未実装**） |
| 関連ドキュメント | [verification_environment.md](./verification_environment.md) §1, §6.4 / [tech_debt_audit_2026-06](../tech_debt_audit_2026-06.md) §5 F4 |
| 状態 | Draft |
| 最終更新 | 2026-07-11 |

---

## 1. 位置づけ

F4 の本体（実装済み）は **決定論的な per-scenario/per-matcher 回帰ゲート**である
（`scripts/check_phase3_regression.py` + `GT_esmini/test/regression_baseline/phase3_expected.yaml`）。
これは verification_environment.md §1 の「**単純系**（信号停止・標識・道路構造など決定論的領域）→ 数値比較 + YAML 宣言で自動判定」に対応する。

本メモが扱うのはその対、すなわち「**複雑系**（交通流・対向車待ち・混合状況など判断が要る領域）→ シチュエーション生成 → 実行 → 人間アノテーション → ラベル付きデータセットで評価」の**自動化の構想**である。matcher（閾値宣言）では表現しきれない「人間が見て自然か」を、既存の**注釈済みデータセットとの類似度**で近似的に自動判定する。

**なぜ matcher と分けるか**: Phase 3d/3e（対向車ギャップ受容・無信号交差点）以降は「唯一の正解軌道」が無い。yield するか proceed するかは対向車速度や幾何に連続的に依存し、閾値マッチャは偽陽性（一瞬の減速を YIELD と誤認）を出しやすい（実際に p017 初版で発生、`phase3d_conflict_resolver.md` 参照）。人間ラベルとの類似度は、この「判断の質」を軌道全体の形で評価できる。

## 2. 前提となる既存資産

- **注釈 UI とレジストリ**: バッチ実行結果が `batch/<set>/<id>` で注釈レジストリに自動登録される（`scenario_authoring_foundation.md`、web backend）。人間は各 run に verdict ラベル（例: `natural` / `too_aggressive` / `too_timid` / `collision`）を付与できる。
- **テレメトリ**: `telemetry.jsonl`（per-frame ego t/x/y/h/speed + preview + policy constraints）と OSI scene（他車 OBB）。`gt_sim_test` が生成。
- **OBB 分離・THW など**の既存幾何指標（`gt_sim_test._obb_separation` 等）。

## 3. 構想アーキテクチャ

```
新規 run (telemetry.jsonl + scene)
        │  特徴抽出（§3.1）
        ▼
   feature vector  ──►  近傍探索（§3.2）  ──►  注釈済みデータセット
        │                                        (feature, human_label)
        ▼
   最近傍ラベル + 距離  ──►  判定（§3.3）  ──►  auto-verdict:
                                              match / needs-review / mismatch
```

### 3.1 特徴抽出（run → 固定長ベクトル）

軌道の長さが可変なので、**シナリオ非依存な要約統計**へ落とす。候補チャネル:

- 速度プロファイル: min / max / 平均 / 停止時間割合 / 最大減速度 / 最大 jerk（`_speed_accel_jerk` 再利用）
- 相互作用: 全フレームの**最小 OBB 分離**（対他車、`_obb_separation` 再利用）、最小 THW、対向車通過時の PET
- 判断イベント: policy constraint（STOP_AT_S 等）の発火回数・総持続・解除タイミング
- 幾何: 経路 s 進捗率、旋回完了フラグ、横位置逸脱

正規化は各チャネルを注釈データセット上の median/IQR で robust スケーリング。**シナリオテンプレート（07/08/09…）ごとに別サブ空間**にするのが素直（テンプレ跨ぎ比較は無意味）。

### 3.2 類似度・近傍

- 距離: 標準化ユークリッド or マハラノビス（チャネル相関を吸収）。
- 近傍: k-NN（k=3〜5）。データセットが小さい初期は k=1 でも可。
- 「軌道そのものの形」を見たい場合の発展形: 速度(s) 曲線の **DTW 距離**を1チャネルとして追加（計算コスト高、初期は要約統計で十分）。

### 3.3 判定ロジック

最近傍集合のラベル合意と距離で3値化:

- **match**: k 近傍が同一ラベルで合意 かつ 距離 ≤ τ_near → その人間ラベルを auto-verdict として採用。
- **needs-review**: ラベル不一致 or τ_near < 距離 ≤ τ_far（未知領域）→ 注釈 UI にキュー（人間の新規ラベル依頼）。
- **mismatch（ゲート対象候補）**: 過去 `natural` クラスタから距離 > τ_far、または最近傍が `collision`/`too_aggressive` → 回帰の疑い。

閾値 τ は**同一ビルド再実行のノイズ床**（`telemetry_golden.py` が実測: 位置 ~1e-3、停止遷移で速度 ~a·dt）を下限に、注釈クラスタ内分散から較正する。

## 4. 決定論ゲートとの棲み分け・統合点

| | 決定論ゲート（実装済み） | 類似度判定（本メモ・未実装） |
| --- | --- | --- |
| 対象 | 03/04/06（信号/標識/先行車） | 07/08/09…（交渉・複雑系） |
| 正解 | matcher 閾値 + 固定ベースライン | 人間ラベル付きデータセット |
| 出力 | pass/fail/needs-review（per-matcher） | match/needs-review/mismatch（per-run） |
| CI | 本 PR で配線（Windows job・非ブロッキング） | データセット成熟後（下記） |

**統合案**: 類似度判定 CLI（仮 `scripts/check_annotation_similarity.py`）を、決定論ゲートと同じ「batch --out を入力に取り Markdown レポート + machine-parseable 末尾行 + exit code」の契約で作る。回帰ゲート Step 2 の後段に**任意ステップ**として足せる（`-CheckSimilarity` スイッチ、既定 OFF）。CI では別 artifact としてレポート upload。

## 5. 着手前の未確定事項（要検討）

1. **ラベル語彙の確定**: 何クラスにするか（binary natural/unnatural か、多クラスか）。注釈 UI 側のスキーマ変更を伴う。
2. **データセット規模**: k-NN が意味を持つ最小注釈数（テンプレごと数十件？）。F1 量産基盤（07×24/08×12/09×20）が供給元。
3. **特徴の妥当性検証**: 抽出特徴が人間ラベルと相関するかを、既存注釈済み run で**事前に相関分析**してから実装（特徴が効かなければ DTW/学習ベースへ）。
4. **非決定性**: 同一ビルド再実行でも軌道は bit 再現しない。類似度は per-frame でなく要約統計/クラスタ距離なので頑健なはずだが、τ 較正時にノイズ床を必ず織り込む。
5. **退行時の扱い**: mismatch を CI でどう surface するか（初期は needs-review 相当の非ブロッキング、人間確認前提）。

## 6. 非目標

- 本メモは**学習済みモデル（分類器）**の導入を第一候補にしない。初期は解釈可能な要約統計 + k-NN で十分と見る。データが増え特徴が飽和したら再検討。
- 決定論ゲートの置換ではない。単純系は引き続き per-matcher ベースラインで守る（本メモは追加レイヤ）。
