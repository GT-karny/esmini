# ドメイン別制御の所有権 — 横=ManualDrive / 縦=VirtualDriver を成立させる（feature:F7）

**対象**: `DomainOwnershipLedger`（`GT_esmini/{include,src}/gt_esmini/control/common/`）と、それを読む
`ControllerManualDrive` / `ControllerVirtualDriver`。
**状態**: S1 実装済み（所有権の記録と可視化のみ／挙動不変）。S2 以降はこの文書に追記する。

---

## 1. 何が壊れていたか

シナリオ層は正しく書ける。`scenario_split_domain_md_vd.xosc` を走らせるとログには必ずこう出る:

```
Controller MD active on domains: Lateral (mask=0x2)
Controller VD active on domains: Longitudinal (mask=0x1)
```

それでも分担にならない。原因は3つ重なっている。

1. **どちらの `Step()` も `active_domains_` を読まない。** 各コントローラが自前の
   `RealVehicleBackend` を積み、`HVDStateApplier::Apply` で object の姿勢を丸ごと書く。
2. **`ScenarioEngine.cpp` の制御ループは宣言順の単一ループ**で、ADDITIVE/OVERRIDE の
   フェーズ分けが無い。したがって**同一フレーム内で最後に Step した方が全ドメインを総取りする**。
3. **`Controller::GetActiveDomains()` は調停に使えない**（後述の upstream 欠陥A）。

### 1.1 実測（baseline, 2026-07-29, 本ワーカー再取得）

`f7_curve_onset.xodr` road 4（R≈49m アーク）、8s、`fixed_timestep 0.05`。
ManualDrive に UDP で定常操舵 0.35 を投入（`manual_drive_headless_udp.json`）。
同一シナリオの `<ObjectController>` 2ブロックを入れ替えただけの対:

| 宣言順 | 車輪角 tail 平均 | lane_offset tail | 横を握ったのは |
|---|---|---|---|
| MD → VD | -0.048 deg | +0.80 m | **VD**（VD 署名 ≈ -0.10 deg） |
| VD → MD | **-0.2135 deg** | **-37.64 m** | **MD**（MD 署名 -0.213 deg） |

宣言順を入れ替えただけで横の持ち主が入れ替わる。これが欠陥の一次証拠である。
`-37.64 m` は定常操舵 0.35 で道路を完全に外れた結果で、「MD の横入力が路面に届いた」
ことを疑う余地なく示す。

> **計器の注意**: この表を最初に取ったとき、`manual_drive_headless_udp.json` が
> `build/GT_esmini/config/` へ未ステージだったため ManualDrive は既定値へ黙って
> フォールバックし（`input=sdl2_wheel`）、両方の順序が同一 csv になった。
> 「順序に依存しない」という**誤った結論が出るところだった**。
> `vd_domain_split_probe.assert_config_loaded()` はこの degrade を実行時に落とすためにある。
> config を足したら**ビルドし直すかコピーする**（CMake の config コピーはビルド時実行）。

---

## 2. upstream 欠陥（R1 Clean Core により GT 側では直さない）

### 欠陥A — per-domain 解放が「現職」ではなく「新参」に対して呼ばれる

`EnvironmentSimulator/Modules/ScenarioEngine/OSCTypeDefs/OSCPrivateAction.cpp`,
`ActivateControllerAction::Start()` の `>= osc v1.3` 分岐:

```cpp
Controller* ctrl = nullptr;
if (activation_mode_[i] == ControlActivationMode::ON &&
    (ctrl = object_->GetControllerActiveOnDomainMask(ControlDomain2DomainMask(domain))) != nullptr)
{
    if (... GetVersionMinor() >= 3)
    {
        // ctrl（現職）を探し当てているのに、DeactivateDomains は controller_（新参）に飛ぶ
        controller_->DeactivateDomains(static_cast<unsigned int>(ControlDomain2DomainMask(domain)));
    }
```

`ctrl` を検索しておきながら使っていない。`< v1.3` 分岐は正しく `ctrl->Deactivate()` を呼ぶ。

**影響**: v1.3 以降、現職が降りない。両者が「自分が lateral を持っている」と認識したまま
残り、`Step()` は両方呼ばれる。ログは `mask=0x3` と成功を告げる。
**stock esmini.exe + upstream コントローラ2つでも同一に再現する**（GT 固有ではない）。

**upstream への提案文**: `controller_->DeactivateDomains(...)` を `ctrl->DeactivateDomains(...)` に
変更する。1トークンの修正。`ctrl` は既にその行の条件式で束縛済みで、`< v1.3` 側の
`ctrl->Deactivate()` と対称になる。現状 `ctrl` は代入されるだけで一度も読まれておらず、
これ自体が意図の取り違えを示している。

### 欠陥B — `AssignControllerAction` の brace-init で横縦が入れ替わる

同ファイル `AssignControllerAction::Start()`:

```cpp
controller_->Activate({lat_activation_mode_, long_activation_mode_, light_..., anim_...});
```

`ControlDomains` は `DOMAIN_LONG = 0`, `DOMAIN_LAT = 1` の順（`CommonMini.hpp:120-127`）。
brace-init は添字順に詰めるので **lat が LONG の枠に入る**。

**影響**: `AssignControllerAction` の `activateLateral="true"` が `mask=0x1`（=Longitudinal）を返す。
`ActivateControllerAction` 側は添字を明示しているので正しい。

