# Azahar YW Compatibility Fork v0.4.2

## 概要

Android版でユーザーフォルダーを選択した後、設定初期化時にクラッシュする問題を修正するパッチプレリリースです。本ソフトウェアはAzahar公式版ではない、独立した非公式フォークです。

## v0.4.1からの変更

- Android用の組み込みデフォルト設定にLevel256関連の3つの設定キーを追加しました。設定検証で宣言漏れを検出した場合に起動を停止する不具合を修正します。既定値はすべて無効のままです。
- v0.4.1で追加したQtデスクトップ版のマウスボタン割り当て機能を維持しています。

## 配布

- Windows MSYS2インストーラーとポータブルZIP
- macOS Universal ZIP
- 固定リリース鍵で署名したAndroid Vanilla APK
- 全配布ファイルのSHA-256チェックサム

Android APKのアプリIDは `io.github.alexandergg0520.azaharyw`、versionNameは `yw-v0.4.2-vanilla`、versionCodeは `4002000` です。固定署名鍵とアプリIDを維持し、既存の正式署名版から更新できる構成です。

## 検証範囲と制限

Android Studioのデバッグ版で、ユーザーフォルダー選択後にクラッシュしないことを実機確認しました。配布用のWindows、macOS、署名済みAndroid APK、およびArchパッケージ検証は、このリリースのGitHub Actionsが成功した場合に限り完了とします。

今回の修正はAndroid設定初期化のクラッシュを対象にしています。AndroidでのLevel256、Pretendo、すれちがい通信、HOMEメニュー復帰や各ゲームの互換性は未検証であり、動作を保証しません。デスクトップ版のゲーム互換性も、ビルド成功だけでは保証されません。

## 安全とライセンス

更新前にセーブデータ、NAND、SDMCをバックアップしてください。ゲームROM、ファームウェア、暗号鍵、アカウント情報は配布物に含めません。ライセンス情報はリポジトリの `license.txt` を参照してください。
