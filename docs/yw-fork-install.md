# Azahar YW Compatibility Fork v0.2.1

> [!WARNING]
> **非公式の試験版（Prerelease）です。** 既存のセーブデータ、NAND、Azaharのユーザーデータを必ずバックアップしてください。Azahar公式リリースではありません。

**Windows向けの互換性改善リリースです。** v0.2.0から、HOME復帰時のNDSP/DSP割り込み競合に対する処理を、タイトル・プログラムカウンターに依存しない方式へ変更しました（[PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17)）。

## v0.2.0 からの変更 — PR #17

### DSP音声IRQの破棄競合に対する汎用ワンショット回避策

v0.2.0の[PR #13](https://github.com/AlexanderGG-0520/azahar/pull/13)では、特定のYW2タイトル、ゲストPC、および9.776ms待機を条件として処理していました。**v0.2.1ではその限定的な処理を削除し、以下のイベント履歴に基づく実験的な回避策に置き換えました。**

- DSP音声IRQイベントを実際に待機したスレッドとイベントの対応を記録。
- 同じイベントが登録解除されたときだけ、そのスレッドに一度限りのgrace tokenを付与。
- **50msのエミュレート時間内**に、同一スレッドが正の有限タイムアウトで`handle=0`の待機を行ったときだけ、`ResultTimeout`で終了。
- 別の`WaitSynchronization1`、トークン消費、または期限切れで通常の無効ハンドル判定に戻します。
- `Kernel::Thread`と`Kernel::Event`の情報をバージョン付きでシリアライズ。旧バージョンのステート読込時には欠けている状態を初期化・復元する処理を追加。
- PR中に判明した**Windows/MSVCのC4456コンパイルエラー**を修正。

一般の無効なハンドルを広く無視する変更ではありません。ただし、**実機の3DSカーネルの厳密な再現ではなく互換性向上のための回避策**です。

### ローカル検証と未検証範囲

**開発者のローカル検証:** PR #17の初期版で、**HOMEメニュー表示 → 妖怪ウォッチ2へ復帰**を3回連続で確認し、各回で音声が正常で、従来のFatalが再現しなかったことを確認しています。ログでは`[DSP-IRQ-GRACE]`が3回の回避処理を記録しています。

**重要:** 上記の3回の成功は**50ms期限・セーブステート対応・MSVC修正を追加する前のコード**での結果です。**今回の最終ソースについては再テスト未実施**であり、他作品への影響、セーブステート互換性、Windows配布版の実際のゲーム動作は未検証です。Windows版のコンパイル成功はGitHub Actionsのビルド結果で個別に確認します。

## 引き継ぐ機能

- 『妖怪ウォッチ2』ローカル通信（対戦・交換・バスターズ等）：[utosa123/azahar](https://github.com/utosa123/azahar)のNWM worker-polling workaroundを継承。
- **すれちがい通信（CECD / Multiplayer Room）**：両端末でゲームが動作している場合にメッセージを交換。ローカル2インスタンスの『妖怪ウォッチ2 真打』で**ツチノコパンダのVIPルーム出現を確認済み**。
- Qtカメラの開始停止と共有キャプチャ処理の改善。Linuxの『妖怪ウォッチ2 ふしぎなレンズ』で緑一色表示の改善をローカル確認。
- APT / HOME関連の診断ログ。

## Windowsダウンロード

- **インストーラー：** `azahar-yw-v0.2.1-windows-msys2-installer.exe`
- **ポータブル版：** `azahar-yw-v0.2.1-windows-msys2.zip`

インストーラーはEXEを実行してください。ZIPは適当なフォルダーに展開し、`azahar.exe`を起動します。既存のユーザーデータを移動・上書きする前にバックアップしてください。

**今回はWindowsのみ配布します。Linux / macOS / Androidの同バージョンのバイナリは含まれません。**

## Linuxソースビルド

```bash
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar
git switch release/yw-v0.2.1
git submodule update --init --recursive
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_ROOM_STANDALONE=OFF \
  -DENABLE_DISCORD_RPC=ON
cmake --build build -j "$(nproc)"
./build/bin/Release/azahar
```

ビルドにはCMake 3.25以降、Ninja、Qt 6開発パッケージ、およびAzaharの通常の依存関係が必要です。

## 通信・不具合報告

同じAzahar Multiplayer Roomに参加し、各仮想本体のユーザー名・MACアドレス・コンソールアドレスが重複しないよう設定してください。すれちがい通信では双方でゲームを起動してください。

不具合を報告する際は、OS、ゲームの版、再現手順、ビルドのコミット、`azahar_log.txt`（機密情報を除去したもの）を添えてください。

---

## English summary

**Azahar YW Compatibility Fork v0.2.1** is an unofficial **Windows prerelease** (installer and portable ZIP).

The primary change since v0.2.0 is [PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17), which replaces PR #13's YW2 title/PC-specific NDSP HOME workaround with a **one-shot DSP audio IRQ retirement grace mechanism** based on per-thread event history, a 50 ms emulated-time expiry, and versioned savestate fields. It also fixes a reported MSVC C4456 build error.

**Local runtime evidence:** Three successful HOME round-trips with normal audio were reported on an *earlier PR #17 revision*. **The final hardened revision has not yet had equivalent runtime testing**, and Windows game-level compatibility remains unverified.

The fork also retains YW2 local multiplayer fixes, experimental room-based StreetPass (**Pandanoko VIP appearance locally confirmed**), and Qt camera improvements.

Project maintained by [AlexanderGG](https://github.com/AlexanderGG-0520), based on [Azahar](https://github.com/azahar-emu/azahar) and [utosa123's YW2 changes](https://github.com/utosa123/azahar). ChatGPT/OpenAI Codex assisted with investigation and implementation; in-game runtime evidence was verified locally by the maintainer. For licensing see `license.txt`.

Source: [release/yw-v0.2.1](https://github.com/AlexanderGG-0520/azahar/tree/release/yw-v0.2.1) · [PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17) · [previous release v0.2.0](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.2.0)
