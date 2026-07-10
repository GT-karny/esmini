---
name: release
description: GT_esminiのリリース作業（バージョン締め・タグ付け・リリースノート作成・GitHub Release公開・master反映）を確立済みの手順で進める。`/release` が呼ばれたとき、またはユーザーがリリース・バージョン締め・タグ・リリースノート・v0.Xの公開に言及したときに必ず使用する。
disable-model-invocation: false
---

# release — GTリリース手順（v0.12で確立）

リリースは対外公開を伴う。**4つのチェックポイントで必ずユーザー承認を取る**（勝手に進めない）:
①ノート承認 → ②master PRマージ承認 → ③publish承認 → ④ブランチ掃除リスト承認

## 0. プリフライト

```powershell
gh repo view --json nameWithOwner   # => GT-karny/esmini であること
```

- **ghはフォーク親（esmini/esmini）をデフォルト解決しうる**。このクローンは
  `gh repo set-default GT-karny/esmini` 設定済み（.git/config、全worktree共有）だが、
  **新規クローンでは再設定が必要**。書き込み系操作は保険として常に `-R GT-karny/esmini` を付ける
  （PreToolUseフックも強制する）。
- upstream新版の取込判断（あるなら `docs/odr_resync_checklist.md` に従いresync→全ゲート緑まで）
- `/gates` の回帰ゲートが緑であること

## 1. バージョン・タグ規約

- タグ = `v<upstreamバージョン>_GTv<GTバージョン>`（例: `v3.4.1_GTv0.12.0`）
- リリースタイトルに副題を付ける（例: 「OpenDRIVE 1.6-1.9」「Electron」等、目玉機能）

## 2. リリースノート

**TL;DR + フル形式**（日本語・絵文字セクション）:
🔴破壊的変更 / ✨upstreamハイライト / 🔧GT側修正 / 📦アップグレードガイド / Known Issues

形式の見本（過去リリースの実物を必ず参照する）:
```powershell
gh release view v3.4.1_GTv0.12.0 -R GT-karny/esmini
```
→ **チェックポイント① ユーザーにノート案を提示して承認を得る**

## 3. パッケージ

`/package --version <GTバージョン>` でZIPをビルド（長時間なのでdetached起動、packageスキル参照）。
完成後に起動スモーク（GT_Sim.exe起動→UI表示確認。ELECTRON_RUN_AS_NODE除去に注意）。

## 4. master反映

- PRタイトル慣例: 「Dev v0.<N>」（dev_v0.<N> → master、マージコミット方式）
```powershell
gh pr create -R GT-karny/esmini --base master --head dev_v0.<N> --title "Dev v0.<N>" --body ...
```
→ **チェックポイント② マージはユーザー承認後**

## 5. 公開

```powershell
git tag v<UP>_GTv<GT> <masterのマージコミット> && git push origin v<UP>_GTv<GT>
gh release create v<UP>_GTv<GT> -R GT-karny/esmini --title "..." --notes-file <notes.md> dist/GT_Sim_v<GT>.zip
```
→ **チェックポイント③ publishはユーザー承認後**。
GTのリリースタグはリモートのみでローカルにfetchされていないことがある（正常）。

## 6. 後処理

- `dev_v0.<N+1>` をmasterから分岐してpush
- ブランチ掃除は**候補リスト提示のみ**（削除はユーザー承認後 = チェックポイント④）。
  `pr/vj-a..d` などのPR資産ブランチは保全対象。
