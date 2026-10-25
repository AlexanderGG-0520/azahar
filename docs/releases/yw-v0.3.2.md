# Azahar YW Compatibility Fork v0.3.2

## 概要

Level256 Network v2.0.1との互換性を改善する非公式の試験版です。Windows版AzaharをWine上で実行した際に、Level256の新規アカウント自動登録が失敗する問題を修正しました。

## 修正した不具合

Windowsの非ブロッキングTCP接続では、接続開始時にWinsockが`WSAEWOULDBLOCK`を返すことがあります。Azaharはこれを3DS向けの`-6`として返していましたが、Linuxで同等の接続開始を表す`EINPROGRESS`は`-26`として返していました。非ブロッキング`SOC_U::Connect()`に限り、Windowsの`WSAEWOULDBLOCK`を`EINPROGRESS`相当の`-26`に変換します。ブロッキング接続やその他のWinsockエラーの扱いは変更していません。

## Level256アカウント登録

修正版のWindows MSYS2ビルドをWine上で実行し、`account.key.new`と`account.id.new`の作成、データ書き込み、正式ファイルへのリネームが成功したことを診断ログで確認しました。ユーザーはLevel256の画面上でもアカウント登録成功を確認しています。

## 診断機能

Level256 Registration Diagnosticsを追加しました。診断機能は初期状態で無効です。通常の利用では有効化する必要はありません。有効にした場合も、秘密鍵、認証トークン、MACアドレス、HTTP本文などは記録しません。

## 技術的な変更

- Windowsの非ブロッキング`SOC_U::Connect()`における接続進行中エラーの変換を修正
- SOCエラー変換の回帰テストを追加
- Level256登録処理の限定的な診断イベントを追加
- 無効時に診断用パス処理を避ける最適化を追加

## テスト結果

- CachyOS Linux Releaseビルド成功
- CTest：184件中177件成功、7件スキップ、失敗0
- SOCテスト：8件成功、2件スキップ（実行環境のソケット作成制限）
- GitHub Actions run `40121583601`：Windows MSYS2、Windows MSVC、macOS、Linuxの各ビルドジョブ成功
- Level256の実登録：Windows MSYS2版をWine上で実行し、ユーザーが登録成功を確認

## 動作確認済み環境

- CachyOS：Linux Releaseビルドと自動テスト
- Wine 11.20：Windows MSYS2版でLevel256のアカウント自動登録

## 未検証・既知の制限

- Windowsネイティブ環境でのLevel256新規登録
- macOS上でのLevel256実ゲーム動作
- Level256の全機能との互換性
- 長時間プレイを含む総合的な安定性

Wine上での確認はWindowsネイティブ環境での実動作確認を意味しません。

## ダウンロードと注意事項

- Windowsインストーラー：`azahar-yw-v0.3.2-windows-msys2-installer.exe`
- Windowsポータブル版：`azahar-yw-v0.3.2-windows-msys2.zip`
- macOS Universal版：`azahar-yw-v0.3.2-macos-universal.zip`
- Linux：従来どおりソースからビルドしてください。Linux向け公式配布バイナリは含みません。
- Android APKは今回配布しません。

本リリースはAzahar公式版ではなく、非公式の試験版です。起動前にセーブデータ・NAND・ユーザーデータのバックアップを推奨します。Level256の3GX、サービス本体、ゲームROM、LFCS、アカウント鍵などは配布物に含みません。
