# Azahar YW Compatibility Fork

**『妖怪ウォッチ2』を中心に、通信・すれちがい通信・カメラ互換性を改善する非公式 Azahar フォーク。**

Maintained by **[AlexanderGG](https://github.com/AlexanderGG-0520)**.

> [!IMPORTANT]
> このリポジトリは [Azahar 公式プロジェクト](https://github.com/azahar-emu/azahar) とは別の**非公式フォーク**です。公式 Azahar のリリース、サポート、動作保証を示すものではありません。
>
> **`integration/yw` には実験的な開発が含まれます。** 配布済みビルドと開発ブランチの違いを確認してください。

[**Windows向け v0.2.1 プレリリース**](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.2.1) · [**開発ブランチ**](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw) · [**インストールガイド**](https://github.com/AlexanderGG-0520/azahar/blob/integration/yw/docs/yw-fork-install.md) · [**English**](#english)

## このフォークについて

このフォークは、[utosa123/azahar](https://github.com/utosa123/azahar) による『妖怪ウォッチ2』のローカル通信修正を土台として、独自の互換性改善・調査を進めています。

主な対象は『妖怪ウォッチ2 元祖／本家／真打』です。『妖怪ウォッチ3』も関心のある対象ですが、**本フォークでの包括的な動作確認や互換性の保証はありません**。

目標は単にゲームを起動させることではなく、実機に近い通信挙動を解析し、再現できる問題を一つずつ修正することです。

## 機能と検証状況

| 項目 | 状況 | 対象 |
| --- | --- | --- |
| YW2 ローカル通信（対戦・交換・バスターズ等） | **実装済み**。日本版の通信成功は元フォークで確認済み。各環境での再検証は必要 | 元フォーク由来の修正／`integration/yw` |
| Linux のシステムカメラ（ふしぎなレンズで緑一色になる問題） | **修正・実機能確認済み** | `integration/yw`・`yw-v0.1.0` |
| Azahar Multiplayer Room 経由のすれちがい通信 | **ローカル検証成功／実験機能**。2インスタンスで真打のツチノコパンダVIP出現を確認（Windows配布版では未検証） | `yw-v0.2.0`・`integration/yw` |
| HOMEメニュー復帰（[PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17)） | **従来のPC/タイトル限定回避策をDSP IRQ履歴に基づくワンショット処理へ変更**。初期版でHOME往復3回と音声正常をローカル確認。50ms期限・ステート対応を加えた最終版は再検証が必要 | `yw-v0.2.1`・`integration/yw` |
| YW2 の高フレームレート化 | **別ブランチの実験**。つつく・おはらい等の副作用があり常用非推奨 | `yw2-120fps-v1-beta` |

すれちがい通信は開発者がローカルで出現を確認しています。HOME復帰は**PR #17初期版で3回連続の往復と音声正常を確認**していますが、**50ms期限・セーブステート対応を追加した最終版では同じ実機能テストを再実施していません**。Windows配布版での動作と全タイトルでの互換性も未検証です。

## ブランチの使い分け

| ブランチ | 用途 |
| --- | --- |
| [`master`](https://github.com/AlexanderGG-0520/azahar/tree/master) | リポジトリのデフォルトブランチ。元フォーク由来の状態が中心で、最新の独自修正は未統合 |
| [`integration/yw`](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw) | **現在の主要開発ブランチ**。カメラ・CECDすれちがい・APT等の変更を統合 |
| [`release/yw-v0.2.1`](https://github.com/AlexanderGG-0520/azahar/tree/release/yw-v0.2.1) | **最新v0.2.1 Windowsプレリリースのソース** |
| [`release/yw-v0.2.0`](https://github.com/AlexanderGG-0520/azahar/tree/release/yw-v0.2.0) | 旧v0.2.0のリリース系列 |
| [`release/yw-v0.1.0`](https://github.com/AlexanderGG-0520/azahar/tree/release/yw-v0.1.0) | 旧v0.1.0のリリース系列 |
| [`yw2-120fps-v1-beta`](https://github.com/AlexanderGG-0520/azahar/tree/yw2-120fps-v1-beta) | フレームレート実験。通常の通信修正とは分離 |

開発状況は **2026年10月9日時点** のものです。新しいコミットにより変わる場合があります。

## ダウンロードとインストール

### Windows

[**Azahar YW Compatibility Fork v0.2.1（プレリリース）**](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.2.1) はWindows向けに次の2形式を配布します。

- **インストーラー:** `azahar-yw-v0.2.1-windows-msys2-installer.exe`
- **ポータブル版:** `azahar-yw-v0.2.1-windows-msys2.zip`

ポータブル版は ZIP を任意のフォルダーに展開して `azahar.exe` を起動してください。OneDrive 管理下などの複雑なパスは問題切り分けのため避けると無難です。

**注意:** v0.2.1はすれちがい通信の実験機能と[PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17)の汎用DSP音声IRQ破棄競合回避策を含みます。**最終コードの実機能テストは未完了**です。使用前にセーブ/NANDをバックアップしてください。Linux／macOS／Android用の同バージョンのバイナリは配布しません。旧版は[v0.2.0](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.2.0)から入手できます。

### Linux（ソースからビルド）

Linux では `integration/yw` をビルドしてください。基本のチェックアウト手順は次のとおりです。

```sh
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar
git switch integration/yw
git submodule update --init --recursive
```

CMake、Ninja、Qt 6 と Azahar のビルド依存関係が必要です。具体的なビルド設定・実行ファイルの場所・カメラ設定は [インストールガイド](https://github.com/AlexanderGG-0520/azahar/blob/integration/yw/docs/yw-fork-install.md) を参照してください。

ビルド前に既存のセーブデータと `user` ディレクトリをバックアップすることを推奨します。異なるビルドで同じデータを扱う場合は、直接移動するのではなくコピーを使ってください。

## ローカル通信の使い方

1. 通信する双方で、YW2 の通信修正を含む互換ビルドを起動します。
2. Azahar の **Multiplayer** から同じ Room に参加します。
3. コンソールの識別情報（ユーザー名、MAC アドレス、コンソールアドレス等）が重複していないことを確認します。
4. 日本版のゲーム同士では、必要に応じてリージョン設定も **JPN** に合わせます。
5. 各自のゲームで通常どおり対戦・交換などを開始します。

同じ Room への参加はゲーム内通信の成功を保証しません。マッチングできない場合は、ゲームのバージョン、識別情報、ファイアウォール、VPN、Room 接続状態、ログを順に切り分けてください。

## すれちがい通信（実験中）

`integration/yw` では、Azahar Multiplayer Room にいる**双方がゲームを起動している状態**を検知し、CECD（すれちがい通信）の OutBox データを相手へ配送する処理を実装しています。

- 同じ Room にいるだけではなく、ゲームの起動状態を利用して交換を開始します。
- **UDS の対戦を開始する必要はありません**。両者のゲームタイトルが異なる場合も交換処理の対象です。
- 同じペアへの無制限な再配送を避ける処理や、受信したメッセージのメタデータ調整を含みます。
- 『妖怪ウォッチ2 真打』の2インスタンス検証で、**ツチノコパンダがVIPルームに実際に出現**したことを確認しています。

これは**セーブデータへ直接出現フラグを注入する機能ではありません**。ゲームが受信したすれちがいメッセージを処理する方式です。

ただし CECD・HOME メニュー・NAND のライフサイクルには未解決の検証項目があります。**重要なデータは必ずバックアップし、実験用データで検証してください。**

## 開発上のポイント

- **NWM / UDS:** 元フォークの YW2 worker-polling workaround を継承。`WaitSynchronization1(timeout=0)` と完了済み worker の処理順序に起因する通信問題を回避します。
- **Qt カメラ:** キャプチャ開始・停止のスレッド処理と共有カメラの参照カウントを修正。Linux のふしぎなレンズで確認しています。
- **CECD / Network:** Multiplayer Room の通信路ですれちがいデータを交換。OutBox／Inbox、重複排除、配信タイミングなどを調査・改善しています。
- **DSP / APT / HOME:** [PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17)で、v0.2.0の[PR #13](https://github.com/AlexanderGG-0520/azahar/pull/13)のYW2タイトル・ゲストPC・固定タイムアウト依存を削除し、DSP音声IRQイベントを実際に待機したスレッドに対して**50msのエミュレート時間に限定した1回限りのgrace**を提供します。別の待機や期限切れでは通常の無効ハンドル処理を維持します。`Kernel::Event`/`Kernel::Thread`の状態はバージョン付きでシリアライズします。**PR #17初期版のHOME往復3回は成功しましたが、最終版の再テストは未実施**です。

問題を調査するときは、再現手順、OS、ブランチとコミット、ゲームの版、関連する `azahar_log.txt` の抜粋を揃えると原因を絞りやすくなります。**ログに含まれる個人情報・認証情報は共有前に削除してください。**

## クレジット・ライセンス

- **[Azahar Emulator](https://github.com/azahar-emu/azahar):** 本フォークのベースとなるオープンソース 3DS エミュレーター
- **[utosa123/azahar](https://github.com/utosa123/azahar):** 『妖怪ウォッチ2』のローカル通信互換性修正（v3 系）の開発元
- **[AlexanderGG](https://github.com/AlexanderGG-0520):** このフォークの独自変更・検証・メンテナンス

調査、実装補助、コードレビューや文書作成には **ChatGPT／OpenAI Codex** を活用しています。ゲーム上の動作確認と採否判断はメンテナーが行います。AI 支援によるコードが Azahar 公式のコントリビューション方針に適合するとは限らず、本フォークの変更が公式へ取り込まれたことを意味しません。

ライセンス・権利表記はリポジトリの [`license.txt`](license.txt) および各ソースファイルを参照してください。ゲーム ROM、暗号鍵、ファームウェア等は同梱していません。

---

## English

**Azahar YW Compatibility Fork** is an **unofficial fork maintained by [AlexanderGG](https://github.com/AlexanderGG-0520)**, focused primarily on *Yo-kai Watch 2* local multiplayer, StreetPass experiments, and camera compatibility. It is not affiliated with or endorsed by the official Azahar project.

### Features and status (October 9, 2026)

- **YW2 local multiplayer:** Inherits utosa123's v3 NWM worker-polling workaround. Japanese editions' local play was confirmed by the original fork's maintainer; broader compatibility is not guaranteed.
- **Linux camera:** Qt camera lifecycle and shared-capture fixes, including a verified fix for the green feed in YW2's Fushigi Lens.
- **StreetPass over multiplayer rooms:** Experimental game-presence-triggered CECD message exchange in **v0.2.1** and `integration/yw`. **Locally verified:** Two Yo-kai Watch 2 Shin'uchi instances produced a **visible Pandanoko in the VIP room**. Windows prerelease runtime testing remains pending; the feature was **not included in v0.1.0**.
- **HOME Menu / APT:** [PR #17](https://github.com/AlexanderGG-0520/azahar/pull/17) replaces PR #13's YW2 title/PC-specific workaround with one-shot DSP audio IRQ retirement grace and a 50 ms emulated-time deadline. **Three HOME round-trips with sound were locally verified on an earlier PR #17 revision**; the final hardened version (including versioned savestate support) has not yet been retested. Windows runtime behavior remains unverified.
- **120 FPS experiments:** Separate experimental branch; not part of the recommended compatibility build.
- **Yo-kai Watch 3:** An area of interest, not a blanket compatibility claim.

### Get started

- **Windows v0.2.1 prerelease (installer and portable ZIP):** [Release assets](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.2.1) · [Previous v0.2.0](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.2.0)
- **Current development:** [`integration/yw`](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw)
- **Linux:** Build from source; see the [installation and camera guide](https://github.com/AlexanderGG-0520/azahar/blob/integration/yw/docs/yw-fork-install.md). No Linux binary is provided with v0.2.1.
- **Original projects:** [Azahar](https://github.com/azahar-emu/azahar) and [utosa123's YW2 local-play fork](https://github.com/utosa123/azahar).

Join the same Azahar multiplayer room with non-duplicated console identities for local play. For experimental StreetPass, run the current development build on both peers, ensure StreetPass is registered in the games, and back up save/NAND data first.

Development uses AI-assisted research and implementation with human runtime verification. See [`license.txt`](license.txt) for licensing information.