**GT 側の回避（恒久）**: **`AssignControllerAction` の `activateLateral` /
`activateLongitudinal` は使用禁止。** 活性化は必ず `ActivateControllerAction` で行う。
本ディレクトリのシナリオ資産はすべてこれに従っている。

**upstream への提案文**: 要素順を `{long_activation_mode_, lat_activation_mode_, ...}` に
入れ替えるか、より安全に添字代入へ書き換える。

---

## 3. S1 の判断: 台帳を採る（運用回避では足りない）

タスクは「GT 側に所有台帳を持つ」か「シナリオ側の2アクション形で運用回避する」かの
選択を求めた。**台帳を採る。** 理由は3つで、3つ目が決定的である。

1. **運用回避は無言で失敗する。** 1アクション形（新参を lateral+longitudinal で活性化するだけ）は
   ログに `mask=0x3` と成功を出しながら挙動が変わらない。エラーも警告も出ない。
   「シナリオ作者が常に2アクション形で書く」ことに依存する規律は、破れたときに
   気づけない。実際 `scenario_full_handover_vd_to_md.xosc` のヘッダはこの罠の記録である。
2. **台帳は S3 でどのみち必要になる。** 「横の steering と縦の throttle/brake を1つの
   積分器に流す」には、そのフレームで steering を出す資格があるのは誰かを引ける表が要る。
   台帳を持たないなら S3 で同等のものを別名で作ることになる。
3. **2アクション形は per-domain 分担を表現できない。** 2アクション形が解くのは
   「現職を降ろして新参に丸ごと渡す」だけである。今回の目的は両者が**同時に**、
   各々1ドメインずつを保持し続ける状態であり、「解放してから取る」では到達できない。
   つまり運用回避は今回の問題に対する代替案ですらない。

### 台帳の調停規則 — last claimer wins

`Claim(object_id, controller, name, domain_mask)`:

- mask に**立っている**ドメイン → その controller が所有者になる（現所有者を追い出す）。
- mask に**立っていない**ドメイン → **その controller 自身が所有者である場合に限り**解放する。
  他人の所有は omission では絶対に奪わない。

この非対称性が肝で、これにより結果が2つの `ActivateControllerAction` の実行順に依存しなくなる。
upstream の帳簿に欠けているのはまさにこの性質である。

分割シナリオでの帰結（どちらの順でも同じ）:

```
MD activate {LAT}  → Claim(MD, 0x2) : lat=MD,  lon=<none>   (MD は lon を持っていないので触らない)
VD activate {LONG} → Claim(VD, 0x1) : lat=MD,  lon=VD       (VD は lat を持っていないので奪わない)
```

丸ごと引き渡し（1アクション形＝欠陥Aが効く形）での帰結:

```
VD activate {LAT,LONG} → lat=VD, lon=VD
MD activate {LAT,LONG} → lat=MD, lon=MD    ← 現職 VD の active_domains_ は 0x3 のまま残るが、
                                              台帳上 VD は何も所有しない
```

`active_domains_` は「自分は動いてよいか」に答え、台帳は「このドメインを書く資格があるのは誰か」に
答える。**別の問いであり、後者だけが調停に使える。**

### 台帳のライフサイクル

キーは (object id, controller のアドレス)。1プロセスで多数のシナリオを連続実行する
経路（web backend / `gt_sim_test batch`）では**両方とも再利用される**ため、
`GT_Close()` で `Clear()` する。

---

## 4. S1 の範囲と、変えていないこと

S1 で入れたもの:

- `DomainOwnershipLedger`（新規2ファイル）
- 両コントローラの `Activate()` / `Deactivate()` / `DeactivateDomains()` からの台帳更新
- 両コントローラの `Step()` 冒頭での毎フレーム所有状況ログ（`LOG_DEBUG`）。
  台帳の判定と**自己申告 `active_domains_` を併記する**（両者の食い違いが欠陥Aの可視化そのもの）
- `GT_Close()` での `Clear()`

S1 で**入れていない**もの: 出力ゲート。この段階では台帳を誰も読んで挙動を変えない。
したがって既存の挙動は 1 ビットも変わらないはずであり、それを csv 完全一致で示す（§5）。

---

## 5. 検証のしかた

```powershell
# 全マトリクス（各ケースを宣言順の両方で走らせる）
DriverScript\.venv\Scripts\python.exe GT_esmini\test\headless\vd_domain_split_matrix.py --outdir <dir>
```

**速度列だけを見て合否を決めてはならない。** 状態段で合流した実装（横=Aが姿勢 / 縦=Bが速度）は
速度列が完全に正常に見えるまま、報告速度 9.93 m/s に対し実移動 13.15 m/s（比 1.32）の個体を作る。
OSI にもその速度が出る。したがって合否は必ず

```
ratio = |d(position)/dt| / 報告速度   ∈ [0.98, 1.02]、1秒窓で4点以上
```

で判定する（`speed_consistency()`）。

**判別可能な入力署名を使う。** ゼロ入力では「効いていない」と「効いた結果ゼロ」が
区別できない。ManualDrive に定常操舵 0.35 を入れたときの車輪角 **-0.213 deg** と、
VirtualDriver の **≈ -0.10 deg** の差で横の帰属を読む。

**宣言順を入れ替えた対を必ず両方走らせる。** 片方だけ緑なのは偶然であり、
実装が順序に依存している証拠である。
