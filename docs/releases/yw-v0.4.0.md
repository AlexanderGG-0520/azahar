# Azahar YW Compatibility Fork v0.4.0

## 概要

Windows・macOS向け配布を継続し、Android Vanilla APKを初めてGitHub Releasesで配布する予定の非公式プレリリースです。Google Playには公開せず、Android APKは独立したアプリIDと固定リリース署名鍵を使用します。Linuxデスクトップ向けには`azahar-yw-git`のAURパッケージング基盤を追加しました（AURへの投稿は含みません）。

## 主な変更

- **Android配布基盤:** Vanilla APKのapplicationIdを`io.github.alexandergg0520.azaharyw`に分離し、公式Azaharと別アプリとして共存できるようにしました。表示名は「Azahar YW」です。`yw-v0.4.0`に対応するversionNameは`yw-v0.4.0-vanilla`、versionCodeは`4000000`です。
- **GitHub Releases / Obtainium:** Android arm64 APKをGitHub Release Assetとして配布し、ObtainiumからPrereleaseを含めて更新できるようにしました。APKの継続更新には同じPackage IDとリリース署名鍵が必要です。
- **Arch Linux / CachyOS:** `integration/yw`を追従する`azahar-yw-git` PKGBUILD、独立したdesktop ID・アイコン・起動コマンドを追加しました。公式版の設定・セーブ・NAND・SDMCとは別のXDGユーザーデータ領域を使用します。AURへの投稿は行っていません。
- **Level256ゲーム切り替え:** PR #32で、終了したプロセスのハンドルを終了処理時に解放し、残留ClientSessionが単一セッションサービスの接続枠を占有し続ける問題を修正しました。`plg:ldr`の逐次接続を確認する回帰テストを追加しています。
- **Android CI:** PR #34でAndroid SDKのplatform-toolsをセットアップし、PR #35でCIからSDKライセンスを自動承諾する処理を削除しました。

## Androidビルド・検証状況

- **コード実装:** Vanilla Release APK、applicationId、semver由来のversionCode、署名検証、arm64ネイティブライブラリ検査をCIに実装しました。
- **CIビルド:** 開発用APKのGradleビルド・APK構造・Package ID・arm64ネイティブライブラリ・APK署名検証は[Workflow Run 40630569846](https://github.com/AlexanderGG-0520/azahar/actions/runs/40630569846)で成功しました。このRunのAPKはデバッグ署名による検証用で、正式配布用署名の証明ではありません。
- **正式配布:** 正式APKは固定リリース鍵による署名が必須です。公開Workflowは設定済みkeystoreとAPK署名fingerprintを照合し、Debug Keyへのフォールバックを許可しません。
- **Android実機:** 実機でのインストール、ゲーム起動、Obtainium更新は未検証です。
- **Android機能互換性:** Level256、Pretendo、3GX、UDS/Multiplayer Room、CECDすれちがい通信、HOMEメニュー復帰はAndroid実機で未検証で、動作を保証しません。

## Level256の既知事項

PR #32はゲーム切り替え時に残留するプロセスハンドル／セッションの解放を対象にしています。TitleConfigのTitle ID不整合や、一部の`MapProcessMemoryEx`エラーは別の調査事項であり、この変更で修正したものではありません。

Level256の署名検証、Capability制御、3GX互換処理を維持しています。Level256プラグイン本体、アカウント情報、認証情報は配布物に含みません。

## 更新とデータ移行

Android APKは`io.github.alexandergg0520.azaharyw`としてインストールされます。ObtainiumではGitHubリポジトリを追加し、Prereleaseを有効にして、次のAPKフィルターを指定してください。

```regex
^azahar-yw-v[0-9]+\.[0-9]+\.[0-9]+-android-arm64\.apk$
```

以前のデバッグ署名APKから正式署名APKへの更新は、署名鍵が異なる場合に通常の上書きインストールができません。アンインストール前に **Settings → Select User Folder** で使用中のAzaharユーザーデータフォルダーを確認し、Androidのファイルアプリでフォルダー全体をアプリ専用領域外の保存先へコピーしてください。debug版を削除した後、正式APKをインストールし、初回設定でバックアップしたユーザーデータフォルダーを選択します。アプリIDまたは署名鍵の変更後も同様に、上書き更新できない場合があります。

## 配布物とテスト上の注意

- Windows MSYS2インストーラー／ポータブルZIP、macOS Universal ZIP、Android arm64 APK、`SHA256SUMS.txt`をGitHub Prereleaseに添付します。
- 各OSのゲーム実動作や長時間プレイの検証状況は、ビルド成功と分けて扱います。Androidの機能互換性は未確認です。
- ROM、ファームウェア、暗号鍵、セーブデータ、NAND、Level256の認証情報は同梱しません。

本ソフトウェアはAzahar公式版ではない非公式フォークであり、公式プロジェクトとは無関係です。
