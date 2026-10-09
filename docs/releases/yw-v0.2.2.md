# Azahar YW Compatibility Fork v0.2.2

> [!WARNING]
> **非公式の試験版（Prerelease）です。** Azahar公式リリースではありません。起動前に既存のセーブデータ・NAND・ユーザーデータをバックアップしてください。ゲームROM・HOMEメニュー・任天堂のシステムデータは一切同梱していません。

## v0.2.1からの変更

### HOME遷移時のDSPセマフォ終了競合への対処（PR #20）

[PR #20](https://github.com/AlexanderGG-0520/azahar/pull/20)では、HOMEへの移行・復帰中にDSPセマフォが閉じられ、別スレッドが直後に `SignalEvent(0)` を呼ぶ競合への実験的な互換性処理を追加しました。

- 正常なDSPセマフォのSignalを記録し、別スレッドによる同じイベントのCloseで、元のSignal担当スレッドにのみワンショットのgraceを発行します。
- **5msのエミュレート時間内**の該当する `SignalEvent(0)` に限り成功扱いにします。一般の無効ハンドルは無条件に成功扱いしません。
- プロセス単位でトークンを分離し、期限切れ・別スレッド・別イベントを拒否します。
- Catch2によるgrace状態管理の単体テスト3件を追加しました。

先行する[PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17)のDSP音声IRQ待機graceも引き続き含まれます。

**確認済み:** Linux版（開発者環境）で『妖怪ウォッチ2』とLLE HOMEメニュー間の往復を繰り返しても、映像・入力・音声は正常でした。grace状態管理のローカル自動テスト3件はすべて成功しました。

**未確認:** 新しいセマフォgraceの「消費」ログはこの安定動作テストでは発生しておらず、PR #20自体がフリーズを防止したとの因果関係は未確定です。Windows・macOS・Android上での同じゲームのHOME移行・復帰動作も未検証です。各OSのビルド成功は、そのOSでのゲーム動作を保証しません。

## ダウンロード

| OS | 配布ファイル | 備考 |
|---|---|---|
| Windows | `azahar-yw-v0.2.2-windows-msys2-installer.exe` | インストーラー |
| Windows | `azahar-yw-v0.2.2-windows-msys2.zip` | ポータブル版 |
| macOS | `azahar-yw-v0.2.2-macos-universal.zip` | Apple Silicon / Intel両対応のUniversalアプリ |
| Android | `azahar-yw-v0.2.2-android-vanilla.apk` | 直接インストール用のリリース署名APK |
| Linux | ソースコード | 各ディストリビューションでローカルビルド |

ダウンロードしたファイルは `SHA256SUMS.txt` のSHA-256ハッシュで検証できます。

### プラットフォーム別の注意

- **Windows:** ZIPは任意のフォルダーに展開し、`azahar.exe`を起動してください。インストーラー利用時も既存データをバックアップしてください。
- **macOS:** Universalアプリはアドホック署名であり、**Apple Developer IDによる署名・公証（notarization）は行っていません**。macOSのGatekeeperによる制限が発生する場合があります。macOS上での実ゲーム動作も未検証です。
- **Android:** リリースAPKはGitHub Actionsに設定した配布用署名鍵で署名されます。署名鍵が変わると同一パッケージの更新インストールができません。公式Azaharと同じパッケージ名の可能性があるため、異なる署名鍵の既存インストールとの上書き更新は保証しません。Google Play版の配布ではありません。
- **Linux:** このリリースではビルド済みLinuxバイナリを添付しません。

## Linuxソースビルド

```sh
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar
git switch --detach yw-v0.2.2
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ROOM_STANDALONE=OFF -DENABLE_DISCORD_RPC=ON
cmake --build build --parallel 6
./build/bin/Release/azahar
```

通常のAzaharの依存関係、Qt 6開発パッケージ、CMake、Ninja等が必要です。

## 引き継ぐ機能

- 『妖怪ウォッチ2』ローカル通信関連の改善（utosa123版由来のNWM worker-polling workaround）
- 実験的なすれちがい通信（CECD / Multiplayer Room）：開発者のローカル2インスタンスで『真打』のツチノコパンダVIPルーム出現を確認済み
- Qtカメラの開始・停止および共有キャプチャ改善
- HOME / APT / DSPの診断ログ

## 不具合報告

OS、ゲーム名・版、実行手順、ビルドのコミット、機密情報を除去した `azahar_log.txt` を添えてください。可能であればHOME遷移前から同じプロセスのログを保存してください。

## English summary

**Azahar YW Compatibility Fork v0.2.2** is an unofficial **prerelease**. It adds an experimental, process- and thread-scoped one-shot DSP semaphore teardown grace ([PR #20](https://github.com/AlexanderGG-0520/azahar/pull/20)) on top of the DSP audio IRQ grace from [PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17).

Linux local tests of repeated Japanese HOME ↔ Yo-kai Watch 2 transitions reported normal video, input, and audio, and three targeted Catch2 tests passed. The new semaphore grace's actual consume path was **not** observed, so the causal explanation for the improved stability remains unproven. In-game compatibility on Windows, macOS, and Android is not yet verified.

Release assets: Windows installer and portable ZIP, macOS universal ZIP (ad-hoc-signed, **not notarized**), signed Android vanilla APK, and checksums. Linux users can build from the source tag. No copyrighted Nintendo software or game data is included.

Maintained by [AlexanderGG](https://github.com/AlexanderGG-0520), based on [Azahar](https://github.com/azahar-emu/azahar) and [utosa123's changes](https://github.com/utosa123/azahar). See `license.txt` for licensing.
