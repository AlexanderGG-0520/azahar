# Azahar YW Compatibility Fork

**『妖怪ウォッチ2』を中心に、ローカル通信・すれちがい通信・カメラ・HOMEメニューなどの互換性を改善する非公式 [Azahar](https://github.com/azahar-emu/azahar) フォークです。** 『妖怪ウォッチバスターズ』とLevel256関連の互換性改善も行っています。

Maintained by [AlexanderGG](https://github.com/AlexanderGG-0520). Based in part on the YW2 local-play fixes from [utosa123/azahar](https://github.com/utosa123/azahar).

> [!IMPORTANT]
> **Azahar公式とは別の非公式フォーク**です。各機能は実験的で、すべてのゲーム・OS・オンラインサービスに対する互換性や動作保証はありません。
>
> **この `integration/yw` が主要開発ブランチです。** `master` には新しい互換性修正が原則未統合です。一方で、開発先端には未リリースのコードが含まれる場合があります。
>
> 使用・アップデートの前に**セーブデータ、NAND、SDMC、ユーザーデータ**をバックアップしてください。

[**Releases（ダウンロード）**](https://github.com/AlexanderGG-0520/azahar/releases) · [**v0.4.4 リリースノート**](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.4.4) · [**開発ブランチ**](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw) · [**English**](#english)

## ブランチとリリース

| 参照先 | 役割 |
| --- | --- |
| [`master`](https://github.com/AlexanderGG-0520/azahar/tree/master) | デフォルトブランチ。**最新の独自機能は未統合**。READMEはフォーク全体の案内を兼ねています。 |
| [`integration/yw`](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw) | **現在の主要開発ブランチ**。リリース前の実験的な修正を含む場合があります。 |
| [Releases / `yw-vX.Y.Z` タグ](https://github.com/AlexanderGG-0520/azahar/releases) | 各版の固定ソース・配布物・変更履歴。 |
| [`yw2-120fps-v1-beta`](https://github.com/AlexanderGG-0520/azahar/tree/yw2-120fps-v1-beta) | 別系統の120 FPS実験。操作への副作用があり、常用非推奨。 |

**2026年10月30日時点**で確認した最新の公開プレリリースは **[yw-v0.4.4](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.4.4)**（10月29日公開）です。新しい版が出ている場合は[Releases一覧](https://github.com/AlexanderGG-0520/azahar/releases)を優先してください。**`master`、`integration/yw`、リリースタグは異なるコードの状態**を示します。

## 機能・検証状況

以下は主に**`integration/yw` と該当リリース**での実装・検証状況です。`master` のコードに全機能が含まれるという意味ではありません。また、CIビルドの成功とゲーム上の動作確認は区別しています。

| 機能 | 確認できている範囲・制限 |
| --- | --- |
| **YW2ローカル通信** | [utosa123/azahar](https://github.com/utosa123/azahar)由来のNWM/UDS worker-polling修正を継承。日本版の通信成功は元フォークで確認。対戦・交換などの全環境での成功は未保証。 |
| **Multiplayer Room経由のすれちがい通信（CECD）** | 実験的なメッセージ転送。ローカルの『妖怪ウォッチ2 真打』2インスタンスで**ツチノコパンダがVIPルームに出現**したことを確認。全OS・全タイトルは未検証。 |
| **Qt / Linuxカメラ** | キャプチャ開始・停止と共有カメラの処理を改善。Linuxの『YW2 ふしぎなレンズ』で緑一色問題の改善を確認。 |
| **HOME / DSP / APT** | HOME往復に関連するイベント・セマフォ競合への修正を含む。LinuxでYW2との往復成功履歴あり。**v0.4.4**ではDiscord有効時のHOME起動停止箇所を通過したが、同版での画面操作やHOMEからのゲーム起動は未検証。 |
| **Level256 Network v2.0.1** | 署名付き3GXの確認・対象プロセス／Capability限定の互換処理をオプトインで実装。**Linuxの『バスターズ 赤猫団 月兎組』でSELECTメニュー表示、オンライン募集部屋表示・入室を確認**。他OS・全機能の保証ではない。 |
| **Level256追加修正** | `FS_USER::GetMediaType`、乱数IPC、Windowsの非ブロッキング接続エラー変換、タイトル切替時の残存ハンドル／セッション解放など。**Windows MSYS2版をWineで実行したLevel256新規登録成功**は確認済みだが、Windowsネイティブの同等検証ではない。 |
| **Qtデスクトップのマウスボタン割り当て** | v0.4.1で導入、v0.4.3でMB1表記・最大27ボタン対応、`[not set]`表示や入力プロファイル保存の修正。対応数はOS・マウスに依存。 |
| **Discord Rich Presence** | v0.4.3でHOMEからのタイトル切り替え時の更新処理を変更。v0.4.4でDiscord有効時のHOME起動デッドロックを修正。ゲーム操作を含む完全な実機能確認は未完了。 |
| **セーブ日時** | YW2・バスターズの本体Offset Time変更後に生じる将来日時への対応と回帰テスト。重要データのバックアップは必要。 |

『妖怪ウォッチ3』も関心対象ですが、**網羅的な動作検証済みとは主張しません**。Pretendoなど外部サービスの常時利用や全機能の互換性も保証しません。

## ダウンロード・インストール

[GitHub Releases](https://github.com/AlexanderGG-0520/azahar/releases)から対象版を選んでください。以下は**v0.4.4に実際に添付されたアセット**です。

| OS | ファイル | 補足 |
| --- | --- | --- |
| Windows | `azahar-yw-v0.4.4-windows-msys2-installer.exe` / `azahar-yw-v0.4.4-windows-msys2.zip` | インストーラー／ポータブルZIP（展開後`azahar.exe`を起動） |
| macOS | `azahar-yw-v0.4.4-macos-universal.zip` | Universal版。実ゲーム動作の確認とは別 |
| Android | `azahar-yw-v0.4.4-android-arm64.apk` | 署名付きVanilla APK。実機でのLevel256等は未検証 |
| Linux | [ソース](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw) / [Arch向けPKGBUILD](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw/packaging/aur) | v0.4.4 Releaseにビルド済みLinuxバイナリなし |
| 共通 | `SHA256SUMS.txt` | Releaseのファイル整合性を確認 |

### Android / Obtainium

AndroidアプリIDは `io.github.alexandergg0520.azaharyw`。公式Azaharとは別アプリです。Google PlayではなくGitHub Releasesで配布しています。

1. [Obtainium](https://github.com/ImranR98/Obtainium/releases)に `https://github.com/AlexanderGG-0520/azahar` をGitHubソースとして追加。
2. **Include prereleases** を有効化。
3. APKフィルターに `^azahar-yw-v[0-9]+\.[0-9]+\.[0-9]+-android-arm64\.apk$` を設定。
4. 対象APKをインストールして更新を管理。

上書き更新には同一アプリIDと署名鍵が必要です。debug署名版からの移行などでは再インストールが必要な場合があるため、**削除前にユーザーデータをバックアップ**してください。Android版にはLevel256互換モード・登録診断の設定項目がありますが、**Android実機での動作検証が完了したという意味ではありません**。

### Arch Linux / CachyOS

`integration/yw` に `packaging/aur/PKGBUILD`・ランチャーなどの**`azahar-yw-git`用パッケージ定義**があります。リポジトリに定義があることは**AURへの公開を意味しません**。AUR上で確認できた場合に限ってAUR helperを使用してください。取得した`packaging/aur`ディレクトリでは、必要な依存関係を用意して`makepkg -si`でローカルビルドできます。

このパッケージは`/usr/bin/azahar-yw`を使用し、公式Azaharとは別のXDGユーザーデータ領域を使う設計です。**署名付きpacmanリポジトリの公開・更新状況は別途確認が必要**です。

### Linux（開発ソースからビルド）

~~~sh
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar
git switch integration/yw
git submodule sync --recursive
git submodule update --init --recursive
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_ROOM_STANDALONE=OFF \
  -DENABLE_DISCORD_RPC=ON
cmake --build build --parallel 6
./build/bin/Release/azahar
~~~

Azaharの通常のビルド依存関係（CMake・Ninja・Qt 6開発パッケージなど）が必要です。バージョン固定で再現する場合は`git switch --detach yw-v0.4.4`などで**タグ**を選んでください。`master`からのビルドでは新機能が揃いません。

## ローカル通信とすれちがい通信

- **ローカル通信:** 双方でYW2向け修正を含むビルドを使用して、同じAzahar Multiplayer Roomへ接続。仮想本体の識別情報（MACアドレスなど）を重複させず、日本版ではリージョン（JPN）も揃えてください。
- **すれちがい通信:** 対応ビルドの双方がゲームを起動し、ゲーム側ですれちがい通信の登録・設定を済ませてください。Room経由のCECD交換にUDSの対戦開始は不要です。
- 同じRoomにいるだけでゲーム通信の成功を保証するものではありません。CECDはセーブフラグを直接書き換える機能ではありません。**バックアップ済みの検証用データで試してください。**

## 不具合報告・クレジット

報告時はOS、ゲームの版、再現手順、**ブランチ／リリースタグとコミットSHA**、関係する`azahar_log.txt`の抜粋を揃えてください。共有前にログから**秘密鍵・アカウント情報・個人情報**を削除してください。

- [Azahar Emulator](https://github.com/azahar-emu/azahar): オリジナルのオープンソース3DSエミュレーター
- [utosa123/azahar](https://github.com/utosa123/azahar): YW2ローカル通信互換性の先行修正
- [AlexanderGG](https://github.com/AlexanderGG-0520): このフォークの変更・検証・保守

**ChatGPT / OpenAI Codex** を調査、実装補助、レビュー、文書作成に利用しています。実行時の検証・採否はメンテナーが担います。公式Azaharへの採用・承認を意味しません。

ライセンスと著作権表記は[`license.txt`](license.txt)および各ソースを参照してください。ゲームROM、ファームウェア、暗号鍵、Level256プラグイン・認証情報等は配布していません。

---

## English

**Azahar YW Compatibility Fork** is an **unofficial, independent** [Azahar](https://github.com/azahar-emu/azahar) fork maintained by [AlexanderGG](https://github.com/AlexanderGG-0520), based in part on [utosa123's YW2 local-play work](https://github.com/utosa123/azahar). It focuses on *Yo-kai Watch 2* multiplayer, experimental StreetPass, camera and HOME Menu compatibility, plus selected *Yo-kai Watch Blasters* / Level256 work.

**Branches:** [`master`](https://github.com/AlexanderGG-0520/azahar/tree/master) is the default **legacy-code branch**, while **[`integration/yw`](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw)** is the main development branch. Do not assume features described above exist on `master`. Release tags contain separate, fixed snapshots.

**Downloads:** Check [Releases](https://github.com/AlexanderGG-0520/azahar/releases). On **October 30, 2026** the most recent published prerelease was [yw-v0.4.4](https://github.com/AlexanderGG-0520/azahar/releases/tag/yw-v0.4.4): Windows MSYS2 installer and portable ZIP, macOS Universal ZIP, signed Android arm64 APK, and `SHA256SUMS.txt`. There is no prebuilt Linux binary attached to this release.

### Feature status

- **YW2 local play:** Inherits the upstream-fork NWM/UDS worker-polling workaround; broad compatibility is not guaranteed.
- **StreetPass over multiplayer rooms:** Experimental CECD delivery, locally verified with two YW2 Shin'uchi instances producing a **visible Pandanoko in the VIP room**; other environments remain unverified.
- **Linux camera:** Locally confirmed fix for YW2 Fushigi Lens displaying a solid green image.
- **HOME / Discord:** HOME/DSP event-race mitigations and Discord title updates. v0.4.4 resolves a HOME startup deadlock associated with Discord Rich Presence; passing the previously blocking startup stage was verified, **not** full on-screen HOME navigation or launching a game.
- **Level256 Network v2.0.1:** Opt-in, restricted signed-3GX compatibility. On Linux, the signed plugin displayed its SELECT menu and *Yo-kai Watch Blasters Red Cat Corps* listed and entered an online room. This is **not blanket support** for every service, OS, or game. Level256 is not distributed here.
- **Mouse input:** Qt desktop button mapping, MB1–MB27 labels and profile fixes in v0.4.3 onward; available buttons depend on hardware and OS.
- **Save dates:** Targeted YW2 and Blasters date-normalization fixes. Always back up saved data.

### Installation notes

- **Android / Obtainium:** Add `https://github.com/AlexanderGG-0520/azahar` as a GitHub source and enable prereleases. APK regex: `^azahar-yw-v[0-9]+\.[0-9]+\.[0-9]+-android-arm64\.apk$`. Android package ID: `io.github.alexandergg0520.azaharyw`. Keep release signing keys consistent; back up data before uninstalling a debug build. Android Level256 behavior is not verified on-device.
- **Arch / CachyOS:** An `azahar-yw-git` [PKGBUILD is provided](https://github.com/AlexanderGG-0520/azahar/tree/integration/yw/packaging/aur), but that does **not** establish its publication on the AUR or a signed pacman repository.
- **Linux:** Clone with submodules, switch to `integration/yw`, then build with CMake/Ninja and Qt 6 development dependencies. For reproducible releases, check out the matching `yw-vX.Y.Z` tag.

Use separate console identities and join the same multiplayer room for YW2 local play. For experimental StreetPass, both peers need games running and StreetPass configured. **Always back up your saves, NAND, SDMC and user data.** Build success alone is not proof of game compatibility, including for Level256 and Pretendo.

This is an independently maintained, AI-assisted project with maintainer review and targeted runtime verification. See [`license.txt`](license.txt) for licensing details.
