# Azahar YW Compatibility Fork v0.2.0

> [!WARNING]
> **非公式・試験版（Prerelease）です。** セーブデータとAzaharのユーザーデータ／NANDはバックアップしてから使用してください。これはAzahar公式のリリースではありません。

**『妖怪ウォッチ2』のローカル通信・すれちがい通信・HOME復帰の互換性改善を含むWindows向けテストリリースです。**

## v0.1.0からの主な変更

### 1. すれちがい通信（CECD / Multiplayer Room）

- Azahar Multiplayer Room内で、双方がゲームを起動した状態になると、CECD OutBoxのメッセージを相手へ転送する実験的な処理を実装しました。
- すれちがい通信のために対戦・交換などのUDSセッションを開始する必要はありません。両端末で異なるゲームを起動していても転送条件の対象になります。
- 受信メッセージのメタデータ調整、Inboxへの格納、重複排除などに対応します。
- **ローカルで実際に動作確認済み：** 2インスタンスで『妖怪ウォッチ2 真打』を使用し、すれちがいデータ受信後、**ツチノコパンダがVIPルームに実際に出現**しました。セーブフラグの直接書き換えではありません。

### 2. HOME復帰のNDSP競合対策（[#13](https://github.com/AlexanderGG-0520/azahar/pull/13)）

- HOME遷移時、YW2のNDSPワーカーが破棄済みIRQイベントに対して待機し、`InvalidHandle`によるFatalが発生する問題への限定的な回避策を追加しました。
- **ローカルで実際に動作確認済み：** JPN HOMEメニューが表示され、そこからYW2へ戻って通常のプレイを継続できました。関連ログでは`ResultTimeout`へのフォールバックが動作し、従来のFatalは再現しませんでした。
- 対象をYW2のタイトル・`handle == 0`・`9,776,000 ns`・ゲストPC `0x00181448` に限定。**一般の無効ハンドルを無視する変更ではありません。**
- 繰り返しのHOME往復・音声の長時間動作・自転車クラッシュ等の網羅的な検証は未完了です。HOME関連の全問題が解決したという保証はありません。

### 3. 引き継ぎ機能

- [utosa123](https://github.com/utosa123/azahar)による『妖怪ウォッチ2』のローカル通信NWMワーカーポーリング修正
- カメラのQtスレッド処理と共有キャプチャの参照カウント調整
- Linuxでの『ふしぎなレンズ』の緑一色表示への修正（ローカルテストで確認済み）
- CECD / APT の互換性改善と診断ログ

**検証範囲について：** 上記のすれちがい通信とHOME復帰の成功結果は開発者のローカル環境のものです。今回配布する**Windowsビルドでの再検証は別途必要**で、動作を保証しません。

## ダウンロード（Windows）

- **インストーラー:** `azahar-yw-v0.2.0-windows-msys2-installer.exe`
- **ポータブルZIP:** `azahar-yw-v0.2.0-windows-msys2.zip`

インストーラーはそのまま実行してください。ZIP版は任意のフォルダに展開し、`azahar.exe` を起動してください。古いデータを上書きする前に、セーブ／NAND／`user` ディレクトリのバックアップを推奨します。

**Linux / macOS / Androidのバイナリは今回配布しません。**

## Linuxでソースビルド

```bash
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar
git switch release/yw-v0.2.0
git submodule update --init --recursive
```

AzaharのLinuxビルド依存（CMake 3.25以降、Ninja、Qt 6の開発パッケージ等）を用意します。

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_ROOM_STANDALONE=OFF \
  -DENABLE_DISCORD_RPC=ON
cmake --build build -j "$(nproc)"
./build/bin/Release/azahar
```

Linuxのカメラは **Emulation → Configure → System → Camera** から **System Camera (qt)** とWebカメラを選択してください。

## 通信時の注意

1. 双方で互換性のあるAzaharビルドを使い、同じMultiplayer Roomへ参加する。
2. ユーザー名・MACアドレス・コンソールアドレスを使い回さない。
3. 対戦・交換はゲーム内から通常どおり開始する。日本版では必要に応じてリージョンをJPNへ設定する。
4. すれちがい通信の実験では、双方のゲームで対応機能を登録し、両方がゲームを起動している状態にする。

再現性のある不具合報告には、OS・ソースのコミット・ゲームのバージョン・再現手順・`azahar_log.txt`（機密情報除去済み）を記載してください。

## Credits / English summary

**Azahar YW Compatibility Fork v0.2.0** is an unofficial **Windows prerelease**.

- Experimental **StreetPass over multiplayer rooms**: **locally verified** with a visible Pandanoko spawn in the VIP room of Yo-kai Watch 2 Shin'uchi.
- **PR #13 — YW2 NDSP zero-IRQ HOME race workaround**: **locally verified** that HOME opened and the game resumed without the previous fatal. This is a targeted workaround, not a general kernel invalid-handle bypass.
- Windows installer + portable ZIP, with the original local-play fixes and camera lifecycle fixes.
- **Windows runtime results are not yet established** from the local tests. Back up saves/NAND before trying the prerelease.

Maintained by [AlexanderGG](https://github.com/AlexanderGG-0520). Based on [Azahar](https://github.com/azahar-emu/azahar) and [utosa123's YW2 work](https://github.com/utosa123/azahar). Research, coding assistance, and documentation have used ChatGPT/OpenAI Codex with local human runtime testing. License information is in `license.txt`.

Source: [release/yw-v0.2.0](https://github.com/AlexanderGG-0520/azahar/tree/release/yw-v0.2.0) · [development](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw) · [PR #13](https://github.com/AlexanderGG-0520/azahar/pull/13)
