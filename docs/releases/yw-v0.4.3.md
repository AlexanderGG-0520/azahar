# Azahar YW Compatibility Fork v0.4.3

## 概要

HOMEメニュー経由のDiscord表示、AndroidのLevel256設定、デスクトップ版マウス入力を修正するパッチプレリリースです。本ソフトウェアはAzahar公式版ではない、独立した非公式フォークです。

## v0.4.2からの変更

- HOMEメニューからゲームを起動したとき、Discord Rich Presenceが実行中のゲーム名に更新されるよう修正しました。ゲーム終了後のHOME復帰や別ゲームへの切り替えにも対応します。
- Androidのシステム設定にLevel256互換モードとアカウント登録診断のスイッチを追加しました。どちらも既定では無効で、ゲーム実行中は変更できません。
- マウスボタン設定後に他の入力が `[not set]` と表示される問題と、アナログ方向の保存処理を修正しました。既存のキーボード、ゲームパッド、マウス設定値を維持します。
- マウスボタンをMB1から表示し、Qtが報告する最大27ボタンまで対応しました。実際に利用できるボタン数はOSとデバイスに依存します。
- Git worktreeからのCMake構成とバージョン情報取得を修正しました。

## 配布

- Windows MSYS2インストーラーとポータブルZIP
- macOS Universal ZIP
- 固定リリース鍵で署名したAndroid Vanilla APK
- 各配布ファイルのSHA-256チェックサム

Android APKのアプリIDは `io.github.alexandergg0520.azaharyw`、versionNameは `yw-v0.4.3-vanilla`、versionCodeは `4003000` です。従来の署名鍵とアプリIDを使用します。Linux向けにはソースと `azahar-yw-git` パッケージ構成を利用できます。

## 検証範囲

LinuxではDiscord RPC有効・無効の両構成をビルドし、回帰テスト198件中193件が成功、既存の音声・DSP関連5件がスキップされました。配布用Windows、macOS、署名済みAndroid APKおよびArchパッケージは、リリースCIでビルドと検証を行います。

実機でのHOMEからのゲーム起動・終了とDiscord表示、Android設定の表示・保存・再起動後の反映、追加マウスボタンの動作は未検証です。Level256、Pretendo、すれちがい通信や各ゲームの互換性は、ビルド成功だけでは保証されません。

## ライセンス

ゲームROM、ファームウェア、暗号鍵、アカウント情報は配布物に含めません。ライセンス情報はリポジトリの `license.txt` を参照してください。
